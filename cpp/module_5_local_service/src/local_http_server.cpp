#include "local_http_server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

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

#include <nlohmann/json.hpp>

namespace fridge {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalid_socket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle invalid_socket = -1;
#endif

constexpr std::size_t max_header_bytes = 16 * 1024;
constexpr std::size_t max_body_bytes = 256 * 1024;

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

std::string socket_error_message(const std::string& prefix) {
#ifdef _WIN32
    return prefix + ": WSA error " + std::to_string(WSAGetLastError());
#else
    return prefix + ": " + std::strerror(errno);
#endif
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

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
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

HttpResponse json_response(int status_code, std::string status_text, std::string body) {
    return HttpResponse{status_code, std::move(status_text), "application/json", std::move(body)};
}

HttpResponse json_error(int status_code, std::string status_text, const std::string& message) {
    std::ostringstream body;
    body << "{\n"
         << "  \"ok\": false,\n"
         << "  \"error\": \"" << escape_json(message) << "\"\n"
         << "}\n";
    return json_response(status_code, std::move(status_text), body.str());
}

HttpResponse text_response(int status_code, std::string status_text, std::string body) {
    return HttpResponse{status_code, std::move(status_text), "text/plain", std::move(body)};
}

bool response_is_ok(const std::string& json_body) {
    return json_body.find("\"ok\": true") != std::string::npos;
}

void replace_db_ready(std::string& json_body, bool db_ready) {
    const std::string marker = "\"db_ready\": true";
    const std::size_t marker_pos = json_body.find(marker);
    if (marker_pos != std::string::npos) {
        json_body.replace(marker_pos, marker.size(), db_ready ? "\"db_ready\": true" : "\"db_ready\": false");
    }
}

bool parse_json_body(const std::string& body, nlohmann::json& json_value, std::string& error_message) {
    if (trim(body).empty()) {
        error_message = "request body is empty";
        return false;
    }

    try {
        json_value = nlohmann::json::parse(body);
    } catch (const std::exception& ex) {
        error_message = "invalid JSON body: " + std::string(ex.what());
        return false;
    }

    if (!json_value.is_object()) {
        error_message = "JSON body must be an object";
        return false;
    }
    return true;
}

std::string json_string_value(const nlohmann::json& json_value, const char* key, const std::string& fallback = {}) {
    const auto it = json_value.find(key);
    if (it == json_value.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_string()) {
        throw std::invalid_argument(std::string("field must be a string: ") + key);
    }
    return it->get<std::string>();
}

int json_int_value(const nlohmann::json& json_value, const char* key, int fallback = 0) {
    const auto it = json_value.find(key);
    if (it == json_value.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_number_integer()) {
        throw std::invalid_argument(std::string("field must be an integer: ") + key);
    }
    return it->get<int>();
}

double json_double_value(const nlohmann::json& json_value, const char* key, double fallback = 0.0) {
    const auto it = json_value.find(key);
    if (it == json_value.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_number()) {
        throw std::invalid_argument(std::string("field must be a number: ") + key);
    }
    return it->get<double>();
}

std::string path_from_target(const std::string& target) {
    const std::size_t query_pos = target.find('?');
    return query_pos == std::string::npos ? target : target.substr(0, query_pos);
}

bool send_all(SocketHandle client_socket, const std::string& data, std::string& error_message) {
    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
        const int sent = send(
            client_socket,
            data.data() + sent_total,
            static_cast<int>(data.size() - sent_total),
            0
        );
        if (sent <= 0) {
            error_message = socket_error_message("send failed");
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

bool parse_content_length_from_header(
    const std::string& header_text,
    std::size_t& content_length,
    std::string& error_message
) {
    content_length = 0;
    std::istringstream header_stream(header_text);
    std::string line;
    if (!std::getline(header_stream, line)) {
        error_message = "missing request line";
        return false;
    }

    while (std::getline(header_stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = to_lower_copy(trim(line.substr(0, separator)));
        if (key != "content-length") {
            continue;
        }
        try {
            content_length = static_cast<std::size_t>(std::stoul(trim(line.substr(separator + 1))));
        } catch (const std::exception&) {
            error_message = "invalid Content-Length";
            return false;
        }
        return true;
    }

    return true;
}

bool read_http_request(SocketHandle client_socket, std::string& raw_request, std::string& error_message) {
    raw_request.clear();
    std::size_t expected_body_bytes = 0;
    bool header_complete = false;

    while (raw_request.size() < max_header_bytes + max_body_bytes) {
        char buffer[4096];
        const int received = recv(client_socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            error_message = socket_error_message("recv failed");
            return false;
        }
        raw_request.append(buffer, static_cast<std::size_t>(received));

        const std::size_t header_end = raw_request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            if (!header_complete) {
                header_complete = true;
                const std::string header_only = raw_request.substr(0, header_end + 4);
                if (!parse_content_length_from_header(header_only, expected_body_bytes, error_message)) {
                    return false;
                }
                if (expected_body_bytes > max_body_bytes) {
                    error_message = "request body is too large";
                    return false;
                }
            }

            const std::size_t current_body_bytes = raw_request.size() - (header_end + 4);
            if (current_body_bytes >= expected_body_bytes) {
                return true;
            }
        } else if (raw_request.size() > max_header_bytes) {
            error_message = "request headers are too large";
            return false;
        }
    }

    error_message = "request is too large";
    return false;
}

bool bind_and_listen(const LocalHttpServerOptions& options, SocketHandle& server_socket, std::string& error_message) {
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == invalid_socket) {
        error_message = socket_error_message("socket creation failed");
        return false;
    }

    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(options.port));
    if (options.host.empty() || options.host == "0.0.0.0") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
        error_message = "Only IPv4 bind hosts are supported in this baseline: " + options.host;
        close_socket(server_socket);
        server_socket = invalid_socket;
        return false;
    }

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error_message = socket_error_message("bind failed");
        close_socket(server_socket);
        server_socket = invalid_socket;
        return false;
    }

    if (listen(server_socket, 16) != 0) {
        error_message = socket_error_message("listen failed");
        close_socket(server_socket);
        server_socket = invalid_socket;
        return false;
    }
    return true;
}

bool serve_requests(
    LocalHttpServer& server,
    const LocalHttpServerOptions& options,
    std::size_t request_limit,
    std::string& error_message
) {
    SocketRuntime runtime;
    if (!runtime.ok()) {
        error_message = "socket runtime initialization failed";
        return false;
    }

    SocketHandle server_socket = invalid_socket;
    if (!bind_and_listen(options, server_socket, error_message)) {
        return false;
    }

    std::cout << "fridge_local_service_server listening on "
              << options.host << ":" << options.port << "\n";

    std::size_t handled = 0;
    while (request_limit == 0 || handled < request_limit) {
        sockaddr_in client_address{};
#ifdef _WIN32
        int client_length = sizeof(client_address);
#else
        socklen_t client_length = sizeof(client_address);
#endif
        const SocketHandle client_socket = accept(
            server_socket,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_length
        );
        if (client_socket == invalid_socket) {
            error_message = socket_error_message("accept failed");
            close_socket(server_socket);
            return false;
        }

        std::string raw_request;
        std::string request_error;
        HttpResponse response;
        if (!read_http_request(client_socket, raw_request, request_error)) {
            response = json_error(400, "Bad Request", request_error);
        } else {
            HttpRequest request;
            if (!parse_http_request(raw_request, request, request_error)) {
                response = json_error(400, "Bad Request", request_error);
            } else {
                response = server.handle_request(request);
            }
        }

        const std::string raw_response = serialize_http_response(response);
        std::string send_error;
        if (!send_all(client_socket, raw_response, send_error)) {
            std::cerr << send_error << "\n";
        }
        close_socket(client_socket);
        ++handled;
    }

    close_socket(server_socket);
    return true;
}

}  // namespace

LocalHttpServer::LocalHttpServer(
    LocalHttpServerOptions options,
    InventoryEngine& engine,
    LocalServiceFacade facade,
    SaveSnapshotCallback save_snapshot
)
    : options_(std::move(options)),
      engine_(engine),
      facade_(std::move(facade)),
      save_snapshot_(std::move(save_snapshot)) {}

HttpResponse LocalHttpServer::handle_request(const HttpRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    return dispatch_locked(request);
}

bool LocalHttpServer::run(std::string& error_message) {
    return serve_requests(*this, options_, 0, error_message);
}

bool LocalHttpServer::run_for_request_count(std::size_t request_count, std::string& error_message) {
    return serve_requests(*this, options_, request_count, error_message);
}

HttpResponse LocalHttpServer::dispatch_locked(const HttpRequest& request) {
    if (request.method == "OPTIONS") {
        return HttpResponse{204, "No Content", "text/plain", ""};
    }
    if (request.method == "GET" && request.path == "/") {
        return text_response(
            200,
            "OK",
            "fridge_local_service routes: GET /health /inventory /events /pending; POST /confirm /manual_update\n"
        );
    }
    if (request.method == "GET" && request.path == "/health") {
        std::string body = facade_.handle_health(engine_);
        replace_db_ready(body, options_.db_ready);
        return json_response(200, "OK", body);
    }
    if (request.method == "GET" && request.path == "/inventory") {
        return json_response(200, "OK", facade_.handle_inventory(engine_));
    }
    if (request.method == "GET" && request.path == "/events") {
        return json_response(200, "OK", facade_.handle_events(engine_));
    }
    if (request.method == "GET" && request.path == "/pending") {
        return json_response(200, "OK", facade_.handle_pending(engine_));
    }
    if (request.method == "POST" && request.path == "/confirm") {
        return handle_confirm_locked(request);
    }
    if (request.method == "POST" && request.path == "/manual_update") {
        return handle_manual_update_locked(request);
    }
    return json_error(404, "Not Found", "route not found");
}

HttpResponse LocalHttpServer::handle_confirm_locked(const HttpRequest& request) {
    nlohmann::json json_value;
    std::string error_message;
    if (!parse_json_body(request.body, json_value, error_message)) {
        return json_error(400, "Bad Request", error_message);
    }

    PendingDecision decision;
    try {
        decision.action = json_string_value(json_value, "action");
        decision.session_id = json_string_value(json_value, "session_id");
        decision.item_name = json_string_value(json_value, "item_name", decision.item_name);
        decision.category = json_string_value(json_value, "category", decision.category);
        decision.count_delta = json_int_value(json_value, "count_delta", decision.count_delta);
        decision.remain_level = json_double_value(json_value, "remain_level", decision.remain_level);
        decision.note = json_string_value(json_value, "note", decision.note);
    } catch (const std::exception& ex) {
        return json_error(400, "Bad Request", ex.what());
    }

    if (trim(decision.session_id).empty()) {
        return json_error(400, "Bad Request", "confirm requires non-empty session_id");
    }
    if (trim(decision.action).empty()) {
        return json_error(400, "Bad Request", "confirm requires non-empty action");
    }

    const std::string response_body = facade_.handle_confirm(engine_, decision);
    if (response_is_ok(response_body)) {
        if (!save_after_success_locked(error_message)) {
            return json_error(500, "Internal Server Error", error_message);
        }
    }
    return json_response(200, "OK", response_body);
}

HttpResponse LocalHttpServer::handle_manual_update_locked(const HttpRequest& request) {
    nlohmann::json json_value;
    std::string error_message;
    if (!parse_json_body(request.body, json_value, error_message)) {
        return json_error(400, "Bad Request", error_message);
    }

    ManualInventoryUpdate update;
    try {
        update.item_name = json_string_value(json_value, "item_name", update.item_name);
        update.category = json_string_value(json_value, "category", update.category);
        update.count = json_int_value(json_value, "count", update.count);
        update.remain_level = json_double_value(json_value, "remain_level", update.remain_level);
        update.expire_date = json_string_value(json_value, "expire_date", update.expire_date);
        update.note = json_string_value(json_value, "note", update.note);
    } catch (const std::exception& ex) {
        return json_error(400, "Bad Request", ex.what());
    }

    if (trim(update.item_name).empty()) {
        return json_error(400, "Bad Request", "manual_update requires non-empty item_name");
    }
    if (trim(update.category).empty()) {
        return json_error(400, "Bad Request", "manual_update requires non-empty category");
    }

    const std::string response_body = facade_.handle_manual_update(engine_, update);
    if (response_is_ok(response_body)) {
        if (!save_after_success_locked(error_message)) {
            return json_error(500, "Internal Server Error", error_message);
        }
    }
    return json_response(200, "OK", response_body);
}

bool LocalHttpServer::save_after_success_locked(std::string& error_message) {
    if (!save_snapshot_) {
        return true;
    }
    return save_snapshot_(error_message);
}

std::string serialize_http_response(const HttpResponse& response) {
    std::ostringstream output;
    output << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n"
           << "Content-Type: " << response.content_type << "; charset=utf-8\r\n"
           << "Content-Length: " << response.body.size() << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type\r\n"
           << "Connection: close\r\n"
           << "\r\n"
           << response.body;
    return output.str();
}

bool parse_http_request(const std::string& raw_request, HttpRequest& request, std::string& error_message) {
    const std::size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        error_message = "missing HTTP header terminator";
        return false;
    }

    std::istringstream header_stream(raw_request.substr(0, header_end));
    std::string request_line;
    if (!std::getline(header_stream, request_line)) {
        error_message = "missing request line";
        return false;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    std::string version;
    if (!(request_line_stream >> request.method >> request.target >> version)) {
        error_message = "invalid request line";
        return false;
    }
    request.path = path_from_target(request.target);

    std::string header_line;
    request.headers.clear();
    while (std::getline(header_stream, header_line)) {
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        const std::size_t separator = header_line.find(':');
        if (separator == std::string::npos) {
            error_message = "invalid header line";
            return false;
        }
        const std::string key = to_lower_copy(trim(header_line.substr(0, separator)));
        const std::string value = trim(header_line.substr(separator + 1));
        request.headers[key] = value;
    }

    request.body = raw_request.substr(header_end + 4);
    const auto length_it = request.headers.find("content-length");
    if (length_it != request.headers.end()) {
        std::size_t content_length = 0;
        try {
            content_length = static_cast<std::size_t>(std::stoul(length_it->second));
        } catch (const std::exception&) {
            error_message = "invalid Content-Length";
            return false;
        }
        if (content_length > max_body_bytes) {
            error_message = "request body is too large";
            return false;
        }
        if (request.body.size() < content_length) {
            error_message = "request body is incomplete";
            return false;
        }
        if (request.body.size() > content_length) {
            request.body.resize(content_length);
        }
    }

    return true;
}

}  // namespace fridge
