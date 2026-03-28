#include "fine_grained_recognizer_client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#if FRIDGE_CLOUD_CLASSIFIER_HAS_CURL
#include <curl/curl.h>
#endif

namespace fridge::cloud {

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

std::string to_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    return value;
}

std::string normalize_text(const std::string& value) {
    return trim(value);
}

std::string canonical_text(const std::string& value) {
    return to_lower(normalize_text(value));
}

std::string canonical_provider(const std::string& value) {
    std::string provider = canonical_text(value);
    std::replace(provider.begin(), provider.end(), '-', '_');
    return provider;
}

std::string tokenize_alnum(const std::string& value) {
    std::string tokenized;
    tokenized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            tokenized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return tokenized;
}

std::vector<std::string> normalize_labels(const std::vector<std::string>& candidate_labels) {
    std::vector<std::string> labels;
    std::unordered_set<std::string> seen;
    for (const auto& raw_label : candidate_labels) {
        const std::string label = normalize_text(raw_label);
        if (label.empty()) {
            continue;
        }

        const std::string canonical = canonical_text(label);
        if (seen.find(canonical) != seen.end()) {
            continue;
        }

        seen.insert(canonical);
        labels.push_back(label);
    }
    return labels;
}

RecognitionResult parse_result_payload(const nlohmann::json& payload) {
    return payload.get<RecognitionResult>();
}

bool is_unknown_label(const std::string& label) {
    static const std::unordered_set<std::string> unknown_tokens = {
        "unknown",
        "other",
        "others",
        "misc",
        "miscellaneous",
    };
    return unknown_tokens.find(canonical_text(label)) != unknown_tokens.end();
}

std::string generate_request_id() {
    static std::random_device device;
    static std::mt19937_64 generator(device());
    static std::uniform_int_distribution<unsigned long long> distribution;

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::ostringstream stream;
    stream << std::hex << now_ms << distribution(generator);
    return stream.str();
}

std::string read_file_binary(const std::filesystem::path& file_path) {
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open image file for request payload: " + file_path.string());
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::string base64_encode(const std::string& input) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((input.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= input.size()) {
        const auto b0 = static_cast<unsigned char>(input[index]);
        const auto b1 = static_cast<unsigned char>(input[index + 1]);
        const auto b2 = static_cast<unsigned char>(input[index + 2]);
        encoded.push_back(table[(b0 >> 2) & 0x3F]);
        encoded.push_back(table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
        encoded.push_back(table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)]);
        encoded.push_back(table[b2 & 0x3F]);
        index += 3;
    }

    const std::size_t remaining = input.size() - index;
    if (remaining == 1) {
        const auto b0 = static_cast<unsigned char>(input[index]);
        encoded.push_back(table[(b0 >> 2) & 0x3F]);
        encoded.push_back(table[(b0 & 0x03) << 4]);
        encoded.push_back('=');
        encoded.push_back('=');
    } else if (remaining == 2) {
        const auto b0 = static_cast<unsigned char>(input[index]);
        const auto b1 = static_cast<unsigned char>(input[index + 1]);
        encoded.push_back(table[(b0 >> 2) & 0x3F]);
        encoded.push_back(table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
        encoded.push_back(table[(b1 & 0x0F) << 2]);
        encoded.push_back('=');
    }

    return encoded;
}

RecognitionCallLog make_call_log(
    const std::string& request_id,
    const std::string& provider,
    const std::string& coarse_class,
    int candidate_count,
    long latency_ms,
    bool success,
    bool parse_success
) {
    return RecognitionCallLog{
        request_id,
        provider,
        coarse_class,
        candidate_count,
        latency_ms,
        success,
        parse_success,
    };
}

#if FRIDGE_CLOUD_CLASSIFIER_HAS_CURL
void ensure_curl_global_init() {
    static const bool initialized = []() {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();

    if (!initialized) {
        throw std::runtime_error("Failed to initialize libcurl.");
    }
}

std::size_t curl_write_callback(char* data, std::size_t size, std::size_t count, void* user_data) {
    const std::size_t bytes = size * count;
    auto* output = static_cast<std::string*>(user_data);
    output->append(data, bytes);
    return bytes;
}

class CurlHeaders {
public:
    CurlHeaders() = default;

    ~CurlHeaders() {
        if (headers_ != nullptr) {
            curl_slist_free_all(headers_);
        }
    }

    CurlHeaders(const CurlHeaders&) = delete;
    CurlHeaders& operator=(const CurlHeaders&) = delete;

    void append(const std::string& header) {
        curl_slist* updated = curl_slist_append(headers_, header.c_str());
        if (updated == nullptr) {
            throw std::runtime_error("Failed to append HTTP header for cloud recognizer request.");
        }
        headers_ = updated;
    }

    curl_slist* get() const {
        return headers_;
    }

private:
    curl_slist* headers_ = nullptr;
};
#endif

}  // namespace

bool load_recognizer_client_config(
    const std::filesystem::path& config_path,
    RecognizerClientConfig& config,
    std::string& error_message
) {
    std::ifstream input(config_path);
    if (!input) {
        error_message = "Failed to open cloud classifier config file: " + config_path.string();
        return false;
    }

    try {
        const nlohmann::json json_value = nlohmann::json::parse(input);

        if (json_value.contains("provider")) {
            config.provider = canonical_provider(json_value.at("provider").get<std::string>());
        }
        if (json_value.contains("endpoint")) {
            config.endpoint = normalize_text(json_value.at("endpoint").get<std::string>());
        }
        if (json_value.contains("api_key")) {
            config.api_key = normalize_text(json_value.at("api_key").get<std::string>());
        }
        if (json_value.contains("model_name")) {
            config.model_name = normalize_text(json_value.at("model_name").get<std::string>());
        }
        if (json_value.contains("timeout_ms")) {
            config.timeout_ms = json_value.at("timeout_ms").get<long>();
        } else if (json_value.contains("timeout_seconds")) {
            const double timeout_seconds = json_value.at("timeout_seconds").get<double>();
            config.timeout_ms = static_cast<long>(timeout_seconds * 1000.0);
        }
        if (json_value.contains("max_retries")) {
            config.max_retries = json_value.at("max_retries").get<int>();
        }
        if (json_value.contains("enable_base64_image")) {
            config.enable_base64_image = json_value.at("enable_base64_image").get<bool>();
        }

        if (config.provider.empty()) {
            config.provider = "mock";
        }
        if (config.timeout_ms <= 0) {
            error_message = "cloud classifier config timeout must be positive";
            return false;
        }
        if (config.max_retries < 0) {
            error_message = "cloud classifier config max_retries must be greater than or equal to 0";
            return false;
        }
    } catch (const std::exception& ex) {
        error_message = "Failed to parse cloud classifier config JSON: " + std::string(ex.what());
        return false;
    }

    return true;
}

FineGrainedRecognizerClient::FineGrainedRecognizerClient(RecognizerClientConfig config)
    : config_(std::move(config)) {
    config_.provider = canonical_provider(config_.provider);
    if (config_.provider.empty()) {
        config_.provider = "mock";
    }
    if (config_.timeout_ms <= 0) {
        config_.timeout_ms = 15000;
    }
    if (config_.max_retries < 0) {
        config_.max_retries = 0;
    }
    last_call_log_.provider = config_.provider;
}

const RecognitionCallLog& FineGrainedRecognizerClient::lastCallLog() const {
    return last_call_log_;
}

RecognitionResult FineGrainedRecognizerClient::recognizeCrop(
    const std::string& image_path,
    const std::string& coarse_class,
    const std::vector<std::string>& candidate_labels
) const {
    const std::string request_id = generate_request_id();
    const auto started_at = std::chrono::steady_clock::now();
    const std::string normalized_coarse_class = normalize_text(coarse_class).empty() ? "unknown" : normalize_text(coarse_class);
    const int candidate_count = static_cast<int>(normalize_labels(candidate_labels).size());
    bool success = false;
    bool parse_success = false;

    try {
        if (normalize_text(image_path).empty()) {
            throw std::invalid_argument("image_path cannot be empty");
        }
        if (normalize_text(coarse_class).empty()) {
            throw std::invalid_argument("coarse_class cannot be empty");
        }

        RecognitionResult result;
        if (config_.provider == "mock") {
            result = recognize_mock(image_path, normalized_coarse_class, candidate_labels);
        } else {
            result = recognize_remote(image_path, normalized_coarse_class, candidate_labels, request_id);
        }

        parse_success = true;
        success = true;

        const auto latency_ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at
        ).count());
        last_call_log_ = make_call_log(
            request_id,
            config_.provider,
            normalized_coarse_class,
            candidate_count,
            latency_ms,
            success,
            parse_success
        );
        return result;
    } catch (...) {
        const auto latency_ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at
        ).count());
        last_call_log_ = make_call_log(
            request_id,
            config_.provider,
            normalized_coarse_class,
            candidate_count,
            latency_ms,
            success,
            parse_success
        );
        throw;
    }
}

RecognitionResult FineGrainedRecognizerClient::recognize_mock(
    const std::string& image_path,
    const std::string& coarse_class,
    const std::vector<std::string>& candidate_labels
) const {
    const std::vector<std::string> labels = normalize_labels(candidate_labels);
    const std::string category = normalize_text(coarse_class).empty() ? "unknown" : normalize_text(coarse_class);
    const std::string filename_token = tokenize_alnum(std::filesystem::path(image_path).stem().string());

    if (labels.empty()) {
        return parse_result_payload(nlohmann::json{
            {"name", "unknown"},
            {"category", category},
            {"confidence", 0.0},
            {"reason", "No candidate labels were provided to the mock C++ recognizer."},
            {"is_unknown", true},
            {"provider", config_.provider},
        });
    }

    for (const auto& label : labels) {
        const std::string label_token = tokenize_alnum(label);
        if (!label_token.empty() && !filename_token.empty() && filename_token.find(label_token) != std::string::npos) {
            return parse_result_payload(nlohmann::json{
                {"name", label},
                {"category", category},
                {"confidence", 0.94},
                {"reason", "Matched candidate label from the image path filename hint."},
                {"is_unknown", false},
                {"provider", config_.provider},
            });
        }
    }

    if (labels.size() == 1) {
        if (is_unknown_label(labels.front())) {
            return parse_result_payload(nlohmann::json{
                {"name", "unknown"},
                {"category", category},
                {"confidence", 0.0},
                {"reason", "Only candidate label was an unknown placeholder."},
                {"is_unknown", true},
                {"provider", config_.provider},
            });
        }
        return parse_result_payload(nlohmann::json{
            {"name", labels.front()},
            {"category", category},
            {"confidence", 0.90},
            {"reason", "Only one candidate label was provided."},
            {"is_unknown", false},
            {"provider", config_.provider},
        });
    }

    if (canonical_text(category) == "unknown") {
        return parse_result_payload(nlohmann::json{
            {"name", "unknown"},
            {"category", "unknown"},
            {"confidence", 0.0},
            {"reason", "Coarse class is unknown, so the mock C++ recognizer will not guess."},
            {"is_unknown", true},
            {"provider", config_.provider},
        });
    }

    if (is_unknown_label(labels.front())) {
        return parse_result_payload(nlohmann::json{
            {"name", "unknown"},
            {"category", category},
            {"confidence", 0.0},
            {"reason", "First candidate label resolved to an unknown placeholder."},
            {"is_unknown", true},
            {"provider", config_.provider},
        });
    }

    return parse_result_payload(nlohmann::json{
        {"name", labels.front()},
        {"category", category},
        {"confidence", 0.78},
        {"reason", "Mock C++ recognizer deterministically selected the first candidate label."},
        {"is_unknown", false},
        {"provider", config_.provider},
    });
}

RecognitionResult FineGrainedRecognizerClient::recognize_remote(
    const std::string& image_path,
    const std::string& coarse_class,
    const std::vector<std::string>& candidate_labels,
    const std::string& request_id
) const {
#if FRIDGE_CLOUD_CLASSIFIER_HAS_CURL
    if (config_.endpoint.empty()) {
        throw std::runtime_error("Cloud recognizer endpoint is empty in config.");
    }

    ensure_curl_global_init();

    std::string last_error;
    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
        try {
            nlohmann::json request_payload = {
                {"request_id", request_id},
                {"coarse_class", coarse_class},
                {"candidate_labels", candidate_labels},
            };
            if (!config_.model_name.empty()) {
                request_payload["model_name"] = config_.model_name;
            }
            if (config_.enable_base64_image) {
                request_payload["image_base64"] = base64_encode(read_file_binary(image_path));
            } else {
                request_payload["image_path"] = image_path;
            }

            const std::string request_body = request_payload.dump();
            std::string response_body;

            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
            if (!curl) {
                throw std::runtime_error("Failed to create libcurl easy handle.");
            }

            CurlHeaders headers;
            headers.append("Content-Type: application/json");
            headers.append("Accept: application/json");
            headers.append("X-Request-Id: " + request_id);
            if (!config_.api_key.empty()) {
                headers.append("Authorization: Bearer " + config_.api_key);
            }

            curl_easy_setopt(curl.get(), CURLOPT_URL, config_.endpoint.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request_body.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, config_.timeout_ms);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_callback);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

            const CURLcode perform_code = curl_easy_perform(curl.get());
            if (perform_code != CURLE_OK) {
                throw std::runtime_error("libcurl request failed: " + std::string(curl_easy_strerror(perform_code)));
            }

            long status_code = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code);
            if (status_code < 200 || status_code >= 300) {
                std::ostringstream stream;
                stream << "Cloud recognizer HTTP request failed with status " << status_code;
                if (!response_body.empty()) {
                    stream << " and body: " << response_body;
                }
                throw std::runtime_error(stream.str());
            }

            RecognitionResult result = nlohmann::json::parse(response_body).get<RecognitionResult>();
            if (result.provider.empty()) {
                result.provider = config_.provider;
            }
            return result;
        } catch (const std::exception& ex) {
            last_error = ex.what();
            if (attempt >= config_.max_retries) {
                throw;
            }
        }
    }
    throw std::runtime_error("Cloud recognizer request failed after retries: " + last_error);
#else
    (void)image_path;
    (void)coarse_class;
    (void)candidate_labels;
    (void)request_id;
    throw std::runtime_error(
        "Remote HTTPS mode requires libcurl development files at build time. Rebuild with libcurl available or use provider=mock."
    );
#endif
}

}  // namespace fridge::cloud
