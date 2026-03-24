#include <iostream>
#include <signal.h>
#include <vector>
#include <string>
#include <fstream>
#include "../include/mcp_handler.hpp"

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#define setenv(k, v, o) _putenv_s(k, v)
#endif

MCP::Server* g_server = nullptr;

void loadEnv(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            size_t lastKey = key.find_last_not_of(" \t\r\n");
            if (lastKey != std::string::npos) key.erase(lastKey + 1);
            
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            size_t lastVal = val.find_last_not_of(" \t\r\n");
            if (lastVal != std::string::npos) val.erase(lastVal + 1);

            // Remove quotes if present
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }
            if (!key.empty()) {
                setenv(key.c_str(), val.c_str(), 1);
            }
        }
    }
}

void signalHandler(int signum) {
    std::cout << "\n🛑 Shutting down MCP server..." << std::endl;
    if (g_server) g_server->stop();
    exit(0);
}

int main() {
    // Handle Ctrl+C gracefully
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Load .env file from current or parent directory
    loadEnv(".env");
    loadEnv("../.env");

    std::cout << R"(
  ╔══════════════════════════════════════╗
  ║     MCP Server (Streamable HTTP)     ║
  ║     Local System Access for Claude   ║
  ╚══════════════════════════════════════╝
)" << std::endl;

    // Create server and MCP handler
    MCP::Server server(MCP::PORT);
    g_server = &server;

    // Load API Key / Credentials from environment if present
    const char* clientId = getenv("MCP_CLIENT_ID");
    const char* apiSecret = getenv("MCP_CLIENT_SECRET");
    if (!apiSecret) apiSecret = getenv("MCP_API_KEY");
    if (!apiSecret) apiSecret = getenv("MCP_TOKEN");

    if (apiSecret) {
        if (clientId) {
            server.setCredentials(std::string(clientId), std::string(apiSecret));
            std::cout << "🔑 Client ID and Secret authentication enabled." << std::endl;
        } else {
            server.setToken(std::string(apiSecret));
            std::cout << "🔑 Bearer token authentication enabled." << std::endl;
        }
    } else {
        std::cout << "⚠️ Running without authentication (MCP_CLIENT_ID/SECRET not set)." << std::endl;
    }

    MCP::McpHandler handler;
    if (apiSecret) {
        handler.setCredentials(clientId ? std::string(clientId) : "", std::string(apiSecret));
    }
    handler.registerRoutes(server);

    // Start listening
    server.start();

    return 0;
}