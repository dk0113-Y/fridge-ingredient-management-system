#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "types.hpp"

namespace fridge {

bool load_frames(const std::filesystem::path& input_path, std::vector<GrayFrame>& frames, std::string& error_message);

bool write_debug_image(const GrayFrame& frame, const std::filesystem::path& output_path, std::string& error_message);

GrayFrame build_overlay_frame(
    const GrayFrame& after_frame,
    const BoundingBox& roi,
    const std::vector<ChangeRegion>& change_regions
);

}  // namespace fridge
