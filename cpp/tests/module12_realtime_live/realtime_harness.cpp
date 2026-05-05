#include "realtime_harness.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef FRIDGE_USE_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

#include "debug_report.hpp"
#include "event_detector.hpp"
#include "frame_selector.hpp"
#include "inventory_engine.hpp"
#include "local_service.hpp"
#include "runtime_config.hpp"
#include "service_config.hpp"
#include "software_closure.hpp"
#include "stable_state_capture.hpp"
#include "video_io.hpp"
#include "yolo_runtime.hpp"

namespace fridge::live_test {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

struct FramePacket {
    GrayFrame gray_frame;
    std::vector<std::uint8_t> jpeg_bytes;
    int sequence = -1;
    std::string captured_at_utc;

    bool empty() const {
        return gray_frame.empty();
    }
};

struct SessionPaths {
    std::filesystem::path session_dir;
    std::filesystem::path preview_dir;
    std::filesystem::path stage1_dir;
    std::filesystem::path stage2_dir;
    std::filesystem::path final_dir;
    std::filesystem::path meta_dir;
    std::filesystem::path preview_latest_path;
    std::filesystem::path stage1_before_path;
    std::filesystem::path stage1_after_path;
    std::filesystem::path stage1_overlay_path;
    std::filesystem::path stage1_event_path;
    std::filesystem::path stage1_debug_path;
    std::filesystem::path stage2_detections_before_path;
    std::filesystem::path stage2_detections_after_path;
    std::filesystem::path stage2_detections_before_image_path;
    std::filesystem::path stage2_detections_after_image_path;
    std::filesystem::path stage2_result_path;
    std::filesystem::path stage2_crops_dir;
    std::filesystem::path final_event_path;
    std::filesystem::path final_report_path;
    std::filesystem::path inventory_response_path;
    std::filesystem::path events_response_path;
    std::filesystem::path pending_response_path;
    std::filesystem::path software_closure_report_path;
    std::filesystem::path live_capture_meta_path;
    std::filesystem::path run_manifest_path;
};

struct CropArtifactRecord {
    CropRequest request;
    std::string output_path;
};

struct Module2Execution {
    bool success = false;
    std::string failure_reason;
    YoloDiffResult result;
    std::vector<YoloDetection> before_detections;
    std::vector<YoloDetection> after_detections;
    std::vector<CropArtifactRecord> crop_artifacts;
};

struct LatestEventState {
    bool has_event = false;
    bool capture_valid = false;
    bool stage2_skipped = false;
    bool stage2_success = false;
    bool fallback_used = false;
    std::string fallback_reason;
    std::string stage2_failure_reason;
    std::string session_id;
    std::string case_id;
    std::string event_type = "none";
    std::string stage1_event_type = "none";
    std::string stage2_event_type = "skipped";
    std::string module2_mode = "mock";
    std::string pipeline_mode = "full_chain";
    std::string session_dir;
    std::string report_path;
    std::string stage2_result_path;
    std::string final_event_path;
    std::string software_closure_report_path;
    std::string crops_dir;
    std::string timestamp;
    std::size_t crop_artifact_count = 0;
};

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

std::filesystem::path resolve_inventory_config_path(const std::filesystem::path& repo_root) {
    const std::vector<std::filesystem::path> candidates = {
        repo_root / "cpp" / "configs" / "module_4_inventory.cfg",
        repo_root / "configs" / "module_4_inventory.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_4_inventory.cfg",
        std::filesystem::current_path() / "configs" / "module_4_inventory.cfg",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return repo_root / "cpp" / "configs" / "module_4_inventory.cfg";
}

#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required_size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        required_size,
        nullptr,
        nullptr
    );
    return result;
}
#endif

std::string path_to_utf8_string(const std::filesystem::path& path) {
#ifdef _WIN32
    return wide_to_utf8(path.generic_wstring());
#else
    return path.generic_string();
#endif
}

ColorFrame promote_gray_to_color(const GrayFrame& frame) {
    ColorFrame color_frame;
    if (frame.empty()) {
        return color_frame;
    }

    color_frame.width = frame.width;
    color_frame.height = frame.height;
    color_frame.index = frame.index;
    color_frame.pixels.resize(static_cast<std::size_t>(frame.width * frame.height * 3));
    for (std::size_t pixel_index = 0; pixel_index < frame.pixels.size(); ++pixel_index) {
        const std::uint8_t value = frame.pixels[pixel_index];
        const std::size_t base = pixel_index * 3;
        color_frame.pixels[base] = value;
        color_frame.pixels[base + 1] = value;
        color_frame.pixels[base + 2] = value;
    }
    return color_frame;
}

ColorFrame decode_color_frame(const FramePacket& packet) {
    if (packet.gray_frame.empty()) {
        return {};
    }

#ifdef FRIDGE_USE_OPENCV
    if (!packet.jpeg_bytes.empty()) {
        const cv::Mat encoded(1, static_cast<int>(packet.jpeg_bytes.size()), CV_8UC1, const_cast<std::uint8_t*>(packet.jpeg_bytes.data()));
        const cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
        if (!decoded.empty()) {
            ColorFrame color_frame;
            color_frame.width = decoded.cols;
            color_frame.height = decoded.rows;
            color_frame.index = packet.sequence;
            color_frame.pixels.assign(decoded.datastart, decoded.dataend);
            return color_frame;
        }
    }
#endif

    return promote_gray_to_color(packet.gray_frame);
}

bool write_color_debug_image(const ColorFrame& frame, const std::filesystem::path& path, std::string& error_message) {
    if (frame.empty()) {
        error_message = "Cannot write an empty color frame.";
        return false;
    }

    std::filesystem::create_directories(path.parent_path());

#ifdef FRIDGE_USE_OPENCV
    cv::Mat image(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.pixels.data()));
    const std::string extension = path.has_extension() ? path.extension().string() : ".jpg";
    std::vector<std::uint8_t> encoded_bytes;
    if (!cv::imencode(extension, image, encoded_bytes)) {
        error_message = "Failed to encode color image: " + path_to_utf8_string(path);
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error_message = "Failed to open output file: " + path_to_utf8_string(path);
        return false;
    }

    output.write(reinterpret_cast<const char*>(encoded_bytes.data()), static_cast<std::streamsize>(encoded_bytes.size()));
    if (!output) {
        error_message = "Failed to write encoded image: " + path_to_utf8_string(path);
        return false;
    }

    return true;
#else
    GrayFrame grayscale;
    grayscale.width = frame.width;
    grayscale.height = frame.height;
    grayscale.index = frame.index;
    grayscale.pixels.resize(static_cast<std::size_t>(frame.width * frame.height));
    for (std::size_t pixel_index = 0; pixel_index < grayscale.pixels.size(); ++pixel_index) {
        grayscale.pixels[pixel_index] = frame.pixels[pixel_index * 3];
    }
    return write_debug_image(grayscale, path, error_message);
#endif
}

ColorFrame build_overlay_color_frame(
    const ColorFrame& after_frame,
    const BoundingBox& roi,
    const std::vector<ChangeRegion>& change_regions
) {
    if (after_frame.empty()) {
        return {};
    }

#ifdef FRIDGE_USE_OPENCV
    ColorFrame overlay = after_frame;
    cv::Mat image(overlay.height, overlay.width, CV_8UC3, overlay.pixels.data());
    const cv::Scalar roi_color(0, 255, 0);
    const cv::Scalar region_color(0, 0, 255);

    if (roi.width > 0 && roi.height > 0 &&
        !(roi.x == 0 && roi.y == 0 && roi.width == after_frame.width && roi.height == after_frame.height)) {
        cv::rectangle(image, cv::Rect(roi.x, roi.y, roi.width, roi.height), roi_color, 2);
    }

    for (const auto& region : change_regions) {
        cv::rectangle(
            image,
            cv::Rect(region.box.x, region.box.y, region.box.width, region.box.height),
            region_color,
            2
        );
    }

    return overlay;
#else
    return after_frame;
#endif
}

BoundingBox clamp_box_to_color_frame(const BoundingBox& box, const ColorFrame& frame) {
    if (frame.empty()) {
        return {};
    }

    const int x0 = std::max(0, std::min(box.x, frame.width));
    const int y0 = std::max(0, std::min(box.y, frame.height));
    const int x1 = std::max(x0, std::min(box.x + box.width, frame.width));
    const int y1 = std::max(y0, std::min(box.y + box.height, frame.height));
    return BoundingBox{x0, y0, x1 - x0, y1 - y0};
}

ColorFrame crop_color_frame(const ColorFrame& source, const BoundingBox& requested_box) {
    ColorFrame cropped;
    const BoundingBox box = clamp_box_to_color_frame(requested_box, source);
    if (source.empty() || box.width <= 0 || box.height <= 0) {
        return cropped;
    }

    cropped.width = box.width;
    cropped.height = box.height;
    cropped.index = source.index;
    cropped.pixels.resize(static_cast<std::size_t>(box.width * box.height * 3));
    for (int y = 0; y < box.height; ++y) {
        const int source_row = box.y + y;
        const std::size_t source_offset = static_cast<std::size_t>((source_row * source.width + box.x) * 3);
        const std::size_t target_offset = static_cast<std::size_t>(y * box.width * 3);
        std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), static_cast<std::ptrdiff_t>(box.width * 3), cropped.pixels.begin() + static_cast<std::ptrdiff_t>(target_offset));
    }
    return cropped;
}

ColorFrame build_detection_overlay_frame(
    const ColorFrame& source_frame,
    const std::vector<YoloDetection>& detections
) {
    if (source_frame.empty()) {
        return {};
    }

    ColorFrame overlay = source_frame;
#ifdef FRIDGE_USE_OPENCV
    cv::Mat image(overlay.height, overlay.width, CV_8UC3, overlay.pixels.data());
    const cv::Scalar box_color(0, 255, 255);
    const cv::Scalar text_bg(0, 0, 0);
    const cv::Scalar text_fg(255, 255, 255);

    for (const auto& detection : detections) {
        const BoundingBox box = clamp_box_to_color_frame(detection.bbox, overlay);
        if (box.width <= 0 || box.height <= 0) {
            continue;
        }

        cv::rectangle(image, cv::Rect(box.x, box.y, box.width, box.height), box_color, 2);

        std::ostringstream label;
        label << detection.coarse_class << " " << std::fixed << std::setprecision(2) << detection.confidence;
        const std::string label_text = label.str();
        int baseline = 0;
        const cv::Size text_size = cv::getTextSize(label_text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        const int text_x = std::max(0, std::min(box.x, overlay.width - text_size.width - 2));
        const int text_y = box.y > text_size.height + 4 ? box.y - 4 : std::min(overlay.height - 2, box.y + text_size.height + 4);
        const int bg_top = std::max(0, text_y - text_size.height - baseline - 2);
        const int bg_height = std::min(overlay.height - bg_top, text_size.height + baseline + 4);
        const int bg_width = std::min(overlay.width - text_x, text_size.width + 4);
        cv::rectangle(image, cv::Rect(text_x, bg_top, bg_width, bg_height), text_bg, cv::FILLED);
        cv::putText(
            image,
            label_text,
            cv::Point(text_x + 2, std::min(overlay.height - 2, text_y - 2)),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            text_fg,
            1,
            cv::LINE_AA
        );
    }
#endif
    return overlay;
}

std::string escape_json(const std::string& value) {
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            escaped << character;
            break;
        }
    }
    return escaped.str();
}

std::string bool_to_json(bool value) {
    return value ? "true" : "false";
}

std::string now_as_utc_string();
std::string now_as_local_filename_string();
std::string sanitize_token(std::string token);
std::string to_lower_copy(std::string value);
std::string pipeline_mode_string(const LiveHarnessOptions& options);
std::string expected_event_type_for_case(const std::string& case_id);
EventType mock_event_type_for_case(const std::string& case_id);
std::string default_mock_class_for_case(const std::string& case_id);
std::string resolve_public_host(const std::string& configured_public_host, const std::string& bind_host);
std::string make_preview_url(const std::string& public_host, int port);
std::string make_status_url(const std::string& public_host, int port);
std::string make_index_url(const std::string& public_host, int port);
BoundingBox clamp_box_to_frame(const BoundingBox& box, const GrayFrame& frame);
GrayFrame crop_frame(const GrayFrame& source, const BoundingBox& requested_box);
BoundingBox choose_primary_box(
    const EventResult& stage1_result,
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const BoundingBox& configured_roi
);
BoundingBox shrink_box(const BoundingBox& box, double factor);
int resolve_class_index(const std::vector<std::string>& class_names, const std::string& coarse_class);
std::array<float, 6> make_onnx_row(
    const BoundingBox& frame_box,
    const GrayFrame& frame,
    const YoloRuntimeConfig& config,
    float score,
    int class_index
);

std::string detection_to_json(const YoloDetection& detection);
std::string crop_request_to_json(const CropRequest& request);
std::string match_to_json(const DetectionMatch& match);

template <typename T>
std::string json_array_from_objects(
    const std::vector<T>& values,
    const std::function<std::string(const T&)>& render
) {
    std::ostringstream output;
    output << "[";
    if (!values.empty()) {
        output << "\n";
        for (std::size_t index = 0; index < values.size(); ++index) {
            output << render(values[index]);
            if (index + 1 < values.size()) {
                output << ",";
            }
            output << "\n";
        }
    }
    output << "]";
    return output.str();
}

std::string build_detection_list_json(
    const std::string& session_id,
    const std::string& frame_label,
    Module2Mode mode,
    const std::vector<YoloDetection>& detections,
    const std::string& error_message
);
std::string build_module2_result_json(
    const std::string& session_id,
    Module2Mode mode,
    const Module2Execution& execution,
    const SessionPaths& paths,
    EventType fallback_event_type
);
std::string build_live_capture_meta_json(
    const LiveHarnessOptions& options,
    int width,
    int height,
    int fps,
    const std::string& preview_url,
    const std::string& start_time
);
std::string build_capture_only_report_json(
    const std::string& session_id,
    const std::string& case_id,
    Module2Mode mode,
    const EventResult& stage1_event,
    const StableCaptureEvent& capture_event
);
std::string build_test_report_json(
    const std::string& session_id,
    const std::string& case_id,
    Module2Mode mode,
    const EventResult& stage1_event,
    const Module2Execution& stage2_execution,
    const EventResult& final_event,
    const std::string& stage2_failure_reason
);
std::string build_run_manifest_json(
    const SessionPaths& paths,
    const std::string& session_id,
    const std::string& case_id,
    const std::string& pipeline_mode,
    const std::string& preview_url,
    const std::string& status_url,
    const std::string& final_event_type,
    bool stage2_success,
    const std::string& stage2_failure_reason
);
std::string build_latest_run_manifest_json(
    const std::string& session_id,
    const std::string& case_id,
    const std::string& final_event_type,
    const std::filesystem::path& session_dir
);
std::string build_placeholder_latest_event_json();
std::string build_placeholder_latest_stage2_json();
std::string build_placeholder_latest_final_event_json();
bool initialize_socket_runtime(std::string& error_message);
void cleanup_socket_runtime();
void close_socket_handle(SocketHandle socket_handle);

class SharedFrameBuffer;
class SessionArtifactWriter;
class LivePreviewPublisher;
class CameraCaptureThread;

std::string now_as_utc_string() {
    const auto now = std::chrono::system_clock::now();
    const auto time_value = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &time_value);
#else
    gmtime_r(&time_value, &utc_tm);
#endif

    std::ostringstream output;
    output << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string now_as_local_filename_string() {
    const auto now = std::chrono::system_clock::now();
    const auto time_value = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_value);
#else
    localtime_r(&time_value, &local_tm);
#endif

    std::ostringstream output;
    output << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
    return output.str();
}

std::string sanitize_token(std::string token) {
    for (char& character : token) {
        const bool valid =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '_' ||
            character == '-';
        if (!valid) {
            character = '_';
        }
    }
    return token.empty() ? "session" : token;
}

std::string to_lower_copy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    return value;
}

std::string expected_event_type_for_case(const std::string& case_id) {
    const std::string normalized = to_lower_copy(case_id);
    if (normalized.find("tc01") != std::string::npos || normalized.find("no_change") != std::string::npos) {
        return "no_change";
    }
    if (normalized.find("reorganize") != std::string::npos || normalized.find("reorg") != std::string::npos) {
        return "reorganize";
    }
    if (normalized.find("tc02") != std::string::npos || normalized.find("put_in") != std::string::npos) {
        return "put_in";
    }
    if (normalized.find("tc03") != std::string::npos || normalized.find("take_out") != std::string::npos) {
        return "take_out";
    }
    if (normalized.find("tc04") != std::string::npos || normalized.find("partial") != std::string::npos) {
        return "partial_take_out_candidate";
    }
    return "unspecified";
}

EventType mock_event_type_for_case(const std::string& case_id) {
    const std::string expected = expected_event_type_for_case(case_id);
    if (expected == "put_in") {
        return EventType::PutIn;
    }
    if (expected == "take_out") {
        return EventType::TakeOut;
    }
    if (expected == "partial_take_out_candidate") {
        return EventType::PartialTakeOutCandidate;
    }
    if (expected == "reorganize") {
        return EventType::Reorganize;
    }
    if (expected == "no_change") {
        return EventType::NoChange;
    }
    return EventType::NoChange;
}

std::string pipeline_mode_string(const LiveHarnessOptions& options) {
    if (options.preview_only) {
        return "preview_only";
    }
    if (options.capture_only) {
        return "capture_only";
    }
    return "full_chain";
}

std::string default_mock_class_for_case(const std::string& case_id) {
    const std::string normalized = to_lower_copy(case_id);
    if (normalized.find("fruit") != std::string::npos || normalized.find("tc04") != std::string::npos) {
        return "fruit_vegetable";
    }
    if (normalized.find("drink") != std::string::npos) {
        return "drink";
    }
    if (normalized.find("meat") != std::string::npos || normalized.find("egg") != std::string::npos) {
        return "meat_egg_fresh";
    }
    return "packaged_food";
}

bool is_wildcard_host(const std::string& host) {
    return host == "0.0.0.0" || host == "::" || host == "[::]";
}

bool is_loopback_ipv4(const std::string& host) {
    return host == "127.0.0.1" || host.rfind("127.", 0) == 0;
}

std::string detect_host_from_hostname() {
    char hostname[256] = {};
    if (gethostname(hostname, static_cast<int>(sizeof(hostname))) != 0) {
        return {};
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0 || result == nullptr) {
        return {};
    }

    std::string detected;
    for (addrinfo* cursor = result; cursor != nullptr; cursor = cursor->ai_next) {
        if (cursor->ai_family != AF_INET || cursor->ai_addr == nullptr) {
            continue;
        }
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(cursor->ai_addr);
        char address_buffer[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address_buffer, sizeof(address_buffer)) == nullptr) {
            continue;
        }
        detected = address_buffer;
        if (!is_loopback_ipv4(detected)) {
            break;
        }
    }

    freeaddrinfo(result);
    return is_loopback_ipv4(detected) ? std::string{} : detected;
}

std::string detect_host_from_udp_route() {
    SocketHandle socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handle == kInvalidSocket) {
        return {};
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    if (inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr) != 1) {
        close_socket_handle(socket_handle);
        return {};
    }

    const int connect_result = connect(
        socket_handle,
        reinterpret_cast<const sockaddr*>(&remote),
        static_cast<int>(sizeof(remote))
    );
    if (connect_result != 0) {
        close_socket_handle(socket_handle);
        return {};
    }

    sockaddr_in local{};
#ifdef _WIN32
    int local_size = sizeof(local);
#else
    socklen_t local_size = sizeof(local);
#endif
    if (getsockname(socket_handle, reinterpret_cast<sockaddr*>(&local), &local_size) != 0) {
        close_socket_handle(socket_handle);
        return {};
    }

    char address_buffer[INET_ADDRSTRLEN] = {};
    const char* converted = inet_ntop(AF_INET, &local.sin_addr, address_buffer, sizeof(address_buffer));
    close_socket_handle(socket_handle);
    if (converted == nullptr) {
        return {};
    }

    const std::string detected = address_buffer;
    return is_loopback_ipv4(detected) ? std::string{} : detected;
}

std::string resolve_public_host(const std::string& configured_public_host, const std::string& bind_host) {
    if (!configured_public_host.empty() && configured_public_host != "auto") {
        return configured_public_host;
    }

    if (!is_wildcard_host(bind_host)) {
        return bind_host;
    }

    const std::string from_hostname = detect_host_from_hostname();
    if (!from_hostname.empty()) {
        return from_hostname;
    }

    const std::string from_route = detect_host_from_udp_route();
    if (!from_route.empty()) {
        return from_route;
    }

    return "127.0.0.1";
}

std::string make_preview_url(const std::string& public_host, int port) {
    std::ostringstream output;
    output << "http://" << public_host << ":" << port << "/live.mjpeg";
    return output.str();
}

std::string make_status_url(const std::string& public_host, int port) {
    std::ostringstream output;
    output << "http://" << public_host << ":" << port << "/status.json";
    return output.str();
}

std::string make_index_url(const std::string& public_host, int port) {
    std::ostringstream output;
    output << "http://" << public_host << ":" << port << "/";
    return output.str();
}

BoundingBox clamp_box_to_frame(const BoundingBox& box, const GrayFrame& frame) {
    if (frame.empty() || box.width <= 0 || box.height <= 0) {
        return {};
    }

    const int x0 = std::clamp(box.x, 0, frame.width);
    const int y0 = std::clamp(box.y, 0, frame.height);
    const int x1 = std::clamp(box.x + box.width, 0, frame.width);
    const int y1 = std::clamp(box.y + box.height, 0, frame.height);
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return BoundingBox{x0, y0, x1 - x0, y1 - y0};
}

GrayFrame crop_frame(const GrayFrame& source, const BoundingBox& requested_box) {
    GrayFrame cropped;
    const BoundingBox box = clamp_box_to_frame(requested_box, source);
    if (source.empty() || box.width <= 0 || box.height <= 0) {
        return cropped;
    }

    cropped.width = box.width;
    cropped.height = box.height;
    cropped.index = source.index;
    cropped.pixels.resize(static_cast<std::size_t>(box.width * box.height));
    for (int y = 0; y < box.height; ++y) {
        for (int x = 0; x < box.width; ++x) {
            cropped.pixels[static_cast<std::size_t>(y * box.width + x)] =
                source.at(box.x + x, box.y + y);
        }
    }
    return cropped;
}

BoundingBox choose_primary_box(
    const EventResult& stage1_result,
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const BoundingBox& configured_roi
) {
    if (!stage1_result.change_regions.empty()) {
        return clamp_box_to_frame(stage1_result.change_regions.front().box, after_frame);
    }

    const BoundingBox full_frame{
        0,
        0,
        std::max(before_frame.width, after_frame.width),
        std::max(before_frame.height, after_frame.height)
    };
    const BoundingBox preferred = configured_roi.width > 0 && configured_roi.height > 0 ? configured_roi : full_frame;
    const GrayFrame& reference = after_frame.empty() ? before_frame : after_frame;
    BoundingBox clamped = clamp_box_to_frame(preferred, reference);
    if (clamped.width > 0 && clamped.height > 0) {
        return clamped;
    }
    return clamp_box_to_frame(full_frame, reference);
}

BoundingBox shrink_box(const BoundingBox& box, double factor) {
    if (box.width <= 0 || box.height <= 0) {
        return {};
    }

    const double clamped_factor = std::clamp(factor, 0.1, 1.0);
    const int new_width = std::max(1, static_cast<int>(std::round(static_cast<double>(box.width) * clamped_factor)));
    const int new_height = std::max(1, static_cast<int>(std::round(static_cast<double>(box.height) * clamped_factor)));
    const int center_x = box.x + box.width / 2;
    const int center_y = box.y + box.height / 2;
    return BoundingBox{
        center_x - new_width / 2,
        center_y - new_height / 2,
        new_width,
        new_height
    };
}

int resolve_class_index(const std::vector<std::string>& class_names, const std::string& coarse_class) {
    const std::string wanted = to_lower_copy(coarse_class);
    for (std::size_t index = 0; index < class_names.size(); ++index) {
        if (to_lower_copy(class_names[index]) == wanted) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

std::array<float, 6> make_onnx_row(
    const BoundingBox& frame_box,
    const GrayFrame& frame,
    const YoloRuntimeConfig& config,
    float score,
    int class_index
) {
    const BoundingBox clamped = clamp_box_to_frame(frame_box, frame);
    if (clamped.width <= 0 || clamped.height <= 0 || frame.width <= 0 || frame.height <= 0) {
        return {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, static_cast<float>(class_index)};
    }

    const float scale_x = static_cast<float>(config.input_width) / static_cast<float>(frame.width);
    const float scale_y = static_cast<float>(config.input_height) / static_cast<float>(frame.height);
    return {
        static_cast<float>(clamped.x) * scale_x,
        static_cast<float>(clamped.y) * scale_y,
        static_cast<float>(clamped.x + clamped.width) * scale_x,
        static_cast<float>(clamped.y + clamped.height) * scale_y,
        score,
        static_cast<float>(class_index)
    };
}

EventResult build_stage1_capture_event(
    const std::string& session_id,
    const std::string& roi_id,
    const StableCaptureEvent& capture_event,
    const std::filesystem::path& before_frame_path,
    const std::filesystem::path& after_frame_path
) {
    EventResult event;
    event.session_id = session_id;
    event.timestamp = now_as_utc_string();
    event.event_type = EventType::CaptureRecorded;
    event.roi_id = roi_id;
    event.confidence = std::clamp(capture_event.selected_frames.final_change_ratio, 0.0, 1.0);
    event.before_frame = path_to_utf8_string(before_frame_path);
    event.after_frame = path_to_utf8_string(after_frame_path);
    event.change_regions = capture_event.final_change.regions;
    event.need_user_confirm = false;
    return event;
}

EventResult build_stage2_unavailable_event(
    const EventResult& stage1_capture_event,
    const std::string& before_frame_path,
    const std::string& after_frame_path
) {
    EventResult event = stage1_capture_event;
    event.event_type = EventType::NotEvaluated;
    event.need_user_confirm = true;
    event.before_frame = before_frame_path;
    event.after_frame = after_frame_path;
    return event;
}

bool initialize_socket_runtime(std::string& error_message) {
#ifdef _WIN32
    WSADATA wsa_data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        error_message = "WSAStartup failed with code " + std::to_string(result);
        return false;
    }
#else
    (void)error_message;
#endif
    return true;
}

void cleanup_socket_runtime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void close_socket_handle(SocketHandle socket_handle) {
    if (socket_handle == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

std::string build_placeholder_latest_event_json() {
    return "{\n"
           "  \"status\": \"waiting\",\n"
           "  \"capture_strategy\": \"stable_state_transition\",\n"
           "  \"message\": \"waiting for an initial stable baseline before the first event\"\n"
           "}\n";
}

std::string build_placeholder_latest_stage2_json() {
    return "{\n"
           "  \"status\": \"waiting\",\n"
           "  \"message\": \"stage2 result is not available until the first completed event\"\n"
           "}\n";
}

std::string build_placeholder_latest_final_event_json() {
    return "{\n"
           "  \"status\": \"waiting\",\n"
           "  \"message\": \"final event json is not available until the first completed event\"\n"
           "}\n";
}

class SharedFrameBuffer {
public:
    void push(FramePacket packet) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_packet_ = packet;
        if (pending_frames_.size() >= 8) {
            pending_frames_.pop_front();
        }
        pending_frames_.push_back(std::move(packet));
        condition_.notify_one();
    }

    bool wait_and_pop(FramePacket& packet, const std::atomic<bool>& stop_requested) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&]() { return stopped_ || stop_requested.load() || !pending_frames_.empty(); });
        if (pending_frames_.empty()) {
            return false;
        }
        packet = std::move(pending_frames_.front());
        pending_frames_.pop_front();
        return true;
    }

    FramePacket latest_snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_packet_;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        condition_.notify_all();
    }

    bool stopped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<FramePacket> pending_frames_;
    FramePacket latest_packet_;
    bool stopped_ = false;
};

std::string detection_to_json(const YoloDetection& detection) {
    std::ostringstream output;
    output << "{\n"
           << "      \"coarse_class\": \"" << escape_json(detection.coarse_class) << "\",\n"
           << "      \"confidence\": " << std::fixed << std::setprecision(3) << detection.confidence << ",\n"
           << "      \"bbox\": {\n"
           << "        \"x\": " << detection.bbox.x << ",\n"
           << "        \"y\": " << detection.bbox.y << ",\n"
           << "        \"width\": " << detection.bbox.width << ",\n"
           << "        \"height\": " << detection.bbox.height << "\n"
           << "      }\n"
           << "    }";
    return output.str();
}

std::string crop_request_to_json(const CropRequest& request) {
    std::ostringstream output;
    output << "{\n"
           << "      \"source_frame\": \"" << escape_json(request.source_frame) << "\",\n"
           << "      \"coarse_class\": \"" << escape_json(request.coarse_class) << "\",\n"
           << "      \"bbox\": {\n"
           << "        \"x\": " << request.bbox.x << ",\n"
           << "        \"y\": " << request.bbox.y << ",\n"
           << "        \"width\": " << request.bbox.width << ",\n"
           << "        \"height\": " << request.bbox.height << "\n"
           << "      }\n"
           << "    }";
    return output.str();
}

std::string crop_artifact_to_json(const CropArtifactRecord& artifact) {
    std::ostringstream output;
    output << "{\n"
           << "      \"source_frame\": \"" << escape_json(artifact.request.source_frame) << "\",\n"
           << "      \"coarse_class\": \"" << escape_json(artifact.request.coarse_class) << "\",\n"
           << "      \"path\": \"" << escape_json(artifact.output_path) << "\",\n"
           << "      \"bbox\": {\n"
           << "        \"x\": " << artifact.request.bbox.x << ",\n"
           << "        \"y\": " << artifact.request.bbox.y << ",\n"
           << "        \"width\": " << artifact.request.bbox.width << ",\n"
           << "        \"height\": " << artifact.request.bbox.height << "\n"
           << "      }\n"
           << "    }";
    return output.str();
}

std::string match_to_json(const DetectionMatch& match) {
    std::ostringstream output;
    output << "{\n"
           << "      \"before_index\": " << match.before_index << ",\n"
           << "      \"after_index\": " << match.after_index << ",\n"
           << "      \"coarse_class\": \"" << escape_json(match.coarse_class) << "\",\n"
           << "      \"iou\": " << std::fixed << std::setprecision(3) << match.iou << ",\n"
           << "      \"normalized_center_distance\": " << std::fixed << std::setprecision(3)
           << match.normalized_center_distance << ",\n"
           << "      \"area_change_ratio\": " << std::fixed << std::setprecision(3)
           << match.area_change_ratio << ",\n"
           << "      \"match_score\": " << std::fixed << std::setprecision(3)
           << match.match_score << "\n"
           << "    }";
    return output.str();
}

std::string count_map_to_json(const DetectionCountMap& counts) {
    std::ostringstream output;
    output << "{";
    if (!counts.empty()) {
        output << "\n";
        std::size_t index = 0;
        for (const auto& entry : counts) {
            output << "    \"" << escape_json(entry.first) << "\": " << entry.second;
            if (index + 1 < counts.size()) {
                output << ",";
            }
            output << "\n";
            ++index;
        }
        output << "  ";
    }
    output << "}";
    return output.str();
}

std::string build_detection_list_json(
    const std::string& session_id,
    const std::string& frame_label,
    Module2Mode mode,
    const std::vector<YoloDetection>& detections,
    const std::string& error_message
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"frame\": \"" << escape_json(frame_label) << "\",\n"
           << "  \"module2_mode\": \"" << to_string(mode) << "\",\n"
           << "  \"error\": \"" << escape_json(error_message) << "\",\n"
           << "  \"detections\": " << json_array_from_objects<YoloDetection>(detections, detection_to_json) << "\n"
           << "}\n";
    return output.str();
}

std::string build_module2_result_json(
    const std::string& session_id,
    Module2Mode mode,
    const Module2Execution& execution,
    const SessionPaths& paths,
    EventType fallback_event_type
) {
    const bool stage2_skipped = !execution.success && execution.failure_reason == "capture_only: module2 skipped";
    const EventType event_type = execution.success ? execution.result.event.event_type : fallback_event_type;
    const std::string review_reason = execution.success ? execution.result.review_reason : execution.failure_reason;

    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"event_type\": \"" << escape_json(stage2_skipped ? "not_evaluated" : to_string(event_type)) << "\",\n"
           << "  \"success\": " << bool_to_json(execution.success) << ",\n"
           << "  \"stage2_skipped\": " << bool_to_json(stage2_skipped) << ",\n"
           << "  \"module2_mode\": \"" << to_string(mode) << "\",\n"
           << "  \"before_detection_overlay_path\": \"" << escape_json(path_to_utf8_string(paths.stage2_detections_before_image_path)) << "\",\n"
           << "  \"after_detection_overlay_path\": \"" << escape_json(path_to_utf8_string(paths.stage2_detections_after_image_path)) << "\",\n"
           << "  \"before_counts\": " << count_map_to_json(execution.result.before_counts) << ",\n"
           << "  \"after_counts\": " << count_map_to_json(execution.result.after_counts) << ",\n"
           << "  \"count_decision\": \"" << escape_json(execution.result.count_decision) << "\",\n"
           << "  \"matched_pairs\": "
           << json_array_from_objects<DetectionMatch>(execution.result.matched_pairs, match_to_json) << ",\n"
           << "  \"new_boxes\": "
           << json_array_from_objects<YoloDetection>(execution.result.new_boxes, detection_to_json) << ",\n"
           << "  \"disappeared_boxes\": "
           << json_array_from_objects<YoloDetection>(execution.result.disappeared_boxes, detection_to_json) << ",\n"
           << "  \"partial_candidates\": "
           << json_array_from_objects<DetectionMatch>(execution.result.partial_candidates, match_to_json) << ",\n"
           << "  \"reorganize_candidates\": "
           << json_array_from_objects<DetectionMatch>(execution.result.reorganize_candidates, match_to_json) << ",\n"
           << "  \"crop_requests\": "
           << json_array_from_objects<CropRequest>(execution.result.crop_requests, crop_request_to_json) << ",\n"
           << "  \"crop_artifacts\": "
           << json_array_from_objects<CropArtifactRecord>(execution.crop_artifacts, crop_artifact_to_json) << ",\n"
           << "  \"review_reason\": \"" << escape_json(review_reason) << "\"\n"
           << "}\n";
    return output.str();
}

std::string build_live_capture_meta_json(
    const LiveHarnessOptions& options,
    int width,
    int height,
    int fps,
    const std::string& preview_url,
    const std::string& start_time
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"device\": \"" << escape_json(options.device) << "\",\n"
           << "  \"width\": " << width << ",\n"
           << "  \"height\": " << height << ",\n"
           << "  \"fps\": " << fps << ",\n"
           << "  \"preview_url\": \"" << escape_json(preview_url) << "\",\n"
           << "  \"start_time\": \"" << escape_json(start_time) << "\",\n"
           << "  \"pipeline_mode\": \"" << escape_json(pipeline_mode_string(options)) << "\",\n"
           << "  \"capture_strategy\": \"stable_state_transition\",\n"
           << "  \"module2_mode\": \"" << to_string(options.module2_mode) << "\"\n"
           << "}\n";
    return output.str();
}

std::string build_capture_only_report_json(
    const std::string& session_id,
    const std::string& case_id,
    Module2Mode mode,
    const EventResult& stage1_event,
    const StableCaptureEvent& capture_event
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"case_id\": \"" << escape_json(case_id) << "\",\n"
           << "  \"mode\": \"live_camera\",\n"
           << "  \"pipeline_mode\": \"capture_only\",\n"
           << "  \"capture_strategy\": \"stable_state_transition\",\n"
           << "  \"module2_mode\": \"" << to_string(mode) << "\",\n"
           << "  \"expected_event_type\": \"not_evaluated\",\n"
           << "  \"actual_stage1_event_type\": \"" << escape_json(to_string(stage1_event.event_type)) << "\",\n"
            << "  \"actual_stage2_event_type\": \"skipped\",\n"
            << "  \"final_event_type\": \"capture_recorded\",\n"
            << "  \"capture_valid\": true,\n"
            << "  \"stage2_skipped\": true,\n"
           << "  \"event_frame_count\": " << capture_event.total_frame_count << ",\n"
           << "  \"peak_interframe_ratio\": " << std::fixed << std::setprecision(4)
           << capture_event.peak_interframe_ratio << ",\n"
           << "  \"peak_baseline_change_ratio\": " << std::fixed << std::setprecision(4)
           << capture_event.peak_baseline_ratio << ",\n"
           << "  \"final_change_ratio\": " << std::fixed << std::setprecision(4)
           << capture_event.selected_frames.final_change_ratio << ",\n"
           << "  \"stable_before_run_length\": " << capture_event.selected_frames.stable_before_run_length << ",\n"
           << "  \"stable_after_run_length\": " << capture_event.selected_frames.stable_after_run_length << ",\n"
            << "  \"pass\": true,\n"
            << "  \"fallback_used\": false,\n"
            << "  \"fallback_reason\": \"\",\n"
           << "  \"notes\": \"YOLO backend is not part of this run. The session is emitted only after a stable baseline, a sustained disturbance, and a re-stabilized final state are observed.\"\n"
           << "}\n";
    return output.str();
}

std::string build_test_report_json(
    const std::string& session_id,
    const std::string& case_id,
    Module2Mode mode,
    const EventResult& stage1_event,
    const Module2Execution& stage2_execution,
    const EventResult& final_event,
    const std::string& stage2_failure_reason
) {
    const std::string expected = expected_event_type_for_case(case_id);
    const std::string stage1_type = to_string(stage1_event.event_type);
    const std::string stage2_type = stage2_execution.success
        ? to_string(stage2_execution.result.event.event_type)
        : to_string(EventType::NotEvaluated);
    const std::string final_type = to_string(final_event.event_type);
    const bool pass = expected == "unspecified" ? false : expected == final_type;

    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"case_id\": \"" << escape_json(case_id) << "\",\n"
           << "  \"mode\": \"live_camera\",\n"
           << "  \"pipeline_mode\": \"full_chain\",\n"
           << "  \"module2_mode\": \"" << to_string(mode) << "\",\n"
           << "  \"expected_event_type\": \"" << escape_json(expected) << "\",\n"
           << "  \"actual_stage1_event_type\": \"" << escape_json(stage1_type) << "\",\n"
           << "  \"actual_stage2_event_type\": \"" << escape_json(stage2_type) << "\",\n"
           << "  \"final_event_type\": \"" << escape_json(final_type) << "\",\n"
           << "  \"stage2_success\": " << bool_to_json(stage2_execution.success) << ",\n"
            << "  \"pass\": " << bool_to_json(pass) << ",\n"
           << "  \"fallback_used\": false,\n"
           << "  \"fallback_reason\": \"\",\n"
           << "  \"stage2_failure_reason\": \"" << escape_json(stage2_failure_reason) << "\",\n"
           << "  \"notes\": \"partial_take_out_candidate only applies to fruit_vegetable scenes. drink scenes do not auto-detect partial removal.\"\n"
           << "}\n";
    return output.str();
}

std::string build_run_manifest_json(
    const SessionPaths& paths,
    const std::string& session_id,
    const std::string& case_id,
    const std::string& pipeline_mode,
    const std::string& preview_url,
    const std::string& status_url,
    const std::string& final_event_type,
    bool stage2_success,
    const std::string& stage2_failure_reason
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"case_id\": \"" << escape_json(case_id) << "\",\n"
           << "  \"mode\": \"live_camera\",\n"
           << "  \"pipeline_mode\": \"" << escape_json(pipeline_mode) << "\",\n"
           << "  \"preview_url\": \"" << escape_json(preview_url) << "\",\n"
           << "  \"status_url\": \"" << escape_json(status_url) << "\",\n"
           << "  \"final_event_type\": \"" << escape_json(final_event_type) << "\",\n"
           << "  \"stage2_success\": " << bool_to_json(stage2_success) << ",\n"
           << "  \"fallback_used\": false,\n"
           << "  \"fallback_reason\": \"\",\n"
           << "  \"stage2_failure_reason\": \"" << escape_json(stage2_failure_reason) << "\",\n"
           << "  \"paths\": {\n"
           << "    \"session_dir\": \"" << escape_json(path_to_utf8_string(paths.session_dir)) << "\",\n"
           << "    \"preview\": \"" << escape_json(path_to_utf8_string(paths.preview_latest_path)) << "\",\n"
           << "    \"stage1_event\": \"" << escape_json(path_to_utf8_string(paths.stage1_event_path)) << "\",\n"
           << "    \"stage2_result\": \"" << escape_json(path_to_utf8_string(paths.stage2_result_path)) << "\",\n"
           << "    \"final_event\": \"" << escape_json(path_to_utf8_string(paths.final_event_path)) << "\",\n"
           << "    \"inventory_response\": \"" << escape_json(path_to_utf8_string(paths.inventory_response_path)) << "\",\n"
           << "    \"events_response\": \"" << escape_json(path_to_utf8_string(paths.events_response_path)) << "\",\n"
           << "    \"pending_response\": \"" << escape_json(path_to_utf8_string(paths.pending_response_path)) << "\",\n"
           << "    \"software_closure_report\": \""
           << escape_json(path_to_utf8_string(paths.software_closure_report_path)) << "\",\n"
           << "    \"test_report\": \"" << escape_json(path_to_utf8_string(paths.final_report_path)) << "\"\n"
           << "  }\n"
           << "}\n";
    return output.str();
}

std::string build_latest_run_manifest_json(
    const std::string& session_id,
    const std::string& case_id,
    const std::string& final_event_type,
    const std::filesystem::path& session_dir
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(session_id) << "\",\n"
           << "  \"case_id\": \"" << escape_json(case_id) << "\",\n"
           << "  \"final_event_type\": \"" << escape_json(final_event_type) << "\",\n"
           << "  \"session_dir\": \"" << escape_json(path_to_utf8_string(session_dir)) << "\"\n"
           << "}\n";
    return output.str();
}

// Writes per-session artifacts under data/test_sessions/module12_realtime_live/.
class SessionArtifactWriter {
public:
    SessionArtifactWriter(std::filesystem::path output_root, std::filesystem::path latest_run_manifest_path)
        : output_root_(std::move(output_root)),
          latest_run_manifest_path_(std::move(latest_run_manifest_path)) {}

    SessionPaths create_layout(const std::string& session_id) const {
        SessionPaths layout;
        layout.session_dir = output_root_ / session_id;
        layout.preview_dir = layout.session_dir / "preview";
        layout.stage1_dir = layout.session_dir / "stage1";
        layout.stage2_dir = layout.session_dir / "stage2";
        layout.final_dir = layout.session_dir / "final";
        layout.meta_dir = layout.session_dir / "meta";
        layout.preview_latest_path = layout.preview_dir / "latest_preview.jpg";
        layout.stage1_before_path = layout.stage1_dir / "before.jpg";
        layout.stage1_after_path = layout.stage1_dir / "after.jpg";
        layout.stage1_overlay_path = layout.stage1_dir / "overlay.jpg";
        layout.stage1_event_path = layout.stage1_dir / "stage1_event.json";
        layout.stage1_debug_path = layout.stage1_dir / "stage1_debug.json";
        layout.stage2_detections_before_path = layout.stage2_dir / "detections_before.json";
        layout.stage2_detections_after_path = layout.stage2_dir / "detections_after.json";
        layout.stage2_detections_before_image_path = layout.stage2_dir / "before_detections.jpg";
        layout.stage2_detections_after_image_path = layout.stage2_dir / "after_detections.jpg";
        layout.stage2_result_path = layout.stage2_dir / "module2_result.json";
        layout.stage2_crops_dir = layout.stage2_dir / "crops";
        layout.final_event_path = layout.final_dir / "event.json";
        layout.final_report_path = layout.final_dir / "test_report.json";
        layout.inventory_response_path = layout.final_dir / "inventory_response.json";
        layout.events_response_path = layout.final_dir / "events_response.json";
        layout.pending_response_path = layout.final_dir / "pending_response.json";
        layout.software_closure_report_path = layout.final_dir / "software_closure_report.json";
        layout.live_capture_meta_path = layout.meta_dir / "live_capture_meta.json";
        layout.run_manifest_path = layout.meta_dir / "run_manifest.json";

        std::filesystem::create_directories(layout.preview_dir);
        std::filesystem::create_directories(layout.stage1_dir);
        std::filesystem::create_directories(layout.stage2_crops_dir);
        std::filesystem::create_directories(layout.final_dir);
        std::filesystem::create_directories(layout.meta_dir);
        return layout;
    }

    bool write_text(const std::filesystem::path& output_path, const std::string& content, std::string& error_message) const {
        std::filesystem::create_directories(output_path.parent_path());
        std::ofstream output(output_path, std::ios::binary);
        if (!output) {
            error_message = "Failed to open output file: " + path_to_utf8_string(output_path);
            return false;
        }
        output << content;
        if (!output) {
            error_message = "Failed to write output file: " + path_to_utf8_string(output_path);
            return false;
        }
        return true;
    }

    bool write_latest_run_manifest(const std::string& content, std::string& error_message) const {
        return write_text(latest_run_manifest_path_, content, error_message);
    }

private:
    std::filesystem::path output_root_;
    std::filesystem::path latest_run_manifest_path_;
};

// Test-only lightweight HTTP publisher for MJPEG preview and status JSON.
class LivePreviewPublisher {
public:
    using FrameProvider = std::function<FramePacket()>;
    using TextProvider = std::function<std::string()>;

    LivePreviewPublisher(
        std::string bind_host,
        int port,
        FrameProvider frame_provider,
        TextProvider status_provider,
        TextProvider latest_event_provider,
        TextProvider latest_stage2_provider,
        TextProvider latest_final_event_provider,
        TextProvider html_provider
    )
        : bind_host_(std::move(bind_host)),
          port_(port),
          frame_provider_(std::move(frame_provider)),
          status_provider_(std::move(status_provider)),
          latest_event_provider_(std::move(latest_event_provider)),
          latest_stage2_provider_(std::move(latest_stage2_provider)),
          latest_final_event_provider_(std::move(latest_final_event_provider)),
          html_provider_(std::move(html_provider)) {}

    ~LivePreviewPublisher() {
        stop();
    }

    bool start(std::string& error_message) {
        if (!initialize_socket_runtime(error_message)) {
            return false;
        }

        listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_socket_ == kInvalidSocket) {
            cleanup_socket_runtime();
            error_message = "Failed to create preview server socket.";
            return false;
        }

        int reuse = 1;
#ifdef _WIN32
        setsockopt(
            listen_socket_,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse),
            static_cast<int>(sizeof(reuse))
        );
#else
        setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port_));
        if (inet_pton(AF_INET, bind_host_.c_str(), &address.sin_addr) != 1) {
            close_socket_handle(listen_socket_);
            listen_socket_ = kInvalidSocket;
            cleanup_socket_runtime();
            error_message = "Invalid bind host: " + bind_host_;
            return false;
        }

        if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close_socket_handle(listen_socket_);
            listen_socket_ = kInvalidSocket;
            cleanup_socket_runtime();
            error_message = "Failed to bind preview server on " + bind_host_ + ":" + std::to_string(port_);
            return false;
        }

        if (listen(listen_socket_, 8) != 0) {
            close_socket_handle(listen_socket_);
            listen_socket_ = kInvalidSocket;
            cleanup_socket_runtime();
            error_message = "Failed to listen on preview server socket.";
            return false;
        }

        running_.store(true);
        accept_thread_ = std::thread(&LivePreviewPublisher::accept_loop, this);
        return true;
    }

    void stop() {
        const bool was_running = running_.exchange(false);
        if (!was_running && listen_socket_ == kInvalidSocket) {
            return;
        }

        close_socket_handle(listen_socket_);
        listen_socket_ = kInvalidSocket;
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        cleanup_socket_runtime();
    }

private:
    bool send_all(SocketHandle socket_handle, const void* data, std::size_t size) const {
        const char* bytes = static_cast<const char*>(data);
        std::size_t offset = 0;
        while (offset < size) {
            const int sent = send(socket_handle, bytes + offset, static_cast<int>(size - offset), 0);
            if (sent <= 0) {
                return false;
            }
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    void send_text_response(SocketHandle socket_handle, const std::string& content_type, const std::string& body) const {
        std::ostringstream header;
        header << "HTTP/1.1 200 OK\r\n"
               << "Connection: close\r\n"
               << "Cache-Control: no-cache\r\n"
               << "Content-Type: " << content_type << "\r\n"
               << "Content-Length: " << body.size() << "\r\n\r\n";
        const std::string header_text = header.str();
        send_all(socket_handle, header_text.data(), header_text.size());
        send_all(socket_handle, body.data(), body.size());
    }

    void send_not_found(SocketHandle socket_handle) const {
        static const std::string header =
            "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: 10\r\n\r\n";
        static const std::string body = "not found\n";
        send_all(socket_handle, header.data(), header.size());
        send_all(socket_handle, body.data(), body.size());
    }

    void stream_live_mjpeg(SocketHandle socket_handle) const {
        static const std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-cache\r\n"
            "Pragma: no-cache\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
        if (!send_all(socket_handle, header.data(), header.size())) {
            return;
        }

        int last_sequence = -1;
        while (running_.load()) {
            const FramePacket frame = frame_provider_();
            if (frame.sequence == last_sequence || frame.jpeg_bytes.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }

            std::ostringstream part_header;
            part_header << "--frame\r\n"
                        << "Content-Type: image/jpeg\r\n"
                        << "Content-Length: " << frame.jpeg_bytes.size() << "\r\n\r\n";
            const std::string part_header_text = part_header.str();
            if (!send_all(socket_handle, part_header_text.data(), part_header_text.size())) {
                break;
            }
            if (!send_all(socket_handle, frame.jpeg_bytes.data(), frame.jpeg_bytes.size())) {
                break;
            }
            static const char kTerminator[] = "\r\n";
            if (!send_all(socket_handle, kTerminator, sizeof(kTerminator) - 1)) {
                break;
            }
            last_sequence = frame.sequence;
        }
    }

    void handle_client(SocketHandle socket_handle) const {
        char buffer[4096];
        const int received = recv(socket_handle, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
        if (received <= 0) {
            close_socket_handle(socket_handle);
            return;
        }
        buffer[received] = '\0';

        std::istringstream input{std::string(buffer)};
        std::string method;
        std::string target;
        std::string version;
        input >> method >> target >> version;
        if (method != "GET") {
            send_not_found(socket_handle);
            close_socket_handle(socket_handle);
            return;
        }

        if (target == "/" || target == "/index.html") {
            send_text_response(socket_handle, "text/html; charset=utf-8", html_provider_());
            close_socket_handle(socket_handle);
            return;
        }
        if (target == "/status.json") {
            send_text_response(socket_handle, "application/json; charset=utf-8", status_provider_());
            close_socket_handle(socket_handle);
            return;
        }
        if (target == "/latest_event.json") {
            send_text_response(socket_handle, "application/json; charset=utf-8", latest_event_provider_());
            close_socket_handle(socket_handle);
            return;
        }
        if (target == "/latest_stage2.json") {
            send_text_response(socket_handle, "application/json; charset=utf-8", latest_stage2_provider_());
            close_socket_handle(socket_handle);
            return;
        }
        if (target == "/latest_final_event.json") {
            send_text_response(socket_handle, "application/json; charset=utf-8", latest_final_event_provider_());
            close_socket_handle(socket_handle);
            return;
        }
        if (target == "/live.mjpeg") {
            stream_live_mjpeg(socket_handle);
            close_socket_handle(socket_handle);
            return;
        }

        send_not_found(socket_handle);
        close_socket_handle(socket_handle);
    }

    void accept_loop() {
        while (running_.load()) {
            sockaddr_in client_address{};
#ifdef _WIN32
            int client_size = sizeof(client_address);
#else
            socklen_t client_size = sizeof(client_address);
#endif
            const SocketHandle client_socket =
                accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_address), &client_size);
            if (client_socket == kInvalidSocket) {
                if (running_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                continue;
            }

            std::thread(&LivePreviewPublisher::handle_client, this, client_socket).detach();
        }
    }

    std::string bind_host_;
    int port_ = 8080;
    FrameProvider frame_provider_;
    TextProvider status_provider_;
    TextProvider latest_event_provider_;
    TextProvider latest_stage2_provider_;
    TextProvider latest_final_event_provider_;
    TextProvider html_provider_;
    std::atomic<bool> running_{false};
    SocketHandle listen_socket_ = kInvalidSocket;
    std::thread accept_thread_;
};

// Single camera reader shared by preview and algorithm processing.
class CameraCaptureThread {
public:
    CameraCaptureThread(const LiveHarnessOptions& options, SharedFrameBuffer& buffer)
        : options_(options),
          buffer_(buffer) {}

    ~CameraCaptureThread() {
        stop();
    }

    bool start(std::string& error_message) {
#ifdef FRIDGE_USE_OPENCV
        if (!open_capture(error_message)) {
            return false;
        }

        running_.store(true);
        worker_ = std::thread(&CameraCaptureThread::capture_loop, this);
        return true;
#else
        error_message = "Realtime live harness requires an OpenCV-enabled build with videoio/imgcodecs support.";
        return false;
#endif
    }

    void stop() {
        running_.store(false);
        if (worker_.joinable()) {
            worker_.join();
        }
#ifdef FRIDGE_USE_OPENCV
        if (capture_.isOpened()) {
            capture_.release();
        }
#endif
    }

    bool failed() const {
        return failed_.load();
    }

    std::string failure_message() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return failure_message_;
    }

    int actual_width() const {
        return actual_width_;
    }

    int actual_height() const {
        return actual_height_;
    }

    int actual_fps() const {
        return actual_fps_;
    }

private:
#ifdef FRIDGE_USE_OPENCV
    bool open_capture(std::string& error_message) {
        const bool is_numeric = !options_.device.empty() &&
            std::all_of(options_.device.begin(), options_.device.end(), [](unsigned char value) {
                return std::isdigit(value) != 0;
            });

        bool opened = false;
#ifdef __linux__
        if (is_numeric) {
            opened = capture_.open(std::stoi(options_.device), cv::CAP_V4L2);
        } else {
            opened = capture_.open(options_.device, cv::CAP_V4L2);
        }
#else
        if (is_numeric) {
            opened = capture_.open(std::stoi(options_.device));
        } else {
            opened = capture_.open(options_.device);
        }
#endif
        if (!opened) {
            error_message = "Failed to open camera device: " + options_.device;
            return false;
        }

        if (options_.capture_width > 0) {
            capture_.set(cv::CAP_PROP_FRAME_WIDTH, options_.capture_width);
        }
        if (options_.capture_height > 0) {
            capture_.set(cv::CAP_PROP_FRAME_HEIGHT, options_.capture_height);
        }
        if (options_.capture_fps > 0) {
            capture_.set(cv::CAP_PROP_FPS, options_.capture_fps);
        }
        capture_.set(cv::CAP_PROP_BUFFERSIZE, 1);

        actual_width_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
        actual_height_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
        const double fps = capture_.get(cv::CAP_PROP_FPS);
        actual_fps_ = fps > 0.0 ? static_cast<int>(std::round(fps)) : options_.capture_fps;
        return true;
    }

    void capture_loop() {
        int sequence = 0;
        std::vector<int> encode_params = {
            cv::IMWRITE_JPEG_QUALITY,
            std::clamp(options_.preview_jpeg_quality, 40, 95)
        };

        while (running_.load()) {
            cv::Mat bgr_frame;
            if (!capture_.read(bgr_frame) || bgr_frame.empty()) {
                set_failure("Camera read failed from device: " + options_.device);
                break;
            }

            cv::Mat gray_frame_mat;
            cv::cvtColor(bgr_frame, gray_frame_mat, cv::COLOR_BGR2GRAY);

            GrayFrame gray_frame;
            gray_frame.width = gray_frame_mat.cols;
            gray_frame.height = gray_frame_mat.rows;
            gray_frame.index = sequence;
            gray_frame.pixels.assign(gray_frame_mat.datastart, gray_frame_mat.dataend);

            std::vector<std::uint8_t> jpeg_bytes;
            if (!cv::imencode(".jpg", bgr_frame, jpeg_bytes, encode_params)) {
                set_failure("Failed to encode MJPEG preview frame.");
                break;
            }

            FramePacket packet;
            packet.gray_frame = std::move(gray_frame);
            packet.jpeg_bytes = std::move(jpeg_bytes);
            packet.sequence = sequence++;
            packet.captured_at_utc = now_as_utc_string();
            buffer_.push(std::move(packet));
        }

        running_.store(false);
        buffer_.stop();
    }
#endif

    void set_failure(const std::string& message) {
        failed_.store(true);
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            failure_message_ = message;
        }
        running_.store(false);
    }

    const LiveHarnessOptions& options_;
    SharedFrameBuffer& buffer_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex error_mutex_;
    std::string failure_message_;
    std::thread worker_;
    int actual_width_ = 0;
    int actual_height_ = 0;
    int actual_fps_ = 0;
#ifdef FRIDGE_USE_OPENCV
    cv::VideoCapture capture_;
#endif
};

}  // namespace

std::string to_string(Module2Mode mode) {
    switch (mode) {
    case Module2Mode::Mock:
        return "mock";
    case Module2Mode::RealOnnxRuntime:
        return "real_onnx_runtime";
    }
    return "mock";
}

class Module12RealtimeHarness::Impl {
public:
    explicit Impl(LiveHarnessOptions options);

    bool run(std::string& error_message);
    void request_stop();

private:
    bool load_configs(std::string& error_message);
    std::string build_status_json() const;
    std::string build_latest_event_json() const;
    std::string build_latest_stage2_json() const;
    std::string build_latest_final_event_json() const;
    std::string build_index_html() const;
    void remember_frame_packet(const FramePacket& packet);
    std::optional<FramePacket> find_frame_packet(int sequence) const;
    void processing_loop();
    Module2Execution run_module2(
        const std::string& session_id,
        const EventResult& stage1_capture_event,
        const GrayFrame& before_frame,
        const GrayFrame& after_frame,
        const ColorFrame& before_color,
        const ColorFrame& after_color,
        const std::filesystem::path& before_frame_path,
        const std::filesystem::path& after_frame_path,
        EventType mock_event_type
    ) const;
    bool write_crop_artifacts(
        const SessionPaths& paths,
        Module2Execution& execution,
        const ColorFrame& before_color,
        const ColorFrame& after_color,
        std::string& error_message
    ) const;
    bool write_detection_overlays(
        const SessionPaths& paths,
        const Module2Execution& execution,
        const ColorFrame& before_color,
        const ColorFrame& after_color,
        std::string& error_message
    ) const;
    void process_event_window(const StableCaptureEvent& capture_event);

    LiveHarnessOptions options_;
    std::filesystem::path repo_root_;
    std::string public_host_;
    std::string preview_url_;
    std::string status_url_;
    std::string index_url_;
    VisionPipelineConfig pipeline_config_;
    YoloRuntimeConfig yolo_runtime_config_;
    YoloAnalysisConfig yolo_analysis_config_;
    InventoryRuntimeConfig inventory_config_;
    InventoryEngine inventory_engine_;
    LocalServiceConfig service_config_;
    mutable std::mutex state_mutex_;
    std::string last_session_id_;
    std::string last_event_type_ = "none";
    bool stage1_ready_ = false;
    bool stage2_ready_ = false;
    std::string latest_event_json_ = build_placeholder_latest_event_json();
    std::string latest_stage2_json_ = build_placeholder_latest_stage2_json();
    std::string latest_final_event_json_ = build_placeholder_latest_final_event_json();
    LatestEventState latest_event_state_;
    std::string run_started_utc_;
    std::chrono::steady_clock::time_point run_started_steady_{};
    int processed_event_count_ = 0;
    std::atomic<bool> stop_requested_{false};
    SharedFrameBuffer frame_buffer_;
    std::deque<FramePacket> frame_history_;
    std::size_t frame_history_limit_ = 256;
    SessionArtifactWriter artifact_writer_;
    CameraCaptureThread capture_thread_;
    std::unique_ptr<LivePreviewPublisher> preview_publisher_;
    std::thread processing_thread_;
};

Module12RealtimeHarness::Impl::Impl(LiveHarnessOptions options)
    : options_(std::move(options)),
      repo_root_(resolve_repo_root()),
      artifact_writer_(options_.output_root, options_.latest_run_manifest_path),
      capture_thread_(options_, frame_buffer_) {}

void Module12RealtimeHarness::Impl::remember_frame_packet(const FramePacket& packet) {
    frame_history_.push_back(packet);
    while (frame_history_.size() > frame_history_limit_) {
        frame_history_.pop_front();
    }
}

std::optional<FramePacket> Module12RealtimeHarness::Impl::find_frame_packet(int sequence) const {
    for (auto it = frame_history_.rbegin(); it != frame_history_.rend(); ++it) {
        if (it->sequence == sequence) {
            return *it;
        }
    }
    return std::nullopt;
}

bool Module12RealtimeHarness::Impl::load_configs(std::string& error_message) {
    if (!load_pipeline_config(options_.module1_config_path, pipeline_config_, error_message)) {
        return false;
    }
    if (!load_yolo_runtime_config(options_.module2_config_path, yolo_runtime_config_, error_message)) {
        return false;
    }
    if (!load_yolo_analysis_config(options_.module2_config_path, yolo_analysis_config_, error_message)) {
        return false;
    }
    if (!load_inventory_runtime_config(resolve_inventory_config_path(repo_root_), inventory_config_, error_message)) {
        return false;
    }
    inventory_engine_ = InventoryEngine(inventory_config_);
    if (!load_local_service_config(options_.service_config_path, service_config_, error_message)) {
        return false;
    }

    if (options_.bind_host == "0.0.0.0" && !service_config_.bind_host.empty()) {
        options_.bind_host = service_config_.bind_host;
    }
    if (options_.public_host == "auto" && !service_config_.public_host.empty()) {
        options_.public_host = service_config_.public_host;
    }
    if (options_.port == 8080 && service_config_.port > 0) {
        options_.port = service_config_.port;
    }

    if (options_.roi_override.has_value()) {
        pipeline_config_.motion_config.roi = *options_.roi_override;
    }
    if (options_.mock_coarse_class.empty()) {
        options_.mock_coarse_class = default_mock_class_for_case(options_.case_id);
    }

    frame_history_limit_ = std::max<std::size_t>(
        64,
        pipeline_config_.frame_selector_config.max_disturbance_frames +
            pipeline_config_.frame_selector_config.baseline_warmup_frames +
            pipeline_config_.frame_selector_config.settle_run_frames +
            pipeline_config_.frame_selector_config.post_event_cooldown_frames +
            16
    );

    public_host_ = resolve_public_host(options_.public_host, options_.bind_host);
    preview_url_ = make_preview_url(public_host_, options_.port);
    status_url_ = make_status_url(public_host_, options_.port);
    index_url_ = make_index_url(public_host_, options_.port);
    return true;
}

std::string Module12RealtimeHarness::Impl::build_status_json() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::ostringstream output;
    output << "{\n"
           << "  \"status\": \"" << (stop_requested_.load() ? "stopping" : "running") << "\",\n"
           << "  \"mode\": \"live_camera\",\n"
           << "  \"pipeline_mode\": \"" << escape_json(pipeline_mode_string(options_)) << "\",\n"
           << "  \"capture_strategy\": \"stable_state_transition\",\n"
           << "  \"device\": \"" << escape_json(options_.device) << "\",\n"
           << "  \"preview_url\": \"" << escape_json(preview_url_) << "\",\n"
           << "  \"last_session_id\": \"" << escape_json(last_session_id_) << "\",\n"
           << "  \"last_event_type\": \"" << escape_json(last_event_type_) << "\",\n"
           << "  \"stage1_ready\": " << bool_to_json(stage1_ready_) << ",\n"
           << "  \"stage2_ready\": " << bool_to_json(stage2_ready_) << "\n"
           << "}\n";
    return output.str();
}

std::string Module12RealtimeHarness::Impl::build_latest_event_json() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return latest_event_json_;
}

std::string Module12RealtimeHarness::Impl::build_latest_stage2_json() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return latest_stage2_json_;
}

std::string Module12RealtimeHarness::Impl::build_latest_final_event_json() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return latest_final_event_json_;
}

std::string Module12RealtimeHarness::Impl::build_index_html() const {
    std::ostringstream output;
    output << "<!doctype html>\n"
           << "<html><head><meta charset=\"utf-8\"><title>Module12 Live Harness</title>"
           << "<style>body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:20px;}"
           << "img{max-width:100%;border:1px solid #444;}pre{background:#1d1d1d;padding:12px;overflow:auto;}"
           << "a{color:#9cf}.row{display:grid;grid-template-columns:2fr 1fr;gap:16px;align-items:start;}"
           << ".results{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px;margin-top:16px;}</style></head><body>\n"
           << "<h1>Module12 Realtime Live Harness</h1>\n"
           << "<p>Preview endpoint: <a href=\"/live.mjpeg\">/live.mjpeg</a> | "
           << "<a href=\"/status.json\">/status.json</a> | "
           << "<a href=\"/latest_event.json\">/latest_event.json</a> | "
           << "<a href=\"/latest_stage2.json\">/latest_stage2.json</a> | "
           << "<a href=\"/latest_final_event.json\">/latest_final_event.json</a></p>\n"
           << "<div class=\"row\"><div><img src=\"/live.mjpeg\" alt=\"live preview\"></div>"
           << "<div><h2>Status</h2><pre id=\"status\">loading...</pre>"
           << "<h2>Latest Event</h2><pre id=\"event\">waiting...</pre></div></div>\n"
           << "<div class=\"results\">"
           << "<div><h2>Latest Stage2</h2><pre id=\"stage2\">waiting...</pre></div>"
           << "<div><h2>Latest Final Event</h2><pre id=\"final\">waiting...</pre></div>"
           << "<div><h2>Quick Checks</h2><pre id=\"quick\">waiting...</pre></div>"
           << "</div>\n"
           << "<script>\n"
           << "async function refresh(){\n"
           << "  const status = await fetch('/status.json').then(r=>r.text()).catch(()=>'{\"status\":\"unavailable\"}');\n"
           << "  const event = await fetch('/latest_event.json').then(r=>r.text()).catch(()=>'{\"status\":\"unavailable\"}');\n"
           << "  const stage2 = await fetch('/latest_stage2.json').then(r=>r.text()).catch(()=>'{\"status\":\"unavailable\"}');\n"
           << "  const finalEvent = await fetch('/latest_final_event.json').then(r=>r.text()).catch(()=>'{\"status\":\"unavailable\"}');\n"
           << "  document.getElementById('status').textContent = status;\n"
           << "  document.getElementById('event').textContent = event;\n"
           << "  document.getElementById('stage2').textContent = stage2;\n"
           << "  document.getElementById('final').textContent = finalEvent;\n"
           << "  let quick = 'waiting...';\n"
           << "  try {\n"
           << "    const parsed = JSON.parse(event);\n"
           << "    quick = JSON.stringify({event_type: parsed.event_type, actual_stage2_event_type: parsed.actual_stage2_event_type, stage2_success: parsed.stage2_success, crop_artifact_count: parsed.crop_artifact_count, stage2_result_path: parsed.stage2_result_path, final_event_path: parsed.final_event_path, software_closure_report_path: parsed.software_closure_report_path}, null, 2);\n"
           << "  } catch (_) {}\n"
           << "  document.getElementById('quick').textContent = quick;\n"
           << "}\n"
           << "refresh(); setInterval(refresh, 1000);\n"
           << "</script></body></html>\n";
    return output.str();
}

bool Module12RealtimeHarness::Impl::run(std::string& error_message) {
    if (!load_configs(error_message)) {
        return false;
    }

    preview_publisher_ = std::make_unique<LivePreviewPublisher>(
        options_.bind_host,
        options_.port,
        [this]() { return frame_buffer_.latest_snapshot(); },
        [this]() { return build_status_json(); },
        [this]() { return build_latest_event_json(); },
        [this]() { return build_latest_stage2_json(); },
        [this]() { return build_latest_final_event_json(); },
        [this]() { return build_index_html(); }
    );

    latest_event_state_.module2_mode = to_string(options_.module2_mode);
    latest_event_state_.pipeline_mode = pipeline_mode_string(options_);
    latest_event_json_ = build_placeholder_latest_event_json();
    latest_stage2_json_ = build_placeholder_latest_stage2_json();
    latest_final_event_json_ = build_placeholder_latest_final_event_json();
    run_started_utc_ = now_as_utc_string();
    run_started_steady_ = std::chrono::steady_clock::now();

    std::filesystem::create_directories(options_.output_root);

    if (!preview_publisher_->start(error_message)) {
        return false;
    }

    if (!capture_thread_.start(error_message)) {
        preview_publisher_->stop();
        return false;
    }

    if (!options_.preview_only) {
        processing_thread_ = std::thread(&Impl::processing_loop, this);
    }

    std::cout << "live_service: " << index_url_ << "\n";
    std::cout << "preview_url: " << preview_url_ << "\n";
    std::cout << "status_url: " << status_url_ << "\n";
    std::cout << "bind_host: " << options_.bind_host << "\n";
    std::cout << "public_host: " << public_host_ << "\n";
    std::cout << "device: " << options_.device << "\n";
    std::cout << "pipeline_mode: " << pipeline_mode_string(options_) << "\n";
    std::cout << "module2_mode: " << to_string(options_.module2_mode) << "\n";

    while (!stop_requested_.load()) {
        if (capture_thread_.failed()) {
            error_message = capture_thread_.failure_message();
            stop_requested_.store(true);
            break;
        }

        if (options_.duration_seconds > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - run_started_steady_
            );
            if (elapsed.count() >= options_.duration_seconds) {
                stop_requested_.store(true);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    request_stop();

    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    capture_thread_.stop();
    preview_publisher_->stop();

    return error_message.empty();
}

void Module12RealtimeHarness::Impl::request_stop() {
    stop_requested_.store(true);
    frame_buffer_.stop();
}

void Module12RealtimeHarness::Impl::processing_loop() {
    StableStateCapture capture_state_machine(
        pipeline_config_.motion_config,
        pipeline_config_.frame_selector_config
    );
    bool baseline_announced = false;

    while (!stop_requested_.load()) {
        FramePacket current_frame;
        if (!frame_buffer_.wait_and_pop(current_frame, stop_requested_)) {
            if (stop_requested_.load() || frame_buffer_.stopped()) {
                break;
            }
            continue;
        }

        remember_frame_packet(current_frame);
        const std::optional<StableCaptureEvent> completed_event =
            capture_state_machine.push_frame(current_frame.gray_frame);
        if (!baseline_announced && capture_state_machine.has_baseline()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            stage1_ready_ = true;
            baseline_announced = true;
        }
        if (completed_event.has_value()) {
            process_event_window(*completed_event);
        }
    }

    if (const std::optional<StableCaptureEvent> flushed_event = capture_state_machine.flush();
        flushed_event.has_value()) {
        process_event_window(*flushed_event);
    }
}

Module2Execution Module12RealtimeHarness::Impl::run_module2(
    const std::string& session_id,
    const EventResult& stage1_capture_event,
    const GrayFrame& before_frame,
    const GrayFrame& after_frame,
    const ColorFrame& before_color,
    const ColorFrame& after_color,
    const std::filesystem::path& before_frame_path,
    const std::filesystem::path& after_frame_path,
    EventType mock_event_type
) const {
    Module2Execution execution;
    const YoloModule2Pipeline pipeline(yolo_runtime_config_, yolo_analysis_config_);

    if (options_.module2_mode == Module2Mode::RealOnnxRuntime) {
        const YoloRuntime runtime(yolo_runtime_config_);
        const YoloOnnxOutput before_output = runtime.run(before_color, repo_root_, execution.failure_reason);
        if (!execution.failure_reason.empty()) {
            return execution;
        }
        const YoloOnnxOutput after_output = runtime.run(after_color, repo_root_, execution.failure_reason);
        if (!execution.failure_reason.empty()) {
            return execution;
        }

        std::string runtime_error;
        execution.before_detections = pipeline.decode_output(before_output, before_frame, runtime_error);
        if (!runtime_error.empty()) {
            execution.failure_reason = runtime_error;
            return execution;
        }
        execution.after_detections = pipeline.decode_output(after_output, after_frame, runtime_error);
        if (!runtime_error.empty()) {
            execution.failure_reason = runtime_error;
            return execution;
        }

        execution.result = pipeline.analyze_outputs(
            before_frame,
            after_frame,
            before_output,
            after_output,
            session_id,
            path_to_utf8_string(before_frame_path),
            path_to_utf8_string(after_frame_path),
            runtime_error
        );
        if (!runtime_error.empty()) {
            execution.failure_reason = runtime_error;
            return execution;
        }

        execution.success = true;
        return execution;
    }

    // Test-only mock mode: generate ONNX-like outputs so the existing C++ module 2
    // decode and diff-analysis path can be exercised without a real runtime backend.
    const BoundingBox primary_box = choose_primary_box(
        stage1_capture_event,
        before_frame,
        after_frame,
        pipeline_config_.motion_config.roi
    );
    const std::string coarse_class =
        mock_event_type == EventType::PartialTakeOutCandidate
            ? "fruit_vegetable"
            : options_.mock_coarse_class;
    const int class_index = resolve_class_index(yolo_runtime_config_.class_names, coarse_class);

    std::vector<float> before_values;
    std::vector<float> after_values;
    auto push_row = [](std::vector<float>& values, const std::array<float, 6>& row) {
        values.insert(values.end(), row.begin(), row.end());
    };

    switch (mock_event_type) {
    case EventType::PutIn:
        push_row(after_values, make_onnx_row(primary_box, after_frame, yolo_runtime_config_, 0.92F, class_index));
        break;
    case EventType::TakeOut:
        push_row(before_values, make_onnx_row(primary_box, before_frame, yolo_runtime_config_, 0.92F, class_index));
        break;
    case EventType::PartialTakeOutCandidate: {
        const BoundingBox before_box = shrink_box(primary_box, 1.0);
        const BoundingBox after_box = shrink_box(primary_box, 0.65);
        push_row(before_values, make_onnx_row(before_box, before_frame, yolo_runtime_config_, 0.91F, class_index));
        push_row(after_values, make_onnx_row(after_box, after_frame, yolo_runtime_config_, 0.90F, class_index));
        break;
    }
    case EventType::Reorganize: {
        const BoundingBox before_box = shrink_box(primary_box, 0.90);
        const BoundingBox after_box{
            before_box.x + std::max(6, before_box.width / 4),
            before_box.y,
            before_box.width,
            before_box.height
        };
        push_row(before_values, make_onnx_row(before_box, before_frame, yolo_runtime_config_, 0.91F, class_index));
        push_row(after_values, make_onnx_row(after_box, after_frame, yolo_runtime_config_, 0.90F, class_index));
        break;
    }
    case EventType::Uncertain: {
        push_row(before_values, make_onnx_row(primary_box, before_frame, yolo_runtime_config_, 0.80F, class_index));
        const BoundingBox shifted_box{
            primary_box.x + primary_box.width / 3,
            primary_box.y,
            primary_box.width,
            primary_box.height
        };
        push_row(after_values, make_onnx_row(shifted_box, after_frame, yolo_runtime_config_, 0.79F, class_index));
        push_row(after_values, make_onnx_row(shrink_box(primary_box, 0.55), after_frame, yolo_runtime_config_, 0.77F, class_index));
        break;
    }
    case EventType::NoChange:
    case EventType::CaptureRecorded:
    case EventType::NotEvaluated:
        break;
    }

    const YoloOnnxOutput before_output{
        static_cast<int>(before_values.size() / static_cast<std::size_t>(yolo_runtime_config_.output_columns)),
        yolo_runtime_config_.output_columns,
        before_values
    };
    const YoloOnnxOutput after_output{
        static_cast<int>(after_values.size() / static_cast<std::size_t>(yolo_runtime_config_.output_columns)),
        yolo_runtime_config_.output_columns,
        after_values
    };

    std::string runtime_error;
    execution.before_detections = pipeline.decode_output(before_output, before_frame, runtime_error);
    if (!runtime_error.empty()) {
        execution.failure_reason = runtime_error;
        return execution;
    }
    execution.after_detections = pipeline.decode_output(after_output, after_frame, runtime_error);
    if (!runtime_error.empty()) {
        execution.failure_reason = runtime_error;
        return execution;
    }

    execution.result = pipeline.analyze_outputs(
        before_frame,
        after_frame,
        before_output,
        after_output,
        session_id,
        path_to_utf8_string(before_frame_path),
        path_to_utf8_string(after_frame_path),
        runtime_error
    );
    if (!runtime_error.empty()) {
        execution.failure_reason = runtime_error;
        return execution;
    }

    execution.success = true;
    return execution;
}

bool Module12RealtimeHarness::Impl::write_crop_artifacts(
    const SessionPaths& paths,
    Module2Execution& execution,
    const ColorFrame& before_color,
    const ColorFrame& after_color,
    std::string& error_message
) const {
    execution.crop_artifacts.clear();

    std::error_code filesystem_error;
    std::filesystem::remove_all(paths.stage2_crops_dir, filesystem_error);
    if (filesystem_error) {
        error_message = "Failed to reset crops directory: " + path_to_utf8_string(paths.stage2_crops_dir) +
                        " (" + filesystem_error.message() + ")";
        return false;
    }
    std::filesystem::create_directories(paths.stage2_crops_dir, filesystem_error);
    if (filesystem_error) {
        error_message = "Failed to create crops directory: " + path_to_utf8_string(paths.stage2_crops_dir) +
                        " (" + filesystem_error.message() + ")";
        return false;
    }

    if (!execution.success) {
        return true;
    }

    for (std::size_t index = 0; index < execution.result.crop_requests.size(); ++index) {
        const auto& request = execution.result.crop_requests[index];
        const ColorFrame& source_frame = request.source_frame == "before" ? before_color : after_color;
        const ColorFrame cropped = crop_color_frame(source_frame, request.bbox);
        std::ostringstream name;
        name << std::setw(2) << std::setfill('0') << index;
        const std::filesystem::path output_path =
            paths.stage2_crops_dir /
            (request.source_frame + "_" + name.str() + "_" + sanitize_token(request.coarse_class) + ".jpg");
        if (!write_color_debug_image(cropped, output_path, error_message)) {
            return false;
        }
        execution.crop_artifacts.push_back(CropArtifactRecord{request, path_to_utf8_string(output_path)});
    }
    return true;
}

bool Module12RealtimeHarness::Impl::write_detection_overlays(
    const SessionPaths& paths,
    const Module2Execution& execution,
    const ColorFrame& before_color,
    const ColorFrame& after_color,
    std::string& error_message
) const {
    const ColorFrame before_overlay = build_detection_overlay_frame(before_color, execution.before_detections);
    if (!write_color_debug_image(before_overlay, paths.stage2_detections_before_image_path, error_message)) {
        return false;
    }

    const ColorFrame after_overlay = build_detection_overlay_frame(after_color, execution.after_detections);
    if (!write_color_debug_image(after_overlay, paths.stage2_detections_after_image_path, error_message)) {
        return false;
    }

    return true;
}

void Module12RealtimeHarness::Impl::process_event_window(const StableCaptureEvent& capture_event) {
    const SelectedFrames& selected = capture_event.selected_frames;
    if (selected.before_frame.empty() || selected.after_frame.empty()) {
        return;
    }

    const std::string session_id = "live_" + now_as_local_filename_string() + "_" + sanitize_token(options_.case_id);
    const SessionPaths paths = artifact_writer_.create_layout(session_id);

    const ColorFrame before_color = [&]() {
        const std::optional<FramePacket> packet = find_frame_packet(selected.before_frame.index);
        return packet.has_value() ? decode_color_frame(*packet) : promote_gray_to_color(selected.before_frame);
    }();
    const ColorFrame after_color = [&]() {
        const std::optional<FramePacket> packet = find_frame_packet(selected.after_frame.index);
        return packet.has_value() ? decode_color_frame(*packet) : promote_gray_to_color(selected.after_frame);
    }();

    std::string error_message;
    if (!write_color_debug_image(after_color, paths.preview_latest_path, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }
    if (!write_color_debug_image(before_color, paths.stage1_before_path, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }
    if (!write_color_debug_image(after_color, paths.stage1_after_path, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    EventResult stage1_event = build_stage1_capture_event(
        session_id,
        pipeline_config_.roi_id,
        capture_event,
        paths.stage1_before_path,
        paths.stage1_after_path
    );

    const ColorFrame overlay = build_overlay_color_frame(
        after_color,
        pipeline_config_.motion_config.roi,
        stage1_event.change_regions
    );
    if (!write_color_debug_image(overlay, paths.stage1_overlay_path, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }
    if (!write_event_json(stage1_event, paths.stage1_event_path, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    if (!write_debug_summary(
            selected,
            stage1_event,
            pipeline_config_,
            DebugArtifacts{
                std::filesystem::path(options_.device),
                options_.module1_config_path,
                paths.stage1_before_path,
                paths.stage1_after_path,
                paths.stage1_overlay_path,
                paths.stage1_event_path
            },
            capture_event.total_frame_count,
            paths.stage1_debug_path,
            error_message
        )) {
        std::cerr << error_message << "\n";
        return;
    }

    Module2Execution stage2_execution;
    if (options_.capture_only) {
        stage2_execution.failure_reason = "capture_only: module2 skipped";
    } else {
        stage2_execution = run_module2(
            session_id,
            stage1_event,
            selected.before_frame,
            selected.after_frame,
            before_color,
            after_color,
            paths.stage1_before_path,
            paths.stage1_after_path,
            mock_event_type_for_case(options_.case_id)
        );
    }

    const std::string before_detection_json = build_detection_list_json(
        session_id,
        "before",
        options_.module2_mode,
        stage2_execution.before_detections,
        stage2_execution.failure_reason
    );
    const std::string after_detection_json = build_detection_list_json(
        session_id,
        "after",
        options_.module2_mode,
        stage2_execution.after_detections,
        stage2_execution.failure_reason
    );
    if (!artifact_writer_.write_text(paths.stage2_detections_before_path, before_detection_json, error_message) ||
        !artifact_writer_.write_text(paths.stage2_detections_after_path, after_detection_json, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    if (!options_.capture_only &&
        !write_detection_overlays(paths, stage2_execution, before_color, after_color, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    if (!options_.capture_only &&
        !write_crop_artifacts(paths, stage2_execution, before_color, after_color, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    const std::string stage2_result_json = build_module2_result_json(
        session_id,
        options_.module2_mode,
        stage2_execution,
        paths,
        EventType::NotEvaluated
    );
    if (!artifact_writer_.write_text(paths.stage2_result_path, stage2_result_json, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    const bool fallback_used = false;
    const std::string fallback_reason;
    const std::string stage2_failure_reason = options_.capture_only ? "" : stage2_execution.failure_reason;
    EventResult final_event = options_.capture_only
        ? stage1_event
        : (stage2_execution.success
               ? stage2_execution.result.event
               : build_stage2_unavailable_event(
                     stage1_event,
                     path_to_utf8_string(paths.stage1_before_path),
                     path_to_utf8_string(paths.stage1_after_path)
                 ));
    final_event.roi_id = pipeline_config_.roi_id;
    final_event.before_frame = path_to_utf8_string(paths.stage1_before_path);
    final_event.after_frame = path_to_utf8_string(paths.stage1_after_path);

    if (!write_event_json(final_event, paths.final_event_path, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    const LocalServiceFacade facade(service_config_);
    SoftwareClosureResult closure_result;
    const SoftwareClosureEvidencePaths closure_paths{
        paths.final_event_path,
        paths.inventory_response_path,
        paths.events_response_path,
        paths.pending_response_path,
        paths.software_closure_report_path
    };
    const SoftwareClosureContext closure_context{
        to_string(options_.module2_mode),
        pipeline_mode_string(options_),
        stage2_failure_reason,
        options_.module2_mode == Module2Mode::Mock
            ? "mock/debug evidence; not real ONNX, camera, or board validation"
            : "real_onnx_runtime live evidence; board validation is not implied"
    };
    const std::string closure_review_reason = options_.capture_only
        ? "capture_only: module2 skipped"
        : (stage2_execution.success ? stage2_execution.result.review_reason : stage2_execution.failure_reason);
    if (!write_software_closure_evidence(
            inventory_engine_,
            facade,
            final_event,
            closure_paths,
            closure_context,
            closure_review_reason,
            closure_result,
            error_message
        )) {
        std::cerr << error_message << "\n";
        return;
    }

    const std::string report_json = options_.capture_only
        ? build_capture_only_report_json(
              session_id,
              options_.case_id,
              options_.module2_mode,
              stage1_event,
              capture_event
          )
        : build_test_report_json(
              session_id,
              options_.case_id,
              options_.module2_mode,
              stage1_event,
              stage2_execution,
              final_event,
              stage2_failure_reason
          );
    if (!artifact_writer_.write_text(paths.final_report_path, report_json, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    const std::string capture_meta_json = build_live_capture_meta_json(
        options_,
        capture_thread_.actual_width(),
        capture_thread_.actual_height(),
        capture_thread_.actual_fps(),
        preview_url_,
        run_started_utc_
    );
    if (!artifact_writer_.write_text(paths.live_capture_meta_path, capture_meta_json, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    const std::string final_event_label = options_.capture_only ? "capture_recorded" : to_string(final_event.event_type);
    const std::string run_manifest_json = build_run_manifest_json(
        paths,
        session_id,
        options_.case_id,
        pipeline_mode_string(options_),
        preview_url_,
        status_url_,
        final_event_label,
        stage2_execution.success,
        stage2_failure_reason
    );
    if (!artifact_writer_.write_text(paths.run_manifest_path, run_manifest_json, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    const std::string latest_manifest_json = build_latest_run_manifest_json(
        session_id,
        options_.case_id,
        final_event_label,
        paths.session_dir
    );
    if (!artifact_writer_.write_latest_run_manifest(latest_manifest_json, error_message)) {
        std::cerr << error_message << "\n";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_session_id_ = session_id;
        last_event_type_ = final_event_label;
        stage1_ready_ = true;
        stage2_ready_ = !options_.capture_only && stage2_execution.success;
        latest_event_state_.has_event = true;
        latest_event_state_.capture_valid = true;
        latest_event_state_.stage2_skipped = options_.capture_only;
        latest_event_state_.stage2_success = stage2_execution.success;
        latest_event_state_.fallback_used = fallback_used;
        latest_event_state_.fallback_reason = fallback_reason;
        latest_event_state_.stage2_failure_reason = stage2_failure_reason;
        latest_event_state_.session_id = session_id;
        latest_event_state_.case_id = options_.case_id;
        latest_event_state_.event_type = final_event_label;
        latest_event_state_.stage1_event_type = to_string(stage1_event.event_type);
        latest_event_state_.stage2_event_type = options_.capture_only
            ? "skipped"
            : (stage2_execution.success
                   ? to_string(stage2_execution.result.event.event_type)
                   : to_string(EventType::NotEvaluated));
        latest_event_state_.pipeline_mode = pipeline_mode_string(options_);
        latest_event_state_.session_dir = path_to_utf8_string(paths.session_dir);
        latest_event_state_.report_path = path_to_utf8_string(paths.final_report_path);
        latest_event_state_.stage2_result_path = path_to_utf8_string(paths.stage2_result_path);
        latest_event_state_.final_event_path = path_to_utf8_string(paths.final_event_path);
        latest_event_state_.software_closure_report_path =
            path_to_utf8_string(paths.software_closure_report_path);
        latest_event_state_.crops_dir = path_to_utf8_string(paths.stage2_crops_dir);
        latest_event_state_.timestamp = final_event.timestamp;
        latest_event_state_.crop_artifact_count = stage2_execution.crop_artifacts.size();

        std::ostringstream latest_json;
        latest_json << "{\n"
                    << "  \"session_id\": \"" << escape_json(latest_event_state_.session_id) << "\",\n"
                    << "  \"case_id\": \"" << escape_json(latest_event_state_.case_id) << "\",\n"
                    << "  \"pipeline_mode\": \"" << escape_json(latest_event_state_.pipeline_mode) << "\",\n"
                    << "  \"capture_strategy\": \"stable_state_transition\",\n"
                    << "  \"event_type\": \"" << escape_json(latest_event_state_.event_type) << "\",\n"
                    << "  \"actual_stage1_event_type\": \"" << escape_json(latest_event_state_.stage1_event_type) << "\",\n"
                    << "  \"actual_stage2_event_type\": \"" << escape_json(latest_event_state_.stage2_event_type) << "\",\n"
                    << "  \"module2_mode\": \"" << to_string(options_.module2_mode) << "\",\n"
                    << "  \"capture_valid\": " << bool_to_json(latest_event_state_.capture_valid) << ",\n"
                    << "  \"stage2_skipped\": " << bool_to_json(latest_event_state_.stage2_skipped) << ",\n"
                    << "  \"stage2_success\": " << bool_to_json(latest_event_state_.stage2_success) << ",\n"
                    << "  \"fallback_used\": " << bool_to_json(latest_event_state_.fallback_used) << ",\n"
                    << "  \"fallback_reason\": \"" << escape_json(latest_event_state_.fallback_reason) << "\",\n"
                    << "  \"stage2_failure_reason\": \"" << escape_json(latest_event_state_.stage2_failure_reason) << "\",\n"
                    << "  \"session_dir\": \"" << escape_json(latest_event_state_.session_dir) << "\",\n"
                    << "  \"report_path\": \"" << escape_json(latest_event_state_.report_path) << "\",\n"
                    << "  \"stage2_result_path\": \"" << escape_json(latest_event_state_.stage2_result_path) << "\",\n"
                    << "  \"final_event_path\": \"" << escape_json(latest_event_state_.final_event_path) << "\",\n"
                    << "  \"software_closure_report_path\": \""
                    << escape_json(latest_event_state_.software_closure_report_path) << "\",\n"
                    << "  \"crops_dir\": \"" << escape_json(latest_event_state_.crops_dir) << "\",\n"
                    << "  \"crop_artifact_count\": " << latest_event_state_.crop_artifact_count << ",\n"
                    << "  \"timestamp\": \"" << escape_json(latest_event_state_.timestamp) << "\"\n"
                    << "}\n";
        latest_event_json_ = latest_json.str();
        latest_stage2_json_ = stage2_result_json;
        latest_final_event_json_ = event_result_to_json(final_event);
    }

    ++processed_event_count_;
    std::cout << "session_id: " << session_id
              << " final_event_type: " << final_event_label
              << " fallback_used: " << (fallback_used ? "true" : "false") << "\n";

    if (options_.stop_after_events > 0 && processed_event_count_ >= options_.stop_after_events) {
        stop_requested_.store(true);
        frame_buffer_.stop();
    }
}

Module12RealtimeHarness::Module12RealtimeHarness(LiveHarnessOptions options)
    : options_(std::move(options)) {}

Module12RealtimeHarness::~Module12RealtimeHarness() = default;

bool Module12RealtimeHarness::run(std::string& error_message) {
    impl_ = std::make_unique<Impl>(options_);
    return impl_->run(error_message);
}

void Module12RealtimeHarness::request_stop() {
    if (impl_) {
        impl_->request_stop();
    }
}

}  // namespace fridge::live_test
