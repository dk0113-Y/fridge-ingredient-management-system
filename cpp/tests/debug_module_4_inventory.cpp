#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "inventory_engine.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path resolve_config_path() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "configs" / "module_4_inventory.cfg",
        std::filesystem::current_path() / "configs" / "module_4_inventory.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_4_inventory.cfg",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "configs" / "module_4_inventory.cfg";
}

bool debug_inventory_flow() {
    fridge::InventoryRuntimeConfig config;
    std::string error_message;
    if (!fridge::load_inventory_runtime_config(resolve_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::InventoryEngine engine(config);

    const auto commit_result = engine.apply_event(fridge::InventoryEventInput{
        "session_put_in",
        "2026-03-29 14:32:10",
        fridge::EventType::PutIn,
        "packaged_food",
        "yogurt",
        1,
        1.0,
        0.90,
        0.85,
        ""
    });

    const auto pending_result = engine.apply_event(fridge::InventoryEventInput{
        "session_partial",
        "2026-03-29 14:40:00",
        fridge::EventType::PartialTakeOutCandidate,
        "fruit_vegetable",
        "strawberry",
        0,
        0.5,
        0.88,
        0.0,
        "partial candidate needs user confirmation"
    });

    const bool confirm_ok = engine.confirm_pending(
        fridge::PendingDecision{
            "session_partial",
            "confirm_partial_take",
            "strawberry",
            "fruit_vegetable",
            0,
            0.5,
            "debug confirm"
        },
        error_message
    );

    const bool manual_ok = engine.manual_update(
        fridge::ManualInventoryUpdate{
            "cola",
            "drink",
            2,
            1.0,
            "2026-04-30",
            "debug manual update"
        },
        error_message
    );

    std::cout
        << "module_4_debug: inventory=" << engine.inventory_items().size()
        << " events=" << engine.event_log().size()
        << " pending=" << engine.pending_reviews().size()
        << " changes=" << engine.inventory_change_log().size() << "\n";

    return expect(commit_result.inventory_updated, "module 4 should commit a high-confidence put_in event") &&
           expect(pending_result.pending_created, "module 4 should queue partial_take_out_candidate for review") &&
           expect(confirm_ok, "module 4 should confirm a pending review item") &&
           expect(manual_ok, "module 4 should support manual inventory updates") &&
           expect(engine.pending_reviews().empty(), "module 4 pending queue should be empty after confirmation") &&
           expect(engine.inventory_items().size() >= 2, "module 4 inventory should contain committed and manual items");
}

}  // namespace

int main() {
    if (!debug_inventory_flow()) {
        return EXIT_FAILURE;
    }

    std::cout << "module_4_debug passed\n";
    return EXIT_SUCCESS;
}
