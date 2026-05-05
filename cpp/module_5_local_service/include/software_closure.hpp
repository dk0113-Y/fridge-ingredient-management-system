#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "inventory_engine.hpp"
#include "local_service.hpp"
#include "types.hpp"

namespace fridge {

struct SoftwareClosureEvidencePaths {
    std::filesystem::path final_event_path;
    std::filesystem::path inventory_response_path;
    std::filesystem::path events_response_path;
    std::filesystem::path pending_response_path;
    std::filesystem::path software_closure_report_path;
};

struct SoftwareClosureContext {
    std::string module2_mode;
    std::string runtime_mode;
    std::string fallback_reason;
    std::string notes;
    bool sqlite_compiled = false;
};

struct SoftwareClosurePersistenceOptions {
    bool enable_sqlite = false;
    std::filesystem::path database_path;
    bool reset_database_for_debug = false;
};

struct SoftwareClosurePersistenceStatus {
    bool requested = false;
    bool sqlite_compiled = false;
    bool db_ready = false;
    bool loaded_existing_state = false;
    bool saved_after_apply = false;
    std::string database_path;
    std::string message;
};

struct SoftwareClosureResult {
    InventoryEventInput inventory_input;
    InventoryApplyResult apply_result;
    std::size_t pending_review_count = 0;
    std::size_t inventory_item_count = 0;
    std::size_t event_log_count = 0;
    std::string closure_status;
    SoftwareClosurePersistenceStatus persistence_status;
};

InventoryEventInput map_event_to_inventory_input(
    const EventResult& event,
    const std::string& review_reason = {}
);

std::string build_software_closure_report_json(
    const EventResult& event,
    const SoftwareClosureContext& context,
    const SoftwareClosureEvidencePaths& paths,
    const SoftwareClosureResult& result
);

bool write_software_closure_evidence(
    InventoryEngine& engine,
    const LocalServiceFacade& facade,
    const EventResult& event,
    const SoftwareClosureEvidencePaths& paths,
    const SoftwareClosureContext& context,
    const std::string& review_reason,
    SoftwareClosureResult& result,
    std::string& error_message
);

bool apply_software_closure_event(
    InventoryEngine& engine,
    const EventResult& event,
    const std::string& review_reason,
    SoftwareClosureResult& result
);

bool write_software_closure_outputs(
    const InventoryEngine& engine,
    const LocalServiceFacade& facade,
    const EventResult& event,
    const SoftwareClosureEvidencePaths& paths,
    const SoftwareClosureContext& context,
    const SoftwareClosureResult& result,
    std::string& error_message
);

bool write_software_closure_evidence_with_sqlite_persistence(
    InventoryEngine& engine,
    const LocalServiceFacade& facade,
    const EventResult& event,
    const SoftwareClosureEvidencePaths& paths,
    const SoftwareClosureContext& context,
    const std::string& review_reason,
    const SoftwareClosurePersistenceOptions& persistence_options,
    SoftwareClosureResult& result,
    std::string& error_message
);

}  // namespace fridge
