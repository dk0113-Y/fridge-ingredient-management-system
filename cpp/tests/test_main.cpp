#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "event_detector.hpp"
#include "frame_selector.hpp"

namespace {

using fridge::EventDetector;
using fridge::EventType;
using fridge::GrayFrame;
using fridge::SelectedFrames;

GrayFrame make_frame(int width, int height, std::uint8_t fill, int index) {
    GrayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.index = index;
    frame.pixels.assign(static_cast<std::size_t>(width * height), fill);
    return frame;
}

void paint_box(GrayFrame& frame, int x0, int y0, int box_width, int box_height, std::uint8_t value) {
    for (int y = y0; y < y0 + box_height; ++y) {
        for (int x = x0; x < x0 + box_width; ++x) {
            frame.pixels[static_cast<std::size_t>(y * frame.width + x)] = value;
        }
    }
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool test_no_change_identical_frames() {
    const auto frame = make_frame(8, 8, 120, 0);
    SelectedFrames selected{frame, frame, 0, 0, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_no_change", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::NoChange, "identical frames should be no_change");
}

bool test_put_in_detected() {
    const auto before = make_frame(8, 8, 180, 0);
    auto after = make_frame(8, 8, 180, 1);
    paint_box(after, 2, 2, 4, 4, 30);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_put_in", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::PutIn, "dark new object should be put_in");
}

bool test_take_out_detected() {
    auto before = make_frame(8, 8, 180, 0);
    paint_box(before, 2, 2, 4, 4, 30);
    const auto after = make_frame(8, 8, 180, 1);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_take_out", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::TakeOut, "removing a dark object should be take_out");
}

bool test_partial_candidate_detected() {
    const auto before = make_frame(8, 8, 120, 0);
    auto after = make_frame(8, 8, 120, 1);
    paint_box(after, 2, 2, 4, 4, 112);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_partial", "before.jpg", "after.jpg");
    return expect(
        result.event_type == EventType::PartialTakeOutCandidate && result.need_user_confirm,
        "weak signed change after clear motion should be partial_take_out_candidate"
    );
}

bool test_low_light_put_in() {
    const auto before = make_frame(8, 8, 40, 0);
    auto after = make_frame(8, 8, 40, 1);
    paint_box(after, 2, 2, 4, 4, 5);
    SelectedFrames selected{before, after, 0, 1, {}};
    EventDetector detector;
    const auto result = detector.detect(selected, "test_low_light", "before.jpg", "after.jpg");
    return expect(result.event_type == EventType::PutIn, "low-light block addition should still look like put_in");
}

bool test_hand_occlusion_sequence_returns_no_change() {
    const auto base = make_frame(8, 8, 140, 0);
    auto occluded = make_frame(8, 8, 140, 1);
    paint_box(occluded, 1, 1, 6, 6, 20);
    auto frames = std::vector<GrayFrame>{base, occluded, base};
    const auto selected = fridge::select_keyframes(frames);
    EventDetector detector;
    const auto result = detector.detect(selected, "test_occlusion", "before.jpg", "after.jpg");
    return expect(
        result.event_type == EventType::NoChange,
        "temporary hand occlusion with no final state change should be no_change"
    );
}

bool test_trailing_black_frame_is_not_selected_as_after() {
    const auto before = make_frame(8, 8, 140, 0);
    auto settled_after = make_frame(8, 8, 140, 1);
    paint_box(settled_after, 2, 2, 4, 4, 30);

    auto stable_after = settled_after;
    stable_after.index = 2;

    const auto trailing_black = make_frame(8, 8, 0, 3);
    auto frames = std::vector<GrayFrame>{before, settled_after, stable_after, trailing_black};

    const auto selected = fridge::select_keyframes(frames);
    EventDetector detector;
    const auto result = detector.detect(selected, "test_trailing_black", "before.jpg", "after.jpg");
    return expect(
        selected.after_index == 2 && result.event_type == EventType::PutIn,
        "trailing black frames should not override the settled after frame"
    );
}

}  // namespace

int main() {
    const std::vector<std::function<bool()>> tests = {
        test_no_change_identical_frames,
        test_put_in_detected,
        test_take_out_detected,
        test_partial_candidate_detected,
        test_low_light_put_in,
        test_hand_occlusion_sequence_returns_no_change,
        test_trailing_black_frame_is_not_selected_as_after
    };

    int failed = 0;
    for (const auto& test : tests) {
        if (!test()) {
            ++failed;
        }
    }

    if (failed > 0) {
        std::cerr << failed << " C++ tests failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All C++ tests passed.\n";
    return EXIT_SUCCESS;
}
