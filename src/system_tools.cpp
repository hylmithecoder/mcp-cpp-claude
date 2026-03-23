#include "../include/system_tools.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <array>
#include <ctime>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>

namespace fs = filesystem;

namespace MCP {

    string SystemTools::exec(const string& cmd) {
        array<char, 4096> buffer;
        string result;
        unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) {
            return "Error: Failed to execute command";
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }

    json SystemTools::makeTextResult(const string& text, bool isError) {
        return {
            {"content", json::array({
                {{"type", "text"}, {"text", text}}
            })},
            {"isError", isError}
        };
    }

    json SystemTools::getToolDefinitions() const {
        return json::array({
            {
                {"name", "get_history"},
                {"description", "Get the list of recently executed tool calls (summary)"},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"limit", {{"type", "integer"}, {"description", "Number of records to fetch (default: 10)"}}} 
                    }}
                }}
            },
            {
                {"name", "get_history_by_id"},
                {"description", "Get detailed history including full result by session/title ID"},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"id", {{"type", "integer"}, {"description", "The ID of the history entry"}}}
                    }},
                    {"required", {"id"}}
                }}
            },
            // 1. list_directory
            {
                {"name", "list_directory"},
                {"description", "List all files and directories at a given path. Returns name, type (file/directory), and size for each entry."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {
                            {"type", "string"},
                            {"description", "Absolute path to the directory to list. Defaults to home directory if not specified."}
                        }}
                    }},
                    {"required", json::array({"path"})}
                }}
            },
            // 2. read_file
            {
                {"name", "read_file"},
                {"description", "Read the contents of a text file. Can read the entire file or a specific line range."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {
                            {"type", "string"},
                            {"description", "Absolute path to the file to read"}
                        }},
                        {"start_line", {
                            {"type", "integer"},
                            {"description", "Optional start line (1-indexed). If omitted, reads from the beginning."}
                        }},
                        {"end_line", {
                            {"type", "integer"},
                            {"description", "Optional end line (1-indexed, inclusive). If omitted, reads to the end."}
                        }}
                    }},
                    {"required", json::array({"path"})}
                }}
            },
            // 3. search_files
            {
                {"name", "search_files"},
                {"description", "Search for files and directories matching a pattern recursively in a given directory."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {
                            {"type", "string"},
                            {"description", "Directory to search in"}
                        }},
                        {"pattern", {
                            {"type", "string"},
                            {"description", "Search pattern (substring match on filename). For example: '.cpp', 'main', 'config'"}
                        }},
                        {"max_results", {
                            {"type", "integer"},
                            {"description", "Maximum number of results. Default: 50"}
                        }}
                    }},
                    {"required", json::array({"path", "pattern"})}
                }}
            },
            // 4. get_file_info
            {
                {"name", "get_file_info"},
                {"description", "Get detailed metadata about a file or directory: size, permissions, owner, modified time, type."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {
                            {"type", "string"},
                            {"description", "Absolute path to the file or directory"}
                        }}
                    }},
                    {"required", json::array({"path"})}
                }}
            },
            // 5. get_system_info
            {
                {"name", "get_system_info"},
                {"description", "Get system information: OS, kernel, hostname, CPU, memory usage, disk usage."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", json::object()}
                }}
            },
            // 6. list_processes
            {
                {"name", "list_processes"},
                {"description", "List running processes with PID, name, CPU usage, and memory usage."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"sort_by", {
                            {"type", "string"},
                            {"description", "Sort by: 'cpu', 'mem', or 'pid'. Default: 'cpu'"},
                            {"enum", json::array({"cpu", "mem", "pid"})}
                        }},
                        {"limit", {
                            {"type", "integer"},
                            {"description", "Max number of processes to return. Default: 20"}
                        }}
                    }}
                }}
            },
            // 7. list_installed_apps
            {
                {"name", "list_installed_apps"},
                {"description", "List installed desktop applications on this Linux system."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"search", {
                            {"type", "string"},
                            {"description", "Optional search filter for application name"}
                        }}
                    }}
                }}
            },
            // 8. run_command
            {
                {"name", "run_command"},
                {"description", "Execute a shell command and return its output (stdout and stderr). Use with caution."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"command", {
                            {"type", "string"},
                            {"description", "The shell command to execute"}
                        }},
                        {"timeout", {
                            {"type", "integer"},
                            {"description", "Timeout in seconds. Default: 30"}
                        }}
                    }},
                    {"required", json::array({"command"})}
                }}
            }
        });
    }

    json SystemTools::callTool(const string& name, const json& arguments) {
        if (name == "list_directory") return listDirectory(arguments);
        if (name == "read_file") return readFile(arguments);
        if (name == "search_files") return searchFiles(arguments);
        if (name == "get_file_info") return getFileInfo(arguments);
        if (name == "get_system_info") return getSystemInfo(arguments);
        if (name == "list_processes") return listProcesses(arguments);
        if (name == "list_installed_apps") return listInstalledApps(arguments);
        if (name == "run_command") return runCommand(arguments);

        return makeTextResult("Unknown tool: " + name, true);
    }

    // ==================== Tool Implementations ====================

    json SystemTools::listDirectory(const json& args) {
        string path = args.value("path", "");
        if (path.empty()) {
            const char* home = getenv("HOME");
            path = home ? home : "/";
        }

        try {
            if (!fs::exists(path)) {
                return makeTextResult("Error: Path does not exist: " + path, true);
            }
            if (!fs::is_directory(path)) {
                return makeTextResult("Error: Not a directory: " + path, true);
            }

            ostringstream oss;
            oss << "Directory: " << path << "\n\n";
            oss << left << setw(50) << "Name"
                << setw(12) << "Type"
                << "Size" << "\n";
            oss << string(75, '-') << "\n";

            vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
                entries.push_back(entry);
            }

            // Sort: directories first, then files
            sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                if (a.is_directory() != b.is_directory()) return a.is_directory() > b.is_directory();
                return a.path().filename() < b.path().filename();
            });

            for (const auto& entry : entries) {
                string name = entry.path().filename().string();
                string type = entry.is_directory() ? "directory" : "file";
                string size = "-";

                if (entry.is_regular_file()) {
                    auto sz = entry.file_size();
                    if (sz < 1024) size = to_string(sz) + " B";
                    else if (sz < 1024*1024) size = to_string(sz/1024) + " KB";
                    else if (sz < 1024*1024*1024) size = to_string(sz/(1024*1024)) + " MB";
                    else size = to_string(sz/(1024*1024*1024)) + " GB";
                }

                oss << left << setw(50) << name
                    << setw(12) << type
                    << size << "\n";
            }

            oss << "\nTotal: " << entries.size() << " items";
            return makeTextResult(oss.str());

        } catch (const exception& e) {
            return makeTextResult("Error listing directory: " + string(e.what()), true);
        }
    }

    json SystemTools::readFile(const json& args) {
        string path = args.value("path", "");
        if (path.empty()) {
            return makeTextResult("Error: 'path' is required", true);
        }

        try {
            if (!fs::exists(path)) {
                return makeTextResult("Error: File does not exist: " + path, true);
            }
            if (!fs::is_regular_file(path)) {
                return makeTextResult("Error: Not a regular file: " + path, true);
            }

            // Check file size — limit to 1MB
            auto fileSize = fs::file_size(path);
            if (fileSize > 1024 * 1024) {
                return makeTextResult("Error: File too large (" + to_string(fileSize) + " bytes). Max 1MB. Use start_line/end_line to read portions.", true);
            }

            ifstream file(path);
            if (!file.is_open()) {
                return makeTextResult("Error: Cannot open file: " + path, true);
            }

            int startLine = args.value("start_line", 0);
            int endLine = args.value("end_line", 0);

            ostringstream oss;

            if (startLine > 0 || endLine > 0) {
                // Read specific line range
                string line;
                int lineNum = 0;
                int actualStart = max(1, startLine);

                while (getline(file, line)) {
                    lineNum++;
                    if (lineNum < actualStart) continue;
                    if (endLine > 0 && lineNum > endLine) break;
                    oss << lineNum << ": " << line << "\n";
                }

                if (oss.str().empty()) {
                    return makeTextResult("No content in specified line range", true);
                }
            } else {
                // Read entire file
                oss << file.rdbuf();
            }

            return makeTextResult(oss.str());

        } catch (const exception& e) {
            return makeTextResult("Error reading file: " + string(e.what()), true);
        }
    }

    json SystemTools::searchFiles(const json& args) {
        string path = args.value("path", "");
        string pattern = args.value("pattern", "");
        int maxResults = args.value("max_results", 50);

        if (path.empty() || pattern.empty()) {
            return makeTextResult("Error: 'path' and 'pattern' are required", true);
        }

        try {
            if (!fs::exists(path)) {
                return makeTextResult("Error: Path does not exist: " + path, true);
            }

            ostringstream oss;
            oss << "Search: \"" << pattern << "\" in " << path << "\n\n";

            int count = 0;
            for (auto it = fs::recursive_directory_iterator(
                    path, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator() && count < maxResults;
                 ++it) {
                try {
                    string name = it->path().filename().string();
                    // Case-insensitive substring match
                    string lowerName = name;
                    string lowerPattern = pattern;
                    transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    transform(lowerPattern.begin(), lowerPattern.end(), lowerPattern.begin(), ::tolower);

                    if (lowerName.find(lowerPattern) != string::npos) {
                        string type = it->is_directory() ? "[DIR] " : "[FILE]";
                        oss << type << " " << it->path().string() << "\n";
                        count++;
                    }
                } catch (...) {
                    continue; // Skip entries we can't access
                }
            }

            if (count == 0) {
                oss << "No files found matching pattern.";
            } else {
                oss << "\nFound: " << count << " results";
                if (count >= maxResults) oss << " (limit reached)";
            }

            return makeTextResult(oss.str());

        } catch (const exception& e) {
            return makeTextResult("Error searching: " + string(e.what()), true);
        }
    }

    json SystemTools::getFileInfo(const json& args) {
        string path = args.value("path", "");
        if (path.empty()) {
            return makeTextResult("Error: 'path' is required", true);
        }

        try {
            if (!fs::exists(path)) {
                return makeTextResult("Error: Path does not exist: " + path, true);
            }

            struct stat st;
            if (stat(path.c_str(), &st) != 0) {
                return makeTextResult("Error: Cannot stat file: " + path, true);
            }

            ostringstream oss;
            oss << "File Info: " << path << "\n\n";

            // Type
            string type;
            if (S_ISREG(st.st_mode)) type = "Regular file";
            else if (S_ISDIR(st.st_mode)) type = "Directory";
            else if (S_ISLNK(st.st_mode)) type = "Symbolic link";
            else if (S_ISBLK(st.st_mode)) type = "Block device";
            else if (S_ISCHR(st.st_mode)) type = "Character device";
            else if (S_ISFIFO(st.st_mode)) type = "FIFO/pipe";
            else if (S_ISSOCK(st.st_mode)) type = "Socket";
            else type = "Unknown";

            oss << "Type:        " << type << "\n";
            oss << "Size:        " << st.st_size << " bytes\n";

            // Permissions
            string perms;
            perms += (st.st_mode & S_IRUSR) ? 'r' : '-';
            perms += (st.st_mode & S_IWUSR) ? 'w' : '-';
            perms += (st.st_mode & S_IXUSR) ? 'x' : '-';
            perms += (st.st_mode & S_IRGRP) ? 'r' : '-';
            perms += (st.st_mode & S_IWGRP) ? 'w' : '-';
            perms += (st.st_mode & S_IXGRP) ? 'x' : '-';
            perms += (st.st_mode & S_IROTH) ? 'r' : '-';
            perms += (st.st_mode & S_IWOTH) ? 'w' : '-';
            perms += (st.st_mode & S_IXOTH) ? 'x' : '-';
            oss << "Permissions: " << perms << " (" << oct << (st.st_mode & 0777) << dec << ")\n";

            // Owner/Group
            struct passwd* pw = getpwuid(st.st_uid);
            struct group* gr = getgrgid(st.st_gid);
            oss << "Owner:       " << (pw ? pw->pw_name : to_string(st.st_uid)) << "\n";
            oss << "Group:       " << (gr ? gr->gr_name : to_string(st.st_gid)) << "\n";

            // Times
            char timebuf[64];
            struct tm* tm_info;
            tm_info = localtime(&st.st_mtime);
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
            oss << "Modified:    " << timebuf << "\n";

            tm_info = localtime(&st.st_atime);
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
            oss << "Accessed:    " << timebuf << "\n";

            return makeTextResult(oss.str());

        } catch (const exception& e) {
            return makeTextResult("Error: " + string(e.what()), true);
        }
    }

    json SystemTools::getSystemInfo(const json& args) {
        ostringstream oss;

        oss << "=== System Information ===\n\n";

        // Hostname
        string hostname = exec("hostname 2>/dev/null");
        if (!hostname.empty() && hostname.back() == '\n') hostname.pop_back();
        oss << "Hostname:     " << hostname << "\n";

        // OS
        string os = exec("cat /etc/os-release 2>/dev/null | grep PRETTY_NAME | cut -d= -f2 | tr -d '\"'");
        if (!os.empty() && os.back() == '\n') os.pop_back();
        oss << "OS:           " << os << "\n";

        // Kernel
        string kernel = exec("uname -r 2>/dev/null");
        if (!kernel.empty() && kernel.back() == '\n') kernel.pop_back();
        oss << "Kernel:       " << kernel << "\n";

        // Architecture
        string arch = exec("uname -m 2>/dev/null");
        if (!arch.empty() && arch.back() == '\n') arch.pop_back();
        oss << "Architecture: " << arch << "\n";

        // CPU
        string cpu = exec("cat /proc/cpuinfo 2>/dev/null | grep 'model name' | head -1 | cut -d: -f2");
        if (!cpu.empty() && cpu[0] == ' ') cpu = cpu.substr(1);
        if (!cpu.empty() && cpu.back() == '\n') cpu.pop_back();
        string cpuCores = exec("nproc 2>/dev/null");
        if (!cpuCores.empty() && cpuCores.back() == '\n') cpuCores.pop_back();
        oss << "CPU:          " << cpu << " (" << cpuCores << " cores)\n";

        // Memory
        string mem = exec("free -h 2>/dev/null | grep Mem | awk '{print \"Total: \" $2 \", Used: \" $3 \", Free: \" $4}'");
        if (!mem.empty() && mem.back() == '\n') mem.pop_back();
        oss << "Memory:       " << mem << "\n";

        // Disk
        string disk = exec("df -h / 2>/dev/null | tail -1 | awk '{print \"Total: \" $2 \", Used: \" $3 \" (\" $5 \"), Free: \" $4}'");
        if (!disk.empty() && disk.back() == '\n') disk.pop_back();
        oss << "Disk (/):     " << disk << "\n";

        // Uptime
        string uptime = exec("uptime -p 2>/dev/null");
        if (!uptime.empty() && uptime.back() == '\n') uptime.pop_back();
        oss << "Uptime:       " << uptime << "\n";

        // Current user
        string user = exec("whoami 2>/dev/null");
        if (!user.empty() && user.back() == '\n') user.pop_back();
        oss << "User:         " << user << "\n";

        return makeTextResult(oss.str());
    }

    json SystemTools::listProcesses(const json& args) {
        string sortBy = args.value("sort_by", "cpu");
        int limit = args.value("limit", 20);

        string sortFlag = "--sort=-%cpu";
        if (sortBy == "mem") sortFlag = "--sort=-%mem";
        else if (sortBy == "pid") sortFlag = "--sort=pid";

        string cmd = "ps aux " + sortFlag + " 2>/dev/null | head -" + to_string(limit + 1);
        string output = exec(cmd);

        if (output.empty()) {
            return makeTextResult("Error: Unable to list processes", true);
        }

        return makeTextResult("Running Processes (sorted by " + sortBy + ", top " + to_string(limit) + "):\n\n" + output);
    }

    json SystemTools::listInstalledApps(const json& args) {
        string search = args.value("search", "");

        ostringstream oss;
        oss << "Installed Desktop Applications";
        if (!search.empty()) oss << " (filter: " << search << ")";
        oss << ":\n\n";

        string appsDir = "/usr/share/applications";
        int count = 0;

        try {
            if (!fs::exists(appsDir)) {
                return makeTextResult("Error: Applications directory not found", true);
            }

            for (const auto& entry : fs::directory_iterator(appsDir)) {
                if (entry.path().extension() == ".desktop") {
                    ifstream file(entry.path());
                    string line;
                    string appName, appComment, appExec;
                    bool inDesktopEntry = false;

                    while (getline(file, line)) {
                        if (line == "[Desktop Entry]") {
                            inDesktopEntry = true;
                            continue;
                        }
                        if (line.size() > 0 && line[0] == '[') {
                            inDesktopEntry = false;
                            continue;
                        }

                        if (inDesktopEntry) {
                            if (line.substr(0, 5) == "Name=" && appName.empty()) {
                                appName = line.substr(5);
                            } else if (line.substr(0, 8) == "Comment=" && appComment.empty()) {
                                appComment = line.substr(8);
                            } else if (line.substr(0, 5) == "Exec=" && appExec.empty()) {
                                appExec = line.substr(5);
                            }
                        }
                    }

                    if (!appName.empty()) {
                        // Apply search filter
                        if (!search.empty()) {
                            string lowerName = appName;
                            string lowerSearch = search;
                            transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                            transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
                            if (lowerName.find(lowerSearch) == string::npos) continue;
                        }

                        oss << "• " << appName;
                        if (!appComment.empty()) oss << " — " << appComment;
                        oss << "\n";
                        count++;
                    }
                }
            }

            // Also check ~/.local/share/applications
            const char* home = getenv("HOME");
            if (home) {
                string userAppsDir = string(home) + "/.local/share/applications";
                if (fs::exists(userAppsDir)) {
                    for (const auto& entry : fs::directory_iterator(userAppsDir)) {
                        if (entry.path().extension() == ".desktop") {
                            ifstream file(entry.path());
                            string line;
                            string appName;
                            bool inDesktopEntry = false;

                            while (getline(file, line)) {
                                if (line == "[Desktop Entry]") { inDesktopEntry = true; continue; }
                                if (line.size() > 0 && line[0] == '[') { inDesktopEntry = false; continue; }
                                if (inDesktopEntry && line.substr(0, 5) == "Name=" && appName.empty()) {
                                    appName = line.substr(5);
                                }
                            }

                            if (!appName.empty()) {
                                if (!search.empty()) {
                                    string lowerName = appName;
                                    string lowerSearch = search;
                                    transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                                    transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
                                    if (lowerName.find(lowerSearch) == string::npos) continue;
                                }
                                oss << "• " << appName << " (user-installed)\n";
                                count++;
                            }
                        }
                    }
                }
            }

        } catch (const exception& e) {
            return makeTextResult("Error: " + string(e.what()), true);
        }

        oss << "\nTotal: " << count << " applications";
        return makeTextResult(oss.str());
    }

    json SystemTools::runCommand(const json& args) {
        string command = args.value("command", "");
        int timeout = args.value("timeout", 30);

        if (command.empty()) {
            return makeTextResult("Error: 'command' is required", true);
        }

        // Wrap with timeout
        string cmd = "timeout " + to_string(timeout) + " bash -c " +
                         "'" + command + "'" + " 2>&1";

        string output = exec(cmd);

        if (output.empty()) {
            output = "(command produced no output)";
        }

        return makeTextResult("$ " + command + "\n\n" + output);
    }

} // namespace MCP
