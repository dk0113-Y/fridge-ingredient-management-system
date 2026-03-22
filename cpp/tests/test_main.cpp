#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "debug_report.hpp"
#include "event_detector.hpp"
#include "frame_selector.hpp"
#include "runtime_config.hpp"
#include "video_io.hpp"

namespace {

using fridge::EventDetector;
using fridge::EventType;
using fridge::GrayFrame;
using fridge::SelectedFrames;

GrayFrame make_frame(int width, int height, std::uint8_t fill, int index) {
    GrayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.index = index;
    frame.pixels.assign(static_cast<std::size_t>(width * height), fill);
    return frame;
}

void paint_box(GrayFrame& frame, int x0, int y0, int box_width, int box_height, std::uint8_t value) {
    for (int y = y0; y < y0 + box_height; ++y) {
        for (int x = x0; x < x0 + box_width; ++x) {
            frame.pixels[static_cast<std::size_t>(y * frame.width + x)] = value;
        }
    }
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool test_no_change_identical_frames() {
    const auto frame = make_frame(8, 8, 120, 0);
    SelectedFrames selected{frame, frame, 0, 0, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_no_change", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::NoChange, "identical frames should be no_change");
}

bool test_put_in_detected() {
    const auto before = make_frame(8, 8, 180, 0);
    auto after = make_frame(8, 8, 180, 1);
    paint_box(after, 2, 2, 4, 4, 30);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_put_in", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::PutIn, "dark new object should be put_in");
}

bool test_take_out_detected() {
    auto before = make_frame(8, 8, 180, 0);
    paint_box(before, 2, 2, 4, 4, 30);
    const auto after = make_frame(8, 8, 180, 1);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_take_out", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::TakeOut, "removing a dark object should be take_out");
}

bool test_partial_candidate_detected() {
    auto before = make_frame(8, 8, 180, 0);
    paint_box(before, 2, 2, 4, 4, 40);
    auto after = make_frame(8, 8, 180, 1);
    paint_box(after, 2, 2, 4, 4, 90);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_partial", "before.jpg", "after.jpg");
    return expect(
        result.event_type == EventType::PartialTakeOutCandidate && result.need_user_confirm,
        "partial removal with visible residue should be partial_take_out_candidate"
    );
}

bool test_low_light_put_in() {
    const auto before = make_frame(8, 8, 40, 0);
    auto after = make_frame(8, 8, 40, 1);
    paint_box(after, 2, 2, 4, 4, 5);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_low_light", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::PutIn, "low-light block addition should still look like put_in");
}

bool test_bright_object_put_in_detected() {
    const auto before = make_frame(8, 8, 120, 0);
    auto after = make_frame(8, 8, 120, 1);
    paint_box(after, 2, 2, 4, 4, 220);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_bright_put_in", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::PutIn, "bright new object should still be put_in");
}

bool test_bright_object_take_out_detected() {
    auto before = make_frame(8, 8, 120, 0);
    paint_box(before, 2, 2, 4, 4, 220);
    const auto after = make_frame(8, 8, 120, 1);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_bright_take_out", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::TakeOut, "removing a bright object should still be take_out");
}

bool test_hand_occlusion_sequence_returns_no_change() {
    const auto base = make_frame(8, 8, 140, 0);
    auto occluded = make_frame(8, 8, 140, 1);
    paint_box(occluded, 1, 1, 6, 6, 20);
    auto frames = std::vector<GrayFrame>{base, occluded, base};
    const auto selected = fridge::select_keyframes(frames);
    EventDetector detector;
    const auto result = detector.detect(selected, "test_occlusion", "before.jpg", "after.jpg");
    return expect(
        result.event_type == EventType::NoChange,
        "temporary hand occlusion with no final state change should be no_change"
    );
}

bool test_roi_ignores_changes_outside_configured_box() {
    const auto before = make_frame(8, 8, 120, 0);
    auto after = make_frame(8, 8, 120, 1);
    paint_box(after, 5, 5, 3, 3, 20);

    SelectedFrames selected{before, after, 0, 1, {}};
    fridge::RoiMotionConfig motion_config;
    motion_config.roi = fridge::BoundingBox{0, 0, 4, 4};

    EventDetector detector({}, motion_config);
    const auto result = detector.detect(selected, "test_roi_ignore", "before.jpg", "after.jpg");
    return expect(
        result.event_type == EventType::NoChange && result.change_regions.empty(),
        "changes outside the configured ROI should not trigger an event"
    );
}

bool test_rearrange_without_inventory_change_returns_no_change() {
    auto before = make_frame(8, 8, 180, 0);
    paint_box(before, 1, 1, 2, 2, 40);

    auto after = make_frame(8, 8, 180, 1);
    paint_box(after, 5, 5, 2, 2, 40);

    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_rearrange", "before.jpg", "after.jpg");
    return expect(
        result.event_type == EventType::NoChange && result.change_regions.size() >= 2,
        "rearranging an object inside the fridge should stay no_change"
    );
}

bool test_event_json_contains_required_schema_fields() {
    const auto before = make_frame(8, 8, 180, 0);
    auto after = make_frame(8, 8, 180, 1);
    paint_box(after, 2, 2, 4, 4, 30);

    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "schema_test", "before.jpg", "after.jpg");
    const std::string json = fridge::event_result_to_json(result);

    return expect(
        json.find("\"session_id\"") != std::string::npos &&
        json.find("\"event_type\": \"put_in\"") != std::string::npos &&
        json.find("\"change_regions\"") != std::string::npos &&
        json.find("\"objects\"") != std::string::npos &&
        json.find("\"need_user_confirm\"") != std::string::npos,
        "serialized event json should keep the required stage-1 schema fields"
    );
}

bool test_pipeline_config_file_is_loaded() {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "fridge_stage1_test_config.cfg";
    {
        std::ofstream output(temp_path);
        output << "roi_id = shelf_a\n";
        output << "roi = 10,20,30,40\n";
        output << "pixel_delta_threshold = 11\n";
        output << "motion_ratio_threshold = 0.025\n";
        output << "background_like_threshold = 15.5\n";
    }

    fridge::VisionPipelineConfig config;
    std::string error_message;
    const bool ok = fridge::load_pipeline_config(temp_path, config, error_message);
    std::filesystem::remove(temp_path);

    return expect(
        ok &&
        config.roi_id == "shelf_a" &&
        config.motion_config.roi.x == 10 &&
        config.motion_config.roi.y == 20 &&
        config.motion_config.roi.width == 30 &&
        config.motion_config.roi.height == 40 &&
        config.motion_config.pixel_delta_threshold == 11 &&
        config.frame_selector_config.motion_ratio_threshold == 0.025 &&
        config.detector_config.background_like_threshold == 15.5,
        "pipeline config file should override the default stage-1 parameters"
    );
}

bool test_full_frame_roi_config_is_allowed() {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "fridge_stage1_full_frame_config.cfg";
    {
        std::ofstream output(temp_path);
        output << "roi = 0,0,0,0\n";
    }

    fridge::VisionPipelineConfig config;
    std::string error_message;
    const bool ok = fridge::load_pipeline_config(temp_path, config, error_message);
    std::filesystem::remove(temp_path);

    return expect(
        ok &&
        config.motion_config.roi.width == 0 &&
        config.motion_config.roi.height == 0,
        "config should allow 0,0,0,0 to mean full-frame ROI"
    );
}

bool test_debug_summary_is_written() {
    const auto before = make_frame(8, 8, 180, 0);
    auto after = make_frame(8, 8, 180, 1);
    paint_box(after, 2, 2, 4, 4, 30);

    SelectedFrames selected{before, after, 0, 1, {}};
    selected.transitions.push_back(fridge::summarize_motion(before, after));

    EventDetector detector;
    const auto result = detector.detect(selected, "debug_test", "before.jpg", "after.jpg");

    fridge::VisionPipelineConfig config;
    config.roi_id = "debug_roi";

    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "fridge_stage1_debug_test";
    const std::filesystem::path output_path = temp_dir / "debug.json";
    std::filesystem::create_directories(temp_dir);

    std::string error_message;
    const bool ok = fridge::write_debug_summary(
        selected,
        result,
        config,
        fridge::DebugArtifacts{
            "input.mp4",
            "config.cfg",
            "before.jpg",
            "after.jpg",
            "overlay.jpg",
            "event.json"
        },
        2,
        output_path,
        error_message
    );

    std::string summary_text;
    if (ok) {
        std::ifstream input(output_path);
        summary_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    std::filesystem::remove_all(temp_dir);

    return expect(
        ok &&
        summary_text.find("\"roi_id\": \"debug_roi\"") != std::string::npos &&
        summary_text.find("\"event_type\": \"put_in\"") != std::string::npos &&
        summary_text.find("\"overlay_frame\": \"overlay.jpg\"") != std::string::npos &&
        summary_text.find("\"transitions\"") != std::string::npos,
        "debug summary should be written with selection and event details"
    );
}

bool test_overlay_frame_marks_change_regions() {
    auto after = make_frame(10, 10, 120, 1);
    fridge::BoundingBox roi{1, 1, 8, 8};
    std::vector<fridge::ChangeRegion> regions = {
        {fridge::BoundingBox{3, 3, 5, 5}, 1.0}
    };

    const auto overlay = fridge::build_overlay_frame(after, roi, regions);

    return expect(
        overlay.width == after.width &&
        overlay.height == after.height &&
        overlay.at(0, 0) == 120 &&
        overlay.at(1, 1) == 255 &&
        overlay.at(2, 2) == 255 &&
        overlay.at(3, 3) == 255 &&
        overlay.at(5, 5) == 0,
        "overlay frame should draw ROI and change-region borders"
    );
}

bool test_trailing_black_frame_is_not_selected_as_after() {
    const auto before = make_frame(8, 8, 140, 0);
    auto settled_after = make_frame(8, 8, 140, 1);
    paint_box(settled_after, 2, 2, 4, 4, 30);

    auto stable_after = settled_after;
    stable_after.index = 2;

    const auto trailing_black = make_frame(8, 8, 0, 3);
    auto frames = std::vector<GrayFrame>{before, settled_after, stable_after, trailing_black};

    const auto selected = fridge::select_keyframes(frames);
    EventDetector detector;
    const auto result = detector.detect(selected, "test_trailing_black", "before.jpg", "after.jpg");
    return expect(
        selected.after_index == 2 && result.event_type == EventType::PutIn,
        "trailing black frames should not override the settled after frame"
    );
}

}  // namespace

int main() {
    const std::vector<std::function<bool()>> tests = {
        test_no_change_identical_frames,
        test_put_in_detected,
        test_take_out_detected,
        test_partial_candidate_detected,
        test_low_light_put_in,
        test_bright_object_put_in_detected,
        test_bright_object_take_out_detected,
        test_hand_occlusion_sequence_returns_no_change,
        test_roi_ignores_changes_outside_configured_box,
        test_rearrange_without_inventory_change_returns_no_change,
        test_event_json_contains_required_schema_fields,
        test_pipeline_config_file_is_loaded,
        test_full_frame_roi_config_is_allowed,
        test_debug_summary_is_written,
        test_overlay_frame_marks_change_regions,
        test_trailing_black_frame_is_not_selected_as_after
    };

    int failed = 0;
    for (const auto& test : tests) {
        if (!test()) {
            ++failed;
        }
    }

    if (failed > 0) {
        std::cerr << failed << " C++ tests failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All C++ tests passed.\n";
    return EXIT_SUCCESS;
}
