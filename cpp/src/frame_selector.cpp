#include "frame_selector.hpp"

#include <algorithm>
#include <limits>

namespace fridge {

namespace {

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
    const std::vector<MotionSummary>& transitions,
    std::size_t begin_index,
    std::size_t end_index
) {
    std::size_t best_index = begin_index;
    double best_score = std::numeric_limits<double>::max();

    for (std::size_t index = begin_index; index <= end_index; ++index) {
        const double score = stability_score(transitions, index);
        if (score < best_score) {
            best_score = score;
            best_index = index;
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

    const auto peak_it = std::max_element(
        selected.transitions.begin(),
        selected.transitions.end(),
        [](const MotionSummary& left, const MotionSummary& right) {
            return left.changed_ratio < right.changed_ratio;
        }
    );

    const std::size_t peak_transition = static_cast<std::size_t>(std::distance(selected.transitions.begin(), peak_it));
    const double peak_ratio = peak_it != selected.transitions.end() ? peak_it->changed_ratio : 0.0;

    if (peak_ratio < 0.005) {
        return selected;
    }

    selected.before_index = select_stable_frame(selected.transitions, 0, peak_transition);
    selected.after_index = select_stable_frame(selected.transitions, peak_transition + 1, frames.size() - 1);

    if (selected.after_index <= selected.before_index) {
        selected.before_index = 0;
        selected.after_index = frames.size() - 1;
    }

    selected.before_frame = frames[selected.before_index];
    selected.after_frame = frames[selected.after_index];
    return selected;
}

}  // namespace fridge
