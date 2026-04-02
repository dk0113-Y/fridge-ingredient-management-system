#include "frame_selector.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace fridge {

namespace {

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

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

bool is_usable_frame(const GrayFrame& frame, const FrameSelectorConfig& config) {
    return !frame.empty() && frame_mean_intensity(frame) >= config.black_frame_mean_threshold;
}

std::size_t find_first_usable_frame(const std::vector<bool>& usable_frames) {
    for (std::size_t index = 0; index < usable_frames.size(); ++index) {
        if (usable_frames[index]) {
            return index;
        }
    }
    return kNoIndex;
}

std::size_t find_last_usable_frame(const std::vector<bool>& usable_frames, std::size_t begin_index = 0) {
    if (usable_frames.empty() || begin_index >= usable_frames.size()) {
        return kNoIndex;
    }

    for (std::size_t index = usable_frames.size(); index-- > begin_index;) {
        if (usable_frames[index]) {
            return index;
        }
    }
    return kNoIndex;
}

std::size_t find_first_motion_transition(
    const std::vector<MotionSummary>& transitions,
    const std::vector<bool>& usable_frames,
    const FrameSelectorConfig& config
) {
    for (std::size_t index = 0; index < transitions.size(); ++index) {
        if (!usable_frames[index] || !usable_frames[index + 1]) {
            continue;
        }
        if (transitions[index].changed_ratio >= config.motion_ratio_threshold) {
            return index;
        }
    }
    return kNoIndex;
}

std::size_t find_last_motion_transition(
    const std::vector<MotionSummary>& transitions,
    const std::vector<bool>& usable_frames,
    const FrameSelectorConfig& config
) {
    if (transitions.empty()) {
        return kNoIndex;
    }

    for (std::size_t index = transitions.size(); index-- > 0;) {
        if (!usable_frames[index] || !usable_frames[index + 1]) {
            continue;
        }
        if (transitions[index].changed_ratio >= config.motion_ratio_threshold) {
            return index;
        }
    }
    return kNoIndex;
}

std::size_t find_before_frame_index(
    const std::vector<bool>& usable_frames,
    std::size_t first_motion_transition,
    std::size_t fallback_index
) {
    if (usable_frames.empty()) {
        return fallback_index;
    }

    if (first_motion_transition == kNoIndex) {
        return fallback_index;
    }

    const std::size_t last_before = std::min(first_motion_transition, usable_frames.size() - 1);
    for (std::size_t index = last_before + 1; index-- > 0;) {
        if (usable_frames[index]) {
            return index;
        }
        if (index == 0) {
            break;
        }
    }
    return fallback_index;
}

std::size_t find_after_frame_index(
    const std::vector<MotionSummary>& transitions,
    const std::vector<bool>& usable_frames,
    std::size_t last_motion_transition,
    const FrameSelectorConfig& config,
    std::size_t fallback_index
) {
    if (usable_frames.empty()) {
        return fallback_index;
    }

    const std::size_t first_after =
        last_motion_transition == kNoIndex ? 0 : std::min(last_motion_transition + 1, usable_frames.size() - 1);
    std::size_t best_index = fallback_index;

    std::size_t frame_index = first_after;
    while (frame_index < usable_frames.size()) {
        while (frame_index < usable_frames.size() && !usable_frames[frame_index]) {
            ++frame_index;
        }
        if (frame_index >= usable_frames.size()) {
            break;
        }

        std::size_t run_end = frame_index;
        while (run_end + 1 < usable_frames.size() &&
               usable_frames[run_end + 1] &&
               transitions[run_end].changed_ratio <= config.stable_ratio_threshold) {
            ++run_end;
        }

        const std::size_t run_length = run_end - frame_index + 1;
        if (run_length >= config.min_stable_run_frames) {
            best_index = run_end;
        }

        frame_index = run_end + 1;
    }

    return best_index;
}

}  // namespace

SelectedFrames select_keyframes(
    const std::vector<GrayFrame>& frames,
    const RoiMotionConfig& motion_config,
    const FrameSelectorConfig& selector_config
) {
    SelectedFrames selected;
    if (frames.empty()) {
        return selected;
    }

    selected.transitions.reserve(frames.size() > 0 ? frames.size() - 1 : 0);
    for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
        selected.transitions.push_back(summarize_motion(frames[index], frames[index + 1], motion_config));
    }

    std::vector<bool> usable_frames;
    usable_frames.reserve(frames.size());
    for (const auto& frame : frames) {
        usable_frames.push_back(is_usable_frame(frame, selector_config));
    }

    const std::size_t first_usable = find_first_usable_frame(usable_frames);
    const std::size_t last_usable = find_last_usable_frame(usable_frames);

    for (const auto& transition : selected.transitions) {
        selected.peak_transition_ratio = std::max(selected.peak_transition_ratio, transition.changed_ratio);
    }

    selected.before_index = first_usable == kNoIndex ? 0 : first_usable;
    selected.after_index = last_usable == kNoIndex ? frames.size() - 1 : last_usable;
    selected.before_frame = frames[selected.before_index];
    selected.after_frame = frames[selected.after_index];

    if (frames.size() == 1 || first_usable == kNoIndex || last_usable == kNoIndex) {
        return selected;
    }

    const std::size_t first_motion = find_first_motion_transition(selected.transitions, usable_frames, selector_config);
    const std::size_t last_motion = find_last_motion_transition(selected.transitions, usable_frames, selector_config);

    if (first_motion == kNoIndex || last_motion == kNoIndex) {
        return selected;
    }

    selected.before_index = find_before_frame_index(usable_frames, first_motion, selected.before_index);
    selected.after_index = find_after_frame_index(
        selected.transitions,
        usable_frames,
        last_motion,
        selector_config,
        selected.after_index
    );

    if (selected.after_index <= selected.before_index) {
        selected.before_index = first_usable;
        selected.after_index = last_usable;
    }

    selected.before_frame = frames[selected.before_index];
    selected.after_frame = frames[selected.after_index];
    selected.final_change_ratio = summarize_motion(
        selected.before_frame,
        selected.after_frame,
        motion_config
    ).changed_ratio;
    return selected;
}

}  // namespace fridge
