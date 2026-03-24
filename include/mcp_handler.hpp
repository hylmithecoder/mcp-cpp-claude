#pragma once

#include <string>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "server.hpp"
#include "system_tools.hpp"
#include "handledb.hpp"

namespace MCP {

    class McpHandler {
    public:
        McpHandler() : db_("mcp_history.db"), tools_(&db_) {}

        void registerRoutes(Server& server);
        void setCredentials(const std::string& id, const std::string& secret) {
            apiClientId_ = id;
            apiToken_ = secret;
        }

    private:
        Tools::DataBase db_;
        SystemTools tools_;
        std::string apiClientId_;
        std::string apiToken_;

        // Session management
        std::map<std::string, socket_t> activeSessions_;
        std::mutex sessionMutex_;

        std::string generateSessionId();

        HttpResponse handlePost(const HttpRequest& req, socket_t client_fd);
        HttpResponse handleGet(const HttpRequest& req, socket_t client_fd);
        HttpResponse handleDelete(const HttpRequest& req, socket_t client_fd);
        HttpResponse handleAuthorize(const HttpRequest& req, socket_t client_fd);
        HttpResponse handleToken(const HttpRequest& req, socket_t client_fd);

        nlohmann::json processJsonRpc(const nlohmann::json& request);

        nlohmann::json handleInitialize(const nlohmann::json& params, const nlohmann::json& id);
        nlohmann::json handleToolsList(const nlohmann::json& id);
        nlohmann::json handleToolsCall(const nlohmann::json& params, const nlohmann::json& id);
        nlohmann::json handlePing(const nlohmann::json& id);

        nlohmann::json makeResponse(const nlohmann::json& id, const nlohmann::json& result);
        nlohmann::json makeError(const nlohmann::json& id, int code, const std::string& message);
    };

} // namespace MCP