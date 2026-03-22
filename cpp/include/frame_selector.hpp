#pragma once

#include <vector>

#include "roi_motion.hpp"
#include "types.hpp"

namespace fridge {

SelectedFrames select_keyframes(
    const std::vector<GrayFrame>& frames,
    const RoiMotionConfig& motion_config = {}
);

}  // namespace fridge
