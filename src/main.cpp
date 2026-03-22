#include <iostream>
#include <signal.h>
#include "../include/mcp_handler.hpp"

MCP::Server* g_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\n🛑 Shutting down MCP server..." << std::endl;
    if (g_server) g_server->stop();
    exit(0);
}

int main() {
    // Handle Ctrl+C gracefully
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << R"(
  ╔══════════════════════════════════════╗
  ║     MCP Server (Streamable HTTP)     ║
  ║     Local System Access for Claude   ║
  ╚══════════════════════════════════════╝
)" << std::endl;

    // Create server and MCP handler
    MCP::Server server(PORT);
    g_server = &server;

    MCP::McpHandler handler;
    handler.registerRoutes(server);

    // Start listening
    server.start();

    return 0;
}