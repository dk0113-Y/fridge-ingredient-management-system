#include "software_closure.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fridge {

namespace {

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

std::string path_to_display_string(const std::filesystem::path& path) {
    return path.generic_string();
}

std::string non_empty_or(const std::string& value, const std::string& fallback) {
    return value.empty() || value == "unknown" ? fallback : value;
}

int fallback_quantity_delta(EventType event_type) {
    if (event_type == EventType::PutIn) {
        return 1;
    }
    if (event_type == EventType::TakeOut) {
        return -1;
    }
    return 0;
}

double fallback_remain_level(EventType event_type) {
    if (event_type == EventType::PutIn) {
        return 1.0;
    }
    if (event_type == EventType::PartialTakeOutCandidate) {
        return 0.5;
    }
    return 0.0;
}

std::string default_review_reason(EventType event_type) {
    if (event_type == EventType::PartialTakeOutCandidate) {
        return "partial_take_out_candidate requires manual confirmation";
    }
    if (event_type == EventType::Uncertain) {
        return "uncertain event requires manual confirmation";
    }
    if (event_type == EventType::NotEvaluated) {
        return "module 2 event was not evaluated";
    }
    return {};
}

bool write_text_file(
    const std::filesystem::path& output_path,
    const std::string& text,
    std::string& error_message
) {
    std::filesystem::create_directories(output_path.parent_path());

    std::ofstream output(output_path);
    if (!output) {
        error_message = "Failed to open software closure output: " + path_to_display_string(output_path);
        return false;
    }

    output << text;
    if (!output) {
        error_message = "Failed to write software closure output: " + path_to_display_string(output_path);
        return false;
    }
    return true;
}

std::string evidence_files_to_json(const SoftwareClosureEvidencePaths& paths) {
    std::ostringstream output;
    output << "{\n"
           << "    \"event\": \"" << escape_json(path_to_display_string(paths.final_event_path)) << "\",\n"
           << "    \"inventory_response\": \"" << escape_json(path_to_display_string(paths.inventory_response_path)) << "\",\n"
           << "    \"events_response\": \"" << escape_json(path_to_display_string(paths.events_response_path)) << "\",\n"
           << "    \"pending_response\": \"" << escape_json(path_to_display_string(paths.pending_response_path)) << "\",\n"
           << "    \"software_closure_report\": \""
           << escape_json(path_to_display_string(paths.software_closure_report_path)) << "\"\n"
           << "  }";
    return output.str();
}

}  // namespace

InventoryEventInput map_event_to_inventory_input(
    const EventResult& event,
    const std::string& review_reason
) {
    DetectedObject object;
    if (!event.objects.empty()) {
        object = event.objects.front();
    }

    const std::string coarse_class = non_empty_or(object.category, "unknown");
    const std::string fine_name = non_empty_or(object.name, coarse_class);
    const int quantity_delta = object.count_delta != 0
        ? object.count_delta
        : fallback_quantity_delta(event.event_type);
    const double remain_level = object.remain_level > 0.0
        ? object.remain_level
        : fallback_remain_level(event.event_type);

    std::string normalized_review_reason = review_reason;
    if (normalized_review_reason.empty()) {
        normalized_review_reason = default_review_reason(event.event_type);
    }

    return InventoryEventInput{
        event.session_id,
        event.timestamp,
        event.event_type,
        coarse_class,
        fine_name,
        quantity_delta,
        std::clamp(remain_level, 0.0, 1.0),
        event.confidence,
        0.0,
        normalized_review_reason
    };
}

std::string build_software_closure_report_json(
    const EventResult& event,
    const SoftwareClosureContext& context,
    const SoftwareClosureEvidencePaths& paths,
    const SoftwareClosureResult& result
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"session_id\": \"" << escape_json(event.session_id) << "\",\n"
           << "  \"closure_status\": \"" << escape_json(result.closure_status) << "\",\n"
           << "  \"event_type\": \"" << escape_json(to_string(event.event_type)) << "\",\n"
           << "  \"coarse_class\": \"" << escape_json(result.inventory_input.coarse_class) << "\",\n"
           << "  \"fine_name\": \"" << escape_json(result.inventory_input.fine_name) << "\",\n"
           << "  \"quantity_delta\": " << result.inventory_input.quantity_delta << ",\n"
           << "  \"yolo_confidence\": " << std::fixed << std::setprecision(3)
           << result.inventory_input.yolo_confidence << ",\n"
           << "  \"llm_confidence\": " << std::fixed << std::setprecision(3)
           << result.inventory_input.llm_confidence << ",\n"
           << "  \"inventory_updated\": " << bool_to_json(result.apply_result.inventory_updated) << ",\n"
           << "  \"pending_created\": " << bool_to_json(result.apply_result.pending_created) << ",\n"
           << "  \"pending_review_count\": " << result.pending_review_count << ",\n"
           << "  \"inventory_item_count\": " << result.inventory_item_count << ",\n"
           << "  \"event_log_count\": " << result.event_log_count << ",\n"
           << "  \"module2_mode\": \"" << escape_json(context.module2_mode) << "\",\n"
           << "  \"runtime_mode\": \"" << escape_json(context.runtime_mode) << "\",\n"
           << "  \"review_reason\": \"" << escape_json(result.inventory_input.review_reason) << "\",\n"
           << "  \"apply_status\": \"" << escape_json(result.apply_result.status) << "\",\n"
           << "  \"apply_message\": \"" << escape_json(result.apply_result.message) << "\",\n"
           << "  \"fallback_reason\": \"" << escape_json(context.fallback_reason) << "\",\n"
           << "  \"notes\": \"" << escape_json(context.notes) << "\",\n"
           << "  \"evidence_files\": " << evidence_files_to_json(paths) << "\n"
           << "}\n";
    return output.str();
}

bool write_software_closure_evidence(
    InventoryEngine& engine,
    const LocalServiceFacade& facade,
    const EventResult& event,
    const SoftwareClosureEvidencePaths& paths,
    const SoftwareClosureContext& context,
    const std::string& review_reason,
    SoftwareClosureResult& result,
    std::string& error_message
) {
    result = SoftwareClosureResult{};
    result.inventory_input = map_event_to_inventory_input(event, review_reason);
    result.apply_result = engine.apply_event(result.inventory_input);
    result.pending_review_count = engine.pending_reviews().size();
    result.inventory_item_count = engine.inventory_items().size();
    result.event_log_count = engine.event_log().size();
    result.closure_status = result.apply_result.status.empty()
        ? "unknown"
        : result.apply_result.status;

    if (!write_text_file(paths.inventory_response_path, facade.handle_inventory(engine), error_message) ||
        !write_text_file(paths.events_response_path, facade.handle_events(engine), error_message) ||
        !write_text_file(paths.pending_response_path, facade.handle_pending(engine), error_message)) {
        return false;
    }

    return write_text_file(
        paths.software_closure_report_path,
        build_software_closure_report_json(event, context, paths, result),
        error_message
    );
}

}  // namespace fridge
