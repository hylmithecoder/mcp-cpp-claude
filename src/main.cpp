#include "../include/mcp_handler.hpp"
#include "../include/utils.hpp"
#include "server.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <signal.h>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#define setenv(k, v, o) _putenv_s(k, v)
#endif

MCP::Server *g_server = nullptr;

std::string generateRandomString(size_t length) {
  const std::string characters =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<> distribution(0, characters.size() - 1);
  std::string result;
  for (size_t i = 0; i < length; ++i) {
    result += characters[distribution(generator)];
  }
  return result;
}

void signalHandler(int signum) {
  std::cout << "\n🛑 Shutting down MCP server..." << std::endl;
  if (g_server)
    g_server->stop();
  exit(0);
}

void handleArguments(int argc, char *argv[], MCP::Utils utilClass = MCP::Utils()) {
  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "-v" || arg == "--version") {
      utilClass.printVersion();
      exit(0);
    }
    if (arg == "-h" || arg == "--help") {
      utilClass.printHelp();
      exit(0);
    }
    if (arg == "-p" || arg == "--port") {
      if (i + 1 < argc) {
        // MCP::PORT = stoi(argv[++i]);
      } else {
        cerr << "Error: Missing port number" << endl;
        exit(1);
      }
    }
    if (arg == "-t" || arg == "--token") {
      if (i + 1 < argc) {
        setenv("MCP_TOKEN", argv[++i], 1);
      } else {
        cerr << "Error: Missing token" << endl;
        exit(1);
      }
    }
    if (arg == "-c" || arg == "--client") {
      if (i + 1 < argc) {
        setenv("MCP_CLIENT_ID", argv[++i], 1);
      } else {
        cerr << "Error: Missing client ID" << endl;
        exit(1);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  // Handle Ctrl+C gracefully
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // Create server and MCP handler
  handleArguments(argc, argv);
  MCP::Server server(MCP::PORT);
  g_server = &server;

  // Load API Key / Credentials from environment if present
  const char *envClientId = getenv("MCP_CLIENT_ID");
  const char *envApiSecret = getenv("MCP_CLIENT_SECRET");
  if (!envApiSecret)
    envApiSecret = getenv("MCP_API_KEY");
  if (!envApiSecret)
    envApiSecret = getenv("MCP_TOKEN");

  std::string clientId = envClientId ? envClientId : "";
  std::string apiSecret = envApiSecret ? envApiSecret : "";

  // Generate if not present
  bool generated = false;
  if (apiSecret.empty()) {
    clientId = "mcp-client-" + generateRandomString(8);
    apiSecret = "mcp-secret-" + generateRandomString(16);
    generated = true;
  }

  server.setCredentials(clientId, apiSecret);

  std::cout << R"(
  ╔══════════════════════════════════════╗
  ║     MCP Server (Streamable HTTP)     ║
  ║     Local System Access for Claude   ║
  ╚══════════════════════════════════════╝
  )" << std::endl;

  std::cout << "🚀 MCP Server starting..." << std::endl;
  std::cout << "---------------------------------------" << std::endl;
  std::cout << "🔑 CONNECTION CREDENTIALS" << std::endl;
  std::cout << "   Client ID:     " << clientId << std::endl;
  std::cout << "   Client Secret: " << apiSecret << std::endl;
  if (generated) {
    std::cout << "   (Generated automatically for this session)" << std::endl;
  }

  MCP::McpHandler handler;
  handler.setCredentials(clientId, apiSecret);
  handler.registerRoutes(server);

  // Start listening
  server.start();

  return 0;
}