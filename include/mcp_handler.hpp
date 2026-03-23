#pragma once

#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include "server.hpp"
#include "system_tools.hpp"
#include "handledb.hpp"

using namespace std;
using namespace Tools;
using json = nlohmann::json;

namespace MCP {

    class McpHandler {
    public:
        McpHandler();

        // Register all MCP routes on the server
        void registerRoutes(Server& server);

    private:
        SystemTools tools_;

        // Session management
        map<string, int> activeSessions_;
        mutex sessionMutex_;

        // Generate a session ID
        string generateSessionId();

        // Handle POST /mcp (JSON-RPC messages)
        HttpResponse handlePost(const HttpRequest& req, int client_fd);

        // Handle GET /mcp (SSE stream)
        HttpResponse handleGet(const HttpRequest& req, int client_fd);

        // Handle DELETE /mcp (session termination)
        HttpResponse handleDelete(const HttpRequest& req, int client_fd);

        // JSON-RPC dispatch
        json processJsonRpc(const json& request);

        // Individual method handlers
        json handleInitialize(const json& params, const json& id);
        json handleToolsList(const json& id);
        json handleToolsCall(const json& params, const json& id);
        json handlePing(const json& id);

        // Build JSON-RPC response
        json makeResponse(const json& id, const json& result);
        json makeError(const json& id, int code, const string& message);
    };

} // namespace MCP
