#include "stable_state_capture.hpp"

#include <algorithm>

namespace fridge {

namespace {

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

std::vector<MotionSummary> build_transitions(
    const std::vector<GrayFrame>& frames,
    const RoiMotionConfig& motion_config
) {
    std::vector<MotionSummary> transitions;
    if (frames.size() < 2) {
        return transitions;
    }

    transitions.reserve(frames.size() - 1);
    for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
        transitions.push_back(summarize_motion(frames[index], frames[index + 1], motion_config));
    }
    return transitions;
}

double largest_region_score(const ChangeAnalysis& analysis) {
    double largest_score = 0.0;
    for (const auto& region : analysis.regions) {
        largest_score = std::max(largest_score, region.score);
    }
    return largest_score;
}

}  // namespace

StableStateCapture::StableStateCapture(
    RoiMotionConfig motion_config,
    FrameSelectorConfig selector_config
)
    : motion_config_(std::move(motion_config)),
      selector_config_(std::move(selector_config)) {}

std::optional<StableCaptureEvent> StableStateCapture::push_frame(const GrayFrame& frame) {
    if (frame.empty()) {
        return std::nullopt;
    }

    if (!has_previous_) {
        previous_frame_ = frame;
        has_previous_ = true;
        stable_run_count_ = is_usable_frame(frame) ? 1 : 0;
        recent_frames_.clear();
        recent_frames_.push_back(frame);
        return std::nullopt;
    }

    const bool current_frame_usable = is_usable_frame(frame);
    const bool previous_frame_usable = is_usable_frame(previous_frame_);
    const MotionSummary adjacent_motion =
        (current_frame_usable && previous_frame_usable)
            ? summarize_motion(previous_frame_, frame, motion_config_)
            : MotionSummary{};
    const bool stable_transition = current_frame_usable &&
        previous_frame_usable &&
        is_stable_transition(adjacent_motion);

    recent_frames_.push_back(frame);
    trim_recent_frames();

    if (!baseline_ready_) {
        update_stable_run(current_frame_usable, stable_transition);
        if (stable_run_count_ >= std::max<std::size_t>(2, selector_config_.baseline_warmup_frames)) {
            baseline_frame_ = frame;
            baseline_ready_ = true;
            baseline_stable_run_length_ = stable_run_count_;
        }
        previous_frame_ = frame;
        return std::nullopt;
    }

    const ChangeAnalysis baseline_change =
        current_frame_usable ? analyze_change(baseline_frame_, frame, motion_config_) : ChangeAnalysis{};

    if (!disturbance_active_) {
        update_stable_run(current_frame_usable, stable_transition);

        if (cooldown_remaining_ > 0) {
            --cooldown_remaining_;
        }

        if (current_frame_usable &&
            stable_run_count_ >= std::max<std::size_t>(2, selector_config_.min_stable_run_frames) &&
            baseline_change.summary.changed_ratio <= selector_config_.persistent_change_ratio_threshold) {
            baseline_frame_ = frame;
            baseline_stable_run_length_ = stable_run_count_;
        }

        const bool disturbance_signal =
            current_frame_usable &&
            cooldown_remaining_ == 0 &&
            (adjacent_motion.changed_ratio >= selector_config_.motion_ratio_threshold ||
             baseline_change.summary.changed_ratio >= selector_config_.baseline_change_ratio_threshold);

        disturbance_candidate_count_ = disturbance_signal ? disturbance_candidate_count_ + 1U : 0U;
        if (disturbance_candidate_count_ >= std::max<std::size_t>(1, selector_config_.disturbance_trigger_frames)) {
            begin_disturbance(frame, baseline_change.summary.changed_ratio, adjacent_motion);
        }

        previous_frame_ = frame;
        return std::nullopt;
    }

    append_event_frame(frame);
    ++total_event_frame_count_;
    peak_interframe_ratio_ = std::max(peak_interframe_ratio_, adjacent_motion.changed_ratio);
    peak_baseline_ratio_ = std::max(peak_baseline_ratio_, baseline_change.summary.changed_ratio);

    if (stable_transition) {
        ++settle_run_count_;
    } else {
        settle_run_count_ = 0;
    }

    previous_frame_ = frame;

    if (settle_run_count_ >= std::max<std::size_t>(2, selector_config_.settle_run_frames)) {
        return finalize_event(frame);
    }

    return std::nullopt;
}

std::optional<StableCaptureEvent> StableStateCapture::flush() {
    if (!disturbance_active_ || event_frames_.empty() || settle_run_count_ < std::max<std::size_t>(2, selector_config_.settle_run_frames)) {
        return std::nullopt;
    }
    return finalize_event(event_frames_.back());
}

bool StableStateCapture::has_baseline() const {
    return baseline_ready_;
}

const GrayFrame& StableStateCapture::baseline_frame() const {
    return baseline_frame_;
}

std::optional<StableCaptureEvent> StableStateCapture::finalize_event(const GrayFrame& settled_frame) {
    append_event_frame(settled_frame);
    const ChangeAnalysis final_change = analyze_change(baseline_frame_, settled_frame, motion_config_);
    const double strongest_region_score = largest_region_score(final_change);

    const bool persistent_change =
        final_change.summary.changed_ratio >= selector_config_.persistent_change_ratio_threshold ||
        strongest_region_score >= selector_config_.persistent_change_ratio_threshold;

    StableCaptureEvent capture_event;
    if (persistent_change) {
        const std::vector<MotionSummary> transitions = build_transitions(event_frames_, motion_config_);
        double transition_peak = 0.0;
        for (const auto& transition : transitions) {
            transition_peak = std::max(transition_peak, transition.changed_ratio);
        }

        capture_event.final_change = final_change;
        capture_event.total_frame_count = total_event_frame_count_;
        capture_event.peak_interframe_ratio = std::max(peak_interframe_ratio_, transition_peak);
        capture_event.peak_baseline_ratio = peak_baseline_ratio_;
        capture_event.selected_frames.before_frame = baseline_frame_;
        capture_event.selected_frames.after_frame = settled_frame;
        capture_event.selected_frames.before_index = 0;
        capture_event.selected_frames.after_index = event_frames_.empty() ? 0 : event_frames_.size() - 1;
        capture_event.selected_frames.transitions = transitions;
        capture_event.selected_frames.peak_transition_ratio = capture_event.peak_interframe_ratio;
        capture_event.selected_frames.peak_baseline_change_ratio = peak_baseline_ratio_;
        capture_event.selected_frames.final_change_ratio = final_change.summary.changed_ratio;
        capture_event.selected_frames.stable_before_run_length = baseline_stable_run_length_;
        capture_event.selected_frames.stable_after_run_length = settle_run_count_;
    }

    baseline_frame_ = settled_frame;
    baseline_ready_ = true;
    stable_run_count_ = settle_run_count_;
    baseline_stable_run_length_ = settle_run_count_;
    disturbance_active_ = false;
    disturbance_candidate_count_ = 0;
    settle_run_count_ = 0;
    total_event_frame_count_ = 0;
    cooldown_remaining_ = selector_config_.post_event_cooldown_frames;
    peak_interframe_ratio_ = 0.0;
    peak_baseline_ratio_ = 0.0;
    event_frames_.clear();

    if (!persistent_change) {
        return std::nullopt;
    }

    return capture_event;
}

void StableStateCapture::begin_disturbance(
    const GrayFrame& current_frame,
    double baseline_ratio,
    const MotionSummary& adjacent_motion
) {
    disturbance_active_ = true;
    disturbance_candidate_count_ = 0;
    settle_run_count_ = 0;
    total_event_frame_count_ = 0;
    peak_interframe_ratio_ = adjacent_motion.changed_ratio;
    peak_baseline_ratio_ = baseline_ratio;
    event_frames_.clear();
    append_event_frame(baseline_frame_);
    for (const auto& historical_frame : recent_frames_) {
        append_event_frame(historical_frame);
    }
    append_event_frame(current_frame);
    total_event_frame_count_ = event_frames_.size();
}

void StableStateCapture::append_event_frame(const GrayFrame& frame) {
    if (event_frames_.empty() || event_frames_.back().index != frame.index) {
        event_frames_.push_back(frame);
        trim_event_frames();
    }
}

void StableStateCapture::trim_recent_frames() {
    const std::size_t limit = std::max<std::size_t>(4, selector_config_.disturbance_trigger_frames + 3);
    while (recent_frames_.size() > limit) {
        recent_frames_.erase(recent_frames_.begin());
    }
}

void StableStateCapture::trim_event_frames() {
    const std::size_t limit = std::max<std::size_t>(6, selector_config_.max_disturbance_frames);
    while (event_frames_.size() > limit && event_frames_.size() > 1) {
        event_frames_.erase(event_frames_.begin() + 1);
    }
}

bool StableStateCapture::is_usable_frame(const GrayFrame& frame) const {
    return !frame.empty() && frame_mean_intensity(frame) >= selector_config_.black_frame_mean_threshold;
}

bool StableStateCapture::is_stable_transition(const MotionSummary& motion) const {
    return motion.changed_ratio <= selector_config_.stable_ratio_threshold;
}

void StableStateCapture::update_stable_run(bool current_frame_usable, bool stable_transition) {
    if (!current_frame_usable) {
        stable_run_count_ = 0;
        return;
    }

    if (!has_previous_) {
        stable_run_count_ = 1;
        return;
    }

    stable_run_count_ = stable_transition ? stable_run_count_ + 1U : 1U;
}

}  // namespace fridge
