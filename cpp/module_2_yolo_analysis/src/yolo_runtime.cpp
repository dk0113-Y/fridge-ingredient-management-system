#include "yolo_runtime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#ifdef FRIDGE_USE_OPENCV
#include <opencv2/dnn.hpp>
#endif

#ifdef FRIDGE_USE_ONNXRUNTIME
#if defined(__has_include)
#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#else
#error "ONNX Runtime headers were not found in the configured include path."
#endif
#else
#include <onnxruntime_cxx_api.h>
#endif
#endif

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

struct RuntimeDetectionRow {
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
    float score = 0.0F;
    int class_index = 0;
};

double compute_iou_xyxy(const RuntimeDetectionRow& lhs, const RuntimeDetectionRow& rhs) {
    const double left = std::max(static_cast<double>(lhs.x1), static_cast<double>(rhs.x1));
    const double top = std::max(static_cast<double>(lhs.y1), static_cast<double>(rhs.y1));
    const double right = std::min(static_cast<double>(lhs.x2), static_cast<double>(rhs.x2));
    const double bottom = std::min(static_cast<double>(lhs.y2), static_cast<double>(rhs.y2));
    const double intersection_width = std::max(0.0, right - left);
    const double intersection_height = std::max(0.0, bottom - top);
    const double intersection_area = intersection_width * intersection_height;
    if (intersection_area <= 0.0) {
        return 0.0;
    }

    const double lhs_area =
        std::max(0.0, static_cast<double>(lhs.x2 - lhs.x1)) *
        std::max(0.0, static_cast<double>(lhs.y2 - lhs.y1));
    const double rhs_area =
        std::max(0.0, static_cast<double>(rhs.x2 - rhs.x1)) *
        std::max(0.0, static_cast<double>(rhs.y2 - rhs.y1));
    const double denominator = lhs_area + rhs_area - intersection_area;
    if (denominator <= 0.0) {
        return 0.0;
    }
    return intersection_area / denominator;
}

struct TensorMatrix {
    int rows = 0;
    int columns = 0;
    std::vector<float> values;
};

bool transpose_matrix(const TensorMatrix& input, TensorMatrix& output) {
    if (input.rows <= 0 || input.columns <= 0) {
        return false;
    }
    output.rows = input.columns;
    output.columns = input.rows;
    output.values.assign(static_cast<std::size_t>(output.rows * output.columns), 0.0F);
    for (int row = 0; row < input.rows; ++row) {
        for (int column = 0; column < input.columns; ++column) {
            output.values[static_cast<std::size_t>(column * output.columns + row)] =
                input.values[static_cast<std::size_t>(row * input.columns + column)];
        }
    }
    return true;
}

bool orient_matrix_columns(const TensorMatrix& input, int expected_columns, TensorMatrix& output) {
    if (expected_columns <= 0) {
        return false;
    }
    if (input.columns == expected_columns) {
        output = input;
        return true;
    }
    if (input.rows == expected_columns) {
        return transpose_matrix(input, output);
    }
    return false;
}

void append_direct_detection_rows(
    const TensorMatrix& matrix,
    std::vector<RuntimeDetectionRow>& rows_out
) {
    for (int row = 0; row < matrix.rows; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row * matrix.columns);
        RuntimeDetectionRow detection;
        detection.x1 = matrix.values[offset];
        detection.y1 = matrix.values[offset + 1];
        detection.x2 = matrix.values[offset + 2];
        detection.y2 = matrix.values[offset + 3];
        detection.score = matrix.values[offset + 4];
        detection.class_index = static_cast<int>(std::lround(static_cast<double>(matrix.values[offset + 5])));
        rows_out.push_back(detection);
    }
}

void append_raw_detection_rows(
    const TensorMatrix& matrix,
    const YoloRuntimeConfig& config,
    std::vector<RuntimeDetectionRow>& rows_out
) {
    const int class_count = static_cast<int>(config.class_names.size());
    const bool has_objectness = matrix.columns == class_count + 5;
    const int class_offset = has_objectness ? 5 : 4;

    for (int row = 0; row < matrix.rows; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row * matrix.columns);
        const float center_x = matrix.values[offset];
        const float center_y = matrix.values[offset + 1];
        const float width = matrix.values[offset + 2];
        const float height = matrix.values[offset + 3];
        const float objectness = has_objectness ? matrix.values[offset + 4] : 1.0F;
        if (!std::isfinite(center_x) || !std::isfinite(center_y) ||
            !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(objectness)) {
            continue;
        }
        if (width <= 0.0F || height <= 0.0F || objectness <= 0.0F) {
            continue;
        }

        float best_score = -std::numeric_limits<float>::infinity();
        int best_class_index = -1;
        for (int class_index = 0; class_index < class_count; ++class_index) {
            const float class_score = matrix.values[offset + static_cast<std::size_t>(class_offset + class_index)];
            if (!std::isfinite(class_score)) {
                continue;
            }
            const float combined_score = objectness * class_score;
            if (combined_score > best_score) {
                best_score = combined_score;
                best_class_index = class_index;
            }
        }

        if (best_class_index < 0 || best_score < static_cast<float>(config.score_threshold)) {
            continue;
        }

        RuntimeDetectionRow detection;
        detection.x1 = center_x - (width * 0.5F);
        detection.y1 = center_y - (height * 0.5F);
        detection.x2 = center_x + (width * 0.5F);
        detection.y2 = center_y + (height * 0.5F);
        detection.score = best_score;
        detection.class_index = best_class_index;
        rows_out.push_back(detection);
    }
}

bool append_e2e_box_score_label_rows(
    const TensorMatrix& boxes_matrix,
    const TensorMatrix& score_label_matrix,
    std::vector<RuntimeDetectionRow>& rows_out
) {
    if (boxes_matrix.rows != score_label_matrix.rows ||
        boxes_matrix.columns != 4 || score_label_matrix.columns != 2) {
        return false;
    }

    for (int row = 0; row < boxes_matrix.rows; ++row) {
        const std::size_t box_offset = static_cast<std::size_t>(row * boxes_matrix.columns);
        const std::size_t meta_offset = static_cast<std::size_t>(row * score_label_matrix.columns);
        RuntimeDetectionRow detection;
        detection.x1 = boxes_matrix.values[box_offset];
        detection.y1 = boxes_matrix.values[box_offset + 1];
        detection.x2 = boxes_matrix.values[box_offset + 2];
        detection.y2 = boxes_matrix.values[box_offset + 3];
        detection.score = score_label_matrix.values[meta_offset];
        detection.class_index =
            static_cast<int>(std::lround(static_cast<double>(score_label_matrix.values[meta_offset + 1])));
        rows_out.push_back(detection);
    }
    return true;
}

bool append_e2e_box_score_and_label_rows(
    const TensorMatrix& boxes_matrix,
    const TensorMatrix& scores_matrix,
    const TensorMatrix& labels_matrix,
    std::vector<RuntimeDetectionRow>& rows_out
) {
    if (boxes_matrix.rows != scores_matrix.rows || boxes_matrix.rows != labels_matrix.rows ||
        boxes_matrix.columns != 4 || scores_matrix.columns != 1 || labels_matrix.columns != 1) {
        return false;
    }

    for (int row = 0; row < boxes_matrix.rows; ++row) {
        const std::size_t box_offset = static_cast<std::size_t>(row * boxes_matrix.columns);
        RuntimeDetectionRow detection;
        detection.x1 = boxes_matrix.values[box_offset];
        detection.y1 = boxes_matrix.values[box_offset + 1];
        detection.x2 = boxes_matrix.values[box_offset + 2];
        detection.y2 = boxes_matrix.values[box_offset + 3];
        detection.score = scores_matrix.values[static_cast<std::size_t>(row)];
        detection.class_index = static_cast<int>(std::lround(static_cast<double>(labels_matrix.values[static_cast<std::size_t>(row)])));
        rows_out.push_back(detection);
    }
    return true;
}

bool append_e2e_box_and_class_rows(
    const TensorMatrix& boxes_matrix,
    const TensorMatrix& class_matrix,
    const YoloRuntimeConfig& config,
    std::vector<RuntimeDetectionRow>& rows_out,
    bool has_objectness
) {
    if (boxes_matrix.rows != class_matrix.rows || boxes_matrix.columns != 4) {
        return false;
    }

    const int class_count = static_cast<int>(config.class_names.size());
    const int expected_columns = has_objectness ? class_count + 1 : class_count;
    if (class_matrix.columns != expected_columns) {
        return false;
    }

    for (int row = 0; row < boxes_matrix.rows; ++row) {
        const std::size_t box_offset = static_cast<std::size_t>(row * boxes_matrix.columns);
        const std::size_t class_offset = static_cast<std::size_t>(row * class_matrix.columns);
        const float objectness = has_objectness ? class_matrix.values[class_offset] : 1.0F;
        if (!std::isfinite(objectness) || objectness <= 0.0F) {
            continue;
        }

        float best_score = -std::numeric_limits<float>::infinity();
        int best_class_index = -1;
        for (int class_index = 0; class_index < class_count; ++class_index) {
            const float class_score = class_matrix.values[class_offset + static_cast<std::size_t>(class_index + (has_objectness ? 1 : 0))];
            if (!std::isfinite(class_score)) {
                continue;
            }
            const float combined_score = objectness * class_score;
            if (combined_score > best_score) {
                best_score = combined_score;
                best_class_index = class_index;
            }
        }

        if (best_class_index < 0 || best_score < static_cast<float>(config.score_threshold)) {
            continue;
        }

        RuntimeDetectionRow detection;
        detection.x1 = boxes_matrix.values[box_offset];
        detection.y1 = boxes_matrix.values[box_offset + 1];
        detection.x2 = boxes_matrix.values[box_offset + 2];
        detection.y2 = boxes_matrix.values[box_offset + 3];
        detection.score = best_score;
        detection.class_index = best_class_index;
        rows_out.push_back(detection);
    }
    return true;
}

bool append_matrix_detections(
    const TensorMatrix& input_matrix,
    const YoloRuntimeConfig& config,
    std::vector<RuntimeDetectionRow>& rows_out
) {
    TensorMatrix normalized;
    if (orient_matrix_columns(input_matrix, config.output_columns, normalized)) {
        append_direct_detection_rows(normalized, rows_out);
        return true;
    }

    const int class_count = static_cast<int>(config.class_names.size());
    if (orient_matrix_columns(input_matrix, class_count + 4, normalized) ||
        orient_matrix_columns(input_matrix, class_count + 5, normalized)) {
        append_raw_detection_rows(normalized, config, rows_out);
        return true;
    }
    return false;
}

bool append_tensor_matrices_as_detections(
    const std::vector<TensorMatrix>& matrices,
    const YoloRuntimeConfig& config,
    std::vector<RuntimeDetectionRow>& rows_out,
    std::string& error_message
) {
    std::size_t rows_before = rows_out.size();
    for (const auto& matrix : matrices) {
        append_matrix_detections(matrix, config, rows_out);
    }
    if (rows_out.size() > rows_before) {
        return true;
    }

    for (std::size_t box_index = 0; box_index < matrices.size(); ++box_index) {
        TensorMatrix boxes_matrix;
        if (!orient_matrix_columns(matrices[box_index], 4, boxes_matrix)) {
            continue;
        }

        for (std::size_t meta_index = 0; meta_index < matrices.size(); ++meta_index) {
            if (meta_index == box_index) {
                continue;
            }

            TensorMatrix score_label_matrix;
            if (orient_matrix_columns(matrices[meta_index], 2, score_label_matrix)) {
                if (append_e2e_box_score_label_rows(boxes_matrix, score_label_matrix, rows_out)) {
                    return true;
                }
            }

            TensorMatrix class_matrix;
            const int class_count = static_cast<int>(config.class_names.size());
            if (orient_matrix_columns(matrices[meta_index], class_count, class_matrix)) {
                if (append_e2e_box_and_class_rows(boxes_matrix, class_matrix, config, rows_out, false)) {
                    return true;
                }
            }
            if (orient_matrix_columns(matrices[meta_index], class_count + 1, class_matrix)) {
                if (append_e2e_box_and_class_rows(boxes_matrix, class_matrix, config, rows_out, true)) {
                    return true;
                }
            }

            TensorMatrix scores_matrix;
            if (!orient_matrix_columns(matrices[meta_index], 1, scores_matrix)) {
                continue;
            }

            for (std::size_t label_index = 0; label_index < matrices.size(); ++label_index) {
                if (label_index == box_index || label_index == meta_index) {
                    continue;
                }
                TensorMatrix labels_matrix;
                if (orient_matrix_columns(matrices[label_index], 1, labels_matrix) &&
                    append_e2e_box_score_and_label_rows(boxes_matrix, scores_matrix, labels_matrix, rows_out)) {
                    return true;
                }
            }
        }
    }

    std::ostringstream message;
    message << "Unsupported YOLO ONNX output layout. Parsed tensor matrices:";
    for (const auto& matrix : matrices) {
        message << " " << matrix.rows << "x" << matrix.columns;
    }
    message << ". Expected direct Nx" << config.output_columns
            << ", raw YOLO rows, or end-to-end box/score/class tensors.";
    error_message = message.str();
    return false;
}

std::vector<RuntimeDetectionRow> apply_classwise_nms(
    std::vector<RuntimeDetectionRow> detections,
    const YoloRuntimeConfig& config
) {
    std::stable_sort(
        detections.begin(),
        detections.end(),
        [](const RuntimeDetectionRow& lhs, const RuntimeDetectionRow& rhs) {
            if (lhs.class_index != rhs.class_index) {
                return lhs.class_index < rhs.class_index;
            }
            return lhs.score > rhs.score;
        }
    );

    std::vector<RuntimeDetectionRow> kept;
    kept.reserve(detections.size());
    for (const auto& detection : detections) {
        if (!std::isfinite(detection.score) || detection.score < static_cast<float>(config.score_threshold)) {
            continue;
        }
        if (detection.class_index < 0 || detection.class_index >= static_cast<int>(config.class_names.size())) {
            continue;
        }
        if (detection.x2 <= detection.x1 || detection.y2 <= detection.y1) {
            continue;
        }

        bool suppressed = false;
        for (const auto& existing : kept) {
            if (existing.class_index != detection.class_index) {
                continue;
            }
            if (compute_iou_xyxy(existing, detection) >= config.nms_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(detection);
        }
    }
    return kept;
}

YoloOnnxOutput build_normalized_output_from_rows(
    std::vector<RuntimeDetectionRow> detection_rows,
    const YoloRuntimeConfig& config,
    std::string& error_message
) {
    const auto filtered_rows = apply_classwise_nms(std::move(detection_rows), config);

    YoloOnnxOutput output;
    output.rows = static_cast<int>(filtered_rows.size());
    output.columns = config.output_columns;
    output.values.reserve(filtered_rows.size() * static_cast<std::size_t>(config.output_columns));
    for (const auto& detection : filtered_rows) {
        output.values.push_back(detection.x1);
        output.values.push_back(detection.y1);
        output.values.push_back(detection.x2);
        output.values.push_back(detection.y2);
        output.values.push_back(detection.score);
        output.values.push_back(static_cast<float>(detection.class_index));
    }
    error_message.clear();
    return output;
}

#ifdef FRIDGE_USE_OPENCV
std::vector<float> copy_tensor_values_as_float(const cv::Mat& tensor) {
    cv::Mat float_tensor;
    if (tensor.depth() == CV_32F) {
        float_tensor = tensor.isContinuous() ? tensor : tensor.clone();
    } else {
        tensor.convertTo(float_tensor, CV_32F);
        if (!float_tensor.isContinuous()) {
            float_tensor = float_tensor.clone();
        }
    }

    const auto* begin = reinterpret_cast<const float*>(float_tensor.datastart);
    const auto* end = reinterpret_cast<const float*>(float_tensor.dataend);
    return std::vector<float>(begin, end);
}

bool flatten_output_tensor_to_matrix(
    const cv::Mat& tensor,
    TensorMatrix& matrix,
    std::string& error_message
) {
    if (tensor.empty()) {
        error_message = "OpenCV DNN returned an empty output tensor";
        return false;
    }

    if (tensor.dims == 2) {
        matrix.rows = tensor.rows;
        matrix.columns = tensor.cols;
        matrix.values = copy_tensor_values_as_float(tensor);
        return true;
    }

    if (tensor.dims == 3 && tensor.size[0] == 1) {
        matrix.rows = tensor.size[1];
        matrix.columns = tensor.size[2];
        matrix.values = copy_tensor_values_as_float(tensor);
        return true;
    }

    if (tensor.dims == 4 && tensor.size[0] == 1 && tensor.size[1] == 1) {
        matrix.rows = tensor.size[2];
        matrix.columns = tensor.size[3];
        matrix.values = copy_tensor_values_as_float(tensor);
        return true;
    }

    std::ostringstream message;
    message << "Unsupported ONNX output tensor rank/layout: dims=" << tensor.dims;
    for (int index = 0; index < tensor.dims; ++index) {
        message << (index == 0 ? " [" : "x") << tensor.size[index];
    }
    if (tensor.dims > 0) {
        message << "]";
    }
    error_message = message.str();
    return false;
}

YoloOnnxOutput build_normalized_output(
    const std::vector<cv::Mat>& tensors,
    const YoloRuntimeConfig& config,
    std::string& error_message
) {
    std::vector<TensorMatrix> matrices;
    matrices.reserve(tensors.size());
    for (const auto& tensor : tensors) {
        TensorMatrix matrix;
        if (!flatten_output_tensor_to_matrix(tensor, matrix, error_message)) {
            return {};
        }
        matrices.push_back(std::move(matrix));
    }

    std::vector<RuntimeDetectionRow> detection_rows;
    if (!append_tensor_matrices_as_detections(matrices, config, detection_rows, error_message)) {
        return {};
    }

    return build_normalized_output_from_rows(std::move(detection_rows), config, error_message);
}
#endif

#ifdef FRIDGE_USE_ONNXRUNTIME
Ort::Env& ort_environment() {
    static Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "fridge_yolo_runtime");
    return environment;
}

Ort::SessionOptions make_ort_session_options() {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    options.SetIntraOpNumThreads(1);
    return options;
}

std::basic_string<ORTCHAR_T> path_to_ort_string(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.native();
#else
    return path.string();
#endif
}

std::vector<float> copy_ort_tensor_values(const Ort::Value& tensor, ONNXTensorElementDataType element_type) {
    const std::size_t value_count = tensor.GetTensorTypeAndShapeInfo().GetElementCount();
    std::vector<float> values(value_count, 0.0F);

    switch (element_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: {
        const float* data = tensor.GetTensorData<float>();
        std::copy(data, data + value_count, values.begin());
        return values;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: {
        const double* data = tensor.GetTensorData<double>();
        std::transform(data, data + value_count, values.begin(), [](double value) {
            return static_cast<float>(value);
        });
        return values;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: {
        const std::int64_t* data = tensor.GetTensorData<std::int64_t>();
        std::transform(data, data + value_count, values.begin(), [](std::int64_t value) {
            return static_cast<float>(value);
        });
        return values;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: {
        const std::int32_t* data = tensor.GetTensorData<std::int32_t>();
        std::transform(data, data + value_count, values.begin(), [](std::int32_t value) {
            return static_cast<float>(value);
        });
        return values;
    }
    default:
        return {};
    }
}

bool flatten_output_tensor_to_matrix(
    const Ort::Value& tensor,
    TensorMatrix& matrix,
    std::string& error_message
) {
    if (!tensor.IsTensor()) {
        error_message = "ONNX Runtime returned a non-tensor output";
        return false;
    }

    const auto type_info = tensor.GetTensorTypeAndShapeInfo();
    const ONNXTensorElementDataType element_type = type_info.GetElementType();
    matrix.values = copy_ort_tensor_values(tensor, element_type);
    if (matrix.values.empty() && type_info.GetElementCount() > 0U) {
        error_message = "ONNX Runtime returned an unsupported tensor element type";
        return false;
    }

    const std::vector<std::int64_t> dims = type_info.GetShape();
    if (dims.size() == 2) {
        matrix.rows = static_cast<int>(dims[0]);
        matrix.columns = static_cast<int>(dims[1]);
        return true;
    }
    if (dims.size() == 3 && dims[0] == 1) {
        matrix.rows = static_cast<int>(dims[1]);
        matrix.columns = static_cast<int>(dims[2]);
        return true;
    }
    if (dims.size() == 4 && dims[0] == 1 && dims[1] == 1) {
        matrix.rows = static_cast<int>(dims[2]);
        matrix.columns = static_cast<int>(dims[3]);
        return true;
    }

    std::ostringstream message;
    message << "Unsupported ONNX Runtime output tensor rank/layout:";
    for (const auto dim : dims) {
        message << " " << dim;
    }
    error_message = message.str();
    return false;
}

bool inspect_onnxruntime_model(
    const std::filesystem::path& model_path,
    std::string& error_message
) {
    try {
        const auto ort_model_path = path_to_ort_string(model_path);
        Ort::Session session(ort_environment(), ort_model_path.c_str(), make_ort_session_options());
        if (session.GetInputCount() == 0) {
            error_message = "ONNX Runtime opened the model but no input tensor was exposed.";
            return false;
        }
        return true;
    } catch (const Ort::Exception& ex) {
        error_message = "Failed to initialize ONNX Runtime for ONNX model: " + std::string(ex.what());
        return false;
    }
}

YoloOnnxOutput run_onnxruntime_model(
    const std::filesystem::path& model_path,
    const YoloRuntimeConfig& config,
    const YoloModelInput& prepared_input,
    std::string& error_message
) {
    const auto ort_model_path = path_to_ort_string(model_path);

    try {
        Ort::Session session(ort_environment(), ort_model_path.c_str(), make_ort_session_options());
        Ort::AllocatorWithDefaultOptions allocator;

        auto input_name = session.GetInputNameAllocated(0, allocator);
        std::vector<const char*> input_names{input_name.get()};

        std::vector<Ort::AllocatedStringPtr> output_name_storage;
        std::vector<const char*> output_names;
        output_name_storage.reserve(session.GetOutputCount());
        output_names.reserve(session.GetOutputCount());
        for (std::size_t index = 0; index < session.GetOutputCount(); ++index) {
            output_name_storage.push_back(session.GetOutputNameAllocated(index, allocator));
            output_names.push_back(output_name_storage.back().get());
        }

        const std::array<std::int64_t, 4> input_shape{
            1,
            prepared_input.channels,
            prepared_input.height,
            prepared_input.width,
        };
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            const_cast<float*>(prepared_input.values.data()),
            prepared_input.values.size(),
            input_shape.data(),
            input_shape.size()
        );

        auto outputs = session.Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            output_names.size()
        );
        if (outputs.empty()) {
            error_message = "ONNX Runtime did not return any output tensors";
            return {};
        }

        std::vector<TensorMatrix> matrices;
        matrices.reserve(outputs.size());
        for (const auto& output : outputs) {
            TensorMatrix matrix;
            if (!flatten_output_tensor_to_matrix(output, matrix, error_message)) {
                return {};
            }
            matrices.push_back(std::move(matrix));
        }

        std::vector<RuntimeDetectionRow> detection_rows;
        if (!append_tensor_matrices_as_detections(matrices, config, detection_rows, error_message)) {
            return {};
        }
        return build_normalized_output_from_rows(std::move(detection_rows), config, error_message);
    } catch (const Ort::Exception& ex) {
        error_message = "ONNX Runtime inference failed: " + std::string(ex.what());
        return {};
    }
}
#endif

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

YoloModelInput prepare_yolo_model_input(const ColorFrame& frame, const YoloRuntimeConfig& config) {
    YoloModelInput input;
    input.channels = 3;
    input.height = std::max(0, config.input_height);
    input.width = std::max(0, config.input_width);
    if (frame.empty() || input.height == 0 || input.width == 0) {
        return input;
    }

    const std::size_t plane_size = static_cast<std::size_t>(input.height * input.width);
    input.values.resize(static_cast<std::size_t>(input.channels) * plane_size, 0.0F);
    for (int y = 0; y < input.height; ++y) {
        const int src_y = std::min(frame.height - 1, (y * frame.height) / input.height);
        for (int x = 0; x < input.width; ++x) {
            const int src_x = std::min(frame.width - 1, (x * frame.width) / input.width);
            const std::size_t source_index = static_cast<std::size_t>((src_y * frame.width + src_x) * 3);
            const float blue = static_cast<float>(frame.pixels[source_index]) / 255.0F;
            const float green = static_cast<float>(frame.pixels[source_index + 1]) / 255.0F;
            const float red = static_cast<float>(frame.pixels[source_index + 2]) / 255.0F;
            const std::size_t spatial_index = static_cast<std::size_t>(y * input.width + x);
            input.values[spatial_index] = red;
            input.values[plane_size + spatial_index] = green;
            input.values[(plane_size * 2) + spatial_index] = blue;
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

YoloModelInput YoloModule2Pipeline::prepare_input(const ColorFrame& frame) const {
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
    info.output_columns = config_.output_columns;
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
#ifdef FRIDGE_USE_ONNXRUNTIME
        if (inspect_onnxruntime_model(info.resolved_model_path, info.message)) {
            info.can_run_in_current_cpp_runtime = true;
            info.message =
                "Detected an ONNX model asset. Current C++ runtime can execute it through ONNX Runtime.";
            return info;
        }
#endif
#ifdef FRIDGE_USE_OPENCV
        try {
            cv::dnn::Net net = cv::dnn::readNetFromONNX(info.resolved_model_path.string());
            if (net.empty()) {
                info.can_run_in_current_cpp_runtime = false;
                info.message = "OpenCV DNN could not initialize the ONNX model backend.";
                return info;
            }

            info.can_run_in_current_cpp_runtime = true;
            info.message =
                "Detected an ONNX model asset. Current C++ runtime can execute it through OpenCV DNN.";
            return info;
        } catch (const cv::Exception& ex) {
            info.can_run_in_current_cpp_runtime = false;
            info.message = "Failed to initialize OpenCV DNN for ONNX model: " + std::string(ex.what());
            return info;
        }
#else
        info.can_run_in_current_cpp_runtime = false;
        info.message =
            "Detected an ONNX model asset. Module 2 preprocessing, output decoding, and diff analysis are ready, "
            "but the ONNX graph execution backend still needs to be connected in C++.";
        return info;
#endif
    }

    info.message =
        "Unsupported YOLO model format for current C++ runtime: " + info.resolved_model_path.extension().string();
    return info;
}

YoloOnnxOutput YoloRuntime::run(
    const GrayFrame& frame,
    const std::filesystem::path& repo_root,
    std::string& error_message
) const {
    error_message.clear();

    if (frame.empty()) {
        error_message = "Cannot run YOLO inference on an empty frame";
        return {};
    }

    const auto info = inspect(repo_root);
    if (!info.can_run_in_current_cpp_runtime) {
        error_message = info.message;
        return {};
    }

    const YoloModelInput prepared = prepare_yolo_model_input(frame, config_);
    if (prepared.height <= 0 || prepared.width <= 0 || prepared.values.empty()) {
        error_message = "Failed to prepare YOLO model input";
        return {};
    }

#ifdef FRIDGE_USE_ONNXRUNTIME
    std::string ort_error;
    YoloOnnxOutput ort_output = run_onnxruntime_model(info.resolved_model_path, config_, prepared, ort_error);
    if (ort_error.empty()) {
        return ort_output;
    }
    error_message = ort_error;
    return {};
#endif

#ifdef FRIDGE_USE_OPENCV
    const int blob_sizes[] = {1, prepared.channels, prepared.height, prepared.width};
    cv::Mat blob(4, blob_sizes, CV_32F);
    std::copy(prepared.values.begin(), prepared.values.end(), reinterpret_cast<float*>(blob.data));

    try {
        cv::dnn::Net net = cv::dnn::readNetFromONNX(info.resolved_model_path.string());
        if (net.empty()) {
            error_message = "OpenCV DNN returned an empty network for model: " + info.resolved_model_path.string();
            return {};
        }

        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        net.setInput(blob);

        std::vector<cv::Mat> outputs;
        const std::vector<cv::String> output_names = net.getUnconnectedOutLayersNames();
        if (output_names.empty()) {
            outputs.push_back(net.forward());
        } else {
            net.forward(outputs, output_names);
        }
        if (outputs.empty()) {
            error_message = "OpenCV DNN did not return any output tensors";
            return {};
        }

        return build_normalized_output(outputs, config_, error_message);
    } catch (const cv::Exception& ex) {
        error_message = "OpenCV DNN inference failed: " + std::string(ex.what());
        return {};
    }
#else
    (void)repo_root;
#ifdef FRIDGE_USE_ONNXRUNTIME
    error_message = ort_error;
#else
    error_message =
        "YOLO ONNX inference requires an OpenCV-enabled build with DNN support.";
#endif
    return {};
#endif
}

YoloOnnxOutput YoloRuntime::run(
    const ColorFrame& frame,
    const std::filesystem::path& repo_root,
    std::string& error_message
) const {
    error_message.clear();

    if (frame.empty()) {
        error_message = "Cannot run YOLO inference on an empty color frame";
        return {};
    }

    const auto info = inspect(repo_root);
    if (!info.can_run_in_current_cpp_runtime) {
        error_message = info.message;
        return {};
    }

    const YoloModelInput prepared = prepare_yolo_model_input(frame, config_);
    if (prepared.height <= 0 || prepared.width <= 0 || prepared.values.empty()) {
        error_message = "Failed to prepare YOLO model input";
        return {};
    }

#ifdef FRIDGE_USE_ONNXRUNTIME
    std::string ort_error;
    YoloOnnxOutput ort_output = run_onnxruntime_model(info.resolved_model_path, config_, prepared, ort_error);
    if (ort_error.empty()) {
        return ort_output;
    }
    error_message = ort_error;
    return {};
#endif

#ifdef FRIDGE_USE_OPENCV
    const int blob_sizes[] = {1, prepared.channels, prepared.height, prepared.width};
    cv::Mat blob(4, blob_sizes, CV_32F);
    std::copy(prepared.values.begin(), prepared.values.end(), reinterpret_cast<float*>(blob.data));

    try {
        cv::dnn::Net net = cv::dnn::readNetFromONNX(info.resolved_model_path.string());
        if (net.empty()) {
            error_message = "OpenCV DNN returned an empty network for model: " + info.resolved_model_path.string();
            return {};
        }

        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        net.setInput(blob);

        std::vector<cv::Mat> outputs;
        const std::vector<cv::String> output_names = net.getUnconnectedOutLayersNames();
        if (output_names.empty()) {
            outputs.push_back(net.forward());
        } else {
            net.forward(outputs, output_names);
        }
        if (outputs.empty()) {
            error_message = "OpenCV DNN did not return any output tensors";
            return {};
        }

        return build_normalized_output(outputs, config_, error_message);
    } catch (const cv::Exception& ex) {
        error_message = "OpenCV DNN inference failed: " + std::string(ex.what());
        return {};
    }
#else
    (void)repo_root;
#ifdef FRIDGE_USE_ONNXRUNTIME
    error_message = ort_error;
#else
    error_message =
        "YOLO ONNX inference requires an OpenCV-enabled build with DNN support.";
#endif
    return {};
#endif
}

}  // namespace fridge
