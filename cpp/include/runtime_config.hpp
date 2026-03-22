#pragma once

#include <filesystem>
#include <string>

#include "event_detector.hpp"
#include "frame_selector.hpp"
#include "roi_motion.hpp"

namespace fridge {

struct VisionPipelineConfig {
    std::string roi_id = "main_compartment";
    RoiMotionConfig motion_config;
    FrameSelectorConfig frame_selector_config;
    DetectorConfig detector_config;
};

bool load_pipeline_config(
    const std::filesystem::path& config_path,
    VisionPipelineConfig& config,
    std::string& error_message
);

}  // namespace fridge
