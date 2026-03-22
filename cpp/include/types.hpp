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
    NoChange,
    PutIn,
    TakeOut,
    PartialTakeOutCandidate
};

inline std::string to_string(EventType event_type) {
    switch (event_type) {
    case EventType::NoChange:
        return "no_change";
    case EventType::PutIn:
        return "put_in";
    case EventType::TakeOut:
        return "take_out";
    case EventType::PartialTakeOutCandidate:
        return "partial_take_out_candidate";
    }
    return "no_change";
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

struct MotionSummary {
    bool has_motion = false;
    double changed_ratio = 0.0;
    double mean_delta = 0.0;
    BoundingBox box;
};

struct SelectedFrames {
    GrayFrame before_frame;
    GrayFrame after_frame;
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    std::vector<MotionSummary> transitions;
};

struct DetectorConfig {
    double no_change_ratio = 0.010;
    double event_ratio = 0.040;
    double partial_ratio = 0.020;
    double signed_delta_threshold = 10.0;
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
