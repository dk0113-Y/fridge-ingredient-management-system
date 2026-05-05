#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "inventory_engine.hpp"
#include "local_http_server.hpp"
#include "local_service.hpp"
#include "service_config.hpp"

#ifdef FRIDGE_HAS_SQLITE
#include "sqlite_inventory_store.hpp"
#endif

namespace {

namespace fs = std::filesystem;

struct AppOptions {
    std::string host;
    int port = 0;
    fs::path service_config_path;
    fs::path inventory_config_path;
    bool enable_sqlite_persistence = false;
    fs::path sqlite_db_path;
    bool seed_demo_data = false;
};

fs::path repo_root() {
#ifdef FRIDGE_CPP_SOURCE_DIR
    const fs::path source_dir = fs::path(FRIDGE_CPP_SOURCE_DIR);
    if (source_dir.filename() == "cpp") {
        return source_dir.parent_path();
    }
#endif
    return fs::current_path();
}

fs::path default_service_config_path() {
    return repo_root() / "cpp" / "configs" / "module_5_local_service.cfg";
}

fs::path default_inventory_config_path() {
    return repo_root() / "cpp" / "configs" / "module_4_inventory.cfg";
}

fs::path default_sqlite_db_path() {
    return repo_root() / "data" / "runtime" / "fridge_inventory.db";
}

fs::path resolve_path(const fs::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }
    return repo_root() / path;
}

void print_usage() {
    std::cerr
        << "Usage: fridge_local_service_server [options]\n\n"
        << "Options:\n"
        << "  --host <ip>                    bind host, default from module_5 config or 0.0.0.0\n"
        << "  --port <number>                bind port, default from module_5 config or 8080\n"
        << "  --service-config <path>        default cpp/configs/module_5_local_service.cfg\n"
        << "  --inventory-config <path>      default cpp/configs/module_4_inventory.cfg\n"
        << "  --enable-sqlite-persistence    load/save InventoryEngine snapshot through SQLite\n"
        << "  --sqlite-db <path>             SQLite path, implies --enable-sqlite-persistence\n"
        << "  --seed-demo-data               seed one deterministic manual inventory item\n";
}

bool parse_args(int argc, char** argv, AppOptions& options, std::string& error_message) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto require_value = [&](const std::string& option_name) -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument("Missing value for " + option_name);
            }
            ++index;
            return argv[index];
        };

        try {
            if (arg == "--host") {
                options.host = require_value("--host");
            } else if (arg == "--port") {
                options.port = std::stoi(require_value("--port"));
            } else if (arg == "--service-config") {
                options.service_config_path = require_value("--service-config");
            } else if (arg == "--inventory-config") {
                options.inventory_config_path = require_value("--inventory-config");
            } else if (arg == "--enable-sqlite-persistence") {
                options.enable_sqlite_persistence = true;
            } else if (arg == "--sqlite-db") {
                options.sqlite_db_path = require_value("--sqlite-db");
                options.enable_sqlite_persistence = true;
            } else if (arg == "--seed-demo-data" || arg == "--demo-data") {
                options.seed_demo_data = true;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                std::exit(0);
            } else {
                error_message = "Unknown argument: " + arg;
                return false;
            }
        } catch (const std::exception& ex) {
            error_message = ex.what();
            return false;
        }
    }

    if (options.port < 0 || options.port > 65535) {
        error_message = "--port must be between 0 and 65535";
        return false;
    }
    return true;
}

bool seed_demo_data(fridge::InventoryEngine& engine, std::string& error_message) {
    return engine.manual_update(
        fridge::ManualInventoryUpdate{
            "demo_yogurt",
            "packaged_food",
            1,
            1.0,
            "2026-04-12",
            "server demo seed"
        },
        error_message
    );
}

}  // namespace

int main(int argc, char** argv) {
    AppOptions options;
    std::string error_message;
    if (!parse_args(argc, argv, options, error_message)) {
        print_usage();
        std::cerr << "Argument error: " << error_message << "\n";
        return EXIT_FAILURE;
    }

    fridge::LocalServiceConfig service_config;
    const fs::path service_config_path = options.service_config_path.empty()
        ? default_service_config_path()
        : resolve_path(options.service_config_path);
    if (!fridge::load_local_service_config(service_config_path, service_config, error_message)) {
        std::cerr << error_message << "\n";
        return EXIT_FAILURE;
    }
    if (!options.host.empty()) {
        service_config.bind_host = options.host;
    }
    if (options.port > 0) {
        service_config.port = options.port;
    }
    if (service_config.bind_host.empty()) {
        service_config.bind_host = "0.0.0.0";
    }
    if (service_config.port <= 0) {
        service_config.port = 8080;
    }

    fridge::InventoryRuntimeConfig inventory_config;
    const fs::path inventory_config_path = options.inventory_config_path.empty()
        ? default_inventory_config_path()
        : resolve_path(options.inventory_config_path);
    if (!fridge::load_inventory_runtime_config(inventory_config_path, inventory_config, error_message)) {
        std::cerr << error_message << "\n";
        return EXIT_FAILURE;
    }

    fridge::InventoryEngine engine(inventory_config);

    fridge::LocalHttpServer::SaveSnapshotCallback save_snapshot;
    bool db_ready = true;
    const fs::path sqlite_db_path = options.sqlite_db_path.empty()
        ? default_sqlite_db_path()
        : resolve_path(options.sqlite_db_path);

    if (options.enable_sqlite_persistence) {
#ifdef FRIDGE_HAS_SQLITE
        auto sqlite_store = std::make_shared<fridge::SQLiteInventoryStore>(sqlite_db_path);
        const bool database_existed = fs::exists(sqlite_db_path);
        if (!sqlite_store->open(error_message) ||
            !sqlite_store->initialize_schema(error_message)) {
            std::cerr << error_message << "\n";
            return EXIT_FAILURE;
        }
        if (database_existed && !sqlite_store->load_snapshot(engine, error_message)) {
            std::cerr << "sqlite load failed: " << error_message << "\n";
            return EXIT_FAILURE;
        }
        db_ready = sqlite_store->db_ready();
        save_snapshot = [sqlite_store, &engine](std::string& save_error) {
            return sqlite_store->save_snapshot(engine, save_error);
        };
        std::cout << "SQLite persistence enabled: " << sqlite_db_path.generic_string() << "\n";
#else
        std::cerr << "SQLite persistence requested but this binary was built without sqlite3 support\n";
        return EXIT_FAILURE;
#endif
    }

    if (options.seed_demo_data) {
        if (!seed_demo_data(engine, error_message)) {
            std::cerr << error_message << "\n";
            return EXIT_FAILURE;
        }
        if (save_snapshot && !save_snapshot(error_message)) {
            std::cerr << "sqlite save failed after demo seed: " << error_message << "\n";
            return EXIT_FAILURE;
        }
    }

    fridge::LocalServiceFacade facade(service_config);
    fridge::LocalHttpServer server(
        fridge::LocalHttpServerOptions{
            service_config.bind_host,
            service_config.port,
            db_ready
        },
        engine,
        facade,
        save_snapshot
    );

    if (!server.run(error_message)) {
        std::cerr << error_message << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
