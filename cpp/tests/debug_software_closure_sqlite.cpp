#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

#include "inventory_engine.hpp"
#include "local_service.hpp"
#include "software_closure.hpp"
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

fridge::EventResult make_event(
    std::string session_id,
    fridge::EventType event_type,
    std::string coarse_class,
    std::string fine_name,
    int quantity_delta,
    double remain_level,
    double confidence
) {
    fridge::EventResult event;
    event.session_id = std::move(session_id);
    event.timestamp = "2026-03-29T14:32:10Z";
    event.event_type = event_type;
    event.confidence = confidence;
    event.before_frame = "before.pgm";
    event.after_frame = "after.pgm";
    event.need_user_confirm = fridge::needs_manual_review(event_type);
    event.objects.push_back(fridge::DetectedObject{
        std::move(coarse_class),
        std::move(fine_name),
        quantity_delta,
        remain_level
    });
    return event;
}

fridge::SoftwareClosureEvidencePaths paths_for(
    const std::filesystem::path& root,
    const std::string& session_id
) {
    const std::filesystem::path final_dir = root / session_id / "final";
    return fridge::SoftwareClosureEvidencePaths{
        final_dir / "event.json",
        final_dir / "inventory_response.json",
        final_dir / "events_response.json",
        final_dir / "pending_response.json",
        final_dir / "software_closure_report.json"
    };
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

bool write_final_event_file(const fridge::EventResult& event, const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        std::cerr << "Failed to open final event output: " << path.generic_string() << "\n";
        return false;
    }
    output << "{\n"
           << "  \"session_id\": \"" << event.session_id << "\",\n"
           << "  \"event_type\": \"" << fridge::to_string(event.event_type) << "\",\n"
           << "  \"timestamp\": \"" << event.timestamp << "\"\n"
           << "}\n";
    return static_cast<bool>(output);
}

bool run_sqlite_closure(
    fridge::InventoryEngine& engine,
    const fridge::LocalServiceFacade& facade,
    const fridge::EventResult& event,
    const std::filesystem::path& artifact_root,
    const std::filesystem::path& db_path,
    bool reset_database,
    fridge::SoftwareClosureResult& result
) {
    const fridge::SoftwareClosureEvidencePaths paths = paths_for(artifact_root, event.session_id);
    std::string error_message;
    if (!write_final_event_file(event, paths.final_event_path)) {
        return false;
    }

    const fridge::SoftwareClosureContext context{
        "mock",
        "debug_software_closure_sqlite",
        "",
        "deterministic mock/debug evidence; not real ONNX, camera, or board validation",
        true
    };
    const fridge::SoftwareClosurePersistenceOptions persistence_options{
        true,
        db_path,
        reset_database
    };

    if (!fridge::write_software_closure_evidence_with_sqlite_persistence(
            engine,
            facade,
            event,
            paths,
            context,
            "",
            persistence_options,
            result,
            error_message
        )) {
        std::cerr << error_message << "\n";
        return false;
    }

    const std::string report_text = read_text(paths.software_closure_report_path);
    return expect(std::filesystem::exists(paths.software_closure_report_path),
                  "software_closure_report.json should exist") &&
           expect(report_text.find("\"sqlite_requested\": true") != std::string::npos,
                  "report should expose sqlite_requested=true") &&
           expect(report_text.find("\"sqlite_compiled\": true") != std::string::npos,
                  "report should expose sqlite_compiled=true") &&
           expect(report_text.find("\"sqlite_db_ready\": true") != std::string::npos,
                  "report should expose sqlite_db_ready=true") &&
           expect(report_text.find("\"sqlite_saved_after_apply\": true") != std::string::npos,
                  "report should expose sqlite_saved_after_apply=true") &&
           expect(report_text.find("\"sqlite_database_path\":") != std::string::npos,
                  "report should expose sqlite_database_path") &&
           expect(report_text.find("\"sqlite_status_message\":") != std::string::npos,
                  "report should expose sqlite_status_message");
}

bool load_snapshot(
    const std::filesystem::path& db_path,
    fridge::InventoryEngine& engine
) {
    fridge::SQLiteInventoryStore store(db_path);
    std::string error_message;
    if (!store.open(error_message) ||
        !store.initialize_schema(error_message) ||
        !store.load_snapshot(engine, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }
    return true;
}

bool debug_software_closure_sqlite(const std::filesystem::path& output_dir) {
    std::error_code cleanup_error;
    std::filesystem::remove_all(output_dir, cleanup_error);

    const std::filesystem::path artifact_root = output_dir / "artifacts";
    const std::filesystem::path db_path = output_dir / "fridge_inventory.db";

    fridge::InventoryRuntimeConfig config;
    config.auto_commit_min_confidence = 0.75;
    fridge::LocalServiceFacade facade;

    fridge::InventoryEngine first_engine(config);
    fridge::SoftwareClosureResult put_in_result;
    if (!run_sqlite_closure(
            first_engine,
            facade,
            make_event("sqlite_closure_put_in", fridge::EventType::PutIn, "packaged_food", "yogurt", 1, 1.0, 0.92),
            artifact_root,
            db_path,
            true,
            put_in_result
        )) {
        return false;
    }

    fridge::InventoryEngine loaded_after_put_in(config);
    if (!load_snapshot(db_path, loaded_after_put_in)) {
        return false;
    }
    if (!expect(loaded_after_put_in.inventory_items().size() == 1,
                "fresh engine should load one persisted inventory item") ||
        !expect(loaded_after_put_in.inventory_items().front().count == 1,
                "fresh engine should load the put_in count") ||
        !expect(loaded_after_put_in.event_log().size() == 1,
                "fresh engine should load the first event log record")) {
        return false;
    }

    fridge::InventoryEngine second_engine(config);
    fridge::SoftwareClosureResult take_out_result;
    if (!run_sqlite_closure(
            second_engine,
            facade,
            make_event("sqlite_closure_take_out", fridge::EventType::TakeOut, "packaged_food", "yogurt", -1, 0.0, 0.90),
            artifact_root,
            db_path,
            false,
            take_out_result
        )) {
        return false;
    }
    if (!expect(take_out_result.persistence_status.loaded_existing_state,
                "second closure should load existing SQLite state") ||
        !expect(take_out_result.apply_result.inventory_updated,
                "take_out should mutate the loaded inventory item")) {
        return false;
    }

    fridge::InventoryEngine loaded_after_take_out(config);
    if (!load_snapshot(db_path, loaded_after_take_out)) {
        return false;
    }
    if (!expect(loaded_after_take_out.inventory_items().size() == 1,
                "take_out reload should preserve the inventory item") ||
        !expect(loaded_after_take_out.inventory_items().front().count == 0,
                "take_out reload should persist the decremented count") ||
        !expect(loaded_after_take_out.event_log().size() == 2,
                "take_out reload should preserve both event records")) {
        return false;
    }

    fridge::InventoryEngine pending_engine(config);
    fridge::SoftwareClosureResult partial_result;
    if (!run_sqlite_closure(
            pending_engine,
            facade,
            make_event(
                "sqlite_closure_partial",
                fridge::EventType::PartialTakeOutCandidate,
                "fruit_vegetable",
                "strawberry",
                0,
                0.5,
                0.82
            ),
            artifact_root,
            db_path,
            false,
            partial_result
        )) {
        return false;
    }

    fridge::InventoryEngine loaded_after_pending(config);
    if (!load_snapshot(db_path, loaded_after_pending)) {
        return false;
    }

    std::cout
        << "software_closure_sqlite_debug: inventory=" << loaded_after_pending.inventory_items().size()
        << " events=" << loaded_after_pending.event_log().size()
        << " pending=" << loaded_after_pending.pending_reviews().size()
        << " db=" << db_path.generic_string() << "\n";

    return expect(partial_result.apply_result.pending_created,
                  "partial_take_out_candidate should create pending review") &&
           expect(loaded_after_pending.event_log().size() == 3,
                  "pending reload should preserve all three event records") &&
           expect(loaded_after_pending.pending_reviews().size() == 1,
                  "pending reload should preserve one pending review");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output_dir =
        executable_dir(argc > 0 ? argv[0] : nullptr) / "software_closure_sqlite_debug";

    if (!debug_software_closure_sqlite(output_dir)) {
        return EXIT_FAILURE;
    }

    std::cout << "software_closure_sqlite_debug passed\n";
    return EXIT_SUCCESS;
}
