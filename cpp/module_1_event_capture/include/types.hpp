#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fridge {

struct BoundingBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ChangeRegion {
    BoundingBox box;
    double score = 0.0;
};

struct DetectedObject {
    std::string category = "unknown";
    std::string name = "unknown";
    int count_delta = 0;
    double remain_level = 0.0;
};

enum class EventType {
    CaptureRecorded,
    NotEvaluated,
    NoChange,
    Reorganize,
    PutIn,
    TakeOut,
    PartialTakeOutCandidate,
    Uncertain
};

inline std::string to_string(EventType event_type) {
    switch (event_type) {
    case EventType::CaptureRecorded:
        return "capture_recorded";
    case EventType::NotEvaluated:
        return "not_evaluated";
    case EventType::NoChange:
        return "no_change";
    case EventType::Reorganize:
        return "reorganize";
    case EventType::PutIn:
        return "put_in";
    case EventType::TakeOut:
        return "take_out";
    case EventType::PartialTakeOutCandidate:
        return "partial_take_out_candidate";
    case EventType::Uncertain:
        return "uncertain";
    }
    return "no_change";
}

inline bool needs_manual_review(EventType event_type) {
    return event_type == EventType::PartialTakeOutCandidate ||
           event_type == EventType::Uncertain ||
           event_type == EventType::NotEvaluated;
}

struct GrayFrame {
    int width = 0;
    int height = 0;
    int index = 0;
    std::vector<std::uint8_t> pixels;

    bool empty() const {
        return width <= 0 || height <= 0 || pixels.empty();
    }

    std::uint8_t at(int x, int y) const {
        return pixels.at(static_cast<std::size_t>(y * width + x));
    }
};

struct ColorFrame {
    int width = 0;
    int height = 0;
    int index = 0;
    std::vector<std::uint8_t> pixels;

    bool empty() const {
        return width <= 0 || height <= 0 ||
               pixels.size() < static_cast<std::size_t>(width * height * 3);
    }
};

struct MotionSummary {
    bool has_motion = false;
    double changed_ratio = 0.0;
    double mean_delta = 0.0;
    BoundingBox box;
};

struct ChangeAnalysis {
    MotionSummary summary;
    std::vector<ChangeRegion> regions;
    std::size_t darker_pixels = 0;
    std::size_t brighter_pixels = 0;
    double darker_ratio = 0.0;
    double brighter_ratio = 0.0;

    double dominant_polarity_ratio() const {
        const std::size_t total = darker_pixels + brighter_pixels;
        if (total == 0) {
            return 0.0;
        }
        const std::size_t dominant = darker_pixels > brighter_pixels ? darker_pixels : brighter_pixels;
        return static_cast<double>(dominant) / static_cast<double>(total);
    }
};

struct SelectedFrames {
    GrayFrame before_frame;
    GrayFrame after_frame;
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    std::vector<MotionSummary> transitions;
    double peak_transition_ratio = 0.0;
    double peak_baseline_change_ratio = 0.0;
    double final_change_ratio = 0.0;
    std::size_t stable_before_run_length = 0;
    std::size_t stable_after_run_length = 0;
};

struct DetectorConfig {
    double no_change_ratio = 0.010;
    double event_ratio = 0.040;
    double partial_ratio = 0.020;
    double signed_delta_threshold = 10.0;
    double dominant_polarity_threshold = 0.68;
    double reorg_balance_threshold = 0.62;
    double background_like_threshold = 12.0;
    double region_direction_margin = 10.0;
    std::size_t reorg_region_threshold = 2;
    int pixel_delta_threshold = 8;
};

struct EventResult {
    std::string session_id;
    std::string timestamp;
    EventType event_type = EventType::NoChange;
    std::string roi_id = "main_compartment";
    double confidence = 0.0;
    std::string before_frame;
    std::string after_frame;
    std::vector<ChangeRegion> change_regions;
    std::vector<DetectedObject> objects;
    bool need_user_confirm = false;
};

}  // namespace fridge
