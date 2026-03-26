#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "handledb.hpp"

namespace MCP {

    class SystemTools {
    public:
        SystemTools(Tools::DataBase* db = nullptr) : db_(db) {}

        // Get definitions of all tools
        nlohmann::json getToolDefinitions() const;

        // Call a tool by name with arguments
        nlohmann::json callTool(const std::string& name, const nlohmann::json& arguments);

    private:
        Tools::DataBase* db_;
        // Individual tool implementations
        nlohmann::json listDirectory(const nlohmann::json& args);
        nlohmann::json readFile(const nlohmann::json& args);
        nlohmann::json searchFiles(const nlohmann::json& args);
        nlohmann::json getFileInfo(const nlohmann::json& args);
        nlohmann::json getSystemInfo(const nlohmann::json& args);
        nlohmann::json listProcesses(const nlohmann::json& args);
        nlohmann::json listInstalledApps(const nlohmann::json& args);
        nlohmann::json runCommand(const nlohmann::json& args);
        nlohmann::json getHistory(const nlohmann::json& args);
        nlohmann::json getHistoryById(const nlohmann::json& args);
        nlohmann::json editor(const nlohmann::json& args);

        // Utility: execute a shell command and capture output
        std::string exec(const std::string& cmd);

        // Utility: make tool result content
        nlohmann::json makeTextResult(const std::string& text, bool isError = false);
    };

} // namespace MCP
