#include "../include/mcp_handler.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace MCP {

    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    string McpHandler::generateSessionId() {
        // Generate a UUID-like session ID
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(0, 15);
        uniform_int_distribution<> dis2(8, 11);

        stringstream ss;
        ss << hex;
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

        // Parse JSON-RPC body
        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error& e) {
            res.statusCode = 400;
            res.statusText = "Bad Request";
            res.headers["Content-Type"] = "application/json";
            res.body = makeError(nullptr, -32700, "Parse error: " + string(e.what())).dump();
            return res;
        }

        // Handle batch array: proses item pertama untuk sekarang
        if (request.is_array()) {
            if (!request.empty()) request = request[0];
            else { res.statusCode = 204; return res; }
        }

        // Notification (tidak ada "id") → 202 Accepted, tidak perlu response body
        if (request.contains("method") && !request.contains("id")) {
            string method = request["method"].get<string>();
            cerr << "[MCP] Notification: " << method << endl;
            res.statusCode = 202;
            res.statusText = "Accepted";
            return res;
        }

        // Bukan request yang valid
        if (!request.contains("method")) {
            res.statusCode = 202;
            res.statusText = "Accepted";
            return res;
        }

        string method = request.value("method", "");
        cerr << "[MCP] Request: " << method << " (id: " << request["id"].dump() << ")" << endl;

        // Cek apakah ada SSE session aktif untuk client ini
        string sessionId = "";
        auto sessionIt = req.headers.find("Mcp-Session-Id");
        if (sessionIt == req.headers.end()) sessionIt = req.headers.find("mcp-session-id");
        if (sessionIt != req.headers.end()) sessionId = sessionIt->second;

        // Cek query string juga
        if (sessionId.empty()) {
            size_t queryPos = req.path.find("sessionId=");
            if (queryPos != string::npos) {
                size_t endPos = req.path.find('&', queryPos);
                if (endPos == string::npos) endPos = req.path.size();
                sessionId = req.path.substr(queryPos + 10, endPos - (queryPos + 10));
            }
        }

        // Proses JSON-RPC
        json result = processJsonRpc(request);

        // Kalau ada SSE session aktif, kirim lewat SSE dan balas 202
        {
            lock_guard<mutex> lock(sessionMutex_);
            if (!sessionId.empty() && activeSessions_.count(sessionId)) {
                int sse_fd = activeSessions_[sessionId];
                string sseMessage = "event: message\ndata: " + result.dump() + "\n\n";
                send(sse_fd, sseMessage.c_str(), sseMessage.size(), 0);
                res.statusCode = 202;
                res.statusText = "Accepted";
                return res;
            }
        }

        // ★ DEFAULT: Balas langsung dengan JSON di HTTP body (ini yang Claude web pakai)
        res.statusCode = 200;
        res.statusText = "OK";
        res.headers["Content-Type"] = "application/json";

        // Kalau ini initialize, buat session baru dan kirim header Mcp-Session-Id
        if (method == "initialize") {
            string newSessionId = generateSessionId();
            // Simpan session dengan fd=-1 karena belum ada SSE stream
            {
                lock_guard<mutex> lock(sessionMutex_);
                activeSessions_[newSessionId] = -1;
            }
            res.headers["Mcp-Session-Id"] = newSessionId;
            cerr << "[MCP] New session created: " << newSessionId << endl;
        }

        res.body = result.dump();
        return res;
    }

    HttpResponse McpHandler::handleGet(const HttpRequest& req, int client_fd) {
        // Cek apakah ini SSE request (Accept: text/event-stream)
        auto acceptIt = req.headers.find("Accept");
        if (acceptIt == req.headers.end()) acceptIt = req.headers.find("accept");

        bool wantsSSE = (acceptIt != req.headers.end() &&
                         acceptIt->second.find("text/event-stream") != string::npos);

        if (!wantsSSE) {
            HttpResponse res;
            res.statusCode = 200;
            res.statusText = "OK";
            res.headers["Content-Type"] = "application/json";
            res.body = "{\"status\":\"MCP server running\"}";
            return res;
        }

        // Buat SSE stream
        string newSessionId = generateSessionId();
        {
            lock_guard<mutex> lock(sessionMutex_);
            activeSessions_[newSessionId] = client_fd;
        }

        cerr << "[MCP] New SSE stream, session: " << newSessionId << endl;

        HttpResponse res;
        res.statusCode = 200;
        res.statusText = "OK";
        res.headers["Content-Type"] = "text/event-stream";
        res.headers["Cache-Control"] = "no-cache";
        res.headers["Connection"] = "keep-alive";
        res.headers["Mcp-Session-Id"] = newSessionId;

        string endpointEvent = "event: endpoint\ndata: /mcp?sessionId=" + newSessionId + "\n\n";
        res.body = endpointEvent;
        res.keepAlive = true;

        return res;
    }

    HttpResponse McpHandler::handleDelete(const HttpRequest& req, int client_fd) {
        HttpResponse res;

        string sessionId = "";
        size_t queryPos = req.path.find("sessionId=");
        if (queryPos != string::npos) {
            size_t endPos = req.path.find('&', queryPos);
            if (endPos == string::npos) endPos = req.path.size();
            sessionId = req.path.substr(queryPos + 10, endPos - (queryPos + 10));
        }

        lock_guard<mutex> lock(sessionMutex_);
        if (!sessionId.empty() && activeSessions_.count(sessionId)) {
            close(activeSessions_[sessionId]); // Close the SSE socket
            activeSessions_.erase(sessionId);
            cerr << "[MCP] Session terminated: " << sessionId << endl;
            res.statusCode = 200;
            res.statusText = "OK";
        } else {
            res.statusCode = 404;
            res.statusText = "Not Found";
        }
        return res;
    }

    json McpHandler::processJsonRpc(const json& request) {
        string method = request.value("method", "");
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
        string clientName = "unknown";
        if (params.contains("clientInfo") && params["clientInfo"].contains("name")) {
            clientName = params["clientInfo"]["name"].get<string>();
        }
        cerr << "[MCP] Initialize from client: " << clientName << endl;

        json result = {
            {"protocolVersion", "2024-11-05"},
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
        if (!params.contains("name")) {
            return makeError(id, -32602, "Missing tool name");
        }

        std::string toolName = params["name"];
        json arguments = params.value("arguments", json::object());

        cerr << "[MCP] Calling tool: " << toolName << endl;

        try {
            json toolResult = tools_.callTool(toolName, arguments);

            // Record to history database
            FormatInput historyData;
            historyData.context = toolName + "(" + arguments.dump() + ")";
            historyData.response = toolResult.dump();
            historyData.timestamp = getCurrentTimestamp();
            db_.insertData(historyData, db_.db);

            return makeResponse(id, toolResult);
        } catch (const exception& e) {
            json errorResult = {
                {"content", json::array({
                    {{"type", "text"}, {"text", string("Error: ") + e.what()}}
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

    json McpHandler::makeError(const json& id, int code, const string& message) {
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
