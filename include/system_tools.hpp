#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace MCP {

    class SystemTools {
    public:
        // Get the list of all tool definitions (for tools/list)
        json getToolDefinitions() const;

        // Execute a tool by name with given arguments
        json callTool(const std::string& name, const json& arguments);

    private:
        // Individual tool implementations
        json listDirectory(const json& args);
        json readFile(const json& args);
        json searchFiles(const json& args);
        json getFileInfo(const json& args);
        json getSystemInfo(const json& args);
        json listProcesses(const json& args);
        json listInstalledApps(const json& args);
        json runCommand(const json& args);

        // Utility: execute a shell command and capture output
        std::string exec(const std::string& cmd);

        // Utility: make tool result content
        json makeTextResult(const std::string& text, bool isError = false);
    };

} // namespace MCP
