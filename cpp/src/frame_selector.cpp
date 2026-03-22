#include "frame_selector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fridge {

namespace {

constexpr double kMinimumUsableFrameMean = 5.0;

double frame_mean_intensity(const GrayFrame& frame) {
    if (frame.empty()) {
        return 0.0;
    }

    double pixel_sum = 0.0;
    for (const std::uint8_t value : frame.pixels) {
        pixel_sum += static_cast<double>(value);
    }

    return pixel_sum / static_cast<double>(frame.pixels.size());
}

bool is_usable_frame(const GrayFrame& frame) {
    return !frame.empty() && frame_mean_intensity(frame) >= kMinimumUsableFrameMean;
}

double stability_score(const std::vector<MotionSummary>& transitions, std::size_t frame_index) {
    double score = 0.0;
    int terms = 0;

    if (frame_index > 0 && frame_index - 1 < transitions.size()) {
        score += transitions[frame_index - 1].changed_ratio;
        ++terms;
    }

    if (frame_index < transitions.size()) {
        score += transitions[frame_index].changed_ratio;
        ++terms;
    }

    if (terms == 0) {
        return 0.0;
    }

    return score / static_cast<double>(terms);
}

std::size_t select_stable_frame(
    const std::vector<GrayFrame>& frames,
    const std::vector<MotionSummary>& transitions,
    std::size_t begin_index,
    std::size_t end_index,
    bool prefer_latest_when_equal
) {
    std::size_t best_index = begin_index;
    double best_score = std::numeric_limits<double>::max();
    bool found_usable_candidate = false;

    for (std::size_t index = begin_index; index <= end_index; ++index) {
        if (!is_usable_frame(frames[index])) {
            continue;
        }

        const double score = stability_score(transitions, index);
        if (score < best_score ||
            (prefer_latest_when_equal && score == best_score && index > best_index)) {
            best_score = score;
            best_index = index;
            found_usable_candidate = true;
        }
    }

    if (!found_usable_candidate) {
        for (std::size_t index = begin_index; index <= end_index; ++index) {
            const double score = stability_score(transitions, index);
            if (score < best_score ||
                (prefer_latest_when_equal && score == best_score && index > best_index)) {
                best_score = score;
                best_index = index;
            }
        }
    }

    return best_index;
}

}  // namespace

SelectedFrames select_keyframes(const std::vector<GrayFrame>& frames, const RoiMotionConfig& motion_config) {
    SelectedFrames selected;
    if (frames.empty()) {
        return selected;
    }

    selected.before_frame = frames.front();
    selected.after_frame = frames.back();
    selected.before_index = 0;
    selected.after_index = frames.size() - 1;

    if (frames.size() == 1) {
        return selected;
    }

    selected.transitions.reserve(frames.size() - 1);
    for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
        selected.transitions.push_back(summarize_motion(frames[index], frames[index + 1], motion_config));
    }

    std::size_t peak_transition = 0;
    double peak_ratio = 0.0;
    bool found_usable_peak = false;
    for (std::size_t index = 0; index < selected.transitions.size(); ++index) {
        if (!is_usable_frame(frames[index]) || !is_usable_frame(frames[index + 1])) {
            continue;
        }

        const double changed_ratio = selected.transitions[index].changed_ratio;
        if (!found_usable_peak || changed_ratio > peak_ratio) {
            peak_transition = index;
            peak_ratio = changed_ratio;
            found_usable_peak = true;
        }
    }

    if (!found_usable_peak) {
        const auto peak_it = std::max_element(
            selected.transitions.begin(),
            selected.transitions.end(),
            [](const MotionSummary& left, const MotionSummary& right) {
                return left.changed_ratio < right.changed_ratio;
            }
        );
        peak_transition = static_cast<std::size_t>(std::distance(selected.transitions.begin(), peak_it));
        peak_ratio = peak_it != selected.transitions.end() ? peak_it->changed_ratio : 0.0;
    }

    if (peak_ratio < 0.005) {
        return selected;
    }

    selected.before_index = select_stable_frame(frames, selected.transitions, 0, peak_transition, false);
    selected.after_index = select_stable_frame(frames, selected.transitions, peak_transition + 1, frames.size() - 1, true);

    if (selected.after_index <= selected.before_index) {
        selected.before_index = 0;
        selected.after_index = frames.size() - 1;
    }

    selected.before_frame = frames[selected.before_index];
    selected.after_frame = frames[selected.after_index];
    return selected;
}

}  // namespace fridge
