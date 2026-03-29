#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "types.hpp"
#include "yolo_diff_analyzer.hpp"

namespace fridge {

enum class YoloModelFormat {
    Unknown,
    Pt,
    Onnx
};

std::string to_string(YoloModelFormat format);

struct YoloRuntimeConfig {
    std::filesystem::path model_path = "models/best.onnx";
    std::vector<std::string> class_names = {
        "fruit_vegetable",
        "meat_egg_fresh",
        "drink",
        "packaged_food",
    };
    int input_width = 640;
    int input_height = 640;
    double score_threshold = 0.25;
    double nms_threshold = 0.45;
    int output_columns = 6;
};

struct YoloRuntimeInfo {
    std::filesystem::path resolved_model_path;
    YoloModelFormat model_format = YoloModelFormat::Unknown;
    bool model_exists = false;
    bool can_run_in_current_cpp_runtime = false;
    std::vector<std::string> class_names;
    int input_width = 640;
    int input_height = 640;
    double score_threshold = 0.25;
    double nms_threshold = 0.45;
    int output_columns = 6;
    std::string message;
};

struct YoloModelInput {
    int channels = 3;
    int height = 0;
    int width = 0;
    std::vector<float> values;
};

struct YoloOnnxOutput {
    int rows = 0;
    int columns = 6;
    std::vector<float> values;
};

bool load_yolo_runtime_config(
    const std::filesystem::path& config_path,
    YoloRuntimeConfig& config,
    std::string& error_message
);

YoloModelInput prepare_yolo_model_input(const GrayFrame& frame, const YoloRuntimeConfig& config);

std::vector<YoloDetection> decode_yolo_onnx_output(
    const YoloOnnxOutput& output,
    const GrayFrame& source_frame,
    const YoloRuntimeConfig& config,
    std::string& error_message
);

class YoloModule2Pipeline {
public:
    YoloModule2Pipeline(YoloRuntimeConfig runtime_config = {}, YoloAnalysisConfig analysis_config = {});

    YoloModelInput prepare_input(const GrayFrame& frame) const;

    std::vector<YoloDetection> decode_output(
        const YoloOnnxOutput& output,
        const GrayFrame& source_frame,
        std::string& error_message
    ) const;

    YoloDiffResult analyze_outputs(
        const GrayFrame& before_frame,
        const GrayFrame& after_frame,
        const YoloOnnxOutput& before_output,
        const YoloOnnxOutput& after_output,
        const std::string& session_id,
        const std::string& before_frame_path,
        const std::string& after_frame_path,
        std::string& error_message
    ) const;

private:
    YoloRuntimeConfig runtime_config_;
    YoloDiffAnalyzer analyzer_;
};

class YoloRuntime {
public:
    explicit YoloRuntime(YoloRuntimeConfig config = {});

    YoloRuntimeInfo inspect(const std::filesystem::path& repo_root) const;

private:
    YoloRuntimeConfig config_;
};

}  // namespace fridge
