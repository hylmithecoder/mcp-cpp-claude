#include "../include/mcp_handler.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace MCP {

    McpHandler::McpHandler() {}

    std::string McpHandler::generateSessionId() {
        // Generate a UUID-like session ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        std::uniform_int_distribution<> dis2(8, 11);

        std::stringstream ss;
        ss << std::hex;
        for (int i = 0; i < 8; i++) ss << dis(gen);
        ss << "-";
        for (int i = 0; i < 4; i++) ss << dis(gen);
        ss << "-4"; // Version 4
        for (int i = 0; i < 3; i++) ss << dis(gen);
        ss << "-";
        ss << dis2(gen);
        for (int i = 0; i < 3; i++) ss << dis(gen);
        ss << "-";
        for (int i = 0; i < 12; i++) ss << dis(gen);

        return ss.str();
    }

    void McpHandler::registerRoutes(Server& server) {
        auto postHandler = [this](const HttpRequest& req, int client_fd) { return handlePost(req, client_fd); };
        auto getHandler = [this](const HttpRequest& req, int client_fd) { return handleGet(req, client_fd); };
        auto deleteHandler = [this](const HttpRequest& req, int client_fd) { return handleDelete(req, client_fd); };

        server.route("POST", "/mcp", postHandler);
        server.route("GET", "/mcp", getHandler);
        server.route("DELETE", "/mcp", deleteHandler);

        // Also route root path to catch users not appending /mcp
        server.route("POST", "/", postHandler);
        server.route("GET", "/", getHandler);
        server.route("DELETE", "/", deleteHandler);
    }

    HttpResponse McpHandler::handlePost(const HttpRequest& req, int client_fd) {
        HttpResponse res;

        // Check Accept header
        auto acceptIt = req.headers.find("Accept");
        if (acceptIt == req.headers.end()) {
            // Also check lowercase
            acceptIt = req.headers.find("accept");
        }

        // Parse JSON-RPC message
        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error& e) {
            res.statusCode = 400;
            res.statusText = "Bad Request";
            res.headers["Content-Type"] = "application/json";
            res.body = makeError(nullptr, -32700, "Parse error: " + std::string(e.what())).dump();
            return res;
        }

        // Check if it's a notification (no id) or a response
        bool isNotification = false;
        bool isRequest = false;

        if (request.is_array()) {
            // Batch — handle first item for now
            // TODO: full batch support
            if (!request.empty()) {
                request = request[0];
            }
        }

        if (request.contains("method")) {
            if (request.contains("id")) {
                isRequest = true;
            } else {
                isNotification = true;
            }
        }

        // Handle notifications — return 202 Accepted
        if (isNotification) {
            std::string method = request["method"].get<std::string>();
            std::cerr << "[MCP] Notification: " << method << std::endl;

            if (method == "notifications/initialized") {
                std::cerr << "[MCP] Client initialized, session is active." << std::endl;
            }

            res.statusCode = 202;
            res.statusText = "Accepted";
            return res;
        }

        // Handle requests
        if (isRequest) {
            std::string method = request.value("method", "");
            std::cerr << "[MCP] Request: " << method << " (id: " << request["id"].dump() << ")" << std::endl;

            // Extract sessionId from query string or headers
            std::string sessionId = "";
            size_t queryPos = req.path.find("sessionId=");
            if (queryPos != std::string::npos) {
                size_t endPos = req.path.find('&', queryPos);
                if (endPos == std::string::npos) endPos = req.path.size();
                sessionId = req.path.substr(queryPos + 10, endPos - (queryPos + 10));
            } else {
                auto sessionIt = req.headers.find("Mcp-Session-Id");
                if (sessionIt == req.headers.end()) sessionIt = req.headers.find("mcp-session-id");
                if (sessionIt != req.headers.end()) sessionId = sessionIt->second;
            }

            int sse_fd = -1;
            {
                std::lock_guard<std::mutex> lock(sessionMutex_);
                if (activeSessions_.count(sessionId)) {
                    sse_fd = activeSessions_[sessionId];
                }
            }

            if (sse_fd == -1) {
                res.statusCode = 400;
                res.statusText = "Bad Request";
                res.headers["Content-Type"] = "application/json";
                res.body = makeError(request["id"], -32600, "Invalid or missing Mcp-Session-Id for SSE").dump();
                return res;
            }

            json result = processJsonRpc(request);

            // Send SSE Message
            std::string sseMessage = "event: message\ndata: " + result.dump() + "\n\n";
            send(sse_fd, sseMessage.c_str(), sseMessage.size(), 0);

            // Respond 202 Accepted to the POST request
            res.statusCode = 202;
            res.statusText = "Accepted";
            return res;
        }

        // Responses from client — 202
        res.statusCode = 202;
        res.statusText = "Accepted";
        return res;
    }

    HttpResponse McpHandler::handleGet(const HttpRequest& req, int client_fd) {
        std::string newSessionId = generateSessionId();
        
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            activeSessions_[newSessionId] = client_fd;
        }

        std::cerr << "[MCP] New SSE stream opened, session ID: " << newSessionId << std::endl;

        HttpResponse res;
        res.statusCode = 200;
        res.statusText = "OK";
        res.headers["Content-Type"] = "text/event-stream";
        res.headers["Cache-Control"] = "no-cache";
        res.headers["Connection"] = "keep-alive";
        
        std::string endpointEvent = "event: endpoint\ndata: /mcp?sessionId=" + newSessionId + "\n\n";
        res.body = endpointEvent;
        res.keepAlive = true;

        return res;
    }

    HttpResponse McpHandler::handleDelete(const HttpRequest& req, int client_fd) {
        HttpResponse res;

        std::string sessionId = "";
        size_t queryPos = req.path.find("sessionId=");
        if (queryPos != std::string::npos) {
            size_t endPos = req.path.find('&', queryPos);
            if (endPos == std::string::npos) endPos = req.path.size();
            sessionId = req.path.substr(queryPos + 10, endPos - (queryPos + 10));
        }

        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (!sessionId.empty() && activeSessions_.count(sessionId)) {
            close(activeSessions_[sessionId]); // Close the SSE socket
            activeSessions_.erase(sessionId);
            std::cerr << "[MCP] Session terminated: " << sessionId << std::endl;
            res.statusCode = 200;
            res.statusText = "OK";
        } else {
            res.statusCode = 404;
            res.statusText = "Not Found";
        }
        return res;
    }

    json McpHandler::processJsonRpc(const json& request) {
        std::string method = request.value("method", "");
        json id = request.value("id", json(nullptr));
        json params = request.value("params", json::object());

        if (method == "initialize") {
            return handleInitialize(params, id);
        } else if (method == "ping") {
            return handlePing(id);
        } else if (method == "tools/list") {
            return handleToolsList(id);
        } else if (method == "tools/call") {
            return handleToolsCall(params, id);
        } else {
            return makeError(id, -32601, "Method not found: " + method);
        }
    }

    json McpHandler::handleInitialize(const json& params, const json& id) {
        std::string clientName = "unknown";
        if (params.contains("clientInfo") && params["clientInfo"].contains("name")) {
            clientName = params["clientInfo"]["name"].get<std::string>();
        }
        std::cerr << "[MCP] Initialize from client: " << clientName << std::endl;

        json result = {
            {"protocolVersion", "2026-03-23"},
            {"capabilities", {
                {"tools", {
                    {"listChanged", false}
                }}
            }},
            {"serverInfo", {
                {"name", "hylmi-local-system"},
                {"version", "1.0.0"}
            }},
            {"instructions", "This MCP server provides access to the local Linux system. "
                           "You can list directories, read files, search for files, "
                           "get file info, view system information, list processes, "
                           "list installed applications, and run shell commands."}
        };

        return makeResponse(id, result);
    }

    json McpHandler::handleToolsList(const json& id) {
        json result = {
            {"tools", tools_.getToolDefinitions()}
        };
        return makeResponse(id, result);
    }

    json McpHandler::handleToolsCall(const json& params, const json& id) {
        std::string toolName = params.value("name", "");
        json arguments = params.value("arguments", json::object());

        std::cerr << "[MCP] Calling tool: " << toolName << std::endl;

        try {
            json toolResult = tools_.callTool(toolName, arguments);
            return makeResponse(id, toolResult);
        } catch (const std::exception& e) {
            json errorResult = {
                {"content", json::array({
                    {{"type", "text"}, {"text", std::string("Error: ") + e.what()}}
                })},
                {"isError", true}
            };
            return makeResponse(id, errorResult);
        }
    }

    json McpHandler::handlePing(const json& id) {
        return makeResponse(id, json::object());
    }

    json McpHandler::makeResponse(const json& id, const json& result) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result}
        };
    }

    json McpHandler::makeError(const json& id, int code, const std::string& message) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
    }

} // namespace MCP
