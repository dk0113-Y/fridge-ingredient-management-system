#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "inventory_engine.hpp"
#include "local_service.hpp"
#include "service_config.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::filesystem::path cpp_source_dir() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    return std::filesystem::path(FRIDGE_CPP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path resolve_service_config_path() {
    const std::vector<std::filesystem::path> candidates = {
        cpp_source_dir() / "configs" / "module_5_local_service.cfg",
        std::filesystem::current_path() / "configs" / "module_5_local_service.cfg",
        std::filesystem::current_path() / "cpp" / "configs" / "module_5_local_service.cfg",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return cpp_source_dir() / "configs" / "module_5_local_service.cfg";
}

bool debug_local_service_facade() {
    fridge::LocalServiceConfig service_config;
    std::string error_message;
    if (!fridge::load_local_service_config(resolve_service_config_path(), service_config, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::InventoryEngine engine;
    engine.apply_event(fridge::InventoryEventInput{
        "session_service",
        "2026-03-29 16:00:00",
        fridge::EventType::PutIn,
        "packaged_food",
        "milk",
        1,
        1.0,
        0.90,
        0.0,
        ""
    });

    engine.apply_event(fridge::InventoryEventInput{
        "session_service_pending",
        "2026-03-29 16:05:00",
        fridge::EventType::Uncertain,
        "fruit_vegetable",
        "apple",
        0,
        0.0,
        0.51,
        0.0,
        "needs review"
    });

    fridge::LocalServiceFacade facade(service_config);
    const std::string health_json = facade.handle_health(engine);
    const std::string inventory_json = facade.handle_inventory(engine);
    const std::string events_json = facade.handle_events(engine);
    const std::string pending_json = facade.handle_pending(engine);
    const std::string confirm_json = facade.handle_confirm(
        engine,
        fridge::PendingDecision{
            "session_service_pending",
            "reject",
            "apple",
            "fruit_vegetable",
            0,
            0.0,
            "debug reject"
        }
    );

    std::cout << "module_5_debug: health=" << health_json << "\n";

    return expect(health_json.find(service_config.service_name) != std::string::npos, "module 5 health should expose service name") &&
           expect(inventory_json.find("milk") != std::string::npos, "module 5 inventory endpoint should expose inventory items") &&
           expect(events_json.find("session_service") != std::string::npos, "module 5 events endpoint should expose event history") &&
           expect(pending_json.find("session_service_pending") != std::string::npos, "module 5 pending endpoint should expose pending items") &&
           expect(confirm_json.find("\"ok\": true") != std::string::npos, "module 5 confirm endpoint should report success");
}

}  // namespace

int main() {
    if (!debug_local_service_facade()) {
        return EXIT_FAILURE;
    }

    std::cout << "module_5_debug passed\n";
    return EXIT_SUCCESS;
}
