#include "local_service.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace fridge {

namespace {

std::string escape_json(const std::string& value) {
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            escaped << character;
            break;
        }
    }
    return escaped.str();
}

std::string bool_to_json(bool value) {
    return value ? "true" : "false";
}

std::string remain_level_label(double value) {
    if (value >= 0.85) {
        return "full";
    }
    if (value >= 0.45) {
        return "half";
    }
    if (value > 0.0) {
        return "low";
    }
    return "empty";
}

}  // namespace

LocalServiceFacade::LocalServiceFacade(LocalServiceConfig config)
    : config_(std::move(config)) {}

std::string LocalServiceFacade::handle_health(const InventoryEngine& engine) const {
    std::ostringstream output;
    output << "{\n"
           << "  \"status\": \"ok\",\n"
           << "  \"service\": \"" << escape_json(config_.service_name) << "\",\n"
           << "  \"bind_host\": \"" << escape_json(config_.bind_host) << "\",\n"
           << "  \"port\": " << config_.port << ",\n"
           << "  \"db_ready\": true,\n"
           << "  \"last_event_time\": \"" << escape_json(engine.last_event_time()) << "\"\n"
           << "}\n";
    return output.str();
}

std::string LocalServiceFacade::handle_inventory(const InventoryEngine& engine) const {
    std::ostringstream output;
    output << "{\n  \"items\": [\n";
    const auto& items = engine.inventory_items();
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        output << "    {\n"
               << "      \"item_id\": " << item.item_id << ",\n"
               << "      \"item_name\": \"" << escape_json(item.item_name) << "\",\n"
               << "      \"category\": \"" << escape_json(item.category) << "\",\n"
               << "      \"count\": " << item.count << ",\n"
               << "      \"remain_level\": \"" << escape_json(remain_level_label(item.remain_level)) << "\",\n"
               << "      \"expire_date\": \"" << escape_json(item.expire_date) << "\",\n"
               << "      \"last_update_time\": \"" << escape_json(item.last_update_time) << "\"\n"
               << "    }";
        if (index + 1 < items.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ],\n"
           << "  \"pending_review_count\": " << engine.pending_reviews().size() << "\n"
           << "}\n";
    return output.str();
}

std::string LocalServiceFacade::handle_events(const InventoryEngine& engine) const {
    std::ostringstream output;
    output << "{\n  \"events\": [\n";
    const auto& events = engine.event_log();
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& event = events[index];
        output << "    {\n"
               << "      \"session_id\": \"" << escape_json(event.session_id) << "\",\n"
               << "      \"event_type\": \"" << escape_json(to_string(event.event_type)) << "\",\n"
               << "      \"coarse_class\": \"" << escape_json(event.coarse_class) << "\",\n"
               << "      \"fine_name\": \"" << escape_json(event.fine_name) << "\",\n"
               << "      \"quantity_delta\": " << event.quantity_delta << ",\n"
               << "      \"yolo_confidence\": " << std::fixed << std::setprecision(2) << event.yolo_confidence << ",\n"
               << "      \"llm_confidence\": " << std::fixed << std::setprecision(2) << event.llm_confidence << ",\n"
               << "      \"review_required\": " << bool_to_json(event.review_required) << ",\n"
               << "      \"timestamp\": \"" << escape_json(event.timestamp) << "\"\n"
               << "    }";
        if (index + 1 < events.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n}\n";
    return output.str();
}

std::string LocalServiceFacade::handle_pending(const InventoryEngine& engine) const {
    std::ostringstream output;
    output << "{\n  \"pending\": [\n";
    const auto& pending = engine.pending_reviews();
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto& item = pending[index];
        output << "    {\n"
               << "      \"session_id\": \"" << escape_json(item.session_id) << "\",\n"
               << "      \"event_type\": \"" << escape_json(to_string(item.event_type)) << "\",\n"
               << "      \"category\": \"" << escape_json(item.category) << "\",\n"
               << "      \"item_name\": \"" << escape_json(item.item_name) << "\",\n"
               << "      \"reason\": \"" << escape_json(item.reason) << "\",\n"
               << "      \"timestamp\": \"" << escape_json(item.timestamp) << "\"\n"
               << "    }";
        if (index + 1 < pending.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n}\n";
    return output.str();
}

std::string LocalServiceFacade::handle_confirm(InventoryEngine& engine, const PendingDecision& decision) const {
    std::string error_message;
    const bool ok = engine.confirm_pending(decision, error_message);
    std::ostringstream output;
    output << "{\n"
           << "  \"ok\": " << bool_to_json(ok) << ",\n"
           << "  \"session_id\": \"" << escape_json(decision.session_id) << "\",\n"
           << "  \"message\": \"" << escape_json(ok ? "pending review handled" : error_message) << "\"\n"
           << "}\n";
    return output.str();
}

std::string LocalServiceFacade::handle_manual_update(InventoryEngine& engine, const ManualInventoryUpdate& request) const {
    std::string error_message;
    const bool ok = engine.manual_update(request, error_message);
    std::ostringstream output;
    output << "{\n"
           << "  \"ok\": " << bool_to_json(ok) << ",\n"
           << "  \"item_name\": \"" << escape_json(request.item_name) << "\",\n"
           << "  \"message\": \"" << escape_json(ok ? "manual update applied" : error_message) << "\"\n"
           << "}\n";
    return output.str();
}

}  // namespace fridge
