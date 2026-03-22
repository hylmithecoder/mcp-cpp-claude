# Native C++ MCP Server

A high-performance Model Context Protocol (MCP) server written in native C++ that enables AI assistants (like Claude) to interact securely and directly with your local Linux filesystem and OS. 

By default, this server listens on HTTP port `5500` and implements the **MCP Streamable HTTP** transport with JSON-RPC 2.0. This allows remote AI models to perform system-level operations on your machine via secure tunnels (e.g., reverse SSH, ngrok, Cloudflare Tunnels).

## Features

This server exposes **8 core system tools** currently available to connected AI models:

1. `list_directory`: View files, directories, and their sizes.
2. `read_file`: Read entire text files or specific line ranges (up to 1MB).
3. `search_files`: Recursively search for matching filenames within directories.
4. `get_file_info`: Inspect detailed metadata (permissions, owner, modification time).
5. `get_system_info`: View host OS, kernel, CPU, RAM, and Disk usage.
6. `list_processes`: Monitor running processes (sorted by CPU, memory, or PID).
7. `list_installed_apps`: Discover installed desktop `.desktop` applications.
8. `run_command`: Execute custom bash commands and capture `stdout`/`stderr` (with timeout protection).

## Prerequisites

- GCC / G++ (supporting C++20)
- CMake (>= 3.14)
- POSIX-compliant Linux (for `sys/socket.h`, `pthread`, `popen`, etc.)
- SQLite3 (optional for legacy components, but core MCP does not require it)

## Build & Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/hylmithecoder/mcp-cpp.git
   cd mcp-cpp
   ```

2. **Build with CMake:**
   ```bash
   cmake -B build
   cmake --build build
   ```

3. **Run the Server:**
   ```bash
   ./build/mcp
   ```
   *The server will start listening on `http://0.0.0.0:5500/mcp`.*

## Integration Setup (Claude.ai Web)

To use this server with Claude.ai web, you need to expose your local port `5500` to the internet.

**Option 1: Using ngrok**
```bash
ngrok http 5500
```
Then copy the `https://...ngrok-free.app/mcp` URL into Claude.ai.

## Future Updates Roadmap

The following enhancements are planned for future releases:

- **Authentication & Security:** 
  - Implementation of token-based authentication (Bearer tokens / API Keys) to prevent unauthorized access to the `run_command` and filesystem tools when exposed publicly.
- **Enhanced SSE (Server-Sent Events) Support:** 
  - Fully bidirectional Server-Sent Events to allow the server to push real-time updates (e.g., long-running command output streaming, filesystem watching) directly to the AI model.
- **Resource Templates:** 
  - Support for the MCP `resources/list` and `resources/read` protocols, allowing the AI to subscribe to file changes natively rather than polling.
- **Windows / macOS Support:** 
  - Refactoring POSIX-specific APIs (`dirent.h`, `sys/socket.h`, `popen`) to use abstract multi-platform libraries (like ASIO or libuv) for cross-platform compatibility.
- **Custom Tool Configurations:** 
  - A JSON/YAML configuration file to selectively enable/disable specific dangerous tools (like `run_command`) without recompiling.
- **History Execution Locally:** 
  - At least i will create a history execution locally, so the AI can see the history of the commands that have been executed.

## License

This project is licensed under the [MIT License](LICENSE).