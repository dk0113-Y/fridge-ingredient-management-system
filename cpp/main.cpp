#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
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

#include "debug_report.hpp"
#include "event_detector.hpp"
#include "frame_selector.hpp"
#include "runtime_config.hpp"
#include "video_io.hpp"
#include "yolo_runtime.hpp"

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

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path resolve_repo_root() {
    const std::filesystem::path source_dir = cpp_source_dir();
    if (source_dir.filename() == "cpp") {
        return source_dir.parent_path();
    }
    return std::filesystem::current_path();
}

struct AppOptions {
    std::filesystem::path input_path;
    std::optional<std::filesystem::path> config_path;
    std::optional<fridge::BoundingBox> roi_override;
    std::optional<std::string> roi_id_override;
};

struct SessionOutputLayout {
    std::filesystem::path session_dir;
    std::filesystem::path before_path;
    std::filesystem::path after_path;
    std::filesystem::path overlay_path;
    std::filesystem::path event_path;
    std::filesystem::path debug_path;
};

std::vector<std::filesystem::path> collect_cli_tokens(int argc, char** argv) {
    std::vector<std::filesystem::path> tokens;
#ifdef _WIN32
    int wide_argc = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv != nullptr) {
        tokens.reserve(static_cast<std::size_t>(std::max(0, wide_argc - 1)));
        for (int index = 1; index < wide_argc; ++index) {
            tokens.emplace_back(wide_argv[index]);
        }
        LocalFree(wide_argv);
        if (!tokens.empty()) {
            return tokens;
        }
    }
#endif

    tokens.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        tokens.emplace_back(argv[index]);
    }
    return tokens;
}

bool parse_roi_spec(const std::string& spec, fridge::BoundingBox& roi, std::string& error_message) {
    std::stringstream stream(spec);
    std::string token;
    std::vector<int> values;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            error_message = "ROI must use x,y,width,height.";
            return false;
        }
        values.push_back(std::stoi(token));
    }

    if (values.size() != 4) {
        error_message = "ROI must contain exactly 4 integers: x,y,width,height.";
        return false;
    }

    if (values[2] <= 0 || values[3] <= 0) {
        error_message = "ROI width and height must be positive.";
        return false;
    }

    roi = fridge::BoundingBox{values[0], values[1], values[2], values[3]};
    return true;
}

bool parse_arguments(const std::vector<std::filesystem::path>& tokens, AppOptions& options, std::string& error_message) {
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const std::string token = path_to_utf8_string(tokens[index]);
        if (token == "--config") {
            if (index + 1 >= tokens.size()) {
                error_message = "Missing value after --config.";
                return false;
            }
            options.config_path = tokens[++index];
            continue;
        }

        if (token == "--roi") {
            if (index + 1 >= tokens.size()) {
                error_message = "Missing value after --roi.";
                return false;
            }
            fridge::BoundingBox roi;
            if (!parse_roi_spec(path_to_utf8_string(tokens[++index]), roi, error_message)) {
                return false;
            }
            options.roi_override = roi;
            continue;
        }

        if (token == "--roi-id") {
            if (index + 1 >= tokens.size()) {
                error_message = "Missing value after --roi-id.";
                return false;
            }
            options.roi_id_override = path_to_utf8_string(tokens[++index]);
            continue;
        }

        if (!token.empty() && token[0] == '-') {
            error_message = "Unknown option: " + token;
            return false;
        }

        if (!options.input_path.empty()) {
            error_message = "Only one input path is supported per run.";
            return false;
        }

        options.input_path = tokens[index];
    }

    if (options.input_path.empty()) {
        error_message = "Missing input video path or frame directory.";
        return false;
    }

    return true;
}

std::filesystem::path resolve_config_path(const AppOptions& options, const std::filesystem::path& repo_root) {
    if (options.config_path.has_value()) {
        return *options.config_path;
    }

    const std::vector<std::filesystem::path> candidates = {
        repo_root / "configs" / "module_1_event_capture.cfg",
        repo_root / "cpp" / "configs" / "module_1_event_capture.cfg",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

std::filesystem::path resolve_yolo_runtime_config_path(const std::filesystem::path& repo_root) {
    const std::vector<std::filesystem::path> candidates = {
        repo_root / "cpp" / "configs" / "module_2_yolo.cfg",
        repo_root / "configs" / "module_2_yolo.cfg",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void apply_cli_overrides(fridge::VisionPipelineConfig& config, const AppOptions& options) {
    if (options.roi_override.has_value()) {
        config.motion_config.roi = *options.roi_override;
    }
    if (options.roi_id_override.has_value()) {
        config.roi_id = *options.roi_id_override;
    }
}

SessionOutputLayout build_session_output_layout(
    const std::filesystem::path& repo_root,
    const std::string& session_id,
    const std::string& image_extension
) {
    const std::filesystem::path session_dir = repo_root / "data" / "sessions" / session_id;

    return SessionOutputLayout{
        session_dir,
        session_dir / ("before" + image_extension),
        session_dir / ("after" + image_extension),
        session_dir / ("overlay" + image_extension),
        session_dir / "event.json",
        session_dir / "debug.json"
    };
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
        const std::filesystem::path repo_root = resolve_repo_root();
        const auto tokens = collect_cli_tokens(argc, argv);
        AppOptions options;
        std::string error_message;
        if (!parse_arguments(tokens, options, error_message)) {
            std::cerr << "Usage: fridge_vision_demo <video_path_or_frame_dir> [--config path] [--roi x,y,width,height] [--roi-id name]\n";
            std::cerr << "Note: real video-file decoding requires an OpenCV-enabled build.\n";
            if (!error_message.empty()) {
                std::cerr << "Argument error: " << error_message << "\n";
            }
            return 1;
        }

        fridge::VisionPipelineConfig pipeline_config;
        const std::filesystem::path config_path = resolve_config_path(options, repo_root);
        if (!config_path.empty()) {
            if (!std::filesystem::exists(config_path)) {
                std::cerr << "Config file does not exist: " << path_to_utf8_string(config_path) << "\n";
                return 1;
            }
            if (!fridge::load_pipeline_config(config_path, pipeline_config, error_message)) {
                std::cerr << "Failed to load config: " << error_message << "\n";
                return 1;
            }
        }
        apply_cli_overrides(pipeline_config, options);

        const std::filesystem::path yolo_runtime_config_path = resolve_yolo_runtime_config_path(repo_root);
        if (!yolo_runtime_config_path.empty()) {
            fridge::YoloRuntimeConfig yolo_runtime_config;
            if (!fridge::load_yolo_runtime_config(yolo_runtime_config_path, yolo_runtime_config, error_message)) {
                std::cerr << "Failed to load YOLO runtime config: " << error_message << "\n";
                return 1;
            }

            const fridge::YoloRuntime runtime(yolo_runtime_config);
            const auto runtime_info = runtime.inspect(repo_root);
            std::cout << "yolo_model: " << path_to_utf8_string(runtime_info.resolved_model_path) << "\n";
            std::cout << "yolo_format: " << fridge::to_string(runtime_info.model_format) << "\n";
            std::cout << "yolo_runtime_ready: "
                      << (runtime_info.can_run_in_current_cpp_runtime ? "yes" : "no") << "\n";
            std::cout << "yolo_status: " << runtime_info.message << "\n";
        }

        const std::filesystem::path input_path = options.input_path;
        const std::string session_id = sanitize_stem(input_path) + "_" + timestamp_for_filename();

        std::vector<fridge::GrayFrame> frames;
        if (!fridge::load_frames(input_path, frames, error_message)) {
            std::cerr << "load_frames failed: " << error_message << "\n";
            return 1;
        }

        const auto selected = fridge::select_keyframes(
            frames,
            pipeline_config.motion_config,
            pipeline_config.frame_selector_config
        );

        const std::string image_extension =
#ifdef FRIDGE_USE_OPENCV
            ".jpg";
#else
            ".pgm";
#endif
        const SessionOutputLayout output_layout =
            build_session_output_layout(repo_root, session_id, image_extension);

        if (!fridge::write_debug_image(selected.before_frame, output_layout.before_path, error_message)) {
            std::cerr << "Failed to write before frame: " << error_message << "\n";
            return 1;
        }

        if (!fridge::write_debug_image(selected.after_frame, output_layout.after_path, error_message)) {
            std::cerr << "Failed to write after frame: " << error_message << "\n";
            return 1;
        }

        fridge::EventDetector detector(pipeline_config.detector_config, pipeline_config.motion_config);
        auto event_result = detector.detect(
            selected,
            session_id,
            path_to_utf8_string(output_layout.before_path),
            path_to_utf8_string(output_layout.after_path)
        );
        event_result.roi_id = pipeline_config.roi_id;
        const auto overlay_frame = fridge::build_overlay_frame(
            selected.after_frame,
            pipeline_config.motion_config.roi,
            event_result.change_regions
        );

        if (!fridge::write_event_json(event_result, output_layout.event_path, error_message)) {
            std::cerr << "Failed to write event json: " << error_message << "\n";
            return 1;
        }

        if (!fridge::write_debug_image(overlay_frame, output_layout.overlay_path, error_message)) {
            std::cerr << "Failed to write overlay frame: " << error_message << "\n";
            return 1;
        }

        const fridge::DebugArtifacts artifacts{
            input_path,
            config_path,
            output_layout.before_path,
            output_layout.after_path,
            output_layout.overlay_path,
            output_layout.event_path
        };
        if (!fridge::write_debug_summary(
                selected,
                event_result,
                pipeline_config,
                artifacts,
                frames.size(),
                output_layout.debug_path,
                error_message
            )) {
            std::cerr << "Failed to write debug summary: " << error_message << "\n";
            return 1;
        }

        std::cout << "session_id: " << event_result.session_id << "\n";
        std::cout << "event_type: " << fridge::to_string(event_result.event_type) << "\n";
        std::cout << "event_json: " << path_to_utf8_string(output_layout.event_path) << "\n";
        std::cout << "debug_json: " << path_to_utf8_string(output_layout.debug_path) << "\n";
        if (!config_path.empty()) {
            std::cout << "config: " << path_to_utf8_string(config_path) << "\n";
        }

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
