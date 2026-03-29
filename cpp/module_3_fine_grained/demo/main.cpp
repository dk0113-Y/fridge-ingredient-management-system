#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fine_grained_recognizer_client.hpp"

namespace {

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

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

std::vector<std::string> split_candidates(const std::string& text) {
    std::vector<std::string> labels;
    std::string current;
    for (char ch : text) {
        if (ch == ',') {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                labels.push_back(trimmed);
            }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    const std::string trimmed = trim(current);
    if (!trimmed.empty()) {
        labels.push_back(trimmed);
    }
    return labels;
}

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  fridge_cloud_classifier_demo --image <path> --coarse-class <name> --candidates <a,b,c>"
        << " [--config <path>]\n\n"
        << "Example:\n"
        << "  fridge_cloud_classifier_demo --image cpp/module_3_fine_grained/examples/apple_crop.ppm"
        << " --coarse-class fruit --candidates apple,orange,lemon --config cpp/configs/module_3_fine_grained.cfg\n";
}

std::filesystem::path resolve_default_config_path() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "configs" / "module_3_fine_grained.cfg",
        std::filesystem::current_path() / "configs" / "module_3_fine_grained.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_3_fine_grained.cfg",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return cpp_source_dir() / "configs" / "module_3_fine_grained.cfg";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string image_path;
        std::string coarse_class;
        std::string candidates_text;
        std::filesystem::path config_path = resolve_default_config_path();

        for (int index = 1; index < argc; ++index) {
            const std::string arg = argv[index];
            auto require_value = [&](const std::string& option_name) -> std::string {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value for " + option_name);
                }
                ++index;
                return argv[index];
            };

            if (arg == "--image") {
                image_path = require_value("--image");
            } else if (arg == "--coarse-class") {
                coarse_class = require_value("--coarse-class");
            } else if (arg == "--candidates") {
                candidates_text = require_value("--candidates");
            } else if (arg == "--config") {
                config_path = require_value("--config");
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("Unknown argument: " + arg);
            }
        }

        if (image_path.empty()) {
            throw std::invalid_argument("--image is required");
        }
        if (!std::filesystem::exists(image_path)) {
            throw std::invalid_argument("Image path does not exist: " + image_path);
        }
        if (!std::filesystem::is_regular_file(image_path)) {
            throw std::invalid_argument("Image path is not a file: " + image_path);
        }
        if (coarse_class.empty()) {
            throw std::invalid_argument("--coarse-class is required");
        }

        const std::vector<std::string> candidate_labels = split_candidates(candidates_text);
        if (candidate_labels.empty()) {
            throw std::invalid_argument("--candidates must contain at least one comma-separated label");
        }

        fridge::cloud::RecognizerClientConfig config;
        std::string error_message;
        if (!fridge::cloud::load_recognizer_client_config(config_path, config, error_message)) {
            throw std::runtime_error(error_message);
        }

        fridge::cloud::FineGrainedRecognizerClient client(config);
        const auto result = client.recognizeCrop(image_path, coarse_class, candidate_labels);
        std::cout << nlohmann::json(result).dump(2) << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Cloud classifier demo failed: " << ex.what() << "\n";
        print_usage();
        return 1;
    }
}
