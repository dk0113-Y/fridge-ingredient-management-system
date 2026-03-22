#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "event_detector.hpp"
#include "frame_selector.hpp"
#include "video_io.hpp"

namespace {

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
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: fridge_vision_demo <video_path_or_frame_dir>\n";
        std::cerr << "Note: real video-file decoding requires an OpenCV-enabled build.\n";
        return 1;
    }

    const std::filesystem::path input_path(argv[1]);
    const std::string session_id = sanitize_stem(input_path) + "_" + timestamp_for_filename();

    std::vector<fridge::GrayFrame> frames;
    std::string error_message;
    if (!fridge::load_frames(input_path.string(), frames, error_message)) {
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

    if (!fridge::write_debug_image(selected.before_frame, before_path.string(), error_message)) {
        std::cerr << "Failed to write before frame: " << error_message << "\n";
        return 1;
    }

    if (!fridge::write_debug_image(selected.after_frame, after_path.string(), error_message)) {
        std::cerr << "Failed to write after frame: " << error_message << "\n";
        return 1;
    }

    if (!fridge::write_debug_image(diff_frame, diff_path.string(), error_message)) {
        std::cerr << "Failed to write diff frame: " << error_message << "\n";
        return 1;
    }

    fridge::EventDetector detector;
    const auto event_result = detector.detect(
        selected,
        session_id,
        before_path.generic_string(),
        after_path.generic_string()
    );

    if (!fridge::write_event_json(event_result, event_path.string(), error_message)) {
        std::cerr << "Failed to write event json: " << error_message << "\n";
        return 1;
    }

    std::cout << "session_id: " << event_result.session_id << "\n";
    std::cout << "event_type: " << fridge::to_string(event_result.event_type) << "\n";
    std::cout << "event_json: " << event_path.generic_string() << "\n";

    // TODO: replace local file input with a live camera stream.
    // TODO: adapt the pipeline and output staging for the embedded board runtime.
    return 0;
}
