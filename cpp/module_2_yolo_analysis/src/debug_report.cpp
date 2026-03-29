#include "debug_report.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fridge {

namespace {

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

double frame_mean_intensity(const GrayFrame& frame) {
    if (frame.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (const auto pixel : frame.pixels) {
        sum += static_cast<double>(pixel);
    }
    return sum / static_cast<double>(frame.pixels.size());
}

std::size_t peak_transition_index(const std::vector<MotionSummary>& transitions) {
    std::size_t best_index = 0;
    double best_ratio = -1.0;
    for (std::size_t index = 0; index < transitions.size(); ++index) {
        if (transitions[index].changed_ratio > best_ratio) {
            best_ratio = transitions[index].changed_ratio;
            best_index = index;
        }
    }
    return best_index;
}

}  // namespace

bool write_debug_summary(
    const SelectedFrames& selected_frames,
    const EventResult& event_result,
    const VisionPipelineConfig& config,
    const DebugArtifacts& artifacts,
    std::size_t total_frame_count,
    const std::filesystem::path& output_path,
    std::string& error_message
) {
    std::filesystem::create_directories(output_path.parent_path());

    std::ofstream output(output_path);
    if (!output) {
        error_message = "Failed to open debug summary output: " + path_to_utf8_string(output_path);
        return false;
    }

    output << "{\n";
    output << "  \"session_id\": \"" << escape_json(event_result.session_id) << "\",\n";
    output << "  \"input_path\": \"" << escape_json(path_to_utf8_string(artifacts.input_path)) << "\",\n";
    output << "  \"config_path\": \"" << escape_json(path_to_utf8_string(artifacts.config_path)) << "\",\n";
    output << "  \"outputs\": {\n";
    output << "    \"before_frame\": \"" << escape_json(path_to_utf8_string(artifacts.before_path)) << "\",\n";
    output << "    \"after_frame\": \"" << escape_json(path_to_utf8_string(artifacts.after_path)) << "\",\n";
    output << "    \"overlay_frame\": \"" << escape_json(path_to_utf8_string(artifacts.overlay_path)) << "\",\n";
    output << "    \"event_json\": \"" << escape_json(path_to_utf8_string(artifacts.event_path)) << "\"\n";
    output << "  },\n";
    output << "  \"roi\": {\n";
    output << "    \"roi_id\": \"" << escape_json(config.roi_id) << "\",\n";
    output << "    \"x\": " << config.motion_config.roi.x << ",\n";
    output << "    \"y\": " << config.motion_config.roi.y << ",\n";
    output << "    \"width\": " << config.motion_config.roi.width << ",\n";
    output << "    \"height\": " << config.motion_config.roi.height << "\n";
    output << "  },\n";
    output << "  \"thresholds\": {\n";
    output << "    \"pixel_delta_threshold\": " << config.motion_config.pixel_delta_threshold << ",\n";
    output << "    \"min_region_area_pixels\": " << config.motion_config.min_region_area_pixels << ",\n";
    output << "    \"stable_ratio_threshold\": " << std::fixed << std::setprecision(4)
           << config.frame_selector_config.stable_ratio_threshold << ",\n";
    output << "    \"motion_ratio_threshold\": " << std::fixed << std::setprecision(4)
           << config.frame_selector_config.motion_ratio_threshold << ",\n";
    output << "    \"black_frame_mean_threshold\": " << std::fixed << std::setprecision(3)
           << config.frame_selector_config.black_frame_mean_threshold << ",\n";
    output << "    \"min_stable_run_frames\": " << config.frame_selector_config.min_stable_run_frames << ",\n";
    output << "    \"no_change_ratio\": " << std::fixed << std::setprecision(4)
           << config.detector_config.no_change_ratio << ",\n";
    output << "    \"event_ratio\": " << std::fixed << std::setprecision(4)
           << config.detector_config.event_ratio << ",\n";
    output << "    \"partial_ratio\": " << std::fixed << std::setprecision(4)
           << config.detector_config.partial_ratio << ",\n";
    output << "    \"signed_delta_threshold\": " << std::fixed << std::setprecision(3)
           << config.detector_config.signed_delta_threshold << ",\n";
    output << "    \"dominant_polarity_threshold\": " << std::fixed << std::setprecision(3)
           << config.detector_config.dominant_polarity_threshold << ",\n";
    output << "    \"reorg_balance_threshold\": " << std::fixed << std::setprecision(3)
           << config.detector_config.reorg_balance_threshold << ",\n";
    output << "    \"background_like_threshold\": " << std::fixed << std::setprecision(3)
           << config.detector_config.background_like_threshold << ",\n";
    output << "    \"region_direction_margin\": " << std::fixed << std::setprecision(3)
           << config.detector_config.region_direction_margin << ",\n";
    output << "    \"reorg_region_threshold\": " << config.detector_config.reorg_region_threshold << "\n";
    output << "  },\n";
    output << "  \"selection\": {\n";
    output << "    \"total_frame_count\": " << total_frame_count << ",\n";
    output << "    \"before_index\": " << selected_frames.before_index << ",\n";
    output << "    \"after_index\": " << selected_frames.after_index << ",\n";
    output << "    \"before_mean_intensity\": " << std::fixed << std::setprecision(3)
           << frame_mean_intensity(selected_frames.before_frame) << ",\n";
    output << "    \"after_mean_intensity\": " << std::fixed << std::setprecision(3)
           << frame_mean_intensity(selected_frames.after_frame) << ",\n";
    output << "    \"transition_count\": " << selected_frames.transitions.size() << ",\n";
    output << "    \"peak_transition_index\": "
           << (selected_frames.transitions.empty() ? 0 : peak_transition_index(selected_frames.transitions)) << "\n";
    output << "  },\n";
    output << "  \"event\": {\n";
    output << "    \"event_type\": \"" << escape_json(to_string(event_result.event_type)) << "\",\n";
    output << "    \"confidence\": " << std::fixed << std::setprecision(3) << event_result.confidence << ",\n";
    output << "    \"need_user_confirm\": " << (event_result.need_user_confirm ? "true" : "false") << ",\n";
    output << "    \"change_region_count\": " << event_result.change_regions.size() << "\n";
    output << "  },\n";
    output << "  \"transitions\": [";
    if (!selected_frames.transitions.empty()) {
        output << "\n";
        for (std::size_t index = 0; index < selected_frames.transitions.size(); ++index) {
            const auto& transition = selected_frames.transitions[index];
            output << "    {\n";
            output << "      \"index\": " << index << ",\n";
            output << "      \"has_motion\": " << (transition.has_motion ? "true" : "false") << ",\n";
            output << "      \"changed_ratio\": " << std::fixed << std::setprecision(4) << transition.changed_ratio << ",\n";
            output << "      \"mean_delta\": " << std::fixed << std::setprecision(3) << transition.mean_delta << ",\n";
            output << "      \"box\": {\n";
            output << "        \"x\": " << transition.box.x << ",\n";
            output << "        \"y\": " << transition.box.y << ",\n";
            output << "        \"width\": " << transition.box.width << ",\n";
            output << "        \"height\": " << transition.box.height << "\n";
            output << "      }\n";
            output << "    }";
            if (index + 1 < selected_frames.transitions.size()) {
                output << ",";
            }
            output << "\n";
        }
        output << "  ";
    }
    output << "]\n";
    output << "}\n";

    if (!output) {
        error_message = "Failed to write debug summary output: " + path_to_utf8_string(output_path);
        return false;
    }

    return true;
}

}  // namespace fridge
