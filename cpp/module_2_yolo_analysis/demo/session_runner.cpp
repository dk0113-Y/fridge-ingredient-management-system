#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "event_detector.hpp"
#include "inventory_engine.hpp"
#include "local_service.hpp"
#include "software_closure.hpp"
#include "service_config.hpp"
#include "video_io.hpp"
#include "yolo_diff_analyzer.hpp"
#include "yolo_runtime.hpp"

namespace {

using fridge::BoundingBox;
using fridge::ChangeRegion;
using fridge::ColorFrame;
using fridge::CropRequest;
using fridge::DetectionMatch;
using fridge::EventResult;
using fridge::EventType;
using fridge::GrayFrame;
using fridge::YoloAnalysisConfig;
using fridge::YoloDetection;
using fridge::YoloDiffResult;
using fridge::YoloModule2Pipeline;
using fridge::YoloOnnxOutput;
using fridge::YoloRuntime;
using fridge::YoloRuntimeConfig;
using fridge::YoloRuntimeInfo;

namespace fs = std::filesystem;

enum class Module2Mode {
    Mock,
    RealOnnxRuntime,
};

struct AppOptions {
    fs::path session_dir;
    fs::path latest_root;
    fs::path watch_root;
    fs::path config_path;
    Module2Mode module2_mode = Module2Mode::Mock;
    std::string mock_coarse_class = "packaged_food";
    bool write_crops = true;
    int poll_interval_ms = 2000;
};

struct Stage1SessionSnapshot {
    EventResult event;
    fs::path before_frame_hint;
    fs::path after_frame_hint;
};

struct CropArtifactRecord {
    CropRequest request;
    std::string output_path;
};

struct Module2Execution {
    bool success = false;
    std::string failure_reason;
    std::vector<YoloDetection> before_detections;
    std::vector<YoloDetection> after_detections;
    YoloDiffResult result;
    std::vector<CropArtifactRecord> crop_artifacts;
};

struct SessionOutputPaths {
    fs::path stage2_dir;
    fs::path detections_before_path;
    fs::path detections_after_path;
    fs::path result_path;
    fs::path crops_dir;
    fs::path final_dir;
    fs::path final_event_path;
    fs::path inventory_response_path;
    fs::path events_response_path;
    fs::path pending_response_path;
    fs::path software_closure_report_path;
};

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

std::string path_to_utf8_string(const fs::path& path) {
#ifdef _WIN32
    return wide_to_utf8(path.generic_wstring());
#else
    return path.generic_string();
#endif
}

std::string to_lower_copy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    return value;
}

std::string sanitize_token(std::string token) {
    for (char& character : token) {
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
    return token.empty() ? "session" : token;
}

std::string escape_json(const std::string& value) {
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            escaped << character;
            break;
        }
    }
    return escaped.str();
}

std::string bool_to_json(bool value) {
    return value ? "true" : "false";
}

std::string to_string(Module2Mode mode) {
    switch (mode) {
    case Module2Mode::Mock:
        return "mock";
    case Module2Mode::RealOnnxRuntime:
        return "real_onnx_runtime";
    }
    return "mock";
}

bool parse_module2_mode(const std::string& text, Module2Mode& mode) {
    const std::string normalized = to_lower_copy(text);
    if (normalized == "mock") {
        mode = Module2Mode::Mock;
        return true;
    }
    if (normalized == "real_onnx_runtime" || normalized == "real-onnx-runtime") {
        mode = Module2Mode::RealOnnxRuntime;
        return true;
    }
    return false;
}

bool parse_event_type(const std::string& text, EventType& event_type) {
    const std::string normalized = to_lower_copy(text);
    if (normalized == "capture_recorded") {
        event_type = EventType::CaptureRecorded;
        return true;
    }
    if (normalized == "not_evaluated") {
        event_type = EventType::NotEvaluated;
        return true;
    }
    if (normalized == "no_change") {
        event_type = EventType::NoChange;
        return true;
    }
    if (normalized == "reorganize") {
        event_type = EventType::Reorganize;
        return true;
    }
    if (normalized == "put_in") {
        event_type = EventType::PutIn;
        return true;
    }
    if (normalized == "take_out") {
        event_type = EventType::TakeOut;
        return true;
    }
    if (normalized == "partial_take_out_candidate") {
        event_type = EventType::PartialTakeOutCandidate;
        return true;
    }
    if (normalized == "uncertain") {
        event_type = EventType::Uncertain;
        return true;
    }
    return false;
}

fs::path resolve_repo_root() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    const fs::path source_dir = fs::path(FRIDGE_CPP_SOURCE_DIR);
    if (source_dir.filename() == "cpp") {
        return source_dir.parent_path();
    }
#endif
    return fs::current_path();
}

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  fridge_module2_session_runner --session-dir <path>\n"
        << "  fridge_module2_session_runner --latest-under <path>\n"
        << "  fridge_module2_session_runner --watch-root <path>\n\n"
        << "Options:\n"
        << "  --config <path>                 module 2 config file, default is cpp/configs/module_2_yolo.cfg\n"
        << "  --module2-mode <mock|real_onnx_runtime>\n"
        << "  --mock-coarse-class <label>     default is packaged_food\n"
        << "  --poll-interval-ms <ms>         watch mode polling interval, default is 2000\n"
        << "  --no-crops                      skip writing stage2/crops/\n";
}

bool parse_arguments(int argc, char** argv, AppOptions& options, std::string& error_message) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto require_value = [&](const std::string& option_name) -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument("Missing value for " + option_name);
            }
            ++index;
            return argv[index];
        };

        try {
            if (arg == "--session-dir") {
                options.session_dir = require_value("--session-dir");
            } else if (arg == "--latest-under") {
                options.latest_root = require_value("--latest-under");
            } else if (arg == "--watch-root") {
                options.watch_root = require_value("--watch-root");
            } else if (arg == "--config") {
                options.config_path = require_value("--config");
            } else if (arg == "--module2-mode") {
                if (!parse_module2_mode(require_value("--module2-mode"), options.module2_mode)) {
                    error_message = "Unsupported module2 mode";
                    return false;
                }
            } else if (arg == "--mock-coarse-class") {
                options.mock_coarse_class = require_value("--mock-coarse-class");
            } else if (arg == "--poll-interval-ms") {
                options.poll_interval_ms = std::stoi(require_value("--poll-interval-ms"));
            } else if (arg == "--no-crops") {
                options.write_crops = false;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                std::exit(0);
            } else {
                error_message = "Unknown argument: " + arg;
                return false;
            }
        } catch (const std::exception& ex) {
            error_message = ex.what();
            return false;
        }
    }

    const int selected_modes =
        (!options.session_dir.empty() ? 1 : 0) +
        (!options.latest_root.empty() ? 1 : 0) +
        (!options.watch_root.empty() ? 1 : 0);
    if (selected_modes == 0) {
        error_message = "One of --session-dir, --latest-under, or --watch-root is required";
        return false;
    }
    if (selected_modes > 1) {
        error_message = "Use only one of --session-dir, --latest-under, or --watch-root";
        return false;
    }
    if (options.poll_interval_ms <= 0) {
        error_message = "--poll-interval-ms must be positive";
        return false;
    }
    return true;
}

fs::path resolve_config_path(const AppOptions& options, const fs::path& repo_root) {
    if (!options.config_path.empty()) {
        return options.config_path;
    }

    const std::vector<fs::path> candidates = {
        repo_root / "cpp" / "configs" / "module_2_yolo.cfg",
        repo_root / "configs" / "module_2_yolo.cfg",
        fs::current_path() / "cpp" / "configs" / "module_2_yolo.cfg",
        fs::current_path() / "configs" / "module_2_yolo.cfg",
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return repo_root / "cpp" / "configs" / "module_2_yolo.cfg";
}

fs::path resolve_inventory_config_path(const fs::path& repo_root) {
    const std::vector<fs::path> candidates = {
        repo_root / "cpp" / "configs" / "module_4_inventory.cfg",
        repo_root / "configs" / "module_4_inventory.cfg",
        fs::current_path() / "cpp" / "configs" / "module_4_inventory.cfg",
        fs::current_path() / "configs" / "module_4_inventory.cfg",
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return repo_root / "cpp" / "configs" / "module_4_inventory.cfg";
}

fs::path resolve_service_config_path(const fs::path& repo_root) {
    const std::vector<fs::path> candidates = {
        repo_root / "cpp" / "configs" / "module_5_local_service.cfg",
        repo_root / "configs" / "module_5_local_service.cfg",
        fs::current_path() / "cpp" / "configs" / "module_5_local_service.cfg",
        fs::current_path() / "configs" / "module_5_local_service.cfg",
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return repo_root / "cpp" / "configs" / "module_5_local_service.cfg";
}

fs::path find_latest_session_dir(const fs::path& root, std::string& error_message) {
    if (!fs::exists(root)) {
        error_message = "Latest-session root does not exist: " + path_to_utf8_string(root);
        return {};
    }
    if (!fs::is_directory(root)) {
        error_message = "Latest-session root is not a directory: " + path_to_utf8_string(root);
        return {};
    }

    fs::path best_path;
    fs::file_time_type best_time{};
    bool found = false;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto current_time = entry.last_write_time();
        if (!found || current_time > best_time) {
            best_path = entry.path();
            best_time = current_time;
            found = true;
        }
    }

    if (!found) {
        error_message = "No session directories were found under: " + path_to_utf8_string(root);
        return {};
    }
    return best_path;
}

fs::path resolve_stage1_event_path(const fs::path& session_dir) {
    const std::vector<fs::path> candidates = {
        session_dir / "stage1" / "stage1_event.json",
        session_dir / "stage1_event.json",
        session_dir / "event.json",
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return session_dir / "stage1" / "stage1_event.json";
}

fs::path resolve_session_frame_path(
    const fs::path& session_dir,
    const fs::path& stage1_event_path,
    const fs::path& json_hint,
    const std::string& stem
) {
    std::vector<fs::path> candidates;
    if (!json_hint.empty()) {
        if (json_hint.is_absolute() && fs::exists(json_hint)) {
            candidates.push_back(json_hint);
        }
        if (!json_hint.filename().empty()) {
            candidates.push_back(stage1_event_path.parent_path() / json_hint.filename());
            candidates.push_back(session_dir / "stage1" / json_hint.filename());
            candidates.push_back(session_dir / json_hint.filename());
        }
    }

    const std::vector<std::string> extensions = {".jpg", ".jpeg", ".png", ".bmp", ".pgm"};
    for (const auto& extension : extensions) {
        candidates.push_back(stage1_event_path.parent_path() / (stem + extension));
        candidates.push_back(session_dir / "stage1" / (stem + extension));
        candidates.push_back(session_dir / (stem + extension));
    }

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

EventType mock_event_type_for_session(const std::string& session_token) {
    const std::string normalized = to_lower_copy(session_token);
    if (normalized.find("tc02") != std::string::npos || normalized.find("put_in") != std::string::npos) {
        return EventType::PutIn;
    }
    if (normalized.find("tc03") != std::string::npos || normalized.find("take_out") != std::string::npos) {
        return EventType::TakeOut;
    }
    if (normalized.find("tc04") != std::string::npos || normalized.find("partial") != std::string::npos) {
        return EventType::PartialTakeOutCandidate;
    }
    if (normalized.find("reorganize") != std::string::npos || normalized.find("reorg") != std::string::npos) {
        return EventType::Reorganize;
    }
    if (normalized.find("tc01") != std::string::npos || normalized.find("no_change") != std::string::npos) {
        return EventType::NoChange;
    }
    return EventType::NoChange;
}

bool load_stage1_session_snapshot(
    const fs::path& stage1_event_path,
    Stage1SessionSnapshot& snapshot,
    std::string& error_message
) {
    std::ifstream input(stage1_event_path);
    if (!input) {
        error_message = "Failed to open stage1 event json: " + path_to_utf8_string(stage1_event_path);
        return false;
    }

    nlohmann::json json_value;
    try {
        input >> json_value;
    } catch (const std::exception& ex) {
        error_message = "Failed to parse stage1 event json: " + std::string(ex.what());
        return false;
    }

    snapshot.event.session_id = json_value.value(
        "session_id",
        stage1_event_path.parent_path().filename() == "stage1"
            ? stage1_event_path.parent_path().parent_path().filename().string()
            : stage1_event_path.parent_path().filename().string()
    );
    snapshot.event.timestamp = json_value.value("timestamp", std::string());
    snapshot.event.roi_id = json_value.value("roi_id", std::string("main_compartment"));
    snapshot.event.confidence = json_value.value("confidence", 0.0);
    snapshot.event.need_user_confirm = json_value.value("need_user_confirm", false);

    const std::string event_type_text = json_value.value("event_type", std::string("no_change"));
    if (!parse_event_type(event_type_text, snapshot.event.event_type)) {
        error_message = "Unsupported stage1 event_type: " + event_type_text;
        return false;
    }

    snapshot.before_frame_hint = json_value.value("before_frame", std::string());
    snapshot.after_frame_hint = json_value.value("after_frame", std::string());
    snapshot.event.before_frame = json_value.value("before_frame", std::string());
    snapshot.event.after_frame = json_value.value("after_frame", std::string());

    if (const auto change_regions_it = json_value.find("change_regions");
        change_regions_it != json_value.end() && change_regions_it->is_array()) {
        for (const auto& region_json : *change_regions_it) {
            ChangeRegion region;
            region.box.x = region_json.value("x", 0);
            region.box.y = region_json.value("y", 0);
            region.box.width = region_json.value("width", 0);
            region.box.height = region_json.value("height", 0);
            region.score = region_json.value("score", 0.0);
            if (region.box.width > 0 && region.box.height > 0) {
                snapshot.event.change_regions.push_back(region);
            }
        }
    }

    if (const auto objects_it = json_value.find("objects");
        objects_it != json_value.end() && objects_it->is_array()) {
        for (const auto& object_json : *objects_it) {
            fridge::DetectedObject object;
            object.category = object_json.value("category", std::string("unknown"));
            object.name = object_json.value("name", std::string("unknown"));
            object.count_delta = object_json.value("count_delta", 0);
            object.remain_level = object_json.value("remain_level", 0.0);
            snapshot.event.objects.push_back(object);
        }
    }

    if (snapshot.event.timestamp.empty()) {
        snapshot.event.timestamp = "unknown";
    }
    if (snapshot.event.objects.empty()) {
        snapshot.event.objects.push_back(fridge::DetectedObject{});
    }
    if (snapshot.event.change_regions.empty()) {
        if (const auto change_box_it = json_value.find("change_box");
            change_box_it != json_value.end() && change_box_it->is_object()) {
            ChangeRegion region;
            region.box.x = change_box_it->value("x", 0);
            region.box.y = change_box_it->value("y", 0);
            region.box.width = change_box_it->value("width", 0);
            region.box.height = change_box_it->value("height", 0);
            region.score = change_box_it->value("score", snapshot.event.confidence);
            if (region.box.width > 0 && region.box.height > 0) {
                snapshot.event.change_regions.push_back(region);
            }
        }
    }

    return true;
}

BoundingBox clamp_box_to_frame(const BoundingBox& box, const GrayFrame& frame) {
    if (frame.empty() || box.width <= 0 || box.height <= 0) {
        return {};
    }

    const int x0 = std::clamp(box.x, 0, frame.width);
    const int y0 = std::clamp(box.y, 0, frame.height);
    const int x1 = std::clamp(box.x + box.width, 0, frame.width);
    const int y1 = std::clamp(box.y + box.height, 0, frame.height);
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return BoundingBox{x0, y0, x1 - x0, y1 - y0};
}

GrayFrame crop_frame(const GrayFrame& source, const BoundingBox& requested_box) {
    GrayFrame cropped;
    const BoundingBox box = clamp_box_to_frame(requested_box, source);
    if (source.empty() || box.width <= 0 || box.height <= 0) {
        return cropped;
    }

    cropped.width = box.width;
    cropped.height = box.height;
    cropped.index = source.index;
    cropped.pixels.resize(static_cast<std::size_t>(box.width * box.height));
    for (int y = 0; y < box.height; ++y) {
        for (int x = 0; x < box.width; ++x) {
            cropped.pixels[static_cast<std::size_t>(y * box.width + x)] =
                source.at(box.x + x, box.y + y);
        }
    }
    return cropped;
}

BoundingBox choose_primary_box(
    const Stage1SessionSnapshot& snapshot,
    const GrayFrame& before_frame,
    const GrayFrame& after_frame
) {
    const GrayFrame& reference = after_frame.empty() ? before_frame : after_frame;
    if (!snapshot.event.change_regions.empty()) {
        return clamp_box_to_frame(snapshot.event.change_regions.front().box, reference);
    }

    const BoundingBox full_frame{
        0,
        0,
        std::max(before_frame.width, after_frame.width),
        std::max(before_frame.height, after_frame.height)
    };
    return clamp_box_to_frame(full_frame, reference);
}

BoundingBox shrink_box(const BoundingBox& box, double factor) {
    if (box.width <= 0 || box.height <= 0) {
        return {};
    }

    const double clamped_factor = std::clamp(factor, 0.1, 1.0);
    const int new_width = std::max(1, static_cast<int>(std::round(static_cast<double>(box.width) * clamped_factor)));
    const int new_height = std::max(1, static_cast<int>(std::round(static_cast<double>(box.height) * clamped_factor)));
    const int center_x = box.x + box.width / 2;
    const int center_y = box.y + box.height / 2;
    return BoundingBox{
        center_x - new_width / 2,
        center_y - new_height / 2,
        new_width,
        new_height
    };
}

int resolve_class_index(const std::vector<std::string>& class_names, const std::string& coarse_class) {
    const std::string wanted = to_lower_copy(coarse_class);
    for (std::size_t index = 0; index < class_names.size(); ++index) {
        if (to_lower_copy(class_names[index]) == wanted) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

std::array<float, 6> make_onnx_row(
    const BoundingBox& frame_box,
    const GrayFrame& frame,
    const YoloRuntimeConfig& config,
    float score,
    int class_index
) {
    const BoundingBox clamped = clamp_box_to_frame(frame_box, frame);
    if (clamped.width <= 0 || clamped.height <= 0 || frame.width <= 0 || frame.height <= 0) {
        return {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, static_cast<float>(class_index)};
    }

    const float scale_x = static_cast<float>(config.input_width) / static_cast<float>(frame.width);
    const float scale_y = static_cast<float>(config.input_height) / static_cast<float>(frame.height);
    return {
        static_cast<float>(clamped.x) * scale_x,
        static_cast<float>(clamped.y) * scale_y,
        static_cast<float>(clamped.x + clamped.width) * scale_x,
        static_cast<float>(clamped.y + clamped.height) * scale_y,
        score,
        static_cast<float>(class_index)
    };
}

template <typename T>
std::string json_array_from_objects(
    const std::vector<T>& values,
    const std::function<std::string(const T&)>& render
) {
    std::ostringstream output;
    output << "[";
    if (!values.empty()) {
        output << "\n";
        for (std::size_t index = 0; index < values.size(); ++index) {
            output << render(values[index]);
            if (index + 1 < values.size()) {
                output << ",";
            }
            output << "\n";
        }
    }
    output << "]";
    return output.str();
}

std::string detection_to_json(const YoloDetection& detection) {
    std::ostringstream output;
    output << "    {\n"
           << "      \"coarse_class\": \"" << escape_json(detection.coarse_class) << "\",\n"
           << "      \"confidence\": " << std::fixed << std::setprecision(3) << detection.confidence << ",\n"
           << "      \"bbox\": {\n"
           << "        \"x\": " << detection.bbox.x << ",\n"
           << "        \"y\": " << detection.bbox.y << ",\n"
           << "        \"width\": " << detection.bbox.width << ",\n"
           << "        \"height\": " << detection.bbox.height << "\n"
           << "      }\n"
           << "    }";
    return output.str();
}

std::string crop_request_to_json(const CropRequest& request) {
    std::ostringstream output;
    output << "    {\n"
           << "      \"source_frame\": \"" << escape_json(request.source_frame) << "\",\n"
           << "      \"coarse_class\": \"" << escape_json(request.coarse_class) << "\",\n"
           << "      \"bbox\": {\n"
           << "        \"x\": " << request.bbox.x << ",\n"
           << "        \"y\": " << request.bbox.y << ",\n"
           << "        \"width\": " << request.bbox.width << ",\n"
           << "        \"height\": " << request.bbox.height << "\n"
           << "      }\n"
           << "    }";
    return output.str();
}

std::string crop_artifact_to_json(const CropArtifactRecord& artifact) {
    std::ostringstream output;
    output << "    {\n"
           << "      \"source_frame\": \"" << escape_json(artifact.request.source_frame) << "\",\n"
           << "      \"coarse_class\": \"" << escape_json(artifact.request.coarse_class) << "\",\n"
           << "      \"path\": \"" << escape_json(artifact.output_path) << "\",\n"
           << "      \"bbox\": {\n"
           << "        \"x\": " << artifact.request.bbox.x << ",\n"
           << "        \"y\": " << artifact.request.bbox.y << ",\n"
           << "        \"width\": " << artifact.request.bbox.width << ",\n"
           << "        \"height\": " << artifact.request.bbox.height << "\n"
           << "      }\n"
           << "    }";
    return output.str();
}

std::string match_to_json(const DetectionMatch& match) {
    std::ostringstream output;
    output << "    {\n"
           << "      \"before_index\": " << match.before_index << ",\n"
           << "      \"after_index\": " << match.after_index << ",\n"
           << "      \"coarse_class\": \"" << escape_json(match.coarse_class) << "\",\n"
            << "      \"iou\": " << std::fixed << std::setprecision(4) << match.iou << ",\n"
            << "      \"normalized_center_distance\": " << std::fixed << std::setprecision(4)
           << match.normalized_center_distance << ",\n"
           << "      \"area_change_ratio\": " << std::fixed << std::setprecision(4)
           << match.area_change_ratio << ",\n"
           << "      \"match_score\": " << std::fixed << std::setprecision(4)
           << match.match_score << "\n"
           << "    }";
    return output.str();
}

std::string count_map_to_json(const fridge::DetectionCountMap& counts) {
    std::ostringstream output;
    output << "{";
    if (!counts.empty()) {
        output << "\n";
        std::size_t index = 0;
        for (const auto& entry : counts) {
            output << "    \"" << escape_json(entry.first) << "\": " << entry.second;
            if (index + 1 < counts.size()) {
                output << ",";
            }
            output << "\n";
            ++index;
        }
        output << "  ";
    }
    output << "}";
    return output.str();
}

std::string build_detection_list_json(
    const std::string& session_id,
    const std::string& frame_label,
    Module2Mode mode,
    const std::vector<YoloDetection>& detections,
    const std::string& error_message
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"frame\": \"" << escape_json(frame_label) << "\",\n"
           << "  \"module2_mode\": \"" << to_string(mode) << "\",\n"
           << "  \"error\": \"" << escape_json(error_message) << "\",\n"
           << "  \"detections\": " << json_array_from_objects<YoloDetection>(detections, detection_to_json) << "\n"
           << "}\n";
    return output.str();
}

std::string build_module2_result_json(
    const std::string& session_id,
    Module2Mode mode,
    const Module2Execution& execution,
    EventType fallback_event_type
) {
    const EventType event_type = execution.success ? execution.result.event.event_type : fallback_event_type;
    const std::string review_reason = execution.success ? execution.result.review_reason : execution.failure_reason;

    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"event_type\": \"" << escape_json(fridge::to_string(event_type)) << "\",\n"
           << "  \"success\": " << bool_to_json(execution.success) << ",\n"
           << "  \"stage2_skipped\": false,\n"
           << "  \"module2_mode\": \"" << to_string(mode) << "\",\n"
           << "  \"before_counts\": " << count_map_to_json(execution.result.before_counts) << ",\n"
           << "  \"after_counts\": " << count_map_to_json(execution.result.after_counts) << ",\n"
           << "  \"count_decision\": \"" << escape_json(execution.result.count_decision) << "\",\n"
           << "  \"matched_pairs\": "
           << json_array_from_objects<DetectionMatch>(execution.result.matched_pairs, match_to_json) << ",\n"
           << "  \"new_boxes\": "
           << json_array_from_objects<YoloDetection>(execution.result.new_boxes, detection_to_json) << ",\n"
           << "  \"disappeared_boxes\": "
           << json_array_from_objects<YoloDetection>(execution.result.disappeared_boxes, detection_to_json) << ",\n"
           << "  \"partial_candidates\": "
           << json_array_from_objects<DetectionMatch>(execution.result.partial_candidates, match_to_json) << ",\n"
           << "  \"reorganize_candidates\": "
           << json_array_from_objects<DetectionMatch>(execution.result.reorganize_candidates, match_to_json) << ",\n"
           << "  \"crop_requests\": "
           << json_array_from_objects<CropRequest>(execution.result.crop_requests, crop_request_to_json) << ",\n"
           << "  \"crop_artifacts\": "
           << json_array_from_objects<CropArtifactRecord>(execution.crop_artifacts, crop_artifact_to_json) << ",\n"
           << "  \"review_reason\": \"" << escape_json(review_reason) << "\"\n"
           << "}\n";
    return output.str();
}

bool write_text_file(const fs::path& output_path, const std::string& text, std::string& error_message) {
    fs::create_directories(output_path.parent_path());

    std::ofstream output(output_path);
    if (!output) {
        error_message = "Failed to open output file: " + path_to_utf8_string(output_path);
        return false;
    }

    output << text;
    if (!output) {
        error_message = "Failed to write output file: " + path_to_utf8_string(output_path);
        return false;
    }
    return true;
}

bool write_crop_artifacts(
    const fs::path& crops_dir,
    Module2Execution& execution,
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    std::string& error_message
) {
    execution.crop_artifacts.clear();

    std::error_code filesystem_error;
    fs::remove_all(crops_dir, filesystem_error);
    if (filesystem_error) {
        error_message = "Failed to reset crops directory: " + path_to_utf8_string(crops_dir) +
                        " (" + filesystem_error.message() + ")";
        return false;
    }
    fs::create_directories(crops_dir, filesystem_error);
    if (filesystem_error) {
        error_message = "Failed to create crops directory: " + path_to_utf8_string(crops_dir) +
                        " (" + filesystem_error.message() + ")";
        return false;
    }

    if (!execution.success) {
        return true;
    }

    const std::string image_extension =
#ifdef FRIDGE_USE_OPENCV
        ".jpg";
#else
        ".pgm";
#endif
    for (std::size_t index = 0; index < execution.result.crop_requests.size(); ++index) {
        const auto& request = execution.result.crop_requests[index];
        const GrayFrame& source_frame = request.source_frame == "before" ? before_frame : after_frame;
        const GrayFrame cropped = crop_frame(source_frame, request.bbox);
        std::ostringstream name;
        name << std::setw(2) << std::setfill('0') << index;
        const fs::path output_path =
            crops_dir /
            (request.source_frame + "_" + name.str() + "_" + sanitize_token(request.coarse_class) + image_extension);
        if (!fridge::write_debug_image(cropped, output_path, error_message)) {
            return false;
        }
        execution.crop_artifacts.push_back(CropArtifactRecord{request, path_to_utf8_string(output_path)});
    }
    return true;
}

Module2Execution run_module2(
    const Stage1SessionSnapshot& snapshot,
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const ColorFrame& before_color,
    const ColorFrame& after_color,
    const fs::path& repo_root,
    const fs::path& before_frame_path,
    const fs::path& after_frame_path,
    const YoloRuntimeConfig& runtime_config,
    const YoloAnalysisConfig& analysis_config,
    Module2Mode mode,
    const std::string& mock_coarse_class
) {
    Module2Execution execution;
    const YoloModule2Pipeline pipeline(runtime_config, analysis_config);

    if (mode == Module2Mode::RealOnnxRuntime) {
        const YoloRuntime runtime(runtime_config);
        const YoloOnnxOutput before_output = runtime.run(before_color, repo_root, execution.failure_reason);
        if (!execution.failure_reason.empty()) {
            return execution;
        }
        const YoloOnnxOutput after_output = runtime.run(after_color, repo_root, execution.failure_reason);
        if (!execution.failure_reason.empty()) {
            return execution;
        }

        std::string runtime_error;
        execution.before_detections = pipeline.decode_output(before_output, before_frame, runtime_error);
        if (!runtime_error.empty()) {
            execution.failure_reason = runtime_error;
            return execution;
        }

        execution.after_detections = pipeline.decode_output(after_output, after_frame, runtime_error);
        if (!runtime_error.empty()) {
            execution.failure_reason = runtime_error;
            return execution;
        }

        execution.result = pipeline.analyze_outputs(
            before_frame,
            after_frame,
            before_output,
            after_output,
            snapshot.event.session_id,
            path_to_utf8_string(before_frame_path),
            path_to_utf8_string(after_frame_path),
            runtime_error
        );
        if (!runtime_error.empty()) {
            execution.failure_reason = runtime_error;
            return execution;
        }

        execution.success = true;
        return execution;
    }

    const BoundingBox primary_box = choose_primary_box(snapshot, before_frame, after_frame);
    const EventType mock_event_type = mock_event_type_for_session(snapshot.event.session_id);
    const std::string coarse_class =
        mock_event_type == EventType::PartialTakeOutCandidate ? "fruit_vegetable" : mock_coarse_class;
    const int class_index = resolve_class_index(runtime_config.class_names, coarse_class);

    std::vector<float> before_values;
    std::vector<float> after_values;
    auto push_row = [](std::vector<float>& values, const std::array<float, 6>& row) {
        values.insert(values.end(), row.begin(), row.end());
    };

    switch (mock_event_type) {
    case EventType::PutIn:
        push_row(after_values, make_onnx_row(primary_box, after_frame, runtime_config, 0.92F, class_index));
        break;
    case EventType::TakeOut:
        push_row(before_values, make_onnx_row(primary_box, before_frame, runtime_config, 0.92F, class_index));
        break;
    case EventType::PartialTakeOutCandidate: {
        const BoundingBox before_box = shrink_box(primary_box, 1.0);
        const BoundingBox after_box = shrink_box(primary_box, 0.65);
        push_row(before_values, make_onnx_row(before_box, before_frame, runtime_config, 0.91F, class_index));
        push_row(after_values, make_onnx_row(after_box, after_frame, runtime_config, 0.90F, class_index));
        break;
    }
    case EventType::Reorganize: {
        const BoundingBox before_box = shrink_box(primary_box, 0.90);
        const BoundingBox after_box{
            before_box.x + std::max(6, before_box.width / 4),
            before_box.y,
            before_box.width,
            before_box.height
        };
        push_row(before_values, make_onnx_row(before_box, before_frame, runtime_config, 0.91F, class_index));
        push_row(after_values, make_onnx_row(after_box, after_frame, runtime_config, 0.90F, class_index));
        break;
    }
    case EventType::Uncertain: {
        push_row(before_values, make_onnx_row(primary_box, before_frame, runtime_config, 0.80F, class_index));
        const BoundingBox shifted_box{
            primary_box.x + primary_box.width / 3,
            primary_box.y,
            primary_box.width,
            primary_box.height
        };
        push_row(after_values, make_onnx_row(shifted_box, after_frame, runtime_config, 0.79F, class_index));
        push_row(after_values, make_onnx_row(shrink_box(primary_box, 0.55), after_frame, runtime_config, 0.77F, class_index));
        break;
    }
    case EventType::NoChange:
    case EventType::CaptureRecorded:
    case EventType::NotEvaluated:
        break;
    }

    const YoloOnnxOutput before_output{
        static_cast<int>(before_values.size() / static_cast<std::size_t>(runtime_config.output_columns)),
        runtime_config.output_columns,
        before_values
    };
    const YoloOnnxOutput after_output{
        static_cast<int>(after_values.size() / static_cast<std::size_t>(runtime_config.output_columns)),
        runtime_config.output_columns,
        after_values
    };

    std::string runtime_error;
    execution.before_detections = pipeline.decode_output(before_output, before_frame, runtime_error);
    if (!runtime_error.empty()) {
        execution.failure_reason = runtime_error;
        return execution;
    }

    execution.after_detections = pipeline.decode_output(after_output, after_frame, runtime_error);
    if (!runtime_error.empty()) {
        execution.failure_reason = runtime_error;
        return execution;
    }

    execution.result = pipeline.analyze_outputs(
        before_frame,
        after_frame,
        before_output,
        after_output,
        snapshot.event.session_id,
        path_to_utf8_string(before_frame_path),
        path_to_utf8_string(after_frame_path),
        runtime_error
    );
    if (!runtime_error.empty()) {
        execution.failure_reason = runtime_error;
        return execution;
    }

    execution.success = true;
    return execution;
}

SessionOutputPaths make_output_paths(const fs::path& session_dir) {
    SessionOutputPaths paths;
    paths.stage2_dir = session_dir / "stage2";
    paths.detections_before_path = paths.stage2_dir / "detections_before.json";
    paths.detections_after_path = paths.stage2_dir / "detections_after.json";
    paths.result_path = paths.stage2_dir / "module2_result.json";
    paths.crops_dir = paths.stage2_dir / "crops";
    paths.final_dir = session_dir / "final";
    paths.final_event_path = paths.final_dir / "event.json";
    paths.inventory_response_path = paths.final_dir / "inventory_response.json";
    paths.events_response_path = paths.final_dir / "events_response.json";
    paths.pending_response_path = paths.final_dir / "pending_response.json";
    paths.software_closure_report_path = paths.final_dir / "software_closure_report.json";
    return paths;
}

bool outputs_are_current(
    const SessionOutputPaths& output_paths,
    const fs::path& stage1_event_path,
    const fs::path& before_frame_path,
    const fs::path& after_frame_path
) {
    const std::vector<fs::path> required_outputs = {
        output_paths.detections_before_path,
        output_paths.detections_after_path,
        output_paths.result_path,
        output_paths.final_event_path,
        output_paths.inventory_response_path,
        output_paths.events_response_path,
        output_paths.pending_response_path,
        output_paths.software_closure_report_path,
    };
    for (const auto& output_path : required_outputs) {
        if (!fs::exists(output_path)) {
            return false;
        }
    }

    fs::file_time_type newest_source = fs::last_write_time(stage1_event_path);
    newest_source = std::max(newest_source, fs::last_write_time(before_frame_path));
    newest_source = std::max(newest_source, fs::last_write_time(after_frame_path));

    fs::file_time_type oldest_output = fs::last_write_time(required_outputs.front());
    for (const auto& output_path : required_outputs) {
        oldest_output = std::min(oldest_output, fs::last_write_time(output_path));
    }

    return oldest_output >= newest_source;
}

std::vector<fs::path> list_session_dirs(const fs::path& root, std::string& error_message) {
    std::vector<fs::path> sessions;
    if (!fs::exists(root)) {
        error_message = "Watch root does not exist: " + path_to_utf8_string(root);
        return sessions;
    }
    if (!fs::is_directory(root)) {
        error_message = "Watch root is not a directory: " + path_to_utf8_string(root);
        return sessions;
    }

    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory()) {
            sessions.push_back(entry.path());
        }
    }

    std::sort(
        sessions.begin(),
        sessions.end(),
        [](const fs::path& lhs, const fs::path& rhs) {
            const auto lhs_time = fs::last_write_time(lhs);
            const auto rhs_time = fs::last_write_time(rhs);
            if (lhs_time != rhs_time) {
                return lhs_time < rhs_time;
            }
            return lhs.filename().string() < rhs.filename().string();
        }
    );
    return sessions;
}

void print_session_summary(
    const fs::path& session_dir,
    const fs::path& stage1_event_path,
    const SessionOutputPaths& output_paths,
    Module2Mode mode,
    const YoloRuntimeInfo& runtime_info,
    const Module2Execution& execution,
    EventType fallback_event_type
) {
    std::cout << "session_dir: " << path_to_utf8_string(session_dir) << "\n";
    std::cout << "stage1_event: " << path_to_utf8_string(stage1_event_path) << "\n";
    std::cout << "stage2_result: " << path_to_utf8_string(output_paths.result_path) << "\n";
    std::cout << "final_event: " << path_to_utf8_string(output_paths.final_event_path) << "\n";
    std::cout << "software_closure_report: "
              << path_to_utf8_string(output_paths.software_closure_report_path) << "\n";
    std::cout << "module2_mode: " << to_string(mode) << "\n";
    std::cout << "runtime_model: " << path_to_utf8_string(runtime_info.resolved_model_path) << "\n";
    std::cout << "runtime_ready: " << (runtime_info.can_run_in_current_cpp_runtime ? "yes" : "no") << "\n";
    std::cout << "stage2_success: " << (execution.success ? "yes" : "no") << "\n";
    std::cout << "stage2_event_type: "
              << fridge::to_string(execution.success ? execution.result.event.event_type : fallback_event_type)
              << "\n";
    if (!execution.failure_reason.empty()) {
        std::cout << "stage2_message: " << execution.failure_reason << "\n";
    } else if (!execution.result.review_reason.empty()) {
        std::cout << "stage2_message: " << execution.result.review_reason << "\n";
    } else {
        std::cout << "stage2_message: ok\n";
    }
    std::cout << "\n";
}

bool process_session(
    const fs::path& session_dir,
    const fs::path& repo_root,
    const AppOptions& options,
    const YoloRuntimeConfig& runtime_config,
    const YoloAnalysisConfig& analysis_config,
    bool skip_if_current,
    bool& processed,
    std::string& error_message
) {
    processed = false;

    if (!fs::exists(session_dir) || !fs::is_directory(session_dir)) {
        error_message = "Session directory does not exist: " + path_to_utf8_string(session_dir);
        return false;
    }

    const fs::path stage1_event_path = resolve_stage1_event_path(session_dir);
    if (!fs::exists(stage1_event_path)) {
        error_message = "Stage1 event json is missing: " + path_to_utf8_string(stage1_event_path);
        return false;
    }

    Stage1SessionSnapshot snapshot;
    if (!load_stage1_session_snapshot(stage1_event_path, snapshot, error_message)) {
        return false;
    }

    const fs::path before_frame_path =
        resolve_session_frame_path(session_dir, stage1_event_path, snapshot.before_frame_hint, "before");
    const fs::path after_frame_path =
        resolve_session_frame_path(session_dir, stage1_event_path, snapshot.after_frame_hint, "after");
    if (before_frame_path.empty() || after_frame_path.empty()) {
        error_message = "Failed to resolve stage1 before/after frames inside session: " +
                        path_to_utf8_string(session_dir);
        return false;
    }

    const SessionOutputPaths output_paths = make_output_paths(session_dir);
    if (skip_if_current && outputs_are_current(output_paths, stage1_event_path, before_frame_path, after_frame_path)) {
        return true;
    }

    GrayFrame before_frame;
    if (!fridge::load_debug_image(before_frame_path, before_frame, error_message)) {
        error_message = "Failed to load before frame: " + error_message;
        return false;
    }
    ColorFrame before_color;
    if (!fridge::load_color_debug_image(before_frame_path, before_color, error_message)) {
        error_message = "Failed to load before color frame: " + error_message;
        return false;
    }

    GrayFrame after_frame;
    if (!fridge::load_debug_image(after_frame_path, after_frame, error_message)) {
        error_message = "Failed to load after frame: " + error_message;
        return false;
    }
    ColorFrame after_color;
    if (!fridge::load_color_debug_image(after_frame_path, after_color, error_message)) {
        error_message = "Failed to load after color frame: " + error_message;
        return false;
    }

    Module2Execution execution = run_module2(
        snapshot,
        before_frame,
        after_frame,
        before_color,
        after_color,
        repo_root,
        before_frame_path,
        after_frame_path,
        runtime_config,
        analysis_config,
        options.module2_mode,
        options.mock_coarse_class
    );

    const std::string before_detection_json = build_detection_list_json(
        snapshot.event.session_id,
        "before",
        options.module2_mode,
        execution.before_detections,
        execution.failure_reason
    );
    const std::string after_detection_json = build_detection_list_json(
        snapshot.event.session_id,
        "after",
        options.module2_mode,
        execution.after_detections,
        execution.failure_reason
    );

    if (!write_text_file(output_paths.detections_before_path, before_detection_json, error_message) ||
        !write_text_file(output_paths.detections_after_path, after_detection_json, error_message)) {
        return false;
    }

    if (options.write_crops &&
        !write_crop_artifacts(output_paths.crops_dir, execution, before_frame, after_frame, error_message)) {
        return false;
    }

    const std::string result_json = build_module2_result_json(
        snapshot.event.session_id,
        options.module2_mode,
        execution,
        EventType::NotEvaluated
    );
    if (!write_text_file(output_paths.result_path, result_json, error_message)) {
        return false;
    }

    EventResult final_event = execution.success ? execution.result.event : snapshot.event;
    if (!execution.success) {
        final_event.event_type = EventType::NotEvaluated;
        final_event.need_user_confirm = true;
    }
    final_event.session_id = snapshot.event.session_id;
    final_event.roi_id = snapshot.event.roi_id;
    final_event.before_frame = path_to_utf8_string(before_frame_path);
    final_event.after_frame = path_to_utf8_string(after_frame_path);
    if (final_event.timestamp.empty()) {
        final_event.timestamp = snapshot.event.timestamp;
    }
    if (!fridge::write_event_json(final_event, output_paths.final_event_path, error_message)) {
        return false;
    }

    fridge::InventoryRuntimeConfig inventory_config;
    if (!fridge::load_inventory_runtime_config(
            resolve_inventory_config_path(repo_root),
            inventory_config,
            error_message
        )) {
        return false;
    }

    fridge::LocalServiceConfig service_config;
    if (!fridge::load_local_service_config(
            resolve_service_config_path(repo_root),
            service_config,
            error_message
        )) {
        return false;
    }

    fridge::InventoryEngine inventory_engine(inventory_config);
    const fridge::LocalServiceFacade facade(service_config);
    fridge::SoftwareClosureResult closure_result;
    const fridge::SoftwareClosureEvidencePaths closure_paths{
        output_paths.final_event_path,
        output_paths.inventory_response_path,
        output_paths.events_response_path,
        output_paths.pending_response_path,
        output_paths.software_closure_report_path
    };
    const fridge::SoftwareClosureContext closure_context{
        to_string(options.module2_mode),
        "module2_session_runner",
        execution.success ? "" : execution.failure_reason,
        options.module2_mode == Module2Mode::Mock
            ? "mock/debug evidence; not real ONNX, camera, or board validation"
            : "real_onnx_runtime session replay evidence; camera and board validation are not implied"
    };
    const std::string closure_review_reason = execution.success
        ? execution.result.review_reason
        : execution.failure_reason;
    if (!fridge::write_software_closure_evidence(
            inventory_engine,
            facade,
            final_event,
            closure_paths,
            closure_context,
            closure_review_reason,
            closure_result,
            error_message
        )) {
        return false;
    }

    const YoloRuntime runtime(runtime_config);
    const auto runtime_info = runtime.inspect(repo_root);
    print_session_summary(
        session_dir,
        stage1_event_path,
        output_paths,
        options.module2_mode,
        runtime_info,
        execution,
        EventType::NotEvaluated
    );

    processed = true;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path repo_root = resolve_repo_root();

        AppOptions options;
        std::string error_message;
        if (!parse_arguments(argc, argv, options, error_message)) {
            print_usage();
            if (!error_message.empty()) {
                std::cerr << "Argument error: " << error_message << "\n";
            }
            return 1;
        }

        const fs::path config_path = resolve_config_path(options, repo_root);
        YoloRuntimeConfig runtime_config;
        if (!fridge::load_yolo_runtime_config(config_path, runtime_config, error_message)) {
            std::cerr << "Failed to load module 2 runtime config: " << error_message << "\n";
            return 1;
        }

        YoloAnalysisConfig analysis_config;
        if (!fridge::load_yolo_analysis_config(config_path, analysis_config, error_message)) {
            std::cerr << "Failed to load module 2 analysis config: " << error_message << "\n";
            return 1;
        }

        if (!options.watch_root.empty()) {
            while (true) {
                std::string watch_error;
                const auto sessions = list_session_dirs(options.watch_root, watch_error);
                if (!watch_error.empty()) {
                    std::cerr << watch_error << "\n";
                    return 1;
                }

                for (const auto& current_session : sessions) {
                    bool processed = false;
                    std::string session_error;
                    if (!process_session(
                            current_session,
                            repo_root,
                            options,
                            runtime_config,
                            analysis_config,
                            true,
                            processed,
                            session_error
                        )) {
                        std::cerr << "module2 session processing failed for "
                                  << path_to_utf8_string(current_session) << ": "
                                  << session_error << "\n";
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(options.poll_interval_ms));
            }
        }

        fs::path session_dir = options.session_dir;
        if (session_dir.empty()) {
            session_dir = find_latest_session_dir(options.latest_root, error_message);
            if (!error_message.empty()) {
                std::cerr << error_message << "\n";
                return 1;
            }
        }

        bool processed = false;
        if (!process_session(
                session_dir,
                repo_root,
                options,
                runtime_config,
                analysis_config,
                false,
                processed,
                error_message
            )) {
            std::cerr << error_message << "\n";
            return 1;
        }

        return processed ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "module 2 session runner failed: " << ex.what() << "\n";
        return 1;
    }
}
