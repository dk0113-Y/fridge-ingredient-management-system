#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fridge::cloud {

struct RecognitionResult {
    std::string name = "unknown";
    std::string category = "unknown";
    double confidence = 0.0;
    std::string reason;
    bool is_unknown = true;
    std::string provider = "mock";
};

struct RecognitionCallLog {
    std::string request_id;
    std::string provider = "mock";
    std::string coarse_class = "unknown";
    int candidate_count = 0;
    long latency_ms = 0;
    bool success = false;
    bool parse_success = false;
};

inline void to_json(nlohmann::json& json_value, const RecognitionResult& result) {
    json_value = nlohmann::json{
        {"name", result.name},
        {"category", result.category},
        {"confidence", result.confidence},
        {"reason", result.reason},
        {"is_unknown", result.is_unknown},
        {"provider", result.provider},
    };
}

inline void from_json(const nlohmann::json& json_value, RecognitionResult& result) {
    result.name = json_value.value("name", std::string("unknown"));
    result.category = json_value.value("category", std::string("unknown"));
    result.confidence = json_value.value("confidence", 0.0);
    result.reason = json_value.value("reason", std::string());
    result.is_unknown = json_value.value("is_unknown", true);
    result.provider = json_value.value("provider", std::string("mock"));
}

inline void to_json(nlohmann::json& json_value, const RecognitionCallLog& call_log) {
    json_value = nlohmann::json{
        {"request_id", call_log.request_id},
        {"provider", call_log.provider},
        {"coarse_class", call_log.coarse_class},
        {"candidate_count", call_log.candidate_count},
        {"latency_ms", call_log.latency_ms},
        {"success", call_log.success},
        {"parse_success", call_log.parse_success},
    };
}

inline void from_json(const nlohmann::json& json_value, RecognitionCallLog& call_log) {
    call_log.request_id = json_value.value("request_id", std::string());
    call_log.provider = json_value.value("provider", std::string("mock"));
    call_log.coarse_class = json_value.value("coarse_class", std::string("unknown"));
    call_log.candidate_count = json_value.value("candidate_count", 0);
    call_log.latency_ms = json_value.value("latency_ms", 0L);
    call_log.success = json_value.value("success", false);
    call_log.parse_success = json_value.value("parse_success", false);
}

struct RecognizerClientConfig {
    std::string provider = "mock";
    std::string endpoint;
    std::string api_key;
    std::string model_name;
    long timeout_ms = 15000;
    int max_retries = 0;
    bool enable_base64_image = false;
};

bool load_recognizer_client_config(
    const std::filesystem::path& config_path,
    RecognizerClientConfig& config,
    std::string& error_message
);

class FineGrainedRecognizerClient {
public:
    explicit FineGrainedRecognizerClient(RecognizerClientConfig config = {});

    RecognitionResult recognizeCrop(
        const std::string& image_path,
        const std::string& coarse_class,
        const std::vector<std::string>& candidate_labels
    ) const;

    const RecognitionCallLog& lastCallLog() const;

private:
    RecognitionResult recognize_mock(
        const std::string& image_path,
        const std::string& coarse_class,
        const std::vector<std::string>& candidate_labels
    ) const;

    RecognitionResult recognize_remote(
        const std::string& image_path,
        const std::string& coarse_class,
        const std::vector<std::string>& candidate_labels,
        const std::string& request_id
    ) const;

    RecognizerClientConfig config_;
    mutable RecognitionCallLog last_call_log_;
};

}  // namespace fridge::cloud
