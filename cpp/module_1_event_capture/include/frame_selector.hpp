#pragma once

#include <vector>

#include "roi_motion.hpp"
#include "types.hpp"

namespace fridge {

struct FrameSelectorConfig {
    double stable_ratio_threshold = 0.004;
    double motion_ratio_threshold = 0.015;
    double black_frame_mean_threshold = 5.0;
    std::size_t min_stable_run_frames = 2;
};

SelectedFrames select_keyframes(
    const std::vector<GrayFrame>& frames,
    const RoiMotionConfig& motion_config = {},
    const FrameSelectorConfig& selector_config = {}
);

}  // namespace fridge
