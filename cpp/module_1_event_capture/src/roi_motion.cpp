#include "roi_motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace fridge {

namespace {

struct ChangeMaskData {
    BoundingBox roi;
    std::vector<std::uint8_t> mask;
    std::size_t changed_pixels = 0;
    std::size_t darker_pixels = 0;
    std::size_t brighter_pixels = 0;
    double signed_delta_sum = 0.0;
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;
};

BoundingBox resolve_roi(const GrayFrame& frame, const BoundingBox& requested_roi) {
    if (frame.empty()) {
        return {};
    }

    if (requested_roi.width <= 0 || requested_roi.height <= 0) {
        return BoundingBox{0, 0, frame.width, frame.height};
    }

    const int x0 = std::clamp(requested_roi.x, 0, frame.width);
    const int y0 = std::clamp(requested_roi.y, 0, frame.height);
    const int x1 = std::clamp(requested_roi.x + requested_roi.width, 0, frame.width);
    const int y1 = std::clamp(requested_roi.y + requested_roi.height, 0, frame.height);

    if (x1 <= x0 || y1 <= y0) {
        return BoundingBox{0, 0, frame.width, frame.height};
    }

    return BoundingBox{x0, y0, x1 - x0, y1 - y0};
}

ChangeMaskData build_change_mask(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config
) {
    ChangeMaskData data;
    data.roi = resolve_roi(before_frame, config.roi);
    data.min_x = data.roi.x + data.roi.width;
    data.min_y = data.roi.y + data.roi.height;

    const std::size_t roi_pixels = static_cast<std::size_t>(data.roi.width * data.roi.height);
    if (roi_pixels == 0) {
        return data;
    }

    data.mask.assign(roi_pixels, 0);

    for (int local_y = 0; local_y < data.roi.height; ++local_y) {
        for (int local_x = 0; local_x < data.roi.width; ++local_x) {
            const int x = data.roi.x + local_x;
            const int y = data.roi.y + local_y;
            const int delta = static_cast<int>(after_frame.at(x, y)) - static_cast<int>(before_frame.at(x, y));
            if (std::abs(delta) < config.pixel_delta_threshold) {
                continue;
            }

            const std::size_t offset = static_cast<std::size_t>(local_y * data.roi.width + local_x);
            data.mask[offset] = 1;
            ++data.changed_pixels;
            data.signed_delta_sum += static_cast<double>(delta);
            if (delta < 0) {
                ++data.darker_pixels;
            } else {
                ++data.brighter_pixels;
            }

            data.min_x = std::min(data.min_x, x);
            data.min_y = std::min(data.min_y, y);
            data.max_x = std::max(data.max_x, x);
            data.max_y = std::max(data.max_y, y);
        }
    }

    return data;
}

std::vector<ChangeRegion> extract_regions(const ChangeMaskData& data, const RoiMotionConfig& config) {
    std::vector<ChangeRegion> regions;
    if (data.mask.empty() || data.changed_pixels == 0) {
        return regions;
    }

    std::vector<std::uint8_t> visited(data.mask.size(), 0);
    const int width = data.roi.width;
    const int height = data.roi.height;
    const std::size_t roi_pixels = static_cast<std::size_t>(width * height);

    auto enqueue_if_valid = [&](int x, int y, std::queue<std::pair<int, int>>& queue) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const std::size_t offset = static_cast<std::size_t>(y * width + x);
        if (visited[offset] != 0 || data.mask[offset] == 0) {
            return;
        }
        visited[offset] = 1;
        queue.emplace(x, y);
    };

    for (int local_y = 0; local_y < height; ++local_y) {
        for (int local_x = 0; local_x < width; ++local_x) {
            const std::size_t seed_offset = static_cast<std::size_t>(local_y * width + local_x);
            if (visited[seed_offset] != 0 || data.mask[seed_offset] == 0) {
                continue;
            }

            std::queue<std::pair<int, int>> queue;
            visited[seed_offset] = 1;
            queue.emplace(local_x, local_y);

            int min_x = local_x;
            int min_y = local_y;
            int max_x = local_x;
            int max_y = local_y;
            std::size_t area = 0;

            while (!queue.empty()) {
                const auto [x, y] = queue.front();
                queue.pop();
                ++area;
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);

                enqueue_if_valid(x - 1, y, queue);
                enqueue_if_valid(x + 1, y, queue);
                enqueue_if_valid(x, y - 1, queue);
                enqueue_if_valid(x, y + 1, queue);
            }

            if (area < static_cast<std::size_t>(std::max(1, config.min_region_area_pixels))) {
                continue;
            }

            ChangeRegion region;
            region.box = BoundingBox{
                data.roi.x + min_x,
                data.roi.y + min_y,
                max_x - min_x + 1,
                max_y - min_y + 1
            };
            region.score = static_cast<double>(area) / static_cast<double>(roi_pixels);
            regions.push_back(region);
        }
    }

    std::sort(
        regions.begin(),
        regions.end(),
        [](const ChangeRegion& left, const ChangeRegion& right) {
            return left.score > right.score;
        }
    );
    return regions;
}

ChangeAnalysis make_change_analysis(const ChangeMaskData& data, const RoiMotionConfig& config) {
    ChangeAnalysis analysis;
    const std::size_t roi_pixels = static_cast<std::size_t>(data.roi.width * data.roi.height);
    if (roi_pixels == 0) {
        return analysis;
    }

    analysis.darker_pixels = data.darker_pixels;
    analysis.brighter_pixels = data.brighter_pixels;
    analysis.darker_ratio = static_cast<double>(data.darker_pixels) / static_cast<double>(roi_pixels);
    analysis.brighter_ratio = static_cast<double>(data.brighter_pixels) / static_cast<double>(roi_pixels);
    analysis.regions = extract_regions(data, config);

    if (data.changed_pixels == 0) {
        analysis.summary.box = data.roi;
        return analysis;
    }

    analysis.summary.has_motion = true;
    analysis.summary.changed_ratio = static_cast<double>(data.changed_pixels) / static_cast<double>(roi_pixels);
    analysis.summary.mean_delta = data.signed_delta_sum / static_cast<double>(data.changed_pixels);
    analysis.summary.box = BoundingBox{
        data.min_x,
        data.min_y,
        data.max_x - data.min_x + 1,
        data.max_y - data.min_y + 1
    };
    return analysis;
}

}  // namespace

MotionSummary summarize_motion(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config
) {
    if (before_frame.empty() || after_frame.empty() ||
        before_frame.width != after_frame.width ||
        before_frame.height != after_frame.height) {
        return {};
    }

    return analyze_change(before_frame, after_frame, config).summary;
}

ChangeAnalysis analyze_change(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const RoiMotionConfig& config
) {
    if (before_frame.empty() || after_frame.empty() ||
        before_frame.width != after_frame.width ||
        before_frame.height != after_frame.height) {
        return {};
    }

    return make_change_analysis(build_change_mask(before_frame, after_frame, config), config);
}

}  // namespace fridge
