#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "fine_grained_recognizer_client.hpp"

namespace {

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
        cpp_source_dir() / "configs" / "module_3_fine_grained.cfg",
        std::filesystem::current_path() / "configs" / "module_3_fine_grained.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_3_fine_grained.cfg",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "configs" / "module_3_fine_grained.cfg";
}

std::filesystem::path resolve_example_image() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "module_3_fine_grained" / "examples" / "apple_crop.ppm",
        std::filesystem::current_path() / "module_3_fine_grained" / "examples" / "apple_crop.ppm",
        std::filesystem::current_path() / "cpp" / "module_3_fine_grained" / "examples" / "apple_crop.ppm",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "module_3_fine_grained" / "examples" / "apple_crop.ppm";
}

bool debug_mock_recognition() {
    fridge::cloud::RecognizerClientConfig config;
    std::string error_message;
    if (!fridge::cloud::load_recognizer_client_config(resolve_config_path(), config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::cloud::FineGrainedRecognizerClient client(config);
    const auto result = client.recognizeCrop(
        resolve_example_image().string(),
        "fruit",
        {"orange", "apple", "banana"}
    );

    std::cout
        << "module_3_debug: name=" << result.name
        << " provider=" << result.provider
        << " confidence=" << result.confidence << "\n";

    return expect(result.name == "apple", "module 3 mock recognizer should infer apple from filename hint") &&
           expect(result.provider == "mock", "module 3 debug should use mock provider by default") &&
           expect(config.llm_confidence_threshold > 0.0, "module 3 cfg should expose llm_confidence_threshold") &&
           expect(!config.prompt_template.empty(), "module 3 cfg should expose prompt_template");
}

}  // namespace

int main() {
    if (!debug_mock_recognition()) {
        return EXIT_FAILURE;
    }

    std::cout << "module_3_debug passed\n";
    return EXIT_SUCCESS;
}
