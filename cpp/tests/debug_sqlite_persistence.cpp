#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "inventory_engine.hpp"
#include "sqlite_inventory_store.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::filesystem::path executable_dir(const char* argv0) {
    if (argv0 == nullptr || std::string(argv0).empty()) {
        return std::filesystem::current_path();
    }

    std::error_code error;
    const std::filesystem::path absolute_path = std::filesystem::absolute(argv0, error);
    if (error || absolute_path.parent_path().empty()) {
        return std::filesystem::current_path();
    }
    return absolute_path.parent_path();
}

fridge::InventoryEventInput make_input(
    std::string session_id,
    fridge::EventType event_type,
    std::string category,
    std::string item_name,
    int quantity_delta,
    double remain_level,
    double confidence,
    std::string review_reason
) {
    return fridge::InventoryEventInput{
        std::move(session_id),
        "2026-03-29T14:32:10Z",
        event_type,
        std::move(category),
        std::move(item_name),
        quantity_delta,
        remain_level,
        confidence,
        0.0,
        std::move(review_reason)
    };
}

bool apply_debug_events(fridge::InventoryEngine& engine) {
    const auto put_in = engine.apply_event(make_input(
        "sqlite_put_in",
        fridge::EventType::PutIn,
        "packaged_food",
        "yogurt",
        1,
        1.0,
        0.92,
        ""
    ));
    const auto take_out = engine.apply_event(make_input(
        "sqlite_take_out",
        fridge::EventType::TakeOut,
        "packaged_food",
        "yogurt",
        -1,
        0.0,
        0.90,
        ""
    ));
    const auto partial = engine.apply_event(make_input(
        "sqlite_partial",
        fridge::EventType::PartialTakeOutCandidate,
        "fruit_vegetable",
        "strawberry",
        0,
        0.5,
        0.82,
        "fruit_vegetable partial candidate needs user confirmation"
    ));
    const auto uncertain = engine.apply_event(make_input(
        "sqlite_uncertain",
        fridge::EventType::Uncertain,
        "drink",
        "drink",
        0,
        0.0,
        0.45,
        "uncertain same-count analysis"
    ));
    const auto low_confidence = engine.apply_event(make_input(
        "sqlite_low_confidence",
        fridge::EventType::PutIn,
        "meat_egg_fresh",
        "egg",
        1,
        1.0,
        0.60,
        "low confidence below auto commit threshold"
    ));

    return expect(put_in.inventory_updated, "sqlite debug put_in should update inventory") &&
           expect(take_out.inventory_updated, "sqlite debug matching take_out should update inventory") &&
           expect(partial.pending_created, "sqlite debug partial candidate should create pending") &&
           expect(uncertain.pending_created, "sqlite debug uncertain event should create pending") &&
           expect(low_confidence.pending_created, "sqlite debug low-confidence put_in should create pending");
}

bool debug_sqlite_persistence(const std::filesystem::path& db_path) {
    std::error_code cleanup_error;
    std::filesystem::remove(db_path, cleanup_error);

    fridge::InventoryRuntimeConfig config;
    config.auto_commit_min_confidence = 0.75;
    fridge::InventoryEngine engine(config);
    if (!apply_debug_events(engine)) {
        return false;
    }

    fridge::SQLiteInventoryStore store(db_path);
    std::string error_message;
    if (!store.open(error_message) ||
        !store.initialize_schema(error_message) ||
        !store.reset(error_message) ||
        !store.save_snapshot(engine, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::InventoryEngine reloaded(config);
    if (!store.load_snapshot(reloaded, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    const bool first_reload_ok =
        expect(reloaded.inventory_items().size() == engine.inventory_items().size(),
               "sqlite reload should preserve inventory item count") &&
        expect(reloaded.event_log().size() == 5, "sqlite reload should preserve five event records") &&
        expect(reloaded.pending_reviews().size() == 3, "sqlite reload should preserve three pending records") &&
        expect(reloaded.inventory_change_log().size() == 2, "sqlite reload should preserve two change records");
    if (!first_reload_ok) {
        return false;
    }

    if (!reloaded.confirm_pending(
            fridge::PendingDecision{
                "sqlite_uncertain",
                "reject",
                "drink",
                "drink",
                0,
                0.0,
                "sqlite debug reject uncertain event after reload"
            },
            error_message
        )) {
        std::cerr << error_message << "\n";
        return false;
    }

    if (!store.save_snapshot(reloaded, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::InventoryEngine reloaded_again(config);
    if (!store.load_snapshot(reloaded_again, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    std::cout
        << "sqlite_persistence_debug: inventory=" << reloaded_again.inventory_items().size()
        << " events=" << reloaded_again.event_log().size()
        << " pending=" << reloaded_again.pending_reviews().size()
        << " changes=" << reloaded_again.inventory_change_log().size()
        << " db=" << db_path.generic_string() << "\n";

    return expect(reloaded_again.event_log().size() == 5, "second sqlite reload should preserve event log") &&
           expect(reloaded_again.pending_reviews().size() == 2, "second sqlite reload should preserve rejected pending state") &&
           expect(reloaded_again.inventory_change_log().size() == 2, "rejecting pending should not create an inventory change");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output_dir = executable_dir(argc > 0 ? argv[0] : nullptr) /
        "sqlite_persistence_debug";
    const std::filesystem::path db_path = output_dir / "fridge_inventory.db";

    if (!debug_sqlite_persistence(db_path)) {
        return EXIT_FAILURE;
    }

    std::cout << "sqlite_persistence_debug passed\n";
    return EXIT_SUCCESS;
}
