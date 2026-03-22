#include "event_detector.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fridge {

namespace {

std::string now_as_utc_string() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &now_time);
#else
    gmtime_r(&now_time, &utc_tm);
#endif

    std::ostringstream output;
    output << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string escape_json(const std::string& value) {
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            escaped << character;
            break;
        }
    }
    return escaped.str();
}

DetectedObject placeholder_object(EventType event_type) {
    DetectedObject object;
    switch (event_type) {
    case EventType::PutIn:
        object.count_delta = 1;
        object.remain_level = 1.0;
        break;
    case EventType::TakeOut:
        object.count_delta = -1;
        object.remain_level = 0.0;
        break;
    case EventType::PartialTakeOutCandidate:
        object.count_delta = 0;
        object.remain_level = 0.5;
        break;
    case EventType::NoChange:
        object.count_delta = 0;
        object.remain_level = 0.0;
        break;
    }
    return object;
}

double compute_confidence(EventType event_type, const MotionSummary& summary, const DetectorConfig& config) {
    if (event_type == EventType::NoChange) {
        return std::clamp(0.85 - summary.changed_ratio * 3.0, 0.50, 0.95);
    }

    double score = 0.40;
    score += std::clamp(summary.changed_ratio / std::max(config.event_ratio, 0.001), 0.0, 1.5) * 0.25;
    score += std::clamp(std::abs(summary.mean_delta) / std::max(config.signed_delta_threshold, 1.0), 0.0, 1.5) * 0.20;
    if (event_type == EventType::PartialTakeOutCandidate) {
        score -= 0.10;
    }
    return std::clamp(score, 0.35, 0.98);
}

}  // namespace

std::vector<DetectedObject> NullClassifier::classify(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const std::vector<ChangeRegion>& regions
) const {
    (void)before_frame;
    (void)after_frame;
    (void)regions;
    // TODO: connect a lightweight category classifier after event detection is stable.
    return {};
}

EventDetector::EventDetector(DetectorConfig config, std::shared_ptr<IObjectClassifier> classifier)
    : config_(config),
      classifier_(std::move(classifier)) {
    if (!classifier_) {
        classifier_ = std::make_shared<NullClassifier>();
    }
}

EventResult EventDetector::detect(
    const SelectedFrames& selected_frames,
    const std::string& session_id,
    const std::string& before_frame_path,
    const std::string& after_frame_path
) const {
    EventResult result;
    result.session_id = session_id;
    result.timestamp = now_as_utc_string();
    result.before_frame = before_frame_path;
    result.after_frame = after_frame_path;

    const MotionSummary summary = summarize_motion(
        selected_frames.before_frame,
        selected_frames.after_frame,
        RoiMotionConfig{config_.pixel_delta_threshold}
    );

    if (summary.changed_ratio < config_.no_change_ratio) {
        result.event_type = EventType::NoChange;
    } else if (summary.changed_ratio < config_.partial_ratio) {
        result.event_type = EventType::NoChange;
    } else if (summary.mean_delta <= -config_.signed_delta_threshold) {
        result.event_type = EventType::PutIn;
    } else if (summary.mean_delta >= config_.signed_delta_threshold) {
        result.event_type = EventType::TakeOut;
    } else if (summary.changed_ratio >= config_.partial_ratio) {
        result.event_type = EventType::PartialTakeOutCandidate;
    } else {
        result.event_type = EventType::NoChange;
    }

    result.confidence = compute_confidence(result.event_type, summary, config_);
    result.need_user_confirm = (result.event_type == EventType::PartialTakeOutCandidate);

    if (summary.has_motion) {
        result.change_regions.push_back(ChangeRegion{summary.box, summary.changed_ratio});
    }

    result.objects = classifier_->classify(selected_frames.before_frame, selected_frames.after_frame, result.change_regions);
    if (result.objects.empty()) {
        result.objects.push_back(placeholder_object(result.event_type));
    }

    return result;
}

std::string event_result_to_json(const EventResult& result) {
    std::ostringstream output;
    output << "{\n";
    output << "  \"session_id\": \"" << escape_json(result.session_id) << "\",\n";
    output << "  \"timestamp\": \"" << escape_json(result.timestamp) << "\",\n";
    output << "  \"event_type\": \"" << to_string(result.event_type) << "\",\n";
    output << "  \"roi_id\": \"" << escape_json(result.roi_id) << "\",\n";
    output << "  \"confidence\": " << std::fixed << std::setprecision(3) << result.confidence << ",\n";
    output << "  \"before_frame\": \"" << escape_json(result.before_frame) << "\",\n";
    output << "  \"after_frame\": \"" << escape_json(result.after_frame) << "\",\n";
    output << "  \"change_regions\": [";
    if (!result.change_regions.empty()) {
        output << "\n";
        for (std::size_t index = 0; index < result.change_regions.size(); ++index) {
            const auto& region = result.change_regions[index];
            output << "    {\n";
            output << "      \"x\": " << region.box.x << ",\n";
            output << "      \"y\": " << region.box.y << ",\n";
            output << "      \"width\": " << region.box.width << ",\n";
            output << "      \"height\": " << region.box.height << ",\n";
            output << "      \"score\": " << std::fixed << std::setprecision(3) << region.score << "\n";
            output << "    }";
            if (index + 1 < result.change_regions.size()) {
                output << ",";
            }
            output << "\n";
        }
        output << "  ";
    }
    output << "],\n";
    output << "  \"objects\": [\n";
    for (std::size_t index = 0; index < result.objects.size(); ++index) {
        const auto& object = result.objects[index];
        output << "    {\n";
        output << "      \"category\": \"" << escape_json(object.category) << "\",\n";
        output << "      \"name\": \"" << escape_json(object.name) << "\",\n";
        output << "      \"count_delta\": " << object.count_delta << ",\n";
        output << "      \"remain_level\": " << std::fixed << std::setprecision(3) << object.remain_level << "\n";
        output << "    }";
        if (index + 1 < result.objects.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ],\n";
    output << "  \"need_user_confirm\": " << (result.need_user_confirm ? "true" : "false") << "\n";
    output << "}\n";
    return output.str();
}

bool write_event_json(const EventResult& result, const std::string& output_path, std::string& error_message) {
    const std::filesystem::path path(output_path);
    std::filesystem::create_directories(path.parent_path());

    std::ofstream output(path);
    if (!output) {
        error_message = "Failed to open event json output: " + path.string();
        return false;
    }

    output << event_result_to_json(result);
    if (!output) {
        error_message = "Failed to write event json output: " + path.string();
        return false;
    }

    return true;
}

}  // namespace fridge
