#pragma once

#include <filesystem>
#include <string>

#include "runtime_config.hpp"
#include "types.hpp"

namespace fridge {

struct DebugArtifacts {
    std::filesystem::path input_path;
    std::filesystem::path config_path;
    std::filesystem::path before_path;
    std::filesystem::path after_path;
    std::filesystem::path overlay_path;
    std::filesystem::path event_path;
};

bool write_debug_summary(
    const SelectedFrames& selected_frames,
    const EventResult& event_result,
    const VisionPipelineConfig& config,
    const DebugArtifacts& artifacts,
    std::size_t total_frame_count,
    const std::filesystem::path& output_path,
    std::string& error_message
);

}  // namespace fridge
