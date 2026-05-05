#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "inventory_engine.hpp"
#include "local_service.hpp"

namespace fridge {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string content_type = "application/json";
    std::string body;
};

struct LocalHttpServerOptions {
    std::string host = "0.0.0.0";
    int port = 8080;
    bool db_ready = true;
};

class LocalHttpServer {
public:
    using SaveSnapshotCallback = std::function<bool(std::string& error_message)>;

    LocalHttpServer(
        LocalHttpServerOptions options,
        InventoryEngine& engine,
        LocalServiceFacade facade,
        SaveSnapshotCallback save_snapshot = {}
    );

    HttpResponse handle_request(const HttpRequest& request);
    bool run(std::string& error_message);
    bool run_for_request_count(std::size_t request_count, std::string& error_message);

private:
    HttpResponse dispatch_locked(const HttpRequest& request);
    HttpResponse handle_confirm_locked(const HttpRequest& request);
    HttpResponse handle_manual_update_locked(const HttpRequest& request);
    bool save_after_success_locked(std::string& error_message);

    LocalHttpServerOptions options_;
    InventoryEngine& engine_;
    LocalServiceFacade facade_;
    SaveSnapshotCallback save_snapshot_;
    std::mutex mutex_;
};

std::string serialize_http_response(const HttpResponse& response);
bool parse_http_request(const std::string& raw_request, HttpRequest& request, std::string& error_message);

}  // namespace fridge
