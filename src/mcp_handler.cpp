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
        server.route("POST", "/mcp", [this](const HttpRequest& req) {
            return handlePost(req);
        });

        server.route("GET", "/mcp", [this](const HttpRequest& req) {
            return handleGet(req);
        });

        server.route("DELETE", "/mcp", [this](const HttpRequest& req) {
            return handleDelete(req);
        });
    }

    HttpResponse McpHandler::handlePost(const HttpRequest& req) {
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
                std::lock_guard<std::mutex> lock(sessionMutex_);
                initialized_ = true;
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

            // Check session for non-initialize requests
            if (method != "initialize") {
                auto sessionIt = req.headers.find("Mcp-Session-Id");
                if (sessionIt == req.headers.end()) {
                    sessionIt = req.headers.find("mcp-session-id");
                }

                std::lock_guard<std::mutex> lock(sessionMutex_);
                if (!sessionId_.empty()) {
                    if (sessionIt == req.headers.end() || sessionIt->second != sessionId_) {
                        res.statusCode = 400;
                        res.statusText = "Bad Request";
                        res.headers["Content-Type"] = "application/json";
                        res.body = makeError(request["id"], -32600, "Invalid or missing Mcp-Session-Id").dump();
                        return res;
                    }
                }
            }

            json result = processJsonRpc(request);

            res.statusCode = 200;
            res.statusText = "OK";
            res.headers["Content-Type"] = "application/json";
            res.body = result.dump();

            // Add session ID header for initialize responses
            if (method == "initialize") {
                std::lock_guard<std::mutex> lock(sessionMutex_);
                res.headers["Mcp-Session-Id"] = sessionId_;
            }

            return res;
        }

        // Responses from client — 202
        res.statusCode = 202;
        res.statusText = "Accepted";
        return res;
    }

    HttpResponse McpHandler::handleGet(const HttpRequest& req) {
        // For now, we don't support server-initiated SSE streams
        // Claude.ai primarily uses POST for communication
        HttpResponse res;
        res.statusCode = 405;
        res.statusText = "Method Not Allowed";
        res.headers["Content-Type"] = "application/json";
        res.body = "{\"error\":\"SSE stream not supported, use POST\"}";
        return res;
    }

    HttpResponse McpHandler::handleDelete(const HttpRequest& req) {
        HttpResponse res;

        auto sessionIt = req.headers.find("Mcp-Session-Id");
        if (sessionIt == req.headers.end()) {
            sessionIt = req.headers.find("mcp-session-id");
        }

        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessionIt != req.headers.end() && sessionIt->second == sessionId_) {
            sessionId_.clear();
            initialized_ = false;
            std::cerr << "[MCP] Session terminated by client." << std::endl;
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
        std::lock_guard<std::mutex> lock(sessionMutex_);

        // Generate new session
        sessionId_ = generateSessionId();
        initialized_ = false;

        std::string clientName = "unknown";
        if (params.contains("clientInfo") && params["clientInfo"].contains("name")) {
            clientName = params["clientInfo"]["name"].get<std::string>();
        }
        std::cerr << "[MCP] Initialize from client: " << clientName << std::endl;
        std::cerr << "[MCP] Session ID: " << sessionId_ << std::endl;

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
