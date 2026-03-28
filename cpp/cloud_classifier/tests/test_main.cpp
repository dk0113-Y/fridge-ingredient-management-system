#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fine_grained_recognizer_client.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool test_mock_uses_filename_hint() {
    fridge::cloud::FineGrainedRecognizerClient client;
    const auto result = client.recognizeCrop(
        "python/cloud_classifier/examples/apple_crop.ppm",
        "fruit",
        {"orange", "apple", "lemon"}
    );

    return expect(
        result.name == "apple" &&
        result.category == "fruit" &&
        result.provider == "mock" &&
        !result.is_unknown &&
        result.confidence > 0.9 &&
        !client.lastCallLog().request_id.empty() &&
        client.lastCallLog().provider == "mock" &&
        client.lastCallLog().coarse_class == "fruit" &&
        client.lastCallLog().candidate_count == 3 &&
        client.lastCallLog().success &&
        client.lastCallLog().parse_success,
        "mock client should use the image filename hint for deterministic recognition"
    );
}

bool test_mock_returns_unknown_for_empty_candidates() {
    fridge::cloud::FineGrainedRecognizerClient client;
    const auto result = client.recognizeCrop("crop.jpg", "fruit", {});

    return expect(
        result.is_unknown &&
        result.name == "unknown" &&
        result.provider == "mock",
        "mock client should return an unknown result when no candidate labels are provided"
    );
}

bool test_config_json_is_loaded() {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "fridge_cloud_classifier_config_test.json";
    {
        std::ofstream output(temp_path);
        output
            << "{\n"
            << "  \"provider\": \"mock\",\n"
            << "  \"endpoint\": \"https://example.invalid/fine-grained\",\n"
            << "  \"api_key\": \"demo-key\",\n"
            << "  \"model_name\": \"generic-fg-v2\",\n"
            << "  \"timeout_ms\": 2300,\n"
            << "  \"max_retries\": 3,\n"
            << "  \"enable_base64_image\": true\n"
            << "}\n";
    }

    fridge::cloud::RecognizerClientConfig config;
    std::string error_message;
    const bool ok = fridge::cloud::load_recognizer_client_config(temp_path, config, error_message);
    std::filesystem::remove(temp_path);

    return expect(
        ok &&
        config.provider == "mock" &&
        config.endpoint == "https://example.invalid/fine-grained" &&
        config.api_key == "demo-key" &&
        config.model_name == "generic-fg-v2" &&
        config.timeout_ms == 2300 &&
        config.max_retries == 3 &&
        config.enable_base64_image,
        "cloud classifier JSON config should load provider-neutral settings"
    );
}

bool test_result_serializes_to_required_json_fields() {
    const fridge::cloud::RecognitionResult result{
        "apple",
        "fruit",
        0.94,
        "Mock test result",
        false,
        "mock"
    };

    const nlohmann::json json_value = result;
    return expect(
        json_value.contains("name") &&
        json_value.contains("category") &&
        json_value.contains("confidence") &&
        json_value.contains("reason") &&
        json_value.contains("is_unknown") &&
        json_value.contains("provider"),
        "recognition result should serialize with the required JSON fields"
    );
}

bool test_call_log_is_sanitized() {
    fridge::cloud::FineGrainedRecognizerClient client(
        fridge::cloud::RecognizerClientConfig{
            "mock",
            "https://example.invalid/fine-grained",
            "secret-token",
            "generic-fg-v2",
            1500,
            2,
            true
        }
    );
    client.recognizeCrop("python/cloud_classifier/examples/apple_crop.ppm", "fruit", {"apple", "orange"});

    const nlohmann::json log_json = client.lastCallLog();
    return expect(
        log_json.contains("request_id") &&
        log_json.contains("provider") &&
        log_json.contains("coarse_class") &&
        log_json.contains("candidate_count") &&
        log_json.contains("latency_ms") &&
        log_json.contains("success") &&
        log_json.contains("parse_success") &&
        !log_json.contains("api_key") &&
        !log_json.contains("endpoint"),
        "call log should expose only sanitized operational fields"
    );
}

}  // namespace

int main() {
    const std::vector<std::function<bool()>> tests = {
        test_mock_uses_filename_hint,
        test_mock_returns_unknown_for_empty_candidates,
        test_config_json_is_loaded,
        test_result_serializes_to_required_json_fields,
        test_call_log_is_sanitized,
    };

    int failed = 0;
    for (const auto& test : tests) {
        if (!test()) {
            ++failed;
        }
    }

    if (failed > 0) {
        std::cerr << failed << " cloud classifier C++ tests failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All cloud classifier C++ tests passed.\n";
    return EXIT_SUCCESS;
}
