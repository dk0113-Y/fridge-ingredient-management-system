#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "types.hpp"

namespace fridge {

struct YoloDetection {
    std::string coarse_class = "unknown";
    double confidence = 0.0;
    BoundingBox bbox;
};

struct DetectionMatch {
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    double iou = 0.0;
    double normalized_center_distance = 1.0;
    double area_change_ratio = 0.0;
    double match_score = 0.0;
    std::string coarse_class = "unknown";
};

struct CropRequest {
    std::string source_frame;
    std::string coarse_class = "unknown";
    BoundingBox bbox;
};

struct YoloAnalysisConfig {
    double confidence_threshold = 0.50;
    double iou_match_threshold = 0.30;
    double center_distance_threshold = 0.45;
    double reorganize_center_distance_threshold = 0.18;
    double partial_area_change_threshold = 0.20;
    double partial_visual_delta_threshold = 18.0;
    double uncertain_unmatched_ratio_threshold = 0.50;
};

using DetectionCountMap = std::map<std::string, int>;

struct YoloDiffResult {
    EventResult event;
    DetectionCountMap before_counts;
    DetectionCountMap after_counts;
    std::string count_decision = "same_count";
    std::vector<DetectionMatch> matched_pairs;
    std::vector<YoloDetection> new_boxes;
    std::vector<YoloDetection> disappeared_boxes;
    std::vector<DetectionMatch> partial_candidates;
    std::vector<DetectionMatch> reorganize_candidates;
    std::vector<CropRequest> crop_requests;
    std::string review_reason;
};

bool load_yolo_analysis_config(
    const std::filesystem::path& config_path,
    YoloAnalysisConfig& config,
    std::string& error_message
);

class YoloDiffAnalyzer {
public:
    explicit YoloDiffAnalyzer(YoloAnalysisConfig config = {});

    YoloDiffResult analyze(
        const GrayFrame& before_frame,
        const GrayFrame& after_frame,
        const std::vector<YoloDetection>& before_detections,
        const std::vector<YoloDetection>& after_detections,
        const std::string& session_id,
        const std::string& before_frame_path,
        const std::string& after_frame_path
    ) const;

private:
    YoloAnalysisConfig config_;
};

}  // namespace fridge
