#include "realtime_harness.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path resolve_repo_root() {
    const std::filesystem::path source_dir = cpp_source_dir();
    if (source_dir.filename() == "cpp") {
        return source_dir.parent_path();
    }
    return std::filesystem::current_path();
}

bool parse_int_value(const std::string& text, int& value) {
    std::size_t consumed = 0;
    value = std::stoi(text, &consumed);
    return consumed == text.size();
}

bool parse_roi_spec(const std::string& spec, fridge::BoundingBox& roi) {
    std::stringstream stream(spec);
    std::string token;
    std::vector<int> values;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            return false;
        }
        values.push_back(std::stoi(token));
    }

    if (values.size() != 4 || values[2] <= 0 || values[3] <= 0) {
        return false;
    }

    roi = fridge::BoundingBox{values[0], values[1], values[2], values[3]};
    return true;
}

std::optional<fridge::live_test::Module2Mode> parse_module2_mode(const std::string& value) {
    if (value == "mock") {
        return fridge::live_test::Module2Mode::Mock;
    }
    if (value == "real_onnx_runtime") {
        return fridge::live_test::Module2Mode::RealOnnxRuntime;
    }
    return std::nullopt;
}

void print_usage() {
    std::cout
        << "Usage: fridge_module12_realtime_live [options]\n"
        << "Options:\n"
        << "  --device <path_or_index>           Camera device, default /dev/video0\n"
        << "  --bind-host <ip>                   HTTP bind host, default 0.0.0.0\n"
        << "  --public-host <ip|auto>            Public host shown in URLs, default auto\n"
        << "  --port <number>                    HTTP port, default 8080\n"
        << "  --case-id <id>                     Test case id, default TC02_live_put_in\n"
        << "  --module2-mode <mock|real_onnx_runtime>\n"
        << "  --mock-class <coarse_class>        Mock YOLO class, default packaged_food\n"
        << "  --roi <x,y,width,height>           Override ROI for module 1\n"
        << "  --preview-only                     Only publish preview and status\n"
        << "  --capture-only                     Validate trigger and keyframe capture only\n"
        << "  --stop-after-events <n>            Stop after n events, 0 means keep running\n"
        << "  --duration-seconds <n>             Stop after n seconds, 0 means no timer\n"
        << "  --width <pixels>                   Capture width hint, default 1280\n"
        << "  --height <pixels>                  Capture height hint, default 720\n"
        << "  --fps <value>                      Capture fps hint, default 15\n"
        << "  --module1-config <path>\n"
        << "  --module2-config <path>\n"
        << "  --service-config <path>\n"
        << "  --output-root <path>\n"
        << "  --latest-run-manifest <path>\n"
        << "  --help\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path repo_root = resolve_repo_root();
        const std::filesystem::path live_dir =
            repo_root / "cpp" / "tests" / "module12_realtime_live";

        fridge::live_test::LiveHarnessOptions options;
        options.module1_config_path = live_dir / "configs" / "test_module_1_event_capture.cfg";
        options.module2_config_path = live_dir / "configs" / "test_module_2_yolo.cfg";
        options.service_config_path = live_dir / "configs" / "test_module_5_live.cfg";
        options.output_root = repo_root / "data" / "test_sessions" / "module12_realtime_live";
        options.latest_run_manifest_path = live_dir / "manifests" / "latest_run.json";

        for (int index = 1; index < argc; ++index) {
            const std::string token = argv[index];
            const auto require_value = [&](const std::string& name) -> std::string {
                if (index + 1 >= argc) {
                    throw std::runtime_error("Missing value after " + name);
                }
                return argv[++index];
            };

            if (token == "--help") {
                print_usage();
                return EXIT_SUCCESS;
            }
            if (token == "--device") {
                options.device = require_value(token);
                continue;
            }
            if (token == "--bind-host") {
                options.bind_host = require_value(token);
                continue;
            }
            if (token == "--public-host") {
                options.public_host = require_value(token);
                continue;
            }
            if (token == "--port") {
                const std::string value = require_value(token);
                if (!parse_int_value(value, options.port)) {
                    throw std::runtime_error("Invalid port: " + value);
                }
                continue;
            }
            if (token == "--case-id") {
                options.case_id = require_value(token);
                continue;
            }
            if (token == "--module2-mode") {
                const std::string value = require_value(token);
                const auto parsed = parse_module2_mode(value);
                if (!parsed.has_value()) {
                    throw std::runtime_error("Unsupported module2 mode: " + value);
                }
                options.module2_mode = *parsed;
                continue;
            }
            if (token == "--mock-class") {
                options.mock_coarse_class = require_value(token);
                continue;
            }
            if (token == "--roi") {
                fridge::BoundingBox roi;
                const std::string value = require_value(token);
                if (!parse_roi_spec(value, roi)) {
                    throw std::runtime_error("ROI must be x,y,width,height with positive size");
                }
                options.roi_override = roi;
                continue;
            }
            if (token == "--preview-only") {
                options.preview_only = true;
                continue;
            }
            if (token == "--capture-only") {
                options.capture_only = true;
                continue;
            }
            if (token == "--stop-after-events") {
                const std::string value = require_value(token);
                if (!parse_int_value(value, options.stop_after_events)) {
                    throw std::runtime_error("Invalid stop-after-events value: " + value);
                }
                continue;
            }
            if (token == "--duration-seconds") {
                const std::string value = require_value(token);
                if (!parse_int_value(value, options.duration_seconds)) {
                    throw std::runtime_error("Invalid duration-seconds value: " + value);
                }
                continue;
            }
            if (token == "--width") {
                const std::string value = require_value(token);
                if (!parse_int_value(value, options.capture_width)) {
                    throw std::runtime_error("Invalid width: " + value);
                }
                continue;
            }
            if (token == "--height") {
                const std::string value = require_value(token);
                if (!parse_int_value(value, options.capture_height)) {
                    throw std::runtime_error("Invalid height: " + value);
                }
                continue;
            }
            if (token == "--fps") {
                const std::string value = require_value(token);
                if (!parse_int_value(value, options.capture_fps)) {
                    throw std::runtime_error("Invalid fps: " + value);
                }
                continue;
            }
            if (token == "--module1-config") {
                options.module1_config_path = require_value(token);
                continue;
            }
            if (token == "--module2-config") {
                options.module2_config_path = require_value(token);
                continue;
            }
            if (token == "--service-config") {
                options.service_config_path = require_value(token);
                continue;
            }
            if (token == "--output-root") {
                options.output_root = require_value(token);
                continue;
            }
            if (token == "--latest-run-manifest") {
                options.latest_run_manifest_path = require_value(token);
                continue;
            }

            throw std::runtime_error("Unknown option: " + token);
        }

        fridge::live_test::Module12RealtimeHarness harness(options);
        std::string error_message;
        if (!harness.run(error_message)) {
            std::cerr << error_message << "\n";
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        print_usage();
        return EXIT_FAILURE;
    }
}
