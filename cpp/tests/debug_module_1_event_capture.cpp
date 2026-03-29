#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "frame_selector.hpp"
#include "roi_motion.hpp"
#include "runtime_config.hpp"

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

}  // namespace

int main() {
    if (!debug_config_and_keyframe_selection()) {
        return EXIT_FAILURE;
    }

    std::cout << "module_1_debug passed\n";
    return EXIT_SUCCESS;
}
