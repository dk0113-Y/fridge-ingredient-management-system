#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "event_detector.hpp"
#include "inventory_engine.hpp"
#include "local_service.hpp"
#include "software_closure.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
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

bool run_closure(
    fridge::InventoryEngine& engine,
    const fridge::LocalServiceFacade& facade,
    const fridge::EventResult& event,
    const std::filesystem::path& root,
    const std::string& review_reason,
    fridge::SoftwareClosureResult& result
) {
    const fridge::SoftwareClosureEvidencePaths paths = paths_for(root, event.session_id);
    std::string error_message;
    if (!fridge::write_event_json(event, paths.final_event_path, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    const fridge::SoftwareClosureContext context{
        "mock",
        "debug_software_closure_test",
        "",
        "deterministic mock/debug evidence; not real ONNX, camera, or board validation"
    };
    if (!fridge::write_software_closure_evidence(
            engine,
            facade,
            event,
            paths,
            context,
            review_reason,
            result,
            error_message
        )) {
        std::cerr << error_message << "\n";
        return false;
    }

    return expect(std::filesystem::exists(paths.final_event_path), "final/event.json should exist") &&
           expect(std::filesystem::exists(paths.inventory_response_path), "inventory_response.json should exist") &&
           expect(std::filesystem::exists(paths.events_response_path), "events_response.json should exist") &&
           expect(std::filesystem::exists(paths.pending_response_path), "pending_response.json should exist") &&
           expect(std::filesystem::exists(paths.software_closure_report_path), "software_closure_report.json should exist");
}

bool debug_software_closure() {
    const std::filesystem::path root =
        std::filesystem::current_path() / "software_closure_debug_artifacts";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    fridge::InventoryRuntimeConfig config;
    config.auto_commit_min_confidence = 0.75;
    fridge::InventoryEngine engine(config);
    fridge::LocalServiceFacade facade;

    fridge::SoftwareClosureResult put_in_result;
    if (!run_closure(
            engine,
            facade,
            make_event("closure_put_in", fridge::EventType::PutIn, "packaged_food", "", 1, 1.0, 0.92),
            root,
            "coarse-class count increased only in after frame for packaged_food",
            put_in_result
        )) {
        return false;
    }

    fridge::SoftwareClosureResult take_out_result;
    if (!run_closure(
            engine,
            facade,
            make_event("closure_take_out", fridge::EventType::TakeOut, "packaged_food", "packaged_food", -1, 0.0, 0.90),
            root,
            "coarse-class count decreased only in after frame for packaged_food",
            take_out_result
        )) {
        return false;
    }

    fridge::SoftwareClosureResult partial_result;
    if (!run_closure(
            engine,
            facade,
            make_event(
                "closure_partial",
                fridge::EventType::PartialTakeOutCandidate,
                "fruit_vegetable",
                "strawberry",
                0,
                0.5,
                0.82
            ),
            root,
            "fruit_vegetable detections kept count and position but changed visible area",
            partial_result
        )) {
        return false;
    }

    fridge::SoftwareClosureResult uncertain_result;
    if (!run_closure(
            engine,
            facade,
            make_event("closure_uncertain", fridge::EventType::Uncertain, "drink", "drink", 0, 0.0, 0.45),
            root,
            "same-count analysis did not produce a stable classification",
            uncertain_result
        )) {
        return false;
    }

    fridge::SoftwareClosureResult low_confidence_result;
    if (!run_closure(
            engine,
            facade,
            make_event("closure_low_confidence", fridge::EventType::PutIn, "meat_egg_fresh", "egg", 1, 1.0, 0.60),
            root,
            "low yolo confidence below inventory auto-commit threshold",
            low_confidence_result
        )) {
        return false;
    }

    std::cout
        << "software_closure_debug: inventory=" << engine.inventory_items().size()
        << " events=" << engine.event_log().size()
        << " pending=" << engine.pending_reviews().size()
        << " artifacts=" << root.generic_string() << "\n";

    return expect(put_in_result.apply_result.inventory_updated, "put_in should mutate inventory") &&
           expect(!put_in_result.apply_result.pending_created, "high-confidence put_in should not create pending") &&
           expect(take_out_result.apply_result.inventory_updated, "matching take_out should mutate inventory") &&
           expect(partial_result.apply_result.pending_created, "partial_take_out_candidate should create pending") &&
           expect(uncertain_result.apply_result.pending_created, "uncertain should create pending") &&
           expect(low_confidence_result.apply_result.pending_created, "low-confidence put_in should create pending") &&
           expect(engine.event_log().size() == 5, "software closure should record every event") &&
           expect(engine.pending_reviews().size() == 3, "software closure should retain three pending reviews");
}

}  // namespace

int main() {
    if (!debug_software_closure()) {
        return EXIT_FAILURE;
    }

    std::cout << "software_closure_debug passed\n";
    return EXIT_SUCCESS;
}
