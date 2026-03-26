#ifdef ANDROID
    #include <ghc/filesystem.hpp>
#else
    #include <filesystem>
#endif
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdio>
#include "../include/system_tools.hpp"

using namespace std;
using json = nlohmann::json;

#ifdef ANDROID
    namespace fs = ghc::filesystem;
#else
    namespace fs = std::filesystem;
#endif

namespace MCP {

    // ======================== HELPER FUNCTIONS ========================

    // Read all lines from a file into a vector
    static bool readAllLines(const string& path, vector<string>& lines, string& error) {
        ifstream file(path, ios::in | ios::binary);
        if (!file.is_open()) {
            error = "Cannot open file: " + path;
            return false;
        }
        string line;
        while (getline(file, line)) {
            // Remove \r if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        file.close();
        return true;
    }

    // Write lines back to file using atomic write (write to .tmp then rename)
    static bool writeLinesToFile(const string& path, const vector<string>& lines, string& error) {
        string tmpPath = path + ".mcp_tmp";

        // Ensure parent directory exists
        try {
            fs::path parentDir = fs::path(path).parent_path();
            if (!parentDir.empty() && !fs::exists(parentDir)) {
                fs::create_directories(parentDir);
            }
        } catch (const exception& e) {
            error = "Cannot create parent directory: " + string(e.what());
            return false;
        }

        // Write to temp file first
        {
            ofstream tmp(tmpPath, ios::out | ios::binary | ios::trunc);
            if (!tmp.is_open()) {
                error = "Cannot create temp file: " + tmpPath;
                return false;
            }
            for (size_t i = 0; i < lines.size(); i++) {
                tmp << lines[i];
                if (i + 1 < lines.size()) {
                    tmp << "\n";
                }
            }
            // Always end with newline for POSIX compliance
            if (!lines.empty()) {
                tmp << "\n";
            }
            tmp.flush();
            if (tmp.fail()) {
                tmp.close();
                remove(tmpPath.c_str());
                error = "Failed to flush data to temp file";
                return false;
            }
            tmp.close();
        }

        // Atomic rename
        try {
            fs::rename(tmpPath, path);
        } catch (const exception& e) {
            // Fallback: copy content if rename fails (cross-device)
            try {
                fs::copy_file(tmpPath, path, fs::copy_options::overwrite_existing);
                fs::remove(tmpPath);
            } catch (const exception& e2) {
                remove(tmpPath.c_str());
                error = "Failed to finalize file: " + string(e2.what());
                return false;
            }
        }

        return true;
    }

    // Write raw content to file using atomic write
    static bool writeRawToFile(const string& path, const string& content, string& error) {
        string tmpPath = path + ".mcp_tmp";

        // Ensure parent directory exists
        try {
            fs::path parentDir = fs::path(path).parent_path();
            if (!parentDir.empty() && !fs::exists(parentDir)) {
                fs::create_directories(parentDir);
            }
        } catch (const exception& e) {
            error = "Cannot create parent directory: " + string(e.what());
            return false;
        }

        // Write to temp file
        {
            ofstream tmp(tmpPath, ios::out | ios::binary | ios::trunc);
            if (!tmp.is_open()) {
                error = "Cannot create temp file: " + tmpPath;
                return false;
            }
            tmp.write(content.c_str(), content.size());
            tmp.flush();
            if (tmp.fail()) {
                tmp.close();
                remove(tmpPath.c_str());
                error = "Failed to write data to temp file";
                return false;
            }
            tmp.close();
        }

        // Atomic rename
        try {
            fs::rename(tmpPath, path);
        } catch (const exception& e) {
            try {
                fs::copy_file(tmpPath, path, fs::copy_options::overwrite_existing);
                fs::remove(tmpPath);
            } catch (const exception& e2) {
                remove(tmpPath.c_str());
                error = "Failed to finalize file: " + string(e2.what());
                return false;
            }
        }

        return true;
    }

    // Create a backup of a file before modifying it
    static bool createBackup(const string& path, string& error) {
        if (!fs::exists(path)) return true; // Nothing to backup
        string bakPath = path + ".mcp_bak";
        try {
            fs::copy_file(path, bakPath, fs::copy_options::overwrite_existing);
        } catch (const exception& e) {
            error = "Cannot create backup: " + string(e.what());
            return false;
        }
        return true;
    }

    // Count occurrences of a needle in a haystack
    static int countOccurrences(const string& haystack, const string& needle) {
        int count = 0;
        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != string::npos) {
            count++;
            pos += needle.size();
        }
        return count;
    }

    // ======================== MAIN EDITOR FUNCTION ========================

    json SystemTools::editor(const json& args) {
        string command = args.value("command", "");
        string path = args.value("path", "");

        if (command.empty()) {
            return makeTextResult("Error: 'command' is required. Available commands: create, write, append, insert, replace, replace_lines, delete_lines, undo, diff, read", true);
        }
        if (path.empty()) {
            return makeTextResult("Error: 'path' is required", true);
        }

        try {
            // ==================== CREATE ====================
            if (command == "create") {
                bool overwrite = args.value("overwrite", false);
                string content = args.value("content", "");

                if (fs::exists(path) && !overwrite) {
                    return makeTextResult("Error: File already exists: " + path + ". Use overwrite=true to force.", true);
                }

                // Backup if overwriting existing file
                if (fs::exists(path) && overwrite) {
                    string backupErr;
                    if (!createBackup(path, backupErr)) {
                        return makeTextResult("Error: " + backupErr, true);
                    }
                }

                string err;
                if (!writeRawToFile(path, content, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                auto fileSize = fs::file_size(path);
                ostringstream oss;
                oss << "✅ File created: " << path << "\n";
                oss << "Size: " << fileSize << " bytes";
                if (!content.empty()) {
                    int lineCount = 1;
                    for (char c : content) if (c == '\n') lineCount++;
                    oss << "\nLines: " << lineCount;
                }
                return makeTextResult(oss.str());
            }

            // ==================== WRITE ====================
            else if (command == "write") {
                string content = args.value("content", "");

                // Backup existing file
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    return makeTextResult("Error: " + backupErr, true);
                }

                string err;
                if (!writeRawToFile(path, content, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                auto fileSize = fs::file_size(path);
                int lineCount = 1;
                for (char c : content) if (c == '\n') lineCount++;

                ostringstream oss;
                oss << "✅ File written: " << path << "\n";
                oss << "Size: " << fileSize << " bytes\n";
                oss << "Lines: " << lineCount;
                return makeTextResult(oss.str());
            }

            // ==================== APPEND ====================
            else if (command == "append") {
                string content = args.value("content", "");
                if (content.empty()) {
                    return makeTextResult("Error: 'content' is required for append", true);
                }

                // Backup
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    return makeTextResult("Error: " + backupErr, true);
                }

                // Read existing content
                string existing;
                if (fs::exists(path)) {
                    ifstream file(path, ios::in | ios::binary);
                    if (file.is_open()) {
                        ostringstream ss;
                        ss << file.rdbuf();
                        existing = ss.str();
                        file.close();
                    }
                }

                string newContent = existing + content;
                string err;
                if (!writeRawToFile(path, newContent, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                auto fileSize = fs::file_size(path);
                ostringstream oss;
                oss << "✅ Content appended to: " << path << "\n";
                oss << "Appended: " << content.size() << " bytes\n";
                oss << "Total size: " << fileSize << " bytes";
                return makeTextResult(oss.str());
            }

            // ==================== INSERT ====================
            else if (command == "insert") {
                int lineNum = args.value("line", 0);
                string content = args.value("content", "");

                if (lineNum < 1) {
                    return makeTextResult("Error: 'line' must be >= 1 (1-indexed)", true);
                }
                if (content.empty()) {
                    return makeTextResult("Error: 'content' is required for insert", true);
                }

                // Backup
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    return makeTextResult("Error: " + backupErr, true);
                }

                // Read existing lines
                vector<string> lines;
                if (fs::exists(path)) {
                    string readErr;
                    if (!readAllLines(path, lines, readErr)) {
                        return makeTextResult("Error: " + readErr, true);
                    }
                }

                // Parse content into lines to insert
                vector<string> insertLines;
                istringstream iss(content);
                string l;
                while (getline(iss, l)) {
                    if (!l.empty() && l.back() == '\r') l.pop_back();
                    insertLines.push_back(l);
                }

                // Adjust line number if beyond file
                size_t insertPos = min(static_cast<size_t>(lineNum - 1), lines.size());

                // Insert the lines
                lines.insert(lines.begin() + insertPos, insertLines.begin(), insertLines.end());

                string err;
                if (!writeLinesToFile(path, lines, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                ostringstream oss;
                oss << "✅ Inserted " << insertLines.size() << " line(s) at line " << lineNum << " in: " << path << "\n";
                oss << "Total lines: " << lines.size();
                return makeTextResult(oss.str());
            }

            // ==================== REPLACE (text search & replace) ====================
            else if (command == "replace") {
                string search = args.value("search", "");
                string replacement = args.value("replacement", "");
                bool replaceAll = args.value("all", false);

                if (search.empty()) {
                    return makeTextResult("Error: 'search' is required for replace", true);
                }

                if (!fs::exists(path)) {
                    return makeTextResult("Error: File does not exist: " + path, true);
                }

                // Backup
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    return makeTextResult("Error: " + backupErr, true);
                }

                // Read file content
                ifstream file(path, ios::in | ios::binary);
                if (!file.is_open()) {
                    return makeTextResult("Error: Cannot open file: " + path, true);
                }
                ostringstream ss;
                ss << file.rdbuf();
                string content = ss.str();
                file.close();

                // Count occurrences
                int total = countOccurrences(content, search);
                if (total == 0) {
                    return makeTextResult("Error: Search text not found in file", true);
                }

                // Do replacement
                int replaced = 0;
                string result;
                size_t pos = 0;
                size_t searchLen = search.size();

                while (pos < content.size()) {
                    size_t found = content.find(search, pos);
                    if (found == string::npos) {
                        result += content.substr(pos);
                        break;
                    }
                    result += content.substr(pos, found - pos);
                    result += replacement;
                    replaced++;
                    pos = found + searchLen;

                    if (!replaceAll && replaced >= 1) {
                        result += content.substr(pos);
                        break;
                    }
                }

                string err;
                if (!writeRawToFile(path, result, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                ostringstream oss;
                oss << "✅ Replaced " << replaced << " occurrence(s) in: " << path << "\n";
                oss << "Total occurrences found: " << total;
                if (!replaceAll && total > 1) {
                    oss << " (only first replaced, use all=true for all)";
                }
                return makeTextResult(oss.str());
            }

            // ==================== REPLACE_LINES ====================
            else if (command == "replace_lines") {
                int startLine = args.value("start_line", 0);
                int endLine = args.value("end_line", 0);
                string content = args.value("content", "");

                if (startLine < 1 || endLine < 1) {
                    return makeTextResult("Error: 'start_line' and 'end_line' must be >= 1 (1-indexed)", true);
                }
                if (startLine > endLine) {
                    return makeTextResult("Error: 'start_line' must be <= 'end_line'", true);
                }
                if (!fs::exists(path)) {
                    return makeTextResult("Error: File does not exist: " + path, true);
                }

                // Backup
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    return makeTextResult("Error: " + backupErr, true);
                }

                // Read lines
                vector<string> lines;
                string readErr;
                if (!readAllLines(path, lines, readErr)) {
                    return makeTextResult("Error: " + readErr, true);
                }

                if (startLine > (int)lines.size()) {
                    return makeTextResult("Error: start_line (" + to_string(startLine) + ") exceeds file length (" + to_string(lines.size()) + " lines)", true);
                }

                // Clamp end_line
                int actualEnd = min(endLine, (int)lines.size());

                // Parse replacement content into lines
                vector<string> newLines;
                if (!content.empty()) {
                    istringstream iss(content);
                    string l;
                    while (getline(iss, l)) {
                        if (!l.empty() && l.back() == '\r') l.pop_back();
                        newLines.push_back(l);
                    }
                }

                // Remove old lines and insert new ones
                int removedCount = actualEnd - startLine + 1;
                lines.erase(lines.begin() + (startLine - 1), lines.begin() + actualEnd);
                lines.insert(lines.begin() + (startLine - 1), newLines.begin(), newLines.end());

                string err;
                if (!writeLinesToFile(path, lines, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                ostringstream oss;
                oss << "✅ Replaced lines " << startLine << "-" << actualEnd << " (" << removedCount << " line(s)) with " << newLines.size() << " new line(s)\n";
                oss << "File: " << path << "\n";
                oss << "Total lines: " << lines.size();
                return makeTextResult(oss.str());
            }

            // ==================== DELETE_LINES ====================
            else if (command == "delete_lines") {
                int startLine = args.value("start_line", 0);
                int endLine = args.value("end_line", 0);

                if (startLine < 1 || endLine < 1) {
                    return makeTextResult("Error: 'start_line' and 'end_line' must be >= 1 (1-indexed)", true);
                }
                if (startLine > endLine) {
                    return makeTextResult("Error: 'start_line' must be <= 'end_line'", true);
                }
                if (!fs::exists(path)) {
                    return makeTextResult("Error: File does not exist: " + path, true);
                }

                // Backup
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    return makeTextResult("Error: " + backupErr, true);
                }

                // Read lines
                vector<string> lines;
                string readErr;
                if (!readAllLines(path, lines, readErr)) {
                    return makeTextResult("Error: " + readErr, true);
                }

                if (startLine > (int)lines.size()) {
                    return makeTextResult("Error: start_line (" + to_string(startLine) + ") exceeds file length (" + to_string(lines.size()) + " lines)", true);
                }

                int actualEnd = min(endLine, (int)lines.size());
                int removedCount = actualEnd - startLine + 1;

                lines.erase(lines.begin() + (startLine - 1), lines.begin() + actualEnd);

                string err;
                if (!writeLinesToFile(path, lines, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                ostringstream oss;
                oss << "✅ Deleted lines " << startLine << "-" << actualEnd << " (" << removedCount << " line(s))\n";
                oss << "File: " << path << "\n";
                oss << "Total lines remaining: " << lines.size();
                return makeTextResult(oss.str());
            }

            // ==================== UNDO ====================
            else if (command == "undo") {
                string bakPath = path + ".mcp_bak";

                if (!fs::exists(bakPath)) {
                    return makeTextResult("Error: No backup found for: " + path + " (nothing to undo)", true);
                }

                // Read backup content
                ifstream bakFile(bakPath, ios::in | ios::binary);
                if (!bakFile.is_open()) {
                    return makeTextResult("Error: Cannot read backup file", true);
                }
                ostringstream ss;
                ss << bakFile.rdbuf();
                string bakContent = ss.str();
                bakFile.close();

                // Save current as new backup (so undo can be toggled)
                string backupErr;
                if (!createBackup(path, backupErr)) {
                    // Non-fatal, continue with undo
                    cerr << "[Editor] Warning: couldn't backup current before undo: " << backupErr << endl;
                }

                // Restore backup
                string err;
                if (!writeRawToFile(path, bakContent, err)) {
                    return makeTextResult("Error: " + err, true);
                }

                auto fileSize = fs::file_size(path);
                ostringstream oss;
                oss << "✅ Undo successful: " << path << "\n";
                oss << "Restored from backup (" << fileSize << " bytes)";
                return makeTextResult(oss.str());
            }

            // ==================== DIFF ====================
            else if (command == "diff") {
                string bakPath = path + ".mcp_bak";

                if (!fs::exists(bakPath)) {
                    return makeTextResult("No backup found for: " + path + " (no changes tracked)", false);
                }
                if (!fs::exists(path)) {
                    return makeTextResult("Error: File does not exist: " + path, true);
                }

                // Read both files
                vector<string> bakLines, curLines;
                string readErr;
                if (!readAllLines(bakPath, bakLines, readErr)) {
                    return makeTextResult("Error reading backup: " + readErr, true);
                }
                if (!readAllLines(path, curLines, readErr)) {
                    return makeTextResult("Error reading current: " + readErr, true);
                }

                // Simple line-by-line diff
                ostringstream oss;
                oss << "Diff for: " << path << "\n";
                oss << "Backup: " << bakLines.size() << " lines | Current: " << curLines.size() << " lines\n";
                oss << string(60, '=') << "\n";

                size_t maxLines = max(bakLines.size(), curLines.size());
                int changes = 0;

                for (size_t i = 0; i < maxLines; i++) {
                    string bakLine = (i < bakLines.size()) ? bakLines[i] : "";
                    string curLine = (i < curLines.size()) ? curLines[i] : "";

                    if (bakLine != curLine) {
                        changes++;
                        if (i < bakLines.size()) {
                            oss << "- " << (i + 1) << ": " << bakLine << "\n";
                        }
                        if (i < curLines.size()) {
                            oss << "+ " << (i + 1) << ": " << curLine << "\n";
                        }
                    }
                }

                if (changes == 0) {
                    oss << "No differences found.";
                } else {
                    oss << string(60, '=') << "\n";
                    oss << "Total changed lines: " << changes;
                }
                return makeTextResult(oss.str());
            }

            // ==================== READ ====================
            else if (command == "read") {
                if (!fs::exists(path)) {
                    return makeTextResult("Error: File does not exist: " + path, true);
                }
                if (!fs::is_regular_file(path)) {
                    return makeTextResult("Error: Not a regular file: " + path, true);
                }

                int startLine = args.value("start_line", 0);
                int endLine = args.value("end_line", 0);

                ifstream file(path, ios::in | ios::binary);
                if (!file.is_open()) {
                    return makeTextResult("Error: Cannot open file: " + path, true);
                }

                ostringstream oss;
                if (startLine > 0 || endLine > 0) {
                    string line;
                    int lineNum = 0;
                    int actualStart = max(1, startLine);

                    while (getline(file, line)) {
                        lineNum++;
                        if (lineNum < actualStart) continue;
                        if (endLine > 0 && lineNum > endLine) break;
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        oss << lineNum << ": " << line << "\n";
                    }

                    if (oss.str().empty()) {
                        return makeTextResult("No content in specified line range", true);
                    }
                } else {
                    oss << file.rdbuf();
                }
                file.close();

                return makeTextResult(oss.str());
            }

            // ==================== UNKNOWN COMMAND ====================
            else {
                return makeTextResult("Error: Unknown command '" + command + "'. Available commands: create, write, append, insert, replace, replace_lines, delete_lines, undo, diff, read", true);
            }

        } catch (const exception& e) {
            return makeTextResult("Error in editor (" + command + "): " + string(e.what()), true);
        }
    }

} // namespace MCP