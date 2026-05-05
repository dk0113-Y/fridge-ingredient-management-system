#include "inventory_engine.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fridge {

namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string strip_inline_comment(const std::string& value) {
    const std::size_t comment_pos = value.find('#');
    if (comment_pos == std::string::npos) {
        return value;
    }
    return value.substr(0, comment_pos);
}

bool parse_double_value(const std::string& text, double& value) {
    std::size_t consumed = 0;
    value = std::stod(text, &consumed);
    return consumed == text.size();
}

bool parse_int_value(const std::string& text, int& value) {
    std::size_t consumed = 0;
    value = std::stoi(text, &consumed);
    return consumed == text.size();
}

template <typename Record, typename IdGetter>
int next_id_after(const std::vector<Record>& records, IdGetter get_id) {
    int max_id = 0;
    for (const auto& record : records) {
        max_id = std::max(max_id, get_id(record));
    }
    return max_id + 1;
}

bool set_config_value(
    InventoryRuntimeConfig& config,
    const std::string& key,
    const std::string& value,
    std::string& error_message
) {
    try {
        if (key == "auto_commit_min_confidence") {
            return parse_double_value(value, config.auto_commit_min_confidence);
        }
        if (key == "default_fruit_vegetable_days") {
            return parse_int_value(value, config.default_fruit_vegetable_days);
        }
        if (key == "default_meat_egg_fresh_days") {
            return parse_int_value(value, config.default_meat_egg_fresh_days);
        }
        if (key == "default_drink_days") {
            return parse_int_value(value, config.default_drink_days);
        }
        if (key == "default_packaged_food_days") {
            return parse_int_value(value, config.default_packaged_food_days);
        }

        error_message = "Unknown inventory config key: " + key;
        return false;
    } catch (const std::exception&) {
        error_message = "Invalid value for key: " + key;
        return false;
    }
}

std::string sanitize_name(const std::string& value, const std::string& fallback) {
    const std::string trimmed = trim(value);
    return trimmed.empty() ? fallback : trimmed;
}

std::string key_for(const std::string& category, const std::string& item_name) {
    return category + "\n" + item_name;
}

InventoryItem* find_item(
    std::vector<InventoryItem>& items,
    const std::string& category,
    const std::string& item_name
) {
    const std::string wanted_key = key_for(category, item_name);
    for (auto& item : items) {
        if (key_for(item.category, item.item_name) == wanted_key) {
            return &item;
        }
    }
    return nullptr;
}

const PendingReviewRecord* find_pending(
    const std::vector<PendingReviewRecord>& pending_reviews,
    const std::string& session_id
) {
    for (const auto& item : pending_reviews) {
        if (item.session_id == session_id) {
            return &item;
        }
    }
    return nullptr;
}

int shelf_life_days_for(const InventoryRuntimeConfig& config, const std::string& category) {
    if (category == "fruit_vegetable") {
        return config.default_fruit_vegetable_days;
    }
    if (category == "meat_egg_fresh") {
        return config.default_meat_egg_fresh_days;
    }
    if (category == "drink") {
        return config.default_drink_days;
    }
    if (category == "packaged_food") {
        return config.default_packaged_food_days;
    }
    return config.default_packaged_food_days;
}

std::string base_date_from_timestamp(const std::string& timestamp) {
    if (timestamp.size() >= 10) {
        return timestamp.substr(0, 10);
    }
    return "1970-01-01";
}

std::string add_days_to_date(const std::string& base_date, int extra_days) {
    if (base_date.size() != 10) {
        return base_date;
    }

    std::tm tm_value{};
    tm_value.tm_year = std::stoi(base_date.substr(0, 4)) - 1900;
    tm_value.tm_mon = std::stoi(base_date.substr(5, 2)) - 1;
    tm_value.tm_mday = std::stoi(base_date.substr(8, 2)) + extra_days;
    tm_value.tm_hour = 12;

    const std::time_t time_value = std::mktime(&tm_value);
    if (time_value == static_cast<std::time_t>(-1)) {
        return base_date;
    }

    std::tm normalized{};
#ifdef _WIN32
    localtime_s(&normalized, &time_value);
#else
    localtime_r(&time_value, &normalized);
#endif

    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << normalized.tm_year + 1900 << "-"
           << std::setw(2) << normalized.tm_mon + 1 << "-"
           << std::setw(2) << normalized.tm_mday;
    return output.str();
}

std::string derive_expire_date(
    const InventoryRuntimeConfig& config,
    const std::string& timestamp,
    const std::string& category
) {
    return add_days_to_date(base_date_from_timestamp(timestamp), shelf_life_days_for(config, category));
}

}  // namespace

bool load_inventory_runtime_config(
    const std::filesystem::path& config_path,
    InventoryRuntimeConfig& config,
    std::string& error_message
) {
    std::ifstream input(config_path);
    if (!input) {
        error_message = "Failed to open inventory config file: " + config_path.string();
        return false;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string normalized = trim(strip_inline_comment(line));
        if (normalized.empty()) {
            continue;
        }

        const std::size_t separator = normalized.find('=');
        if (separator == std::string::npos) {
            error_message = "Missing '=' at line " + std::to_string(line_number);
            return false;
        }

        const std::string key = trim(normalized.substr(0, separator));
        const std::string value = trim(normalized.substr(separator + 1));
        std::string line_error;
        if (!set_config_value(config, key, value, line_error)) {
            error_message = line_error + " at line " + std::to_string(line_number);
            return false;
        }
    }

    return true;
}

InventoryEngine::InventoryEngine(InventoryRuntimeConfig config)
    : config_(config) {}

InventoryApplyResult InventoryEngine::apply_event(const InventoryEventInput& input) {
    InventoryApplyResult result;

    const std::string coarse_class = sanitize_name(input.coarse_class, "unknown");
    const std::string item_name = sanitize_name(input.fine_name, coarse_class);
    const bool review_required =
        needs_manual_review(input.event_type) ||
        input.yolo_confidence < config_.auto_commit_min_confidence;

    event_log_.push_back(InventoryEventRecord{
        next_event_id_++,
        input.session_id,
        input.event_type,
        coarse_class,
        item_name,
        input.quantity_delta,
        input.yolo_confidence,
        input.llm_confidence,
        review_required,
        input.review_reason,
        input.timestamp
    });

    if (review_required) {
        const std::string reason = input.review_reason.empty()
            ? "event requires manual review before inventory commit"
            : input.review_reason;
        pending_reviews_.push_back(PendingReviewRecord{
            next_pending_id_,
            input.session_id,
            input.event_type,
            coarse_class,
            item_name,
            reason,
            input.quantity_delta,
            input.remain_level,
            input.timestamp
        });

        result.pending_created = true;
        result.status = "pending_review";
        result.message = reason;
        result.pending_id = next_pending_id_++;
        return result;
    }

    if (input.event_type == EventType::NoChange) {
        result.status = "recorded_only";
        result.message = "no inventory mutation required";
        return result;
    }

    InventoryItem* item = find_item(inventory_items_, coarse_class, item_name);
    if (item == nullptr && input.event_type == EventType::PutIn) {
        inventory_items_.push_back(InventoryItem{
            next_item_id_++,
            item_name,
            coarse_class,
            0,
            0.0,
            derive_expire_date(config_, input.timestamp, coarse_class),
            input.timestamp
        });
        item = &inventory_items_.back();
    }

    if (item == nullptr) {
        result.pending_created = true;
        result.status = "pending_review";
        result.message = "target inventory item does not exist for direct mutation";
        pending_reviews_.push_back(PendingReviewRecord{
            next_pending_id_++,
            input.session_id,
            input.event_type,
            coarse_class,
            item_name,
            result.message,
            input.quantity_delta,
            input.remain_level,
            input.timestamp
        });
        result.pending_id = next_pending_id_ - 1;
        return result;
    }

    item->count = std::max(0, item->count + input.quantity_delta);
    item->remain_level = std::clamp(input.remain_level, 0.0, 1.0);
    item->last_update_time = input.timestamp;
    if (item->count == 0) {
        item->expire_date.clear();
    } else if (item->expire_date.empty()) {
        item->expire_date = derive_expire_date(config_, input.timestamp, coarse_class);
    }

    change_log_.push_back(InventoryChangeRecord{
        next_change_id_++,
        input.session_id,
        item_name,
        coarse_class,
        input.quantity_delta,
        item->remain_level,
        input.timestamp,
        "apply_event"
    });

    result.inventory_updated = true;
    result.status = "committed";
    result.message = "inventory updated";
    return result;
}

bool InventoryEngine::confirm_pending(const PendingDecision& decision, std::string& error_message) {
    const PendingReviewRecord* pending = find_pending(pending_reviews_, decision.session_id);
    if (pending == nullptr) {
        error_message = "Pending review item not found for session_id: " + decision.session_id;
        return false;
    }

    if (decision.action == "reject") {
        pending_reviews_.erase(
            std::remove_if(
                pending_reviews_.begin(),
                pending_reviews_.end(),
                [&](const PendingReviewRecord& item) { return item.session_id == decision.session_id; }
            ),
            pending_reviews_.end()
        );
        return true;
    }

    const std::string category = sanitize_name(decision.category, pending->category);
    const std::string item_name = sanitize_name(decision.item_name, pending->item_name);
    InventoryItem* item = find_item(inventory_items_, category, item_name);
    if (item == nullptr) {
        inventory_items_.push_back(InventoryItem{
            next_item_id_++,
            item_name,
            category,
            0,
            0.0,
            derive_expire_date(config_, pending->timestamp, category),
            pending->timestamp
        });
        item = &inventory_items_.back();
    }

    item->count = std::max(0, item->count + decision.count_delta);
    item->remain_level = std::clamp(decision.remain_level, 0.0, 1.0);
    item->last_update_time = pending->timestamp;
    if (item->count == 0) {
        item->expire_date.clear();
    } else if (item->expire_date.empty()) {
        item->expire_date = derive_expire_date(config_, pending->timestamp, category);
    }

    change_log_.push_back(InventoryChangeRecord{
        next_change_id_++,
        decision.session_id,
        item_name,
        category,
        decision.count_delta,
        item->remain_level,
        pending->timestamp,
        "confirm_pending"
    });

    pending_reviews_.erase(
        std::remove_if(
            pending_reviews_.begin(),
            pending_reviews_.end(),
            [&](const PendingReviewRecord& item_record) { return item_record.session_id == decision.session_id; }
        ),
        pending_reviews_.end()
    );
    return true;
}

bool InventoryEngine::manual_update(const ManualInventoryUpdate& request, std::string& error_message) {
    if (trim(request.item_name).empty() || trim(request.category).empty()) {
        error_message = "manual update requires non-empty item_name and category";
        return false;
    }

    InventoryItem* item = find_item(inventory_items_, request.category, request.item_name);
    if (item == nullptr) {
        inventory_items_.push_back(InventoryItem{
            next_item_id_++,
            request.item_name,
            request.category,
            request.count,
            std::clamp(request.remain_level, 0.0, 1.0),
            request.expire_date,
            "manual_update"
        });
        item = &inventory_items_.back();
    } else {
        item->count = std::max(0, request.count);
        item->remain_level = std::clamp(request.remain_level, 0.0, 1.0);
        item->expire_date = request.expire_date;
        item->last_update_time = "manual_update";
    }

    change_log_.push_back(InventoryChangeRecord{
        next_change_id_++,
        "manual",
        request.item_name,
        request.category,
        request.count,
        item->remain_level,
        "manual_update",
        "manual_update"
    });
    return true;
}

const std::vector<InventoryItem>& InventoryEngine::inventory_items() const {
    return inventory_items_;
}

const std::vector<InventoryEventRecord>& InventoryEngine::event_log() const {
    return event_log_;
}

const std::vector<PendingReviewRecord>& InventoryEngine::pending_reviews() const {
    return pending_reviews_;
}

const std::vector<InventoryChangeRecord>& InventoryEngine::inventory_change_log() const {
    return change_log_;
}

InventorySnapshot InventoryEngine::export_snapshot() const {
    return InventorySnapshot{
        inventory_items_,
        event_log_,
        pending_reviews_,
        change_log_
    };
}

void InventoryEngine::replace_state_for_persistence(const InventorySnapshot& snapshot) {
    inventory_items_ = snapshot.inventory_items;
    event_log_ = snapshot.event_log;
    pending_reviews_ = snapshot.pending_reviews;
    change_log_ = snapshot.inventory_change_log;

    next_item_id_ = next_id_after(
        inventory_items_,
        [](const InventoryItem& item) { return item.item_id; }
    );
    next_event_id_ = next_id_after(
        event_log_,
        [](const InventoryEventRecord& record) { return record.event_id; }
    );
    next_pending_id_ = next_id_after(
        pending_reviews_,
        [](const PendingReviewRecord& record) { return record.pending_id; }
    );
    next_change_id_ = next_id_after(
        change_log_,
        [](const InventoryChangeRecord& record) { return record.change_id; }
    );
}

std::string InventoryEngine::last_event_time() const {
    if (event_log_.empty()) {
        return {};
    }
    return event_log_.back().timestamp;
}

}  // namespace fridge
