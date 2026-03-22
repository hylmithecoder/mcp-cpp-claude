#include "../include/server.hpp"

namespace MCP {

    // Build HTTP response string
    std::string HttpResponse::build() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
        for (auto& [key, val] : headers) {
            oss << key << ": " << val << "\r\n";
        }
        if (!keepAlive && !body.empty() && headers.find("Content-Length") == headers.end()) {
            oss << "Content-Length: " << body.size() << "\r\n";
        }
        oss << "\r\n";
        oss << body;
        return oss.str();
    }

    Server::Server(int port) : port_(port) {}

    Server::~Server() {
        stop();
    }

    void Server::route(const std::string& method, const std::string& path, RouteHandler handler) {
        routes_[method][path] = handler;
    }

    void Server::start() {
        struct sockaddr_in address;
        int opt = 1;

        // Create socket
        if ((server_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }

        // Allow port reuse
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("setsockopt failed");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }

        if (listen(server_fd_, 10) < 0) {
            perror("listen failed");
            exit(EXIT_FAILURE);
        }

        running_ = true;
        std::cout << "🚀 MCP Server listening on http://0.0.0.0:" << port_ << "/mcp" << std::endl;
        std::cout << "   Press Ctrl+C to stop." << std::endl;

        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

            if (client_fd < 0) {
                if (running_) perror("accept failed");
                continue;
            }

            // Spawn thread for each client
            std::thread(&Server::handleClient, this, client_fd).detach();
        }
    }

    void Server::stop() {
        running_ = false;
        if (server_fd_ != -1) {
            close(server_fd_);
            server_fd_ = -1;
        }
    }

    HttpRequest Server::parseRequest(const std::string& raw) {
        HttpRequest req;
        std::istringstream stream(raw);
        std::string line;

        // Parse request line: METHOD /path HTTP/1.1
        if (std::getline(stream, line)) {
            // Remove trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream reqLine(line);
            reqLine >> req.method >> req.path;
        }

        // Parse headers
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break; // Empty line = end of headers

            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                // Trim leading spaces from value
                size_t start = val.find_first_not_of(' ');
                if (start != std::string::npos) val = val.substr(start);
                req.headers[key] = val;
            }
        }

        // Parse body (rest of the data)
        std::string bodyPart;
        while (std::getline(stream, bodyPart)) {
            req.body += bodyPart;
            if (!stream.eof()) req.body += "\n";
        }

        // Remove trailing newline if present
        if (!req.body.empty() && req.body.back() == '\n') {
            req.body.pop_back();
        }

        return req;
    }

    void Server::handleClient(int client_fd) {
        char buffer[BUFFER_SIZE] = {0};
        std::string rawData;

        // Read data — may need multiple reads for large bodies
        ssize_t total = 0;
        ssize_t bytes_read;

        // First read
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) {
            close(client_fd);
            return;
        }
        buffer[bytes_read] = '\0';
        rawData = std::string(buffer, bytes_read);

        // Check if we need to read more (Content-Length)
        auto headerEnd = rawData.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            // Parse Content-Length from headers
            std::string headerSection = rawData.substr(0, headerEnd);
            size_t clPos = headerSection.find("Content-Length:");
            if (clPos == std::string::npos) clPos = headerSection.find("content-length:");

            if (clPos != std::string::npos) {
                size_t valStart = headerSection.find(':', clPos) + 1;
                size_t valEnd = headerSection.find("\r\n", valStart);
                if (valEnd == std::string::npos) valEnd = headerSection.size();
                int contentLength = std::stoi(headerSection.substr(valStart, valEnd - valStart));

                size_t bodyStart = headerEnd + 4;
                size_t bodyReceived = rawData.size() - bodyStart;

                // Read remaining body if needed
                while ((int)bodyReceived < contentLength) {
                    memset(buffer, 0, BUFFER_SIZE);
                    bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
                    if (bytes_read <= 0) break;
                    rawData.append(buffer, bytes_read);
                    bodyReceived += bytes_read;
                }
            }
        }

        HttpRequest req = parseRequest(rawData);

        // Handle CORS preflight
        if (req.method == "OPTIONS") {
            HttpResponse res = handleCors(req);
            std::string response = res.build();
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            return;
        }

        // Find route handler (strip query string for matching)
        std::string routePath = req.path;
        size_t qmark = routePath.find('?');
        if (qmark != std::string::npos) {
            routePath = routePath.substr(0, qmark);
        }

        HttpResponse res;
        auto methodIt = routes_.find(req.method);
        if (methodIt != routes_.end()) {
            auto pathIt = methodIt->second.find(routePath);
            if (pathIt != methodIt->second.end()) {
                res = pathIt->second(req, client_fd);
            } else {
                res.statusCode = 404;
                res.statusText = "Not Found";
                res.body = "{\"error\":\"Not Found\"}";
                res.headers["Content-Type"] = "application/json";
            }
        } else {
            res.statusCode = 405;
            res.statusText = "Method Not Allowed";
            res.body = "{\"error\":\"Method Not Allowed\"}";
            res.headers["Content-Type"] = "application/json";
        }

        addCorsHeaders(res);
        std::string response = res.build();
        send(client_fd, response.c_str(), response.size(), 0);
        
        if (!res.keepAlive) {
            close(client_fd);
        }
    }

    HttpResponse Server::handleCors(const HttpRequest& req) {
        HttpResponse res;
        res.statusCode = 204;
        res.statusText = "No Content";
        addCorsHeaders(res);
        res.headers["Access-Control-Max-Age"] = "86400";
        return res;
    }

    void Server::addCorsHeaders(HttpResponse& res) {
        res.headers["Access-Control-Allow-Origin"] = "*";
        res.headers["Access-Control-Allow-Methods"] = "GET, POST, DELETE, OPTIONS";
        res.headers["Access-Control-Allow-Headers"] = "Content-Type, Accept, Mcp-Session-Id, Last-Event-ID";
        res.headers["Access-Control-Expose-Headers"] = "Mcp-Session-Id";
    }

} // namespace MCP