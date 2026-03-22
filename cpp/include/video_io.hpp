#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace fridge {

bool load_frames(const std::string& input_path, std::vector<GrayFrame>& frames, std::string& error_message);

bool write_debug_image(const GrayFrame& frame, const std::string& output_path, std::string& error_message);

GrayFrame build_diff_frame(const GrayFrame& before_frame, const GrayFrame& after_frame);

}  // namespace fridge
