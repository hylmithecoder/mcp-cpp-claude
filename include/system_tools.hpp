#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "handledb.hpp"

using namespace std;
using json = nlohmann::json;

namespace MCP {

    class SystemTools {
    public:
        SystemTools(Tools::DataBase* db = nullptr) : db_(db) {}

        // Get definitions of all tools
        json getToolDefinitions() const;

        // Call a tool by name with arguments
        json callTool(const string& name, const json& arguments);

    private:
        Tools::DataBase* db_;
        // Individual tool implementations
        json listDirectory(const json& args);
        json readFile(const json& args);
        json searchFiles(const json& args);
        json getFileInfo(const json& args);
        json getSystemInfo(const json& args);
        json listProcesses(const json& args);
        json listInstalledApps(const json& args);
        json runCommand(const json& args);
        json getHistory(const json& args);
        json getHistoryById(const json& args);

        // Utility: execute a shell command and capture output
        string exec(const string& cmd);

        // Utility: make tool result content
        json makeTextResult(const string& text, bool isError = false);
    };

} // namespace MCP
