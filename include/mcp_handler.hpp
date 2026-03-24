#pragma once

#include <string>
#include <map>
#include <mutex>
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
        McpHandler() : db_("mcp_history.db"), tools_(&db_) {}

        void registerRoutes(Server& server);

    private:
        Tools::DataBase db_;
        SystemTools tools_;

        // Session management
        map<string, int> activeSessions_;
        mutex sessionMutex_;

        string generateSessionId();

        HttpResponse handlePost(const HttpRequest& req, int client_fd);
        HttpResponse handleGet(const HttpRequest& req, int client_fd);
        HttpResponse handleDelete(const HttpRequest& req, int client_fd);

        json processJsonRpc(const json& request);

        json handleInitialize(const json& params, const json& id);
        json handleToolsList(const json& id);
        json handleToolsCall(const json& params, const json& id);
        json handlePing(const json& id);

        json makeResponse(const json& id, const json& result);
        json makeError(const json& id, int code, const string& message);
    };

} // namespace MCP