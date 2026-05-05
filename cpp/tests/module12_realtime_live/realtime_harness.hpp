#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "types.hpp"

namespace fridge::live_test {

enum class Module2Mode {
    Mock,
    RealOnnxRuntime
};

std::string to_string(Module2Mode mode);

struct LiveHarnessOptions {
    std::string device = "/dev/video0";
    std::string bind_host = "0.0.0.0";
    std::string public_host = "auto";
    int port = 8080;
    std::filesystem::path module1_config_path;
    std::filesystem::path module2_config_path;
    std::filesystem::path service_config_path;
    std::filesystem::path output_root;
    std::filesystem::path latest_run_manifest_path;
    std::filesystem::path sqlite_db_path;
    std::string case_id = "TC02_live_put_in";
    std::string mock_coarse_class = "packaged_food";
    Module2Mode module2_mode = Module2Mode::Mock;
    std::optional<BoundingBox> roi_override;
    bool preview_only = false;
    bool capture_only = false;
    int capture_width = 1280;
    int capture_height = 720;
    int capture_fps = 15;
    int preview_jpeg_quality = 80;
    int pre_event_frame_count = 12;
    int max_event_frame_count = 90;
    int cooldown_frame_count = 8;
    int stop_after_events = 1;
    int duration_seconds = 0;
    bool enable_sqlite_persistence = false;
};

class Module12RealtimeHarness {
public:
    explicit Module12RealtimeHarness(LiveHarnessOptions options);
    ~Module12RealtimeHarness();

    bool run(std::string& error_message);
    void request_stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    LiveHarnessOptions options_;
};

}  // namespace fridge::live_test
