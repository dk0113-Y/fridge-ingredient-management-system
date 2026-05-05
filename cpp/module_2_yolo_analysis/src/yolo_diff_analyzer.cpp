#include "yolo_diff_analyzer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace fridge {

namespace {

struct MatchComputation {
    std::vector<DetectionMatch> matched_pairs;
    std::vector<int> before_match_lookup;
    std::vector<int> after_match_lookup;
    std::vector<bool> before_used;
    std::vector<bool> after_used;
};

struct CountDeltaEntry {
    std::string coarse_class;
    int delta = 0;
};

struct CountDecisionSummary {
    DetectionCountMap before_counts;
    DetectionCountMap after_counts;
    std::vector<CountDeltaEntry> increases;
    std::vector<CountDeltaEntry> decreases;
    std::string decision = "same_count";
};

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
        if (key == "reorganize_center_distance_threshold") {
            return parse_double_value(value, config.reorganize_center_distance_threshold);
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

DetectionCountMap build_counts(const std::vector<YoloDetection>& detections) {
    DetectionCountMap counts;
    for (const auto& detection : detections) {
        ++counts[detection.coarse_class];
    }
    return counts;
}

CountDecisionSummary summarize_counts(
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections
) {
    CountDecisionSummary summary;
    summary.before_counts = build_counts(before_detections);
    summary.after_counts = build_counts(after_detections);

    std::vector<std::string> classes;
    classes.reserve(summary.before_counts.size() + summary.after_counts.size());
    for (const auto& entry : summary.before_counts) {
        classes.push_back(entry.first);
    }
    for (const auto& entry : summary.after_counts) {
        if (summary.before_counts.find(entry.first) == summary.before_counts.end()) {
            classes.push_back(entry.first);
        }
    }
    std::sort(classes.begin(), classes.end());

    for (const auto& coarse_class : classes) {
        const int before_count = summary.before_counts.count(coarse_class) != 0
            ? summary.before_counts.at(coarse_class)
            : 0;
        const int after_count = summary.after_counts.count(coarse_class) != 0
            ? summary.after_counts.at(coarse_class)
            : 0;
        const int delta = after_count - before_count;
        if (delta > 0) {
            summary.increases.push_back(CountDeltaEntry{coarse_class, delta});
        } else if (delta < 0) {
            summary.decreases.push_back(CountDeltaEntry{coarse_class, delta});
        }
    }

    if (summary.increases.empty() && summary.decreases.empty()) {
        summary.decision = "same_count";
    } else if (summary.increases.size() == 1 && summary.decreases.empty()) {
        const auto& increase = summary.increases.front();
        summary.decision =
            "put_in_by_count:" + increase.coarse_class + ":" + std::to_string(increase.delta);
    } else if (summary.increases.empty() && summary.decreases.size() == 1) {
        const auto& decrease = summary.decreases.front();
        summary.decision =
            "take_out_by_count:" + decrease.coarse_class + ":" + std::to_string(decrease.delta);
    } else if (!summary.increases.empty() && !summary.decreases.empty()) {
        summary.decision = "conflicting_count_changes";
    } else {
        summary.decision = "multi_class_count_change";
    }

    return summary;
}

MatchComputation compute_matches(
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections,
    const YoloAnalysisConfig& config
) {
    MatchComputation computation;
    computation.before_match_lookup.assign(before_detections.size(), -1);
    computation.after_match_lookup.assign(after_detections.size(), -1);
    computation.before_used.assign(before_detections.size(), false);
    computation.after_used.assign(after_detections.size(), false);

    while (true) {
        double best_score = -std::numeric_limits<double>::infinity();
        DetectionMatch best_match{};
        bool found = false;

        for (std::size_t before_index = 0; before_index < before_detections.size(); ++before_index) {
            if (computation.before_used[before_index]) {
                continue;
            }

            for (std::size_t after_index = 0; after_index < after_detections.size(); ++after_index) {
                if (computation.after_used[after_index]) {
                    continue;
                }
                if (before_detections[before_index].coarse_class != after_detections[after_index].coarse_class) {
                    continue;
                }

                const double iou = compute_iou(before_detections[before_index].bbox, after_detections[after_index].bbox);
                const double center_distance = compute_normalized_center_distance(
                    before_detections[before_index].bbox,
                    after_detections[after_index].bbox
                );
                if (iou < config.iou_match_threshold &&
                    center_distance > config.center_distance_threshold) {
                    continue;
                }

                const double score = iou * 2.0 - center_distance;
                if (!found || score > best_score) {
                    best_score = score;
                    best_match.before_index = before_index;
                    best_match.after_index = after_index;
                    best_match.iou = iou;
                    best_match.normalized_center_distance = center_distance;
                    best_match.area_change_ratio = compute_area_change_ratio(
                        before_detections[before_index].bbox,
                        after_detections[after_index].bbox
                    );
                    best_match.match_score = score;
                    best_match.coarse_class = before_detections[before_index].coarse_class;
                    found = true;
                }
            }
        }

        if (!found) {
            break;
        }

        computation.before_used[best_match.before_index] = true;
        computation.after_used[best_match.after_index] = true;
        const int match_index = static_cast<int>(computation.matched_pairs.size());
        computation.before_match_lookup[best_match.before_index] = match_index;
        computation.after_match_lookup[best_match.after_index] = match_index;
        computation.matched_pairs.push_back(best_match);
    }

    return computation;
}

std::vector<YoloDetection> collect_unmatched(
    const std::vector<YoloDetection>& detections,
    const std::vector<bool>& used_flags
) {
    std::vector<YoloDetection> unmatched;
    for (std::size_t index = 0; index < detections.size(); ++index) {
        if (index < used_flags.size() && !used_flags[index]) {
            unmatched.push_back(detections[index]);
        }
    }
    return unmatched;
}

std::vector<YoloDetection> filter_by_class(
    const std::vector<YoloDetection>& detections,
    const std::string& coarse_class
) {
    std::vector<YoloDetection> filtered;
    for (const auto& detection : detections) {
        if (detection.coarse_class == coarse_class) {
            filtered.push_back(detection);
        }
    }
    return filtered;
}

std::vector<YoloDetection> select_changed_boxes_for_count_delta(
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections,
    const std::string& coarse_class,
    int requested_count,
    bool select_from_after,
    const YoloAnalysisConfig& config
) {
    if (requested_count <= 0) {
        return {};
    }

    const auto class_before = filter_by_class(before_detections, coarse_class);
    const auto class_after = filter_by_class(after_detections, coarse_class);
    const MatchComputation match = compute_matches(class_before, class_after, config);

    struct Candidate {
        YoloDetection detection;
        bool matched = false;
        double match_score = -std::numeric_limits<double>::infinity();
    };

    std::vector<Candidate> candidates;
    if (select_from_after) {
        candidates.reserve(class_after.size());
        for (std::size_t index = 0; index < class_after.size(); ++index) {
            Candidate candidate;
            candidate.detection = class_after[index];
            const int lookup = index < match.after_match_lookup.size() ? match.after_match_lookup[index] : -1;
            candidate.matched = lookup >= 0;
            candidate.match_score = lookup >= 0 ? match.matched_pairs[static_cast<std::size_t>(lookup)].match_score
                                                : -std::numeric_limits<double>::infinity();
            candidates.push_back(candidate);
        }
    } else {
        candidates.reserve(class_before.size());
        for (std::size_t index = 0; index < class_before.size(); ++index) {
            Candidate candidate;
            candidate.detection = class_before[index];
            const int lookup = index < match.before_match_lookup.size() ? match.before_match_lookup[index] : -1;
            candidate.matched = lookup >= 0;
            candidate.match_score = lookup >= 0 ? match.matched_pairs[static_cast<std::size_t>(lookup)].match_score
                                                : -std::numeric_limits<double>::infinity();
            candidates.push_back(candidate);
        }
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.matched != rhs.matched) {
                return !lhs.matched;
            }
            if (lhs.match_score != rhs.match_score) {
                return lhs.match_score < rhs.match_score;
            }
            return lhs.detection.confidence < rhs.detection.confidence;
        }
    );

    std::vector<YoloDetection> selected;
    const std::size_t limit = std::min(static_cast<std::size_t>(requested_count), candidates.size());
    selected.reserve(limit);
    for (std::size_t index = 0; index < limit; ++index) {
        selected.push_back(candidates[index].detection);
    }
    return selected;
}

std::string choose_primary_class(
    EventType event_type,
    const CountDecisionSummary& counts,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections,
    const std::vector<DetectionMatch>& partial_candidates,
    const std::vector<DetectionMatch>& reorganize_candidates
) {
    if (event_type == EventType::PutIn && !counts.increases.empty()) {
        return counts.increases.front().coarse_class;
    }
    if (event_type == EventType::TakeOut && !counts.decreases.empty()) {
        return counts.decreases.front().coarse_class;
    }
    if (event_type == EventType::PartialTakeOutCandidate && !partial_candidates.empty()) {
        return partial_candidates.front().coarse_class;
    }
    if (event_type == EventType::Reorganize && !reorganize_candidates.empty()) {
        return reorganize_candidates.front().coarse_class;
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
    EventType event_type,
    const std::vector<YoloDetection>& new_boxes,
    const std::vector<YoloDetection>& disappeared_boxes,
    const std::vector<DetectionMatch>& partial_candidates,
    const std::vector<DetectionMatch>& reorganize_candidates,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections
) {
    std::vector<ChangeRegion> regions;
    auto append_pair_regions = [&](const std::vector<DetectionMatch>& pairs) {
        for (const auto& match : pairs) {
            regions.push_back(ChangeRegion{
                union_box(before_detections[match.before_index].bbox, after_detections[match.after_index].bbox),
                std::max(before_detections[match.before_index].confidence, after_detections[match.after_index].confidence)
            });
        }
    };

    if (event_type == EventType::PutIn || event_type == EventType::Uncertain) {
        for (const auto& detection : new_boxes) {
            regions.push_back(ChangeRegion{detection.bbox, detection.confidence});
        }
    }
    if (event_type == EventType::TakeOut || event_type == EventType::Uncertain) {
        for (const auto& detection : disappeared_boxes) {
            regions.push_back(ChangeRegion{detection.bbox, detection.confidence});
        }
    }
    if (event_type == EventType::PartialTakeOutCandidate || event_type == EventType::Uncertain) {
        append_pair_regions(partial_candidates);
    }
    if (event_type == EventType::Reorganize || event_type == EventType::Uncertain) {
        append_pair_regions(reorganize_candidates);
    }
    return regions;
}

std::vector<CropRequest> build_crop_requests(
    EventType event_type,
    const std::vector<YoloDetection>& put_in_boxes,
    const std::vector<YoloDetection>& take_out_boxes,
    const std::vector<DetectionMatch>& partial_candidates,
    const std::vector<YoloDetection>& before_detections,
    const std::vector<YoloDetection>& after_detections
) {
    std::vector<CropRequest> requests;
    if (event_type == EventType::PutIn) {
        for (const auto& detection : put_in_boxes) {
            requests.push_back(CropRequest{"after", detection.coarse_class, detection.bbox});
        }
        return requests;
    }
    if (event_type == EventType::TakeOut) {
        for (const auto& detection : take_out_boxes) {
            requests.push_back(CropRequest{"before", detection.coarse_class, detection.bbox});
        }
        return requests;
    }
    if (event_type == EventType::PartialTakeOutCandidate) {
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
    }
    return requests;
}

double compute_event_confidence(
    EventType event_type,
    int count_delta,
    std::size_t matched_count,
    std::size_t partial_count,
    std::size_t reorganize_count
) {
    if (event_type == EventType::NoChange) {
        return matched_count > 0 ? 0.90 : 0.70;
    }
    if (event_type == EventType::Uncertain || event_type == EventType::NotEvaluated) {
        return 0.45;
    }
    if (event_type == EventType::Reorganize) {
        return std::clamp(0.62 + static_cast<double>(reorganize_count) * 0.05, 0.62, 0.85);
    }
    if (event_type == EventType::PartialTakeOutCandidate) {
        return std::clamp(0.58 + static_cast<double>(partial_count) * 0.06, 0.58, 0.82);
    }

    const double magnitude = static_cast<double>(std::max(1, std::abs(count_delta)));
    return std::clamp(0.76 + magnitude * 0.06, 0.76, 0.94);
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

    const CountDecisionSummary count_summary = summarize_counts(filtered_before, filtered_after);
    result.before_counts = count_summary.before_counts;
    result.after_counts = count_summary.after_counts;
    result.count_decision = count_summary.decision;

    const MatchComputation global_match = compute_matches(filtered_before, filtered_after, config_);
    result.matched_pairs = global_match.matched_pairs;
    result.disappeared_boxes = collect_unmatched(filtered_before, global_match.before_used);
    result.new_boxes = collect_unmatched(filtered_after, global_match.after_used);

    EventType event_type = EventType::NoChange;
    std::string review_reason;
    std::vector<YoloDetection> put_in_crop_boxes;
    std::vector<YoloDetection> take_out_crop_boxes;
    int primary_count_delta = 0;

    if (count_summary.increases.empty() && count_summary.decreases.empty()) {
        if (!result.new_boxes.empty() || !result.disappeared_boxes.empty()) {
            event_type = EventType::Uncertain;
            review_reason = "same-count scene left unmatched detections after box matching";
        } else {
            bool has_no_change_pair = false;
            bool has_partial_pair = false;
            bool has_reorganize_pair = false;
            bool has_uncertain_pair = false;

            for (const auto& match : result.matched_pairs) {
                const bool position_changed =
                    match.normalized_center_distance >= config_.reorganize_center_distance_threshold;
                const bool area_changed =
                    match.area_change_ratio >= config_.partial_area_change_threshold;

                if (!position_changed && !area_changed) {
                    has_no_change_pair = true;
                    continue;
                }

                if (position_changed && !area_changed) {
                    has_reorganize_pair = true;
                    result.reorganize_candidates.push_back(match);
                    continue;
                }

                if (!position_changed && area_changed && match.coarse_class == "fruit_vegetable") {
                    has_partial_pair = true;
                    result.partial_candidates.push_back(match);
                    continue;
                }

                has_uncertain_pair = true;
            }

            if (has_uncertain_pair) {
                event_type = EventType::Uncertain;
                review_reason = "same-count matches disagree on movement versus area-change pattern";
            } else if (has_reorganize_pair && has_partial_pair) {
                event_type = EventType::Uncertain;
                review_reason = "same-count scene mixed reorganize and partial-take-out patterns";
            } else if (has_partial_pair) {
                event_type = EventType::PartialTakeOutCandidate;
                review_reason =
                    "fruit_vegetable detections kept count and position but changed visible area";
            } else if (has_reorganize_pair) {
                event_type = EventType::Reorganize;
                review_reason = "matched detections moved without a meaningful area change";
            } else if (has_no_change_pair || (filtered_before.empty() && filtered_after.empty())) {
                event_type = EventType::NoChange;
            } else {
                event_type = EventType::Uncertain;
                review_reason = "same-count analysis did not produce a stable classification";
            }
        }
    } else if (count_summary.increases.size() == 1 && count_summary.decreases.empty()) {
        const auto& increase = count_summary.increases.front();
        event_type = EventType::PutIn;
        primary_count_delta = increase.delta;
        put_in_crop_boxes = select_changed_boxes_for_count_delta(
            filtered_before,
            filtered_after,
            increase.coarse_class,
            increase.delta,
            true,
            config_
        );
        review_reason =
            "coarse-class count increased only in after frame for " + increase.coarse_class;
    } else if (count_summary.increases.empty() && count_summary.decreases.size() == 1) {
        const auto& decrease = count_summary.decreases.front();
        event_type = EventType::TakeOut;
        primary_count_delta = decrease.delta;
        take_out_crop_boxes = select_changed_boxes_for_count_delta(
            filtered_before,
            filtered_after,
            decrease.coarse_class,
            std::abs(decrease.delta),
            false,
            config_
        );
        review_reason =
            "coarse-class count decreased only in after frame for " + decrease.coarse_class;
    } else {
        event_type = EventType::Uncertain;
        review_reason = count_summary.decision == "conflicting_count_changes"
            ? "multiple coarse classes changed in opposite directions"
            : "multiple coarse classes changed counts in a single event";
    }

    result.review_reason = review_reason;

    result.event.session_id = session_id;
    result.event.timestamp = now_as_utc_string();
    result.event.event_type = event_type;
    result.event.before_frame = before_frame_path;
    result.event.after_frame = after_frame_path;
    result.event.change_regions = build_change_regions(
        event_type,
        event_type == EventType::PutIn ? put_in_crop_boxes : result.new_boxes,
        event_type == EventType::TakeOut ? take_out_crop_boxes : result.disappeared_boxes,
        result.partial_candidates,
        result.reorganize_candidates,
        filtered_before,
        filtered_after
    );
    result.event.need_user_confirm = needs_manual_review(event_type);
    result.event.confidence = compute_event_confidence(
        event_type,
        primary_count_delta,
        result.matched_pairs.size(),
        result.partial_candidates.size(),
        result.reorganize_candidates.size()
    );

    const std::string primary_class = choose_primary_class(
        event_type,
        count_summary,
        filtered_before,
        filtered_after,
        result.partial_candidates,
        result.reorganize_candidates
    );
    DetectedObject object;
    object.category = primary_class;
    object.name = primary_class;
    if (event_type == EventType::PutIn) {
        object.count_delta = primary_count_delta;
        object.remain_level = 1.0;
    } else if (event_type == EventType::TakeOut) {
        object.count_delta = primary_count_delta;
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
        event_type,
        put_in_crop_boxes,
        take_out_crop_boxes,
        result.partial_candidates,
        filtered_before,
        filtered_after
    );

    (void)compute_crop_mean_abs_delta;
    return result;
}

}  // namespace fridge
