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

using namespace std;
using json = nlohmann::json;

#define PORT 5500
#define BUFFER_SIZE 8192

namespace MCP {

    // Simple HTTP request structure
    struct HttpRequest {
        string method;
        string path;
        map<string, string> headers;
        string body;
    };

    // Simple HTTP response structure
    struct HttpResponse {
        int statusCode = 200;
        string statusText = "OK";
        map<string, string> headers;
        string body;
        bool keepAlive = false;

        string build() const;
    };

    // Route handler type
    using RouteHandler = function<HttpResponse(const HttpRequest&, int)>;

    class Server {
    public:
        Server(int port = PORT);
        ~Server();

        // Register route handlers
        void route(const string& method, const string& path, RouteHandler handler);

        // Start listening
        void start();
        void stop();

    private:
        int port_;
        int server_fd_ = -1;
        bool running_ = false;
        mutex mutex_;

        // Routes: method -> path -> handler
        map<string, map<string, RouteHandler>> routes_;

        // Parse raw HTTP request
        HttpRequest parseRequest(const string& raw);

        // Handle a single client connection
        void handleClient(int client_fd);

        // Build CORS preflight response
        HttpResponse handleCors(const HttpRequest& req);

        // Add CORS headers to response
        void addCorsHeaders(HttpResponse& res);
    };

} // namespace MCP