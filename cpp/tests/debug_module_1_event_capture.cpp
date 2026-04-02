#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "frame_selector.hpp"
#include "roi_motion.hpp"
#include "runtime_config.hpp"
#include "stable_state_capture.hpp"

namespace {

using fridge::GrayFrame;

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

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path resolve_config_path() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "configs" / "module_1_event_capture.cfg",
        std::filesystem::current_path() / "configs" / "module_1_event_capture.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_1_event_capture.cfg",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "configs" / "module_1_event_capture.cfg";
}

bool debug_config_and_keyframe_selection() {
    fridge::VisionPipelineConfig config;
    std::string error_message;
    if (!fridge::load_pipeline_config(resolve_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    auto before = make_frame(12, 12, 140, 0);
    auto disturbed = make_frame(12, 12, 140, 1);
    auto after = make_frame(12, 12, 140, 2);
    paint_box(disturbed, 3, 3, 4, 4, 20);
    paint_box(after, 3, 3, 4, 4, 30);

    const std::vector<GrayFrame> frames = {before, disturbed, after};
    const auto selected = fridge::select_keyframes(frames, config.motion_config, config.frame_selector_config);
    const auto motion = fridge::summarize_motion(selected.before_frame, selected.after_frame, config.motion_config);

    std::cout
        << "module_1_debug: roi_id=" << config.roi_id
        << " before_index=" << selected.before_index
        << " after_index=" << selected.after_index
        << " changed_ratio=" << motion.changed_ratio
        << " mean_delta=" << motion.mean_delta << "\n";

    return expect(selected.before_index == 0, "module 1 should keep the stable first frame as before") &&
           expect(selected.after_index == 2, "module 1 should keep the settled last frame as after") &&
           expect(motion.changed_ratio > 0.0, "module 1 motion summary should detect a changed ROI");
}

bool debug_stable_state_capture() {
    fridge::VisionPipelineConfig config;
    std::string error_message;
    if (!fridge::load_pipeline_config(resolve_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::StableStateCapture capture(config.motion_config, config.frame_selector_config);
    std::optional<fridge::StableCaptureEvent> completed_event;

    int frame_index = 0;
    for (int index = 0; index < 12; ++index) {
        auto frame = make_frame(20, 20, 140, frame_index++);
        completed_event = capture.push_frame(frame);
        if (completed_event.has_value()) {
            break;
        }
    }

    auto moving = make_frame(20, 20, 140, frame_index++);
    paint_box(moving, 6, 6, 6, 6, 40);
    completed_event = capture.push_frame(moving);
    auto settled = make_frame(20, 20, 140, frame_index++);
    paint_box(settled, 6, 6, 6, 6, 40);
    for (int index = 0; index < 8 && !completed_event.has_value(); ++index) {
        completed_event = capture.push_frame(settled);
        settled.index = frame_index++;
    }

    if (!completed_event.has_value()) {
        std::cerr << "[FAIL] stable state capture did not emit an event\n";
        return false;
    }

    const auto& event = *completed_event;
    std::cout
        << "module_1_stable_capture: total_frame_count=" << event.total_frame_count
        << " peak_interframe_ratio=" << event.peak_interframe_ratio
        << " peak_baseline_ratio=" << event.peak_baseline_ratio
        << " final_change_ratio=" << event.final_change.summary.changed_ratio << "\n";

    return expect(event.selected_frames.before_frame.index == 11, "baseline frame should come from the stable pre-event state") &&
           expect(event.selected_frames.after_frame.index >= 13, "after frame should come from the settled post-event state") &&
           expect(event.selected_frames.final_change_ratio > 0.0, "stable state capture should keep a non-zero final change ratio") &&
           expect(event.selected_frames.stable_after_run_length >= config.frame_selector_config.settle_run_frames,
                  "after frame should only be emitted after a full settled run");
}

bool debug_transient_disturbance_is_ignored() {
    fridge::VisionPipelineConfig config;
    std::string error_message;
    if (!fridge::load_pipeline_config(resolve_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::StableStateCapture capture(config.motion_config, config.frame_selector_config);
    std::optional<fridge::StableCaptureEvent> completed_event;

    int frame_index = 0;
    for (int index = 0; index < 12; ++index) {
        completed_event = capture.push_frame(make_frame(20, 20, 140, frame_index++));
    }

    auto occluded = make_frame(20, 20, 140, frame_index++);
    paint_box(occluded, 4, 4, 8, 8, 40);
    completed_event = capture.push_frame(occluded);
    for (int index = 0; index < 8 && !completed_event.has_value(); ++index) {
        completed_event = capture.push_frame(make_frame(20, 20, 140, frame_index++));
    }

    return expect(!completed_event.has_value(), "transient occlusion that returns to baseline should not emit a session");
}

bool debug_tiny_residual_change_is_ignored() {
    fridge::VisionPipelineConfig config;
    std::string error_message;
    if (!fridge::load_pipeline_config(resolve_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::StableStateCapture capture(config.motion_config, config.frame_selector_config);
    std::optional<fridge::StableCaptureEvent> completed_event;

    int frame_index = 0;
    for (int index = 0; index < 12; ++index) {
        completed_event = capture.push_frame(make_frame(640, 480, 140, frame_index++));
    }

    auto disturbed = make_frame(640, 480, 140, frame_index++);
    paint_box(disturbed, 220, 120, 180, 120, 40);
    completed_event = capture.push_frame(disturbed);

    auto nearly_restored = make_frame(640, 480, 140, frame_index++);
    paint_box(nearly_restored, 620, 4, 6, 6, 40);
    for (int index = 0; index < 8 && !completed_event.has_value(); ++index) {
        completed_event = capture.push_frame(nearly_restored);
        nearly_restored.index = frame_index++;
    }

    return expect(
        !completed_event.has_value(),
        "tiny residual change after a strong transient disturbance should not emit a session"
    );
}

}  // namespace

int main() {
    if (!debug_config_and_keyframe_selection() ||
        !debug_stable_state_capture() ||
        !debug_transient_disturbance_is_ignored() ||
        !debug_tiny_residual_change_is_ignored()) {
        return EXIT_FAILURE;
    }

    std::cout << "module_1_debug passed\n";
    return EXIT_SUCCESS;
}
