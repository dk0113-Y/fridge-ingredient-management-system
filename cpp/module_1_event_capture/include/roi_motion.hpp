#pragma once

#include "types.hpp"

namespace fridge {

struct RoiMotionConfig {
    BoundingBox roi{};
    int pixel_delta_threshold = 8;
    int min_region_area_pixels = 4;
    bool compensate_global_brightness = true;
};

MotionSummary summarize_motion(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config = {}
);

ChangeAnalysis analyze_change(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config = {}
);

}  // namespace fridge
