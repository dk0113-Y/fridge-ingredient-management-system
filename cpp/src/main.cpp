#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "event_detector.hpp"
#include "frame_selector.hpp"
#include "video_io.hpp"

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

std::string path_to_utf8_string(const std::filesystem::path& path) {
#ifdef _WIN32
    return wide_to_utf8(path.generic_wstring());
#else
    return path.generic_string();
#endif
}

std::vector<std::filesystem::path> collect_input_paths(int argc, char** argv) {
    std::vector<std::filesystem::path> input_paths;
#ifdef _WIN32
    int wide_argc = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv != nullptr) {
        input_paths.reserve(static_cast<std::size_t>(std::max(0, wide_argc - 1)));
        for (int index = 1; index < wide_argc; ++index) {
            input_paths.emplace_back(wide_argv[index]);
        }
        LocalFree(wide_argv);
        if (!input_paths.empty()) {
            return input_paths;
        }
    }
#endif

    input_paths.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        input_paths.emplace_back(argv[index]);
    }
    return input_paths;
}

std::string timestamp_for_filename() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif

    std::ostringstream output;
    output << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
    return output.str();
}

std::string sanitize_stem(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring stem = path.stem().wstring();
    std::string sanitized;
    sanitized.reserve(stem.size());
    for (const wchar_t character : stem) {
        const bool valid =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '_' ||
            character == '-';
        if (!valid) {
            sanitized.push_back('_');
        } else {
            sanitized.push_back(static_cast<char>(character));
        }
    }
    return sanitized.empty() ? "session" : sanitized;
#else
    std::string stem = path.stem().string();
    for (char& character : stem) {
        const bool valid =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '_' ||
            character == '-';
        if (!valid) {
            character = '_';
        }
    }
    return stem.empty() ? "session" : stem;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto input_paths = collect_input_paths(argc, argv);
        if (input_paths.empty()) {
            std::cerr << "Usage: fridge_vision_demo <video_path_or_frame_dir>\n";
            std::cerr << "Note: real video-file decoding requires an OpenCV-enabled build.\n";
            return 1;
        }

        const std::filesystem::path input_path = input_paths.front();
        const std::string session_id = sanitize_stem(input_path) + "_" + timestamp_for_filename();

        std::vector<fridge::GrayFrame> frames;
        std::string error_message;
        if (!fridge::load_frames(input_path, frames, error_message)) {
            std::cerr << "load_frames failed: " << error_message << "\n";
            return 1;
        }

        const auto selected = fridge::select_keyframes(frames);
        const auto diff_frame = fridge::build_diff_frame(selected.before_frame, selected.after_frame);

        const std::filesystem::path repo_root = std::filesystem::current_path();
        const std::string image_extension =
#ifdef FRIDGE_USE_OPENCV
            ".jpg";
#else
            ".pgm";
#endif
        const std::filesystem::path before_path = repo_root / "data" / "keyframes" / (session_id + "_before" + image_extension);
        const std::filesystem::path after_path = repo_root / "data" / "keyframes" / (session_id + "_after" + image_extension);
        const std::filesystem::path diff_path = repo_root / "data" / "outputs" / (session_id + "_diff" + image_extension);
        const std::filesystem::path event_path = repo_root / "data" / "outputs" / (session_id + "_event.json");

        if (!fridge::write_debug_image(selected.before_frame, before_path, error_message)) {
            std::cerr << "Failed to write before frame: " << error_message << "\n";
            return 1;
        }

        if (!fridge::write_debug_image(selected.after_frame, after_path, error_message)) {
            std::cerr << "Failed to write after frame: " << error_message << "\n";
            return 1;
        }

        if (!fridge::write_debug_image(diff_frame, diff_path, error_message)) {
            std::cerr << "Failed to write diff frame: " << error_message << "\n";
            return 1;
        }

        fridge::EventDetector detector;
        const auto event_result = detector.detect(
            selected,
            session_id,
            path_to_utf8_string(before_path),
            path_to_utf8_string(after_path)
        );

        if (!fridge::write_event_json(event_result, event_path, error_message)) {
            std::cerr << "Failed to write event json: " << error_message << "\n";
            return 1;
        }

        std::cout << "session_id: " << event_result.session_id << "\n";
        std::cout << "event_type: " << fridge::to_string(event_result.event_type) << "\n";
        std::cout << "event_json: " << path_to_utf8_string(event_path) << "\n";

        // TODO: replace local file input with a live camera stream.
        // TODO: adapt the pipeline and output staging for the embedded board runtime.
        return 0;
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "filesystem error: " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "runtime error: " << error.what() << "\n";
        return 1;
    }
}
