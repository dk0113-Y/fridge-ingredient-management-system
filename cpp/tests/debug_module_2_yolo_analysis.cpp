#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "yolo_diff_analyzer.hpp"
#include "yolo_runtime.hpp"

namespace {

using fridge::GrayFrame;

GrayFrame make_frame(int width, int height, std::uint8_t fill, int index) {
    GrayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.index = index;
    frame.pixels.assign(static_cast<std::size_t>(width * height), fill);
    return frame;
}

void paint_box(GrayFrame& frame, int x0, int y0, int box_width, int box_height, std::uint8_t value) {
    for (int y = y0; y < y0 + box_height; ++y) {
        for (int x = x0; x < x0 + box_width; ++x) {
            frame.pixels[static_cast<std::size_t>(y * frame.width + x)] = value;
        }
    }
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path resolve_config_path() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "configs" / "module_2_yolo.cfg",
        std::filesystem::current_path() / "configs" / "module_2_yolo.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_2_yolo.cfg",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "configs" / "module_2_yolo.cfg";
}

std::filesystem::path resolve_runtime_config_path() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "configs" / "module_2_yolo.cfg",
        std::filesystem::current_path() / "configs" / "module_2_yolo.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_2_yolo.cfg",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "configs" / "module_2_yolo.cfg";
}

std::filesystem::path resolve_repo_root() {
    const std::filesystem::path source_dir = cpp_source_dir();
    if (source_dir.filename() == "cpp") {
        return source_dir.parent_path();
    }
    return std::filesystem::current_path();
}

bool debug_runtime_inspection() {
    fridge::YoloRuntimeConfig config;
    std::string error_message;
    if (!fridge::load_yolo_runtime_config(resolve_runtime_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    const fridge::YoloRuntime runtime(config);
    const auto info = runtime.inspect(resolve_repo_root());

    std::cout
        << "module_2_runtime: model=" << info.resolved_model_path.string()
        << " format=" << fridge::to_string(info.model_format)
        << " exists=" << info.model_exists
        << " runnable=" << info.can_run_in_current_cpp_runtime << "\n";

#if defined(FRIDGE_USE_ONNXRUNTIME) || defined(FRIDGE_USE_OPENCV)
    return expect(info.model_exists, "module 2 should find the configured YOLO model asset") &&
           expect(info.model_format == fridge::YoloModelFormat::Onnx, "module 2 should detect the configured asset as onnx format") &&
           expect(info.can_run_in_current_cpp_runtime, "native runtime should claim ONNX inference support when ONNX Runtime or OpenCV DNN is available") &&
           expect(
               info.message.find("ONNX Runtime") != std::string::npos ||
                   info.message.find("OpenCV DNN") != std::string::npos,
               "module 2 runtime message should explain which native ONNX backend is active"
           );
#else
    return expect(info.model_exists, "module 2 should find the configured YOLO model asset") &&
           expect(info.model_format == fridge::YoloModelFormat::Onnx, "module 2 should detect the configured asset as onnx format") &&
           expect(!info.can_run_in_current_cpp_runtime, "builds without ONNX Runtime or OpenCV DNN should not claim end-to-end ONNX inference support") &&
           expect(
               info.message.find("ONNX") != std::string::npos || info.message.find("onnx") != std::string::npos,
               "module 2 runtime message should explain that ONNX still needs a C++ inference backend"
           );
#endif
}

bool debug_module2_pipeline_on_onnx_outputs() {
    fridge::YoloAnalysisConfig analysis_config;
    std::string error_message;
    if (!fridge::load_yolo_analysis_config(resolve_config_path(), analysis_config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::YoloRuntimeConfig runtime_config;
    if (!fridge::load_yolo_runtime_config(resolve_runtime_config_path(), runtime_config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    const auto before = make_frame(16, 16, 140, 0);
    auto after = make_frame(16, 16, 140, 1);
    paint_box(after, 4, 4, 5, 5, 30);

    fridge::YoloModule2Pipeline pipeline(runtime_config, analysis_config);
    const auto prepared_input = pipeline.prepare_input(before);

    const fridge::YoloOnnxOutput before_output{
        0,
        runtime_config.output_columns,
        {}
    };
    const fridge::YoloOnnxOutput after_output{
        1,
        runtime_config.output_columns,
        {160.0F, 160.0F, 360.0F, 360.0F, 0.91F, 3.0F}
    };

    const auto put_in_result = pipeline.analyze_outputs(
        before,
        after,
        before_output,
        after_output,
        "module2_put_in",
        "before.jpg",
        "after.jpg",
        error_message
    );
    if (!error_message.empty()) {
        std::cerr << error_message << "\n";
        return false;
    }

    auto fruit_before = make_frame(16, 16, 140, 0);
    auto fruit_after = make_frame(16, 16, 140, 1);
    paint_box(fruit_before, 4, 4, 6, 6, 50);
    paint_box(fruit_after, 4, 4, 4, 4, 70);

    const fridge::YoloOnnxOutput partial_before_output{
        1,
        runtime_config.output_columns,
        {160.0F, 160.0F, 400.0F, 400.0F, 0.93F, 0.0F}
    };
    const fridge::YoloOnnxOutput partial_after_output{
        1,
        runtime_config.output_columns,
        {160.0F, 160.0F, 320.0F, 320.0F, 0.92F, 0.0F}
    };

    const auto partial_result = pipeline.analyze_outputs(
        fruit_before,
        fruit_after,
        partial_before_output,
        partial_after_output,
        "module2_partial",
        "before.jpg",
        "after.jpg",
        error_message
    );
    if (!error_message.empty()) {
        std::cerr << error_message << "\n";
        return false;
    }

    std::cout
        << "module_2_input: values=" << prepared_input.values.size()
        << " channels=" << prepared_input.channels
        << " height=" << prepared_input.height
        << " width=" << prepared_input.width << "\n"
        << "module_2_debug: put_in=" << fridge::to_string(put_in_result.event.event_type)
        << " partial=" << fridge::to_string(partial_result.event.event_type)
        << " crop_requests=" << partial_result.crop_requests.size() << "\n";

    return expect(
               prepared_input.values.size() ==
                   static_cast<std::size_t>(prepared_input.channels * prepared_input.height * prepared_input.width),
               "module 2 should prepare an NCHW input tensor for ONNX inference"
           ) &&
           expect(
               put_in_result.event.event_type == fridge::EventType::PutIn &&
               put_in_result.new_boxes.size() == 1,
               "module 2 should classify a single ONNX-decoded new detection as put_in"
           ) &&
           expect(
               partial_result.event.event_type == fridge::EventType::PartialTakeOutCandidate &&
               partial_result.event.need_user_confirm &&
               !partial_result.partial_candidates.empty(),
               "module 2 should classify matched ONNX-decoded fruit_vegetable changes as partial_take_out_candidate"
           );
}

}  // namespace

int main() {
    if (!debug_runtime_inspection()) {
        return EXIT_FAILURE;
    }

    if (!debug_module2_pipeline_on_onnx_outputs()) {
        return EXIT_FAILURE;
    }

    std::cout << "module_2_debug passed\n";
    return EXIT_SUCCESS;
}
