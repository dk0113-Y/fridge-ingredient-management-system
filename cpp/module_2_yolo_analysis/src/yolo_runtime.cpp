#include "yolo_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <fstream>
#include <string>
#include <utility>

namespace fridge {

namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string strip_inline_comment(const std::string& value) {
    const std::size_t comment_pos = value.find('#');
    if (comment_pos == std::string::npos) {
        return value;
    }
    return value.substr(0, comment_pos);
}

bool parse_int_value(const std::string& text, int& value) {
    std::size_t consumed = 0;
    value = std::stoi(text, &consumed);
    return consumed == text.size();
}

bool parse_double_value(const std::string& text, double& value) {
    std::size_t consumed = 0;
    value = std::stod(text, &consumed);
    return consumed == text.size();
}

std::vector<std::string> split_csv(const std::string& text) {
    std::vector<std::string> values;
    std::string current;
    for (char ch : text) {
        if (ch == ',') {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                values.push_back(trimmed);
            }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    const std::string trimmed = trim(current);
    if (!trimmed.empty()) {
        values.push_back(trimmed);
    }
    return values;
}

std::filesystem::path resolve_model_path(
    const std::filesystem::path& configured_path,
    const std::filesystem::path& repo_root
) {
    if (configured_path.is_absolute()) {
        return configured_path;
    }

    const std::vector<std::filesystem::path> candidates = {
        repo_root / configured_path,
        repo_root / "cpp" / configured_path,
        std::filesystem::current_path() / configured_path,
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return repo_root / configured_path;
}

YoloModelFormat detect_format(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );

    if (extension == ".pt") {
        return YoloModelFormat::Pt;
    }
    if (extension == ".onnx") {
        return YoloModelFormat::Onnx;
    }
    return YoloModelFormat::Unknown;
}

bool set_config_value(
    YoloRuntimeConfig& config,
    const std::string& key,
    const std::string& value,
    std::string& error_message
) {
    try {
        if (key == "model_path") {
            config.model_path = value;
            return true;
        }
        if (key == "class_names") {
            const auto values = split_csv(value);
            if (values.empty()) {
                error_message = "class_names must contain at least one label";
                return false;
            }
            config.class_names = values;
            return true;
        }
        if (key == "input_width") {
            return parse_int_value(value, config.input_width);
        }
        if (key == "input_height") {
            return parse_int_value(value, config.input_height);
        }
        if (key == "score_threshold") {
            return parse_double_value(value, config.score_threshold);
        }
        if (key == "nms_threshold") {
            return parse_double_value(value, config.nms_threshold);
        }
        if (key == "output_columns") {
            return parse_int_value(value, config.output_columns);
        }

        error_message = "Unknown YOLO runtime config key: " + key;
        return false;
    } catch (const std::exception&) {
        error_message = "Invalid value for key: " + key;
        return false;
    }
}

int clamp_int(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

BoundingBox scale_box_to_frame(
    double x1,
    double y1,
    double x2,
    double y2,
    const GrayFrame& source_frame,
    const YoloRuntimeConfig& config
) {
    if (source_frame.width <= 0 || source_frame.height <= 0 ||
        config.input_width <= 0 || config.input_height <= 0) {
        return {};
    }

    const double scale_x = static_cast<double>(source_frame.width) / static_cast<double>(config.input_width);
    const double scale_y = static_cast<double>(source_frame.height) / static_cast<double>(config.input_height);

    const int left = clamp_int(static_cast<int>(std::floor(x1 * scale_x)), 0, source_frame.width);
    const int top = clamp_int(static_cast<int>(std::floor(y1 * scale_y)), 0, source_frame.height);
    const int right = clamp_int(static_cast<int>(std::ceil(x2 * scale_x)), 0, source_frame.width);
    const int bottom = clamp_int(static_cast<int>(std::ceil(y2 * scale_y)), 0, source_frame.height);
    return BoundingBox{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

}  // namespace

std::string to_string(YoloModelFormat format) {
    switch (format) {
    case YoloModelFormat::Pt:
        return "pt";
    case YoloModelFormat::Onnx:
        return "onnx";
    case YoloModelFormat::Unknown:
        return "unknown";
    }
    return "unknown";
}

bool load_yolo_runtime_config(
    const std::filesystem::path& config_path,
    YoloRuntimeConfig& config,
    std::string& error_message
) {
    std::ifstream input(config_path);
    if (!input) {
        error_message = "Failed to open YOLO runtime config file: " + config_path.string();
        return false;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string normalized = trim(strip_inline_comment(line));
        if (normalized.empty()) {
            continue;
        }

        const std::size_t separator = normalized.find('=');
        if (separator == std::string::npos) {
            error_message = "Missing '=' at line " + std::to_string(line_number);
            return false;
        }

        const std::string key = trim(normalized.substr(0, separator));
        const std::string value = trim(normalized.substr(separator + 1));
        std::string line_error;
        if (!set_config_value(config, key, value, line_error)) {
            if (line_error.find("Unknown YOLO runtime config key:") == 0) {
                continue;
            }
            error_message = line_error + " at line " + std::to_string(line_number);
            return false;
        }
    }

    return true;
}

YoloModelInput prepare_yolo_model_input(const GrayFrame& frame, const YoloRuntimeConfig& config) {
    YoloModelInput input;
    input.channels = 3;
    input.height = std::max(0, config.input_height);
    input.width = std::max(0, config.input_width);
    if (frame.empty() || input.height == 0 || input.width == 0) {
        return input;
    }

    input.values.resize(static_cast<std::size_t>(input.channels * input.height * input.width), 0.0F);
    for (int y = 0; y < input.height; ++y) {
        const int src_y = std::min(frame.height - 1, (y * frame.height) / input.height);
        for (int x = 0; x < input.width; ++x) {
            const int src_x = std::min(frame.width - 1, (x * frame.width) / input.width);
            const float normalized = static_cast<float>(frame.at(src_x, src_y)) / 255.0F;
            const std::size_t spatial_index = static_cast<std::size_t>(y * input.width + x);
            input.values[spatial_index] = normalized;
            input.values[static_cast<std::size_t>(input.height * input.width) + spatial_index] = normalized;
            input.values[static_cast<std::size_t>(2 * input.height * input.width) + spatial_index] = normalized;
        }
    }
    return input;
}

std::vector<YoloDetection> decode_yolo_onnx_output(
    const YoloOnnxOutput& output,
    const GrayFrame& source_frame,
    const YoloRuntimeConfig& config,
    std::string& error_message
) {
    if (output.columns <= 0 || config.output_columns <= 0) {
        error_message = "YOLO ONNX output column count must be positive";
        return {};
    }
    if (output.columns != config.output_columns) {
        error_message =
            "YOLO ONNX output column count does not match config: got " +
            std::to_string(output.columns) + ", expected " + std::to_string(config.output_columns);
        return {};
    }
    if (output.values.size() % static_cast<std::size_t>(output.columns) != 0U) {
        error_message = "YOLO ONNX output tensor size is not divisible by the configured column count";
        return {};
    }

    const int inferred_rows = static_cast<int>(output.values.size() / static_cast<std::size_t>(output.columns));
    if (output.rows != 0 && output.rows != inferred_rows) {
        error_message =
            "YOLO ONNX output row count does not match tensor size: got " +
            std::to_string(output.rows) + ", expected " + std::to_string(inferred_rows);
        return {};
    }

    std::vector<YoloDetection> detections;
    detections.reserve(static_cast<std::size_t>(inferred_rows));
    for (int row = 0; row < inferred_rows; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row * output.columns);
        const double score = static_cast<double>(output.values[offset + 4]);
        const int class_index = static_cast<int>(std::lround(static_cast<double>(output.values[offset + 5])));
        if (!std::isfinite(score) || score < config.score_threshold) {
            continue;
        }
        if (class_index < 0 || class_index >= static_cast<int>(config.class_names.size())) {
            continue;
        }

        const BoundingBox bbox = scale_box_to_frame(
            static_cast<double>(output.values[offset]),
            static_cast<double>(output.values[offset + 1]),
            static_cast<double>(output.values[offset + 2]),
            static_cast<double>(output.values[offset + 3]),
            source_frame,
            config
        );
        if (bbox.width <= 0 || bbox.height <= 0) {
            continue;
        }

        detections.push_back(YoloDetection{
            config.class_names[static_cast<std::size_t>(class_index)],
            score,
            bbox
        });
    }

    error_message.clear();
    return detections;
}

YoloModule2Pipeline::YoloModule2Pipeline(YoloRuntimeConfig runtime_config, YoloAnalysisConfig analysis_config)
    : runtime_config_(std::move(runtime_config)),
      analyzer_(std::move(analysis_config)) {}

YoloModelInput YoloModule2Pipeline::prepare_input(const GrayFrame& frame) const {
    return prepare_yolo_model_input(frame, runtime_config_);
}

std::vector<YoloDetection> YoloModule2Pipeline::decode_output(
    const YoloOnnxOutput& output,
    const GrayFrame& source_frame,
    std::string& error_message
) const {
    return decode_yolo_onnx_output(output, source_frame, runtime_config_, error_message);
}

YoloDiffResult YoloModule2Pipeline::analyze_outputs(
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const YoloOnnxOutput& before_output,
    const YoloOnnxOutput& after_output,
    const std::string& session_id,
    const std::string& before_frame_path,
    const std::string& after_frame_path,
    std::string& error_message
) const {
    const auto before_detections = decode_output(before_output, before_frame, error_message);
    if (!error_message.empty()) {
        return {};
    }

    const auto after_detections = decode_output(after_output, after_frame, error_message);
    if (!error_message.empty()) {
        return {};
    }

    return analyzer_.analyze(
        before_frame,
        after_frame,
        before_detections,
        after_detections,
        session_id,
        before_frame_path,
        after_frame_path
    );
}

YoloRuntime::YoloRuntime(YoloRuntimeConfig config)
    : config_(std::move(config)) {}

YoloRuntimeInfo YoloRuntime::inspect(const std::filesystem::path& repo_root) const {
    YoloRuntimeInfo info;
    info.class_names = config_.class_names;
    info.input_width = config_.input_width;
    info.input_height = config_.input_height;
    info.score_threshold = config_.score_threshold;
    info.nms_threshold = config_.nms_threshold;
    info.resolved_model_path = resolve_model_path(config_.model_path, repo_root);
    info.model_exists = std::filesystem::exists(info.resolved_model_path);
    info.model_format = detect_format(info.resolved_model_path);

    if (!info.model_exists) {
        info.message = "YOLO model file not found: " + info.resolved_model_path.string();
        return info;
    }

    if (info.model_format == YoloModelFormat::Pt) {
        info.can_run_in_current_cpp_runtime = false;
        info.message =
            "Detected a .pt model asset. Current C++ runtime does not execute PyTorch .pt directly; "
            "export the model to ONNX before wiring real inference.";
        return info;
    }

    if (info.model_format == YoloModelFormat::Onnx) {
        info.can_run_in_current_cpp_runtime = false;
        info.message =
            "Detected an ONNX model asset. Module 2 preprocessing, output decoding, and diff analysis are ready, "
            "but the ONNX graph execution backend still needs to be connected in C++.";
        return info;
    }

    info.message =
        "Unsupported YOLO model format for current C++ runtime: " + info.resolved_model_path.extension().string();
    return info;
}

}  // namespace fridge
