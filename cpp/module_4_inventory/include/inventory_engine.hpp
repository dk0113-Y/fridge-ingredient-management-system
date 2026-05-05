#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "types.hpp"

namespace fridge {

struct InventoryRuntimeConfig {
    double auto_commit_min_confidence = 0.75;
    int default_fruit_vegetable_days = 5;
    int default_meat_egg_fresh_days = 3;
    int default_drink_days = 30;
    int default_packaged_food_days = 14;
};

bool load_inventory_runtime_config(
    const std::filesystem::path& config_path,
    InventoryRuntimeConfig& config,
    std::string& error_message
);

struct InventoryItem {
    int item_id = 0;
    std::string item_name = "unknown";
    std::string category = "unknown";
    int count = 0;
    double remain_level = 0.0;
    std::string expire_date;
    std::string last_update_time;
};

struct InventoryEventRecord {
    int event_id = 0;
    std::string session_id;
    EventType event_type = EventType::NoChange;
    std::string coarse_class = "unknown";
    std::string fine_name = "unknown";
    int quantity_delta = 0;
    double yolo_confidence = 0.0;
    double llm_confidence = 0.0;
    bool review_required = false;
    std::string review_reason;
    std::string timestamp;
};

struct PendingReviewRecord {
    int pending_id = 0;
    std::string session_id;
    EventType event_type = EventType::Uncertain;
    std::string category = "unknown";
    std::string item_name = "unknown";
    std::string reason;
    int count_delta = 0;
    double remain_level = 0.0;
    std::string timestamp;
};

struct InventoryChangeRecord {
    int change_id = 0;
    std::string session_id;
    std::string item_name = "unknown";
    std::string category = "unknown";
    int count_delta = 0;
    double remain_level = 0.0;
    std::string timestamp;
    std::string source;
};

struct InventorySnapshot {
    std::vector<InventoryItem> inventory_items;
    std::vector<InventoryEventRecord> event_log;
    std::vector<PendingReviewRecord> pending_reviews;
    std::vector<InventoryChangeRecord> inventory_change_log;
};

struct InventoryEventInput {
    std::string session_id;
    std::string timestamp;
    EventType event_type = EventType::NoChange;
    std::string coarse_class = "unknown";
    std::string fine_name = "unknown";
    int quantity_delta = 0;
    double remain_level = 0.0;
    double yolo_confidence = 0.0;
    double llm_confidence = 0.0;
    std::string review_reason;
};

struct InventoryApplyResult {
    bool inventory_updated = false;
    bool pending_created = false;
    std::string status;
    std::string message;
    int pending_id = 0;
};

struct PendingDecision {
    std::string session_id;
    std::string action;
    std::string item_name = "unknown";
    std::string category = "unknown";
    int count_delta = 0;
    double remain_level = 0.0;
    std::string note;
};

struct ManualInventoryUpdate {
    std::string item_name = "unknown";
    std::string category = "unknown";
    int count = 0;
    double remain_level = 0.0;
    std::string expire_date;
    std::string note;
};

class InventoryEngine {
public:
    explicit InventoryEngine(InventoryRuntimeConfig config = {});

    InventoryApplyResult apply_event(const InventoryEventInput& input);
    bool confirm_pending(const PendingDecision& decision, std::string& error_message);
    bool manual_update(const ManualInventoryUpdate& request, std::string& error_message);

    const std::vector<InventoryItem>& inventory_items() const;
    const std::vector<InventoryEventRecord>& event_log() const;
    const std::vector<PendingReviewRecord>& pending_reviews() const;
    const std::vector<InventoryChangeRecord>& inventory_change_log() const;

    InventorySnapshot export_snapshot() const;
    void replace_state_for_persistence(const InventorySnapshot& snapshot);

    std::string last_event_time() const;

private:
    InventoryRuntimeConfig config_;
    std::vector<InventoryItem> inventory_items_;
    std::vector<InventoryEventRecord> event_log_;
    std::vector<PendingReviewRecord> pending_reviews_;
    std::vector<InventoryChangeRecord> change_log_;
    int next_item_id_ = 1;
    int next_event_id_ = 1;
    int next_pending_id_ = 1;
    int next_change_id_ = 1;
};

}  // namespace fridge
