#include "service_config.hpp"

#include <cctype>
#include <fstream>
#include <string>

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

}  // namespace

bool load_local_service_config(
    const std::filesystem::path& config_path,
    LocalServiceConfig& config,
    std::string& error_message
) {
    std::ifstream input(config_path);
    if (!input) {
        error_message = "Failed to open local service config file: " + config_path.string();
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
        if (key == "service_name") {
            config.service_name = value;
            continue;
        }
        if (key == "bind_host") {
            config.bind_host = value;
            continue;
        }
        if (key == "port") {
            if (!parse_int_value(value, config.port)) {
                error_message = "Invalid port at line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        error_message = "Unknown local service config key: " + key + " at line " + std::to_string(line_number);
        return false;
    }

    return true;
}

}  // namespace fridge
