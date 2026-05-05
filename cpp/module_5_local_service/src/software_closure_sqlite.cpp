#include "software_closure.hpp"

#include <filesystem>

#include "sqlite_inventory_store.hpp"

namespace fridge {

namespace {

std::string path_to_display_string(const std::filesystem::path& path) {
    return path.generic_string();
}

SoftwareClosurePersistenceStatus requested_status(
    const SoftwareClosurePersistenceOptions& options
) {
    SoftwareClosurePersistenceStatus status;
    status.requested = options.enable_sqlite;
    status.sqlite_compiled = true;
    status.database_path = path_to_display_string(options.database_path);
    return status;
}

}  // namespace

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
) {
    if (!persistence_options.enable_sqlite) {
        return write_software_closure_evidence(
            engine,
            facade,
            event,
            paths,
            context,
            review_reason,
            result,
            error_message
        );
    }

    result = SoftwareClosureResult{};
    result.persistence_status = requested_status(persistence_options);
    if (persistence_options.database_path.empty()) {
        result.persistence_status.message = "sqlite persistence requested but database path is empty";
        error_message = result.persistence_status.message;
        return false;
    }

    const bool database_existed = std::filesystem::exists(persistence_options.database_path);
    SQLiteInventoryStore store(persistence_options.database_path);
    if (!store.open(error_message)) {
        result.persistence_status.message = "sqlite open failed: " + error_message;
        error_message = result.persistence_status.message;
        return false;
    }
    result.persistence_status.db_ready = store.db_ready();

    if (!store.initialize_schema(error_message)) {
        result.persistence_status.message = "sqlite schema initialization failed: " + error_message;
        error_message = result.persistence_status.message;
        return false;
    }

    if (persistence_options.reset_database_for_debug) {
        if (!store.reset(error_message)) {
            result.persistence_status.message = "sqlite reset failed: " + error_message;
            error_message = result.persistence_status.message;
            return false;
        }
    } else if (database_existed) {
        if (!store.load_snapshot(engine, error_message)) {
            result.persistence_status.message = "sqlite load failed: " + error_message;
            error_message = result.persistence_status.message;
            return false;
        }
        result.persistence_status.loaded_existing_state = true;
    }

    if (!apply_software_closure_event(engine, event, review_reason, result)) {
        result.persistence_status.message = "software closure apply failed";
        error_message = result.persistence_status.message;
        return false;
    }

    if (!store.save_snapshot(engine, error_message)) {
        result.persistence_status.message = "sqlite save failed: " + error_message;
        error_message = result.persistence_status.message;
        return false;
    }
    result.persistence_status.saved_after_apply = true;

    if (persistence_options.reset_database_for_debug) {
        result.persistence_status.message = "sqlite persistence reset, applied, and saved";
    } else if (result.persistence_status.loaded_existing_state) {
        result.persistence_status.message = "sqlite persistence loaded existing state, applied, and saved";
    } else {
        result.persistence_status.message = "sqlite persistence initialized, applied, and saved";
    }

    return write_software_closure_outputs(
        engine,
        facade,
        event,
        paths,
        context,
        result,
        error_message
    );
}

}  // namespace fridge
