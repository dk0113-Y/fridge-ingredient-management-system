#include "runtime_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

std::string normalize_value(std::string value) {
    value = trim(strip_inline_comment(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool parse_int_value(const std::string& text, int& value) {
    std::size_t consumed = 0;
    value = std::stoi(text, &consumed);
    return consumed == text.size();
}

bool parse_size_value(const std::string& text, std::size_t& value) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(text, &consumed);
    if (consumed != text.size()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_double_value(const std::string& text, double& value) {
    std::size_t consumed = 0;
    value = std::stod(text, &consumed);
    return consumed == text.size();
}

bool parse_roi_value(const std::string& text, BoundingBox& roi) {
    std::stringstream stream(text);
    std::string token;
    std::vector<int> values;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (token.empty()) {
            return false;
        }

        int parsed = 0;
        if (!parse_int_value(token, parsed)) {
            return false;
        }
        values.push_back(parsed);
    }

    if (values.size() != 4) {
        return false;
    }

    if (values[2] == 0 && values[3] == 0 && values[0] == 0 && values[1] == 0) {
        roi = BoundingBox{};
        return true;
    }

    if (values[2] <= 0 || values[3] <= 0) {
        return false;
    }

    roi = BoundingBox{values[0], values[1], values[2], values[3]};
    return true;
}

bool set_config_value(
    VisionPipelineConfig& config,
    const std::string& key,
    const std::string& value,
    std::string& error_message
) {
    try {
        if (key == "roi_id") {
            config.roi_id = value;
            return true;
        }

        if (key == "roi") {
            if (!parse_roi_value(value, config.motion_config.roi)) {
                error_message = "roi must be x,y,width,height, or 0,0,0,0 for full-frame mode";
                return false;
            }
            return true;
        }

        if (key == "pixel_delta_threshold") {
            return parse_int_value(value, config.motion_config.pixel_delta_threshold);
        }
        if (key == "min_region_area_pixels") {
            return parse_int_value(value, config.motion_config.min_region_area_pixels);
        }
        if (key == "stable_ratio_threshold") {
            return parse_double_value(value, config.frame_selector_config.stable_ratio_threshold);
        }
        if (key == "motion_ratio_threshold") {
            return parse_double_value(value, config.frame_selector_config.motion_ratio_threshold);
        }
        if (key == "black_frame_mean_threshold") {
            return parse_double_value(value, config.frame_selector_config.black_frame_mean_threshold);
        }
        if (key == "min_stable_run_frames") {
            return parse_size_value(value, config.frame_selector_config.min_stable_run_frames);
        }
        if (key == "no_change_ratio") {
            return parse_double_value(value, config.detector_config.no_change_ratio);
        }
        if (key == "event_ratio") {
            return parse_double_value(value, config.detector_config.event_ratio);
        }
        if (key == "partial_ratio") {
            return parse_double_value(value, config.detector_config.partial_ratio);
        }
        if (key == "signed_delta_threshold") {
            return parse_double_value(value, config.detector_config.signed_delta_threshold);
        }
        if (key == "dominant_polarity_threshold") {
            return parse_double_value(value, config.detector_config.dominant_polarity_threshold);
        }
        if (key == "reorg_balance_threshold") {
            return parse_double_value(value, config.detector_config.reorg_balance_threshold);
        }
        if (key == "background_like_threshold") {
            return parse_double_value(value, config.detector_config.background_like_threshold);
        }
        if (key == "region_direction_margin") {
            return parse_double_value(value, config.detector_config.region_direction_margin);
        }
        if (key == "reorg_region_threshold") {
            return parse_size_value(value, config.detector_config.reorg_region_threshold);
        }

        error_message = "Unknown config key: " + key;
        return false;
    } catch (const std::exception&) {
        error_message = "Invalid value for key: " + key;
        return false;
    }
}

}  // namespace

bool load_pipeline_config(
    const std::filesystem::path& config_path,
    VisionPipelineConfig& config,
    std::string& error_message
) {
    std::ifstream input(config_path);
    if (!input) {
        error_message = "Failed to open config file: " + config_path.string();
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
        const std::string value = normalize_value(normalized.substr(separator + 1));
        if (key.empty() || value.empty()) {
            error_message = "Invalid key/value at line " + std::to_string(line_number);
            return false;
        }

        std::string line_error;
        if (!set_config_value(config, key, value, line_error)) {
            if (line_error.empty()) {
                line_error = "Invalid value";
            }
            error_message = line_error + " at line " + std::to_string(line_number);
            return false;
        }
    }

    return true;
}

}  // namespace fridge
