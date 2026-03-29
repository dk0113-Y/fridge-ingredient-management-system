#include "yolo_diff_analyzer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace fridge {

namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string strip_inline_comment(const std::string& value) {
    const std::size_t comment_pos = value.find('#');
    if (comment_pos == std::string::npos) {
        return value;
    }
    return value.substr(0, comment_pos);
}

bool parse_double_value(const std::string& text, double& value) {
    std::size_t consumed = 0;
    value = std::stod(text, &consumed);
    return consumed == text.size();
}

bool set_config_value(
    YoloAnalysisConfig& config,
    const std::string& key,
    const std::string& value,
    std::string& error_message
) {
    try {
        if (key == "confidence_threshold") {
            return parse_double_value(value, config.confidence_threshold);
        }
        if (key == "iou_match_threshold") {
            return parse_double_value(value, config.iou_match_threshold);
        }
        if (key == "center_distance_threshold") {
            return parse_double_value(value, config.center_distance_threshold);
        }
        if (key == "partial_area_change_threshold") {
            return parse_double_value(value, config.partial_area_change_threshold);
        }
        if (key == "partial_visual_delta_threshold") {
            return parse_double_value(value, config.partial_visual_delta_threshold);
        }
        if (key == "uncertain_unmatched_ratio_threshold") {
            return parse_double_value(value, config.uncertain_unmatched_ratio_threshold);
        }

        error_message = "Unknown YOLO analysis config key: " + key;
        return false;
    } catch (const std::exception&) {
        error_message = "Invalid value for key: " + key;
        return false;
    }
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

int box_area(const BoundingBox& box) {
    if (box.width <= 0 || box.height <= 0) {
        return 0;
    }
    return box.width * box.height;
}

BoundingBox union_box(const BoundingBox& first, const BoundingBox& second) {
    const int x0 = std::min(first.x, second.x);
    const int y0 = std::min(first.y, second.y);
    const int x1 = std::max(first.x + first.width, second.x + second.width);
    const int y1 = std::max(first.y + first.height, second.y + second.height);
    return BoundingBox{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

double compute_iou(const BoundingBox& first, const BoundingBox& second) {
    const int x0 = std::max(first.x, second.x);
    const int y0 = std::max(first.y, second.y);
    const int x1 = std::min(first.x + first.width, second.x + second.width);
    const int y1 = std::min(first.y + first.height, second.y + second.height);

    const int intersection_width = std::max(0, x1 - x0);
    const int intersection_height = std::max(0, y1 - y0);
    const int intersection_area = intersection_width * intersection_height;
    const int total_area = box_area(first) + box_area(second) - intersection_area;
    if (total_area <= 0) {
        return 0.0;
    }
    return static_cast<double>(intersection_area) / static_cast<double>(total_area);
}

double compute_center_distance(const BoundingBox& first, const BoundingBox& second) {
    const double first_center_x = static_cast<double>(first.x) + static_cast<double>(first.width) * 0.5;
    const double first_center_y = static_cast<double>(first.y) + static_cast<double>(first.height) * 0.5;
    const double second_center_x = static_cast<double>(second.x) + static_cast<double>(second.width) * 0.5;
    const double second_center_y = static_cast<double>(second.y) + static_cast<double>(second.height) * 0.5;

    const double dx = first_center_x - second_center_x;
    const double dy = first_center_y - second_center_y;
    return std::sqrt(dx * dx + dy * dy);
}

double compute_normalized_center_distance(const BoundingBox& first, const BoundingBox& second) {
    const BoundingBox combined = union_box(first, second);
    const double diagonal = std::sqrt(
        static_cast<double>(combined.width * combined.width + combined.height * combined.height)
    );
    if (diagonal <= 1e-6) {
        return 0.0;
    }
    return compute_center_distance(first, second) / diagonal;
}

double compute_crop_mean_abs_delta(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const BoundingBox& box
) {
    if (before_frame.empty() || after_frame.empty()) {
        return 0.0;
    }

    const int x0 = std::max(0, box.x);
    const int y0 = std::max(0, box.y);
    const int x1 = std::min(before_frame.width, box.x + box.width);
    const int y1 = std::min(before_frame.height, box.y + box.height);
    if (x1 <= x0 || y1 <= y0) {
        return 0.0;
    }

    double sum = 0.0;
    std::size_t count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int delta = static_cast<int>(before_frame.at(x, y)) - static_cast<int>(after_frame.at(x, y));
            sum += std::abs(delta);
            ++count;
        }
    }

    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

double compute_area_change_ratio(const BoundingBox& before_box, const BoundingBox& after_box) {
    const double before_area = static_cast<double>(std::max(1, box_area(before_box)));
    const double after_area = static_cast<double>(std::max(1, box_area(after_box)));
    return std::abs(after_area - before_area) / std::max(before_area, after_area);
}

std::vector<YoloDetection> filter_detections(
    const std::vector<YoloDetection>& detections,
    double confidence_threshold
) {
    std::vector<YoloDetection> filtered;
    filtered.reserve(detections.size());
    for (const auto& detection : detections) {
        if (detection.confidence >= confidence_threshold) {
            filtered.push_back(detection);
        }
    }
    return filtered;
}

std::string choose_primary_class(
    const std::vector<YoloDetection>& new_boxes,
    const std::vector<YoloDetection>& disappeared_boxes,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections
) {
    if (!new_boxes.empty()) {
        return new_boxes.front().coarse_class;
    }
    if (!disappeared_boxes.empty()) {
        return disappeared_boxes.front().coarse_class;
    }
    if (!after_detections.empty()) {
        return after_detections.front().coarse_class;
    }
    if (!before_detections.empty()) {
        return before_detections.front().coarse_class;
    }
    return "unknown";
}

std::vector<ChangeRegion> build_change_regions(
    const std::vector<YoloDetection>& new_boxes,
    const std::vector<YoloDetection>& disappeared_boxes,
    const std::vector<DetectionMatch>& partial_candidates,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections
) {
    std::vector<ChangeRegion> regions;
    for (const auto& detection : new_boxes) {
        regions.push_back(ChangeRegion{detection.bbox, detection.confidence});
    }
    for (const auto& detection : disappeared_boxes) {
        regions.push_back(ChangeRegion{detection.bbox, detection.confidence});
    }
    for (const auto& match : partial_candidates) {
        regions.push_back(ChangeRegion{
            union_box(before_detections[match.before_index].bbox, after_detections[match.after_index].bbox),
            std::max(before_detections[match.before_index].confidence, after_detections[match.after_index].confidence)
        });
    }
    return regions;
}

std::vector<CropRequest> build_crop_requests(
    const std::vector<YoloDetection>& new_boxes,
    const std::vector<YoloDetection>& disappeared_boxes,
    const std::vector<DetectionMatch>& partial_candidates,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections
) {
    std::vector<CropRequest> requests;
    for (const auto& detection : new_boxes) {
        requests.push_back(CropRequest{"after", detection.coarse_class, detection.bbox});
    }
    for (const auto& detection : disappeared_boxes) {
        requests.push_back(CropRequest{"before", detection.coarse_class, detection.bbox});
    }
    for (const auto& match : partial_candidates) {
        requests.push_back(CropRequest{
            "before",
            before_detections[match.before_index].coarse_class,
            before_detections[match.before_index].bbox
        });
        requests.push_back(CropRequest{
            "after",
            after_detections[match.after_index].coarse_class,
            after_detections[match.after_index].bbox
        });
    }
    return requests;
}

double compute_event_confidence(
    EventType event_type,
    std::size_t matched_count,
    std::size_t new_count,
    std::size_t disappeared_count,
    std::size_t partial_count
) {
    if (event_type == EventType::NoChange) {
        return matched_count > 0 ? 0.90 : 0.65;
    }
    if (event_type == EventType::Uncertain) {
        return 0.45;
    }

    const double base = 0.55;
    const double signal = static_cast<double>(new_count + disappeared_count + partial_count);
    return std::clamp(base + signal * 0.10, 0.55, 0.92);
}

}  // namespace

bool load_yolo_analysis_config(
    const std::filesystem::path& config_path,
    YoloAnalysisConfig& config,
    std::string& error_message
) {
    std::ifstream input(config_path);
    if (!input) {
        error_message = "Failed to open YOLO analysis config file: " + config_path.string();
        return false;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string normalized = trim(strip_inline_comment(line));
        if (normalized.empty()) {
            continue;
        }

        const std::size_t separator = normalized.find('=');
        if (separator == std::string::npos) {
            error_message = "Missing '=' at line " + std::to_string(line_number);
            return false;
        }

        const std::string key = trim(normalized.substr(0, separator));
        const std::string value = trim(normalized.substr(separator + 1));
        std::string line_error;
        if (!set_config_value(config, key, value, line_error)) {
            if (line_error.find("Unknown YOLO analysis config key:") == 0) {
                continue;
            }
            error_message = line_error + " at line " + std::to_string(line_number);
            return false;
        }
    }

    return true;
}

YoloDiffAnalyzer::YoloDiffAnalyzer(YoloAnalysisConfig config)
    : config_(config) {}

YoloDiffResult YoloDiffAnalyzer::analyze(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections,
    const std::string& session_id,
    const std::string& before_frame_path,
    const std::string& after_frame_path
) const {
    YoloDiffResult result;

    const std::vector<YoloDetection> filtered_before =
        filter_detections(before_detections, config_.confidence_threshold);
    const std::vector<YoloDetection> filtered_after =
        filter_detections(after_detections, config_.confidence_threshold);

    std::vector<bool> before_used(filtered_before.size(), false);
    std::vector<bool> after_used(filtered_after.size(), false);

    while (true) {
        double best_score = -std::numeric_limits<double>::infinity();
        DetectionMatch best_match{};
        bool found = false;

        for (std::size_t before_index = 0; before_index < filtered_before.size(); ++before_index) {
            if (before_used[before_index]) {
                continue;
            }

            for (std::size_t after_index = 0; after_index < filtered_after.size(); ++after_index) {
                if (after_used[after_index]) {
                    continue;
                }
                if (filtered_before[before_index].coarse_class != filtered_after[after_index].coarse_class) {
                    continue;
                }

                const double iou = compute_iou(filtered_before[before_index].bbox, filtered_after[after_index].bbox);
                const double center_distance = compute_normalized_center_distance(
                    filtered_before[before_index].bbox,
                    filtered_after[after_index].bbox
                );

                if (iou < config_.iou_match_threshold &&
                    center_distance > config_.center_distance_threshold) {
                    continue;
                }

                const double score = iou * 2.0 - center_distance;
                if (!found || score > best_score) {
                    best_score = score;
                    best_match = DetectionMatch{before_index, after_index, iou, center_distance};
                    found = true;
                }
            }
        }

        if (!found) {
            break;
        }

        before_used[best_match.before_index] = true;
        after_used[best_match.after_index] = true;
        result.matched_pairs.push_back(best_match);
    }

    for (std::size_t index = 0; index < filtered_before.size(); ++index) {
        if (!before_used[index]) {
            result.disappeared_boxes.push_back(filtered_before[index]);
        }
    }

    for (std::size_t index = 0; index < filtered_after.size(); ++index) {
        if (!after_used[index]) {
            result.new_boxes.push_back(filtered_after[index]);
        }
    }

    for (const auto& match : result.matched_pairs) {
        const auto& before_detection = filtered_before[match.before_index];
        const auto& after_detection = filtered_after[match.after_index];
        if (before_detection.coarse_class != "fruit_vegetable") {
            continue;
        }

        const double area_change_ratio = compute_area_change_ratio(before_detection.bbox, after_detection.bbox);
        const double visual_delta = compute_crop_mean_abs_delta(
            before_frame,
            after_frame,
            union_box(before_detection.bbox, after_detection.bbox)
        );

        if (area_change_ratio >= config_.partial_area_change_threshold ||
            visual_delta >= config_.partial_visual_delta_threshold) {
            result.partial_candidates.push_back(match);
        }
    }

    const std::size_t total_observations =
        filtered_before.size() + filtered_after.size() + result.matched_pairs.size();
    const std::size_t unmatched_count = result.new_boxes.size() + result.disappeared_boxes.size();
    const double unmatched_ratio = total_observations == 0
        ? 0.0
        : static_cast<double>(unmatched_count) / static_cast<double>(total_observations);

    EventType event_type = EventType::NoChange;
    std::string review_reason;

    if (!result.new_boxes.empty() && result.disappeared_boxes.empty()) {
        event_type = EventType::PutIn;
    } else if (result.new_boxes.empty() && !result.disappeared_boxes.empty()) {
        event_type = EventType::TakeOut;
    } else if (!result.partial_candidates.empty() &&
               result.new_boxes.empty() &&
               result.disappeared_boxes.empty()) {
        event_type = EventType::PartialTakeOutCandidate;
        review_reason = "matched fruit_vegetable targets changed shape or crop appearance";
    } else if (result.new_boxes.empty() && result.disappeared_boxes.empty()) {
        event_type = EventType::NoChange;
    } else {
        event_type = EventType::Uncertain;
        review_reason = "mixed new and disappeared boxes cannot be resolved into a single event";
    }

    if (event_type != EventType::NoChange &&
        unmatched_ratio >= config_.uncertain_unmatched_ratio_threshold &&
        !result.matched_pairs.empty()) {
        event_type = EventType::Uncertain;
        review_reason = "too many unmatched detections after box matching";
    }

    result.review_reason = review_reason;

    result.event.session_id = session_id;
    result.event.timestamp = now_as_utc_string();
    result.event.event_type = event_type;
    result.event.before_frame = before_frame_path;
    result.event.after_frame = after_frame_path;
    result.event.change_regions = build_change_regions(
        result.new_boxes,
        result.disappeared_boxes,
        result.partial_candidates,
        filtered_before,
        filtered_after
    );
    result.event.need_user_confirm = needs_manual_review(event_type);
    result.event.confidence = compute_event_confidence(
        event_type,
        result.matched_pairs.size(),
        result.new_boxes.size(),
        result.disappeared_boxes.size(),
        result.partial_candidates.size()
    );

    const std::string primary_class = choose_primary_class(
        result.new_boxes,
        result.disappeared_boxes,
        filtered_before,
        filtered_after
    );
    DetectedObject object;
    object.category = primary_class;
    object.name = primary_class;
    if (event_type == EventType::PutIn) {
        object.count_delta = static_cast<int>(result.new_boxes.size());
        object.remain_level = 1.0;
    } else if (event_type == EventType::TakeOut) {
        object.count_delta = -static_cast<int>(result.disappeared_boxes.size());
        object.remain_level = 0.0;
    } else if (event_type == EventType::PartialTakeOutCandidate) {
        object.count_delta = 0;
        object.remain_level = 0.5;
    } else {
        object.count_delta = 0;
        object.remain_level = 0.0;
    }
    result.event.objects = {object};

    result.crop_requests = build_crop_requests(
        result.new_boxes,
        result.disappeared_boxes,
        result.partial_candidates,
        filtered_before,
        filtered_after
    );

    return result;
}

}  // namespace fridge
