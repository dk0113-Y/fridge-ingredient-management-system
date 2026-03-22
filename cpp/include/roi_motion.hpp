#pragma once

#include "types.hpp"

namespace fridge {

struct RoiMotionConfig {
    int pixel_delta_threshold = 8;
};

MotionSummary summarize_motion(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config = {}
);

}  // namespace fridge
