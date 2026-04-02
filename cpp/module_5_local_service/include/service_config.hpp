#pragma once

#include <filesystem>
#include <string>

namespace fridge {

struct LocalServiceConfig {
    std::string service_name = "fridge_local_service";
    std::string bind_host = "127.0.0.1";
    std::string public_host;
    int port = 8080;
};

bool load_local_service_config(
    const std::filesystem::path& config_path,
    LocalServiceConfig& config,
    std::string& error_message
);

}  // namespace fridge
