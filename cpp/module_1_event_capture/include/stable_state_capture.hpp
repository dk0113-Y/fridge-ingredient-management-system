#pragma once

#include <optional>
#include <vector>

#include "frame_selector.hpp"
#include "roi_motion.hpp"
#include "types.hpp"

namespace fridge {

struct StableCaptureEvent {
    SelectedFrames selected_frames;
    ChangeAnalysis final_change;
    std::size_t total_frame_count = 0;
    double peak_interframe_ratio = 0.0;
    double peak_baseline_ratio = 0.0;
};

class StableStateCapture {
public:
    StableStateCapture(
        RoiMotionConfig motion_config = {},
        FrameSelectorConfig selector_config = {}
    );

    std::optional<StableCaptureEvent> push_frame(const GrayFrame& frame);
    std::optional<StableCaptureEvent> flush();

    bool has_baseline() const;
    const GrayFrame& baseline_frame() const;

private:
    std::optional<StableCaptureEvent> finalize_event(const GrayFrame& settled_frame);
    void begin_disturbance(const GrayFrame& current_frame, double baseline_ratio, const MotionSummary& adjacent_motion);
    void append_event_frame(const GrayFrame& frame);
    void trim_recent_frames();
    void trim_event_frames();
    bool is_usable_frame(const GrayFrame& frame) const;
    bool is_stable_transition(const MotionSummary& motion) const;
    void update_stable_run(bool current_frame_usable, bool stable_transition);

    RoiMotionConfig motion_config_;
    FrameSelectorConfig selector_config_;
    GrayFrame previous_frame_;
    GrayFrame baseline_frame_;
    bool has_previous_ = false;
    bool baseline_ready_ = false;
    bool disturbance_active_ = false;
    std::size_t stable_run_count_ = 0;
    std::size_t baseline_stable_run_length_ = 0;
    std::size_t disturbance_candidate_count_ = 0;
    std::size_t settle_run_count_ = 0;
    std::size_t total_event_frame_count_ = 0;
    std::size_t cooldown_remaining_ = 0;
    double peak_interframe_ratio_ = 0.0;
    double peak_baseline_ratio_ = 0.0;
    std::vector<GrayFrame> recent_frames_;
    std::vector<GrayFrame> event_frames_;
};

}  // namespace fridge
