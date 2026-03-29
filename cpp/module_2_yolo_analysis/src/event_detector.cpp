#include "event_detector.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fridge {

namespace {

struct RegionDirectionStats {
    double weight = 0.0;
    double before_mean = 0.0;
    double after_mean = 0.0;
    double border_mean = 0.0;
    double before_background_error = 0.0;
    double after_background_error = 0.0;
};

#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required_size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        required_size,
        nullptr,
        nullptr
    );
    return result;
}
#endif

std::string path_to_display_string(const std::filesystem::path& path) {
#ifdef _WIN32
    return wide_to_utf8(path.generic_wstring());
#else
    return path.generic_string();
#endif
}

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
    case EventType::Uncertain:
        object.count_delta = 0;
        object.remain_level = 0.0;
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
    if (event_type == EventType::Uncertain) {
        return std::clamp(0.45 + summary.changed_ratio, 0.35, 0.70);
    }

    double score = 0.40;
    score += std::clamp(summary.changed_ratio / std::max(config.event_ratio, 0.001), 0.0, 1.5) * 0.25;
    score += std::clamp(std::abs(summary.mean_delta) / std::max(config.signed_delta_threshold, 1.0), 0.0, 1.5) * 0.20;
    if (event_type == EventType::PartialTakeOutCandidate) {
        score -= 0.10;
    }
    return std::clamp(score, 0.35, 0.98);
}

double compute_box_mean(const GrayFrame& frame, const BoundingBox& box) {
    if (frame.empty() || box.width <= 0 || box.height <= 0) {
        return 0.0;
    }

    const int x0 = std::clamp(box.x, 0, frame.width);
    const int y0 = std::clamp(box.y, 0, frame.height);
    const int x1 = std::clamp(box.x + box.width, 0, frame.width);
    const int y1 = std::clamp(box.y + box.height, 0, frame.height);
    if (x1 <= x0 || y1 <= y0) {
        return 0.0;
    }

    double sum = 0.0;
    std::size_t count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            sum += static_cast<double>(frame.at(x, y));
            ++count;
        }
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

double compute_border_mean(const GrayFrame& frame, const BoundingBox& box, int margin = 2) {
    if (frame.empty() || box.width <= 0 || box.height <= 0) {
        return 0.0;
    }

    const int outer_x0 = std::max(0, box.x - margin);
    const int outer_y0 = std::max(0, box.y - margin);
    const int outer_x1 = std::min(frame.width, box.x + box.width + margin);
    const int outer_y1 = std::min(frame.height, box.y + box.height + margin);

    double sum = 0.0;
    std::size_t count = 0;
    for (int y = outer_y0; y < outer_y1; ++y) {
        for (int x = outer_x0; x < outer_x1; ++x) {
            const bool inside_box =
                x >= box.x && x < box.x + box.width &&
                y >= box.y && y < box.y + box.height;
            if (inside_box) {
                continue;
            }
            sum += static_cast<double>(frame.at(x, y));
            ++count;
        }
    }

    if (count == 0) {
        return compute_box_mean(frame, box);
    }
    return sum / static_cast<double>(count);
}

RegionDirectionStats analyze_region_direction(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const ChangeRegion& region
) {
    RegionDirectionStats stats;
    stats.weight = region.score;
    stats.before_mean = compute_box_mean(before_frame, region.box);
    stats.after_mean = compute_box_mean(after_frame, region.box);

    const double before_border = compute_border_mean(before_frame, region.box);
    const double after_border = compute_border_mean(after_frame, region.box);
    stats.border_mean = (before_border + after_border) * 0.5;
    stats.before_background_error = std::abs(stats.before_mean - stats.border_mean);
    stats.after_background_error = std::abs(stats.after_mean - stats.border_mean);
    return stats;
}

EventType classify_event(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const ChangeAnalysis& analysis,
    const DetectorConfig& config
) {
    const MotionSummary& summary = analysis.summary;
    if (summary.changed_ratio < config.no_change_ratio || summary.changed_ratio < config.partial_ratio) {
        return EventType::NoChange;
    }

    const double dominant_ratio = analysis.dominant_polarity_ratio();
    const bool darker_dominant =
        analysis.darker_ratio >= config.event_ratio &&
        analysis.darker_pixels > analysis.brighter_pixels &&
        dominant_ratio >= config.dominant_polarity_threshold;
    const bool brighter_dominant =
        analysis.brighter_ratio >= config.event_ratio &&
        analysis.brighter_pixels > analysis.darker_pixels &&
        dominant_ratio >= config.dominant_polarity_threshold;

    const bool looks_like_reorg =
        summary.changed_ratio >= config.partial_ratio &&
        dominant_ratio <= config.reorg_balance_threshold &&
        analysis.regions.size() >= config.reorg_region_threshold;

    if (looks_like_reorg) {
        return EventType::NoChange;
    }

    double put_in_weight = 0.0;
    double take_out_weight = 0.0;
    double partial_take_out_weight = 0.0;

    for (const auto& region : analysis.regions) {
        const RegionDirectionStats stats = analyze_region_direction(before_frame, after_frame, region);
        const bool before_background_like = stats.before_background_error <= config.background_like_threshold;
        const bool after_background_like = stats.after_background_error <= config.background_like_threshold;
        const double direction_delta = stats.after_background_error - stats.before_background_error;

        if (before_background_like &&
            direction_delta >= config.region_direction_margin &&
            stats.after_background_error > config.background_like_threshold) {
            put_in_weight += stats.weight;
        }

        if (after_background_like &&
            direction_delta <= -config.region_direction_margin &&
            stats.before_background_error > config.background_like_threshold) {
            take_out_weight += stats.weight;
        }

        if (!after_background_like &&
            direction_delta <= -config.region_direction_margin &&
            stats.before_background_error > stats.after_background_error) {
            partial_take_out_weight += stats.weight;
        }
    }

    if (put_in_weight >= config.partial_ratio) {
        return EventType::PutIn;
    }

    if (take_out_weight >= config.partial_ratio) {
        return EventType::TakeOut;
    }

    if (partial_take_out_weight >= config.partial_ratio) {
        return EventType::PartialTakeOutCandidate;
    }

    if (darker_dominant && summary.mean_delta <= -config.signed_delta_threshold) {
        return EventType::PutIn;
    }

    if (brighter_dominant && summary.mean_delta >= config.signed_delta_threshold) {
        return EventType::TakeOut;
    }

    if (summary.changed_ratio >= config.partial_ratio &&
        dominant_ratio > config.reorg_balance_threshold &&
        !analysis.regions.empty()) {
        return EventType::PartialTakeOutCandidate;
    }

    return EventType::NoChange;
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

EventDetector::EventDetector(
    DetectorConfig config,
    RoiMotionConfig motion_config,
    std::shared_ptr<IObjectClassifier> classifier
)
    : config_(config),
      motion_config_(motion_config),
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

    RoiMotionConfig motion_config = motion_config_;
    motion_config.pixel_delta_threshold = config_.pixel_delta_threshold;
    const ChangeAnalysis analysis =
        analyze_change(selected_frames.before_frame, selected_frames.after_frame, motion_config);

    result.event_type = classify_event(
        selected_frames.before_frame,
        selected_frames.after_frame,
        analysis,
        config_
    );
    result.confidence = compute_confidence(result.event_type, analysis.summary, config_);
    result.need_user_confirm = needs_manual_review(result.event_type);

    if (!analysis.regions.empty()) {
        result.change_regions = analysis.regions;
    } else if (analysis.summary.has_motion) {
        result.change_regions.push_back(ChangeRegion{analysis.summary.box, analysis.summary.changed_ratio});
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

bool write_event_json(const EventResult& result, const std::filesystem::path& path, std::string& error_message) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream output(path);
    if (!output) {
        error_message = "Failed to open event json output: " + path_to_display_string(path);
        return false;
    }

    output << event_result_to_json(result);
    if (!output) {
        error_message = "Failed to write event json output: " + path_to_display_string(path);
        return false;
    }

    return true;
}

}  // namespace fridge
