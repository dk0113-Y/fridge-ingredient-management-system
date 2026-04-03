#include "video_io.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef FRIDGE_USE_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace fridge {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required_size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        required_size,
        nullptr,
        nullptr
    );
    return result;
}
#endif

std::string path_to_display_string(const fs::path& path) {
#ifdef _WIN32
    return wide_to_utf8(path.generic_wstring());
#else
    return path.generic_string();
#endif
}

#ifdef FRIDGE_USE_OPENCV
bool write_encoded_image_file(const GrayFrame& frame, const fs::path& path, std::string& error_message) {
    cv::Mat image(frame.height, frame.width, CV_8UC1, const_cast<std::uint8_t*>(frame.pixels.data()));

    const std::string extension = path.has_extension() ? path.extension().string() : ".jpg";
    std::vector<std::uint8_t> encoded_bytes;
    if (!cv::imencode(extension, image, encoded_bytes)) {
        error_message = "Failed to encode image: " + path_to_display_string(path);
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error_message = "Failed to open output file: " + path_to_display_string(path);
        return false;
    }

    output.write(reinterpret_cast<const char*>(encoded_bytes.data()), static_cast<std::streamsize>(encoded_bytes.size()));
    if (!output) {
        error_message = "Failed to write encoded image: " + path_to_display_string(path);
        return false;
    }

    return true;
}
#endif

bool read_token(std::istream& input, std::string& token) {
    token.clear();
    while (input >> token) {
        if (!token.empty() && token[0] == '#') {
            std::string ignored;
            std::getline(input, ignored);
            continue;
        }
        return true;
    }
    return false;
}

bool load_pgm_file(const fs::path& path, int frame_index, GrayFrame& frame, std::string& error_message) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error_message = "Failed to open frame file: " + path_to_display_string(path);
        return false;
    }

    std::string magic;
    std::string width_token;
    std::string height_token;
    std::string max_value_token;
    if (!read_token(input, magic) || magic != "P5" ||
        !read_token(input, width_token) ||
        !read_token(input, height_token) ||
        !read_token(input, max_value_token)) {
        error_message = "Unsupported PGM file: " + path_to_display_string(path);
        return false;
    }

    const int width = std::stoi(width_token);
    const int height = std::stoi(height_token);
    const int max_value = std::stoi(max_value_token);
    if (width <= 0 || height <= 0 || max_value != 255) {
        error_message = "Invalid PGM metadata: " + path_to_display_string(path);
        return false;
    }

    input.get();
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(width * height));
    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (!input) {
        error_message = "Unexpected EOF while reading PGM payload: " + path_to_display_string(path);
        return false;
    }

    frame.width = width;
    frame.height = height;
    frame.index = frame_index;
    frame.pixels = std::move(buffer);
    return true;
}

bool load_encoded_image_file(const fs::path& path, int frame_index, GrayFrame& frame, std::string& error_message) {
#ifdef FRIDGE_USE_OPENCV
    const cv::Mat image = cv::imread(path_to_display_string(path), cv::IMREAD_GRAYSCALE);
    if (image.empty()) {
        error_message = "Failed to load encoded image: " + path_to_display_string(path);
        return false;
    }

    frame.width = image.cols;
    frame.height = image.rows;
    frame.index = frame_index;
    frame.pixels.assign(image.datastart, image.dataend);
    return true;
#else
    (void)path;
    (void)frame_index;
    (void)frame;
    error_message =
        "Reading JPG/PNG debug images requires an OpenCV-enabled build. "
        "Without OpenCV, use .pgm debug images instead.";
    return false;
#endif
}

bool write_pgm_file(const GrayFrame& frame, const fs::path& path, std::string& error_message) {
    if (frame.empty()) {
        error_message = "Cannot write an empty frame.";
        return false;
    }

    fs::create_directories(path.parent_path());

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error_message = "Failed to open output file: " + path_to_display_string(path);
        return false;
    }

    output << "P5\n" << frame.width << " " << frame.height << "\n255\n";
    output.write(reinterpret_cast<const char*>(frame.pixels.data()), static_cast<std::streamsize>(frame.pixels.size()));
    if (!output) {
        error_message = "Failed to write PGM payload: " + path_to_display_string(path);
        return false;
    }

    return true;
}

bool load_frame_directory(const fs::path& directory, std::vector<GrayFrame>& frames, std::string& error_message) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto extension = entry.path().extension().string();
        if (extension == ".pgm" || extension == ".PGM") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    if (files.empty()) {
        error_message = "Frame directory does not contain any .pgm files: " + path_to_display_string(directory);
        return false;
    }

    frames.clear();
    frames.reserve(files.size());
    for (std::size_t index = 0; index < files.size(); ++index) {
        GrayFrame frame;
        if (!load_pgm_file(files[index], static_cast<int>(index), frame, error_message)) {
            return false;
        }
        frames.push_back(std::move(frame));
    }
    return true;
}

void draw_box_border(GrayFrame& frame, const BoundingBox& box, int thickness) {
    if (frame.empty() || box.width <= 0 || box.height <= 0 || thickness <= 0) {
        return;
    }

    const int x0 = std::clamp(box.x, 0, frame.width - 1);
    const int y0 = std::clamp(box.y, 0, frame.height - 1);
    const int x1 = std::clamp(box.x + box.width - 1, 0, frame.width - 1);
    const int y1 = std::clamp(box.y + box.height - 1, 0, frame.height - 1);
    if (x1 < x0 || y1 < y0) {
        return;
    }

    for (int offset = 0; offset < thickness; ++offset) {
        const int top = std::min(y0 + offset, y1);
        const int bottom = std::max(y1 - offset, y0);
        const int left = std::min(x0 + offset, x1);
        const int right = std::max(x1 - offset, x0);

        for (int x = left; x <= right; ++x) {
            const auto top_index = static_cast<std::size_t>(top * frame.width + x);
            const auto bottom_index = static_cast<std::size_t>(bottom * frame.width + x);
            frame.pixels[top_index] = 255;
            frame.pixels[bottom_index] = 255;
        }

        for (int y = top; y <= bottom; ++y) {
            const auto left_index = static_cast<std::size_t>(y * frame.width + left);
            const auto right_index = static_cast<std::size_t>(y * frame.width + right);
            frame.pixels[left_index] = 255;
            frame.pixels[right_index] = 255;
        }
    }

    if (box.width > 2 && box.height > 2) {
        const BoundingBox inner{
            box.x + thickness,
            box.y + thickness,
            std::max(0, box.width - thickness * 2),
            std::max(0, box.height - thickness * 2)
        };

        if (inner.width > 0 && inner.height > 0) {
            const int inner_x0 = std::clamp(inner.x, 0, frame.width - 1);
            const int inner_y0 = std::clamp(inner.y, 0, frame.height - 1);
            const int inner_x1 = std::clamp(inner.x + inner.width - 1, 0, frame.width - 1);
            const int inner_y1 = std::clamp(inner.y + inner.height - 1, 0, frame.height - 1);

            for (int x = inner_x0; x <= inner_x1; ++x) {
                frame.pixels[static_cast<std::size_t>(inner_y0 * frame.width + x)] = 0;
                frame.pixels[static_cast<std::size_t>(inner_y1 * frame.width + x)] = 0;
            }

            for (int y = inner_y0; y <= inner_y1; ++y) {
                frame.pixels[static_cast<std::size_t>(y * frame.width + inner_x0)] = 0;
                frame.pixels[static_cast<std::size_t>(y * frame.width + inner_x1)] = 0;
            }
        }
    }
}

}  // namespace

bool load_frames(const fs::path& source, std::vector<GrayFrame>& frames, std::string& error_message) {
    if (!fs::exists(source)) {
        error_message = "Input path does not exist: " + path_to_display_string(source);
        return false;
    }

    if (fs::is_directory(source)) {
        return load_frame_directory(source, frames, error_message);
    }

#ifdef FRIDGE_USE_OPENCV
    cv::VideoCapture capture(path_to_display_string(source));
    if (!capture.isOpened()) {
        error_message = "Failed to open video file: " + path_to_display_string(source);
        return false;
    }

    frames.clear();
    cv::Mat bgr_frame;
    cv::Mat gray_frame;
    int frame_index = 0;
    while (capture.read(bgr_frame)) {
        if (bgr_frame.empty()) {
            continue;
        }

        cv::cvtColor(bgr_frame, gray_frame, cv::COLOR_BGR2GRAY);
        GrayFrame frame;
        frame.width = gray_frame.cols;
        frame.height = gray_frame.rows;
        frame.index = frame_index++;
        frame.pixels.assign(gray_frame.datastart, gray_frame.dataend);
        frames.push_back(std::move(frame));
    }

    if (frames.empty()) {
        error_message = "Video file did not yield any frames: " + path_to_display_string(source);
        return false;
    }

    return true;
#else
    error_message =
        "Video-file input requires an OpenCV-enabled build. "
        "Without OpenCV, pass a directory of .pgm frames for debug runs.";
    return false;
#endif
}

bool load_debug_image(const fs::path& input_path, GrayFrame& frame, std::string& error_message) {
    if (!fs::exists(input_path)) {
        error_message = "Debug image path does not exist: " + path_to_display_string(input_path);
        return false;
    }
    if (!fs::is_regular_file(input_path)) {
        error_message = "Debug image path is not a file: " + path_to_display_string(input_path);
        return false;
    }

    std::string extension = input_path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );

    if (extension == ".pgm") {
        return load_pgm_file(input_path, 0, frame, error_message);
    }

    return load_encoded_image_file(input_path, 0, frame, error_message);
}

bool write_debug_image(const GrayFrame& frame, const fs::path& path, std::string& error_message) {
    if (frame.empty()) {
        error_message = "Cannot write an empty frame.";
        return false;
    }

    fs::create_directories(path.parent_path());

#ifdef FRIDGE_USE_OPENCV
    return write_encoded_image_file(frame, path, error_message);
#else
    // TODO: replace the fallback PGM writer with a small JPEG encoder if OpenCV is not used.
    return write_pgm_file(frame, path, error_message);
#endif
}

GrayFrame build_overlay_frame(
    const GrayFrame& after_frame,
    const BoundingBox& roi,
    const std::vector<ChangeRegion>& change_regions
) {
    if (after_frame.empty()) {
        return {};
    }

    GrayFrame overlay = after_frame;

    if (roi.width > 0 && roi.height > 0 &&
        !(roi.x == 0 && roi.y == 0 && roi.width == after_frame.width && roi.height == after_frame.height)) {
        draw_box_border(overlay, roi, 2);
    }

    for (const auto& region : change_regions) {
        draw_box_border(overlay, region.box, 2);
    }

    return overlay;
}

}  // namespace fridge
