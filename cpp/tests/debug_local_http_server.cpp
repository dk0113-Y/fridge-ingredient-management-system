#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "inventory_engine.hpp"
#include "local_http_server.hpp"
#include "local_service.hpp"

#ifdef FRIDGE_HAS_SQLITE
#include "sqlite_inventory_store.hpp"
#endif

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalid_socket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle invalid_socket = -1;
#endif

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

void close_socket(SocketHandle socket_handle) {
    if (socket_handle == invalid_socket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ok_ = true;
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        if (ok_) {
            WSACleanup();
        }
#endif
    }

    bool ok() const {
        return ok_;
    }

private:
    bool ok_ = false;
};

struct HttpClientResponse {
    int status_code = 0;
    std::string raw;
    std::string body;
};

bool send_all(SocketHandle socket_handle, const std::string& data) {
    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
        const int sent = send(
            socket_handle,
            data.data() + sent_total,
            static_cast<int>(data.size() - sent_total),
            0
        );
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

bool http_request(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    HttpClientResponse& response
) {
    SocketHandle client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == invalid_socket) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(18080);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        close_socket(client_socket);
        return false;
    }
    if (connect(client_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(client_socket);
        return false;
    }

    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n"
            << "Host: 127.0.0.1:18080\r\n"
            << "Connection: close\r\n";
    if (!body.empty()) {
        request << "Content-Type: application/json\r\n"
                << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n" << body;

    if (!send_all(client_socket, request.str())) {
        close_socket(client_socket);
        return false;
    }

    response.raw.clear();
    char buffer[4096];
    while (true) {
        const int received = recv(client_socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            break;
        }
        response.raw.append(buffer, static_cast<std::size_t>(received));
    }
    close_socket(client_socket);

    const std::size_t first_space = response.raw.find(' ');
    if (first_space != std::string::npos && first_space + 4 <= response.raw.size()) {
        response.status_code = std::stoi(response.raw.substr(first_space + 1, 3));
    }
    const std::size_t body_pos = response.raw.find("\r\n\r\n");
    response.body = body_pos == std::string::npos ? std::string{} : response.raw.substr(body_pos + 4);
    return !response.raw.empty();
}

bool run_server_thread(fridge::LocalHttpServer& server, std::size_t request_count, std::thread& thread) {
    thread = std::thread([&server, request_count]() {
        std::string error_message;
        if (!server.run_for_request_count(request_count, error_message)) {
            std::cerr << "HTTP server debug run failed: " << error_message << "\n";
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return true;
}

bool debug_http_routes() {
    SocketRuntime socket_runtime;
    if (!socket_runtime.ok()) {
        std::cerr << "Socket runtime initialization failed\n";
        return false;
    }

    fridge::InventoryRuntimeConfig inventory_config;
    inventory_config.auto_commit_min_confidence = 0.75;
    fridge::InventoryEngine engine(inventory_config);
    engine.apply_event(fridge::InventoryEventInput{
        "http_pending_seed",
        "2026-03-29T14:32:10Z",
        fridge::EventType::Uncertain,
        "fruit_vegetable",
        "apple",
        0,
        0.5,
        0.40,
        0.0,
        "http debug seeded pending review"
    });

    fridge::LocalServiceConfig service_config;
    service_config.bind_host = "127.0.0.1";
    service_config.port = 18080;
    fridge::LocalServiceFacade facade(service_config);
    fridge::LocalHttpServer server(
        fridge::LocalHttpServerOptions{service_config.bind_host, service_config.port, true},
        engine,
        facade
    );

    std::thread server_thread;
    run_server_thread(server, 7, server_thread);

    HttpClientResponse health;
    HttpClientResponse inventory_before;
    HttpClientResponse manual_update;
    HttpClientResponse inventory_after;
    HttpClientResponse events;
    HttpClientResponse pending;
    HttpClientResponse bad_confirm;

    const bool ok =
        http_request("GET", "/health", "", health) &&
        http_request("GET", "/inventory", "", inventory_before) &&
        http_request(
            "POST",
            "/manual_update",
            "{\"item_name\":\"demo_yogurt\",\"category\":\"packaged_food\",\"count\":1,"
            "\"remain_level\":1.0,\"expire_date\":\"2026-04-12\",\"note\":\"debug manual update\"}",
            manual_update
        ) &&
        http_request("GET", "/inventory", "", inventory_after) &&
        http_request("GET", "/events", "", events) &&
        http_request("GET", "/pending", "", pending) &&
        http_request("POST", "/confirm", "{\"action\":\"reject\"}", bad_confirm);

    if (server_thread.joinable()) {
        server_thread.join();
    }
    if (!ok) {
        return false;
    }

    std::cout
        << "local_http_server_debug: health=" << health.status_code
        << " manual_update=" << manual_update.status_code
        << " bad_confirm=" << bad_confirm.status_code << "\n";

    return expect(health.status_code == 200, "GET /health should return 200") &&
           expect(health.body.find("\"status\": \"ok\"") != std::string::npos, "health should be JSON facade output") &&
           expect(inventory_before.status_code == 200, "GET /inventory should return 200") &&
           expect(manual_update.status_code == 200, "POST /manual_update should return 200") &&
           expect(manual_update.body.find("\"ok\": true") != std::string::npos, "manual_update should succeed") &&
           expect(inventory_after.body.find("demo_yogurt") != std::string::npos, "inventory should reflect manual update") &&
           expect(events.status_code == 200, "GET /events should return 200") &&
           expect(pending.status_code == 200, "GET /pending should return 200") &&
           expect(pending.body.find("http_pending_seed") != std::string::npos, "pending should expose seeded review") &&
           expect(bad_confirm.status_code == 400, "POST /confirm missing session_id should return 400");
}

#ifdef FRIDGE_HAS_SQLITE
std::filesystem::path executable_dir(const char* argv0) {
    if (argv0 == nullptr || std::string(argv0).empty()) {
        return std::filesystem::current_path();
    }

    std::error_code error;
    const std::filesystem::path absolute_path = std::filesystem::absolute(argv0, error);
    if (error || absolute_path.parent_path().empty()) {
        return std::filesystem::current_path();
    }
    return absolute_path.parent_path();
}

bool debug_http_sqlite_persistence(const std::filesystem::path& db_path) {
    std::error_code cleanup_error;
    std::filesystem::remove(db_path, cleanup_error);

    fridge::InventoryRuntimeConfig inventory_config;
    fridge::InventoryEngine engine(inventory_config);
    fridge::SQLiteInventoryStore store(db_path);
    std::string error_message;
    if (!store.open(error_message) || !store.initialize_schema(error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    fridge::LocalServiceConfig service_config;
    service_config.bind_host = "127.0.0.1";
    service_config.port = 18080;
    fridge::LocalServiceFacade facade(service_config);
    fridge::LocalHttpServer server(
        fridge::LocalHttpServerOptions{service_config.bind_host, service_config.port, store.db_ready()},
        engine,
        facade,
        [&store, &engine](std::string& save_error) {
            return store.save_snapshot(engine, save_error);
        }
    );

    std::thread server_thread;
    run_server_thread(server, 1, server_thread);

    HttpClientResponse manual_update;
    const bool request_ok = http_request(
        "POST",
        "/manual_update",
        "{\"item_name\":\"sqlite_http_yogurt\",\"category\":\"packaged_food\",\"count\":2,"
        "\"remain_level\":1.0,\"expire_date\":\"2026-04-13\",\"note\":\"sqlite http debug\"}",
        manual_update
    );

    if (server_thread.joinable()) {
        server_thread.join();
    }
    if (!request_ok ||
        !expect(manual_update.status_code == 200, "SQLite HTTP manual_update should return 200") ||
        !expect(manual_update.body.find("\"ok\": true") != std::string::npos,
                "SQLite HTTP manual_update should succeed")) {
        return false;
    }

    fridge::InventoryEngine reloaded(inventory_config);
    fridge::SQLiteInventoryStore reload_store(db_path);
    if (!reload_store.open(error_message) ||
        !reload_store.initialize_schema(error_message) ||
        !reload_store.load_snapshot(reloaded, error_message)) {
        std::cerr << error_message << "\n";
        return false;
    }

    return expect(reloaded.inventory_items().size() == 1, "SQLite HTTP reload should preserve one item") &&
           expect(reloaded.inventory_items().front().item_name == "sqlite_http_yogurt",
                  "SQLite HTTP reload should preserve item name") &&
           expect(reloaded.inventory_items().front().count == 2,
                  "SQLite HTTP reload should preserve item count");
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (!debug_http_routes()) {
        return EXIT_FAILURE;
    }

#ifdef FRIDGE_HAS_SQLITE
    const std::filesystem::path db_path =
        executable_dir(argc > 0 ? argv[0] : nullptr) /
        "local_http_server_sqlite_debug" /
        "fridge_inventory.db";
    if (!debug_http_sqlite_persistence(db_path)) {
        return EXIT_FAILURE;
    }
    std::cout << "local_http_server_sqlite_debug passed\n";
#endif

    std::cout << "local_http_server_debug passed\n";
    return EXIT_SUCCESS;
}
