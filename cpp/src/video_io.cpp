#include "video_io.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
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

GrayFrame build_diff_frame(const GrayFrame& before_frame, const GrayFrame& after_frame) {
    GrayFrame diff;
    if (before_frame.empty() || after_frame.empty() ||
        before_frame.width != after_frame.width ||
        before_frame.height != after_frame.height) {
        return diff;
    }

    diff.width = before_frame.width;
    diff.height = before_frame.height;
    diff.index = after_frame.index;
    diff.pixels.resize(before_frame.pixels.size());

    for (std::size_t index = 0; index < diff.pixels.size(); ++index) {
        const int delta = static_cast<int>(after_frame.pixels[index]) - static_cast<int>(before_frame.pixels[index]);
        diff.pixels[index] = static_cast<std::uint8_t>(std::clamp(std::abs(delta), 0, 255));
    }

    return diff;
}

}  // namespace fridge
