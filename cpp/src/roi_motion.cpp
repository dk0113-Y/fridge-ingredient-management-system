#include "roi_motion.hpp"

#include <algorithm>
#include <cmath>

namespace fridge {

MotionSummary summarize_motion(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config
) {
    MotionSummary summary;
    if (before_frame.empty() || after_frame.empty() ||
        before_frame.width != after_frame.width ||
        before_frame.height != after_frame.height) {
        return summary;
    }

    const int width = before_frame.width;
    const int height = before_frame.height;
    const std::size_t pixel_count = static_cast<std::size_t>(width * height);

    int min_x = width;
    int min_y = height;
    int max_x = -1;
    int max_y = -1;
    std::size_t changed_pixels = 0;
    double signed_delta_sum = 0.0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int before_value = before_frame.at(x, y);
            const int after_value = after_frame.at(x, y);
            const int delta = after_value - before_value;
            if (std::abs(delta) < config.pixel_delta_threshold) {
                continue;
            }

            ++changed_pixels;
            signed_delta_sum += static_cast<double>(delta);
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }

    if (changed_pixels == 0) {
        summary.box = BoundingBox{0, 0, width, height};
        return summary;
    }

    summary.has_motion = true;
    summary.changed_ratio = static_cast<double>(changed_pixels) / static_cast<double>(pixel_count);
    summary.mean_delta = signed_delta_sum / static_cast<double>(changed_pixels);
    summary.box = BoundingBox{
        min_x,
        min_y,
        max_x - min_x + 1,
        max_y - min_y + 1
    };
    return summary;
}

}  // namespace fridge
