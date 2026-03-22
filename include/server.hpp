#pragma once

#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <map>
#include <thread>
#include <mutex>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define PORT 5500
#define BUFFER_SIZE 8192

namespace MCP {

    // Simple HTTP request structure
    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    // Simple HTTP response structure
    struct HttpResponse {
        int statusCode = 200;
        std::string statusText = "OK";
        std::map<std::string, std::string> headers;
        std::string body;

        std::string build() const;
    };

    // Route handler type
    using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

    class Server {
    public:
        Server(int port = PORT);
        ~Server();

        // Register route handlers
        void route(const std::string& method, const std::string& path, RouteHandler handler);

        // Start listening
        void start();
        void stop();

    private:
        int port_;
        int server_fd_ = -1;
        bool running_ = false;
        std::mutex mutex_;

        // Routes: method -> path -> handler
        std::map<std::string, std::map<std::string, RouteHandler>> routes_;

        // Parse raw HTTP request
        HttpRequest parseRequest(const std::string& raw);

        // Handle a single client connection
        void handleClient(int client_fd);

        // Build CORS preflight response
        HttpResponse handleCors(const HttpRequest& req);

        // Add CORS headers to response
        void addCorsHeaders(HttpResponse& res);
    };

} // namespace MCP