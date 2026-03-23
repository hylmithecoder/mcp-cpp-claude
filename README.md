<div align="center">

# 🖥️ Native C++ MCP Server

**Give Claude direct access to your Linux machine — no Python, no Node.js, no runtime overhead.**

[![Language](https://img.shields.io/badge/language-C%2B%2B20-blue?logo=cplusplus)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-orange?logo=linux)](https://kernel.org/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![Protocol](https://img.shields.io/badge/MCP-Streamable%20HTTP-purple)](https://modelcontextprotocol.io)
[![Transport](https://img.shields.io/badge/transport-JSON--RPC%202.0-yellow)](https://www.jsonrpc.org/)

*Built from scratch in pure C++ — raw sockets, zero runtime dependencies.*

</div>

---

## ✨ What is this?

A **Model Context Protocol (MCP) server** written in native C++ that lets AI assistants like **Claude** interact directly with your local Linux system. No Python interpreter, no Node.js runtime — just a lean compiled binary that wakes up in milliseconds.

Connect it to Claude.ai via a tunnel (SSH, ngrok, Cloudflare) and Claude can:
- 📁 Browse your filesystem
- 📄 Read your source files
- ⚙️ Execute shell commands
- 📊 Monitor your system in real-time

```
 ┌─────────────┐   HTTPS Tunnel   ┌──────────────────┐   localhost:5500  ┌─────────────────┐
 │  Claude.ai  │ ◄──────────────► │  Tunnel (ngrok / │ ◄───────────────► │  MCP C++ Server │
 │   (Web UI)  │  MCP over HTTP   │  SSH / Cloudflare)│   JSON-RPC 2.0   │  (your machine) │
 └─────────────┘                  └──────────────────┘                   └─────────────────┘
```

---

## 🛠️ Tools Available

| Tool | Description |
|------|-------------|
| `list_directory` | List files & directories with sizes |
| `read_file` | Read text files or specific line ranges (up to 1MB) |
| `search_files` | Recursively search by filename pattern |
| `get_file_info` | File metadata: permissions, owner, timestamps |
| `get_system_info` | OS, kernel, CPU, RAM, disk usage |
| `list_processes` | Running processes sorted by CPU/memory/PID |
| `list_installed_apps` | Discover installed .desktop applications |
| `run_command` | Execute shell commands with timeout protection |

---

## ⚡ Quick Start

### Prerequisites
- GCC / G++ with C++20 support
- CMake >= 3.14
- SQLite3: `sudo apt install libsqlite3-dev`

### Build & Run

```bash
git clone https://github.com/hylmithecoder/mcp-cpp-claude.git
cd mcp-cpp-claude

cmake -B build
cmake --build build

./build/mcp
# 🚀 MCP Server listening on http://0.0.0.0:5500/mcp
```

### Connect to Claude.ai

Expose your local port via a tunnel, then add the URL in **Claude.ai → Settings → Integrations → Add MCP Server**.

**Option 1 — ngrok:**
```bash
ngrok http 5500
# Copy https://xxxx.ngrok-free.app/mcp → paste to Claude.ai
```

**Option 2 — Cloudflare Tunnel:**
```bash
cloudflared tunnel --url http://localhost:5500
```

**Option 3 — Reverse SSH to VPS:**
```bash
ssh -R 5500:localhost:5500 user@your-vps-ip
```

---

## 🏗️ Architecture

```
mcp-cpp-claude/
├── include/
│   ├── server.hpp          # Raw TCP socket HTTP server
│   ├── mcp_handler.hpp     # MCP protocol + JSON-RPC routing
│   ├── system_tools.hpp    # Tool definitions & implementations
│   └── handledb.hpp        # SQLite3 history logging
├── src/
│   ├── main.cpp            # Entry point
│   ├── server.cpp          # HTTP/1.1 parser, thread-per-client
│   ├── mcp_handler.cpp     # initialize / tools/list / tools/call
│   ├── system_tools.cpp    # All 8 tool implementations
│   └── handledb.cpp        # DB init, insert, read
└── CMakeLists.txt
```

**Key design decisions:**
- **Thread-per-client** via `pthread` — each connection isolated
- **Dual response mode** — direct HTTP body (Claude.ai web) or SSE stream
- **SQLite history** — every tool call logged locally for auditability

---

## 🔒 Security Notice

> ⚠️ **`run_command` executes arbitrary shell commands.** Never expose to the public internet without authentication.

For now, use a private tunnel (SSH reverse forwarding to a VPS you control). Bearer token auth is on the roadmap.

---

## 🗺️ Roadmap

- [ ] **Bearer token auth** — protect `run_command` from unauthorized access
- [ ] **Streaming SSE output** — real-time stdout for long-running commands
- [ ] **MCP Resources protocol** — `resources/list` + `resources/read`
- [ ] **Tool config file** — enable/disable tools without recompile
- [ ] **Windows / macOS support** — cross-platform via ASIO or libuv
- [x] **Execution history** — SQLite logging of all tool calls

---

## 🤝 Contributing

PRs welcome! Add new tools in `src/system_tools.cpp` and register in `getToolDefinitions()`.

---

## 📄 License

[MIT](LICENSE) — © 2026 Hylmi

---

<div align="center">

*Built with ❤️ and raw C++ sockets. No frameworks were harmed in the making of this server.*

</div>
