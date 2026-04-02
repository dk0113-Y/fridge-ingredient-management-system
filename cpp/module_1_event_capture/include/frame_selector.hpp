#pragma once

#include <vector>

#include "roi_motion.hpp"
#include "types.hpp"

namespace fridge {

struct FrameSelectorConfig {
    double stable_ratio_threshold = 0.004;
    double motion_ratio_threshold = 0.015;
    double baseline_change_ratio_threshold = 0.020;
    double persistent_change_ratio_threshold = 0.010;
    double black_frame_mean_threshold = 5.0;
    std::size_t min_stable_run_frames = 2;
    std::size_t baseline_warmup_frames = 10;
    std::size_t disturbance_trigger_frames = 2;
    std::size_t settle_run_frames = 5;
    std::size_t post_event_cooldown_frames = 12;
    std::size_t max_disturbance_frames = 180;
};

SelectedFrames select_keyframes(
    const std::vector<GrayFrame>& frames,
    const RoiMotionConfig& motion_config = {},
    const FrameSelectorConfig& selector_config = {}
);

}  // namespace fridge
