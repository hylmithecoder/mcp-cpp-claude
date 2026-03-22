#pragma once

#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include "server.hpp"
#include "system_tools.hpp"

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
        std::string sessionId_;
        bool initialized_ = false;
        std::mutex sessionMutex_;

        // Generate a session ID
        std::string generateSessionId();

        // Handle POST /mcp (JSON-RPC messages)
        HttpResponse handlePost(const HttpRequest& req);

        // Handle GET /mcp (SSE stream — returns 405 for now, simple mode)
        HttpResponse handleGet(const HttpRequest& req);

        // Handle DELETE /mcp (session termination)
        HttpResponse handleDelete(const HttpRequest& req);

        // JSON-RPC dispatch
        json processJsonRpc(const json& request);

        // Individual method handlers
        json handleInitialize(const json& params, const json& id);
        json handleToolsList(const json& id);
        json handleToolsCall(const json& params, const json& id);
        json handlePing(const json& id);

        // Build JSON-RPC response
        json makeResponse(const json& id, const json& result);
        json makeError(const json& id, int code, const std::string& message);
    };

} // namespace MCP
