#include "../include/fs_alias.hpp"
#include "../include/system_tools.hpp"
#include <array>
#include <fstream>
#include <ftp/ftp.hpp>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;
using json = nlohmann::json;

namespace MCP {

// ======================== HELPER FUNCTIONS ========================

// Standard Base64 alphabet (RFC 4648).
static const char *kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Encode raw bytes to a Base64 string.
static string base64Encode(const string &in) {
  string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  int val = 0, bits = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    bits += 8;
    while (bits >= 0) {
      out.push_back(kBase64Chars[(val >> bits) & 0x3F]);
      bits -= 6;
    }
  }
  if (bits > -6)
    out.push_back(kBase64Chars[((val << 8) >> (bits + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

// Thread-safe reverse lookup table (magic static, initialized once).
static const array<int, 256> &base64ReverseTable() {
  static const array<int, 256> table = [] {
    array<int, 256> t{};
    t.fill(-1);
    for (int i = 0; i < 64; i++)
      t[(unsigned char)kBase64Chars[i]] = i;
    return t;
  }();
  return table;
}

// Decode a Base64 string into raw bytes. Whitespace is ignored so newline-
// wrapped payloads from web clients are accepted. Returns false on invalid
// input so callers can report an error instead of writing garbage.
static bool base64Decode(const string &in, string &out) {
  const auto &T = base64ReverseTable();
  out.clear();
  out.reserve((in.size() / 4) * 3);
  int val = 0, bits = -8;
  for (unsigned char c : in) {
    if (c == '=')
      break;
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
      continue;
    int d = T[c];
    if (d == -1)
      return false; // invalid character
    val = (val << 6) + d;
    bits += 6;
    if (bits >= 0) {
      out.push_back(char((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return true;
}

// Get the local FTP mirror base path: $HOME/.mcp/ftp/
static string getFtpMirrorBase() {
  const char *home = getenv("HOME");
  string base = home ? string(home) : ".";
  base += "/.mcp/ftp";
  return base;
}

// Convert a remote path to a local mirror path
// e.g. /public_html/index.html → $HOME/.mcp/ftp/public_html/index.html
static string toLocalMirrorPath(const string &remotePath) {
  string base = getFtpMirrorBase();
  string clean = remotePath;
  // Ensure leading slash
  if (!clean.empty() && clean[0] != '/') {
    clean = "/" + clean;
  }
  return base + clean;
}

// Ensure parent directories exist for a local path
static void ensureParentDir(const string &filePath) {
  fs::path p(filePath);
  fs::path parent = p.parent_path();
  if (!parent.empty() && !fs::exists(parent)) {
    fs::create_directories(parent);
  }
}

// Read FTP credentials from environment
struct FtpCredentials {
  string hostname;
  string username;
  string password;
  bool valid;
  string error;
};

static FtpCredentials getFtpCredentials() {
  FtpCredentials creds;
  const char *host = getenv("FTPHOSTNAME");
  const char *user = getenv("FTPUSERNAME");
  const char *pass = getenv("FTPPASSWORD");

  if (!host || string(host).empty()) {
    creds.valid = false;
    creds.error =
        "FTPHOSTNAME not set in environment. Please set it in .env file.";
    return creds;
  }
  if (!user || string(user).empty()) {
    creds.valid = false;
    creds.error =
        "FTPUSERNAME not set in environment. Please set it in .env file.";
    return creds;
  }
  if (!pass || string(pass).empty()) {
    creds.valid = false;
    creds.error =
        "FTPPASSWORD not set in environment. Please set it in .env file.";
    return creds;
  }

  creds.hostname = host;
  creds.username = user;
  creds.password = pass;
  creds.valid = true;
  return creds;
}

// ======================== MAIN FTP HANDLER ========================

json SystemTools::ftpHandler(const json &args) {
  string command = args.value("command", "");

  if (command.empty()) {
    return makeTextResult(
        "Error: 'command' is required. Available commands: "
        "list, read, upload, edit, delete, delete_dir, rename, mkdir, info",
        true);
  }

  // When encoding == "base64", text payloads are Base64-decoded on write
  // (upload/edit) and Base64-encoded on read. This avoids corruption of
  // quotes, $, backticks and indentation when called from web AI clients.
  string encoding = args.value("encoding", "plain");
  bool useBase64 = (encoding == "base64" || encoding == "b64");

  // Get credentials
  FtpCredentials creds = getFtpCredentials();
  if (!creds.valid) {
    return makeTextResult("Error: " + creds.error, true);
  }

  try {
    // Create FTP client and connect
    ftp::client ftpClient;
    ftpClient.connect(creds.hostname, 21, creds.username, creds.password);

    json result;

    // ==================== LIST ====================
    if (command == "list") {
      string remotePath = args.value("remote_path", "/");

      auto fileListReply = ftpClient.get_file_list(remotePath);
      const auto &fileList = fileListReply.get_file_list();

      ostringstream oss;
      oss << "📂 FTP Directory: " << remotePath << "\n";
      oss << "Host: " << creds.hostname << "\n";
      oss << string(60, '-') << "\n";

      if (fileList.empty()) {
        oss << "(empty directory)\n";
      } else {
        for (const auto &entry : fileList) {
          if (!entry.empty()) {
            oss << entry << "\n";
          }
        }
      }

      oss << string(60, '-') << "\n";
      oss << "Total: " << fileList.size() << " entries";

      // Cache the listing locally
      string localDir = toLocalMirrorPath(remotePath);
      ensureParentDir(localDir + "/.listing");
      ofstream listFile(localDir + "/.listing", ios::trunc);
      if (listFile.is_open()) {
        listFile << oss.str();
        listFile.close();
      }

      result = makeTextResult(oss.str());
    }

    // ==================== READ ====================
    else if (command == "read") {
      string remotePath = args.value("remote_path", "");
      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for read command", true);
      }

      // Download to local mirror
      string localPath = toLocalMirrorPath(remotePath);
      ensureParentDir(localPath);

      {
        ofstream ofs(localPath, ios::binary | ios::trunc);
        if (!ofs.is_open()) {
          ftpClient.disconnect();
          return makeTextResult(
              "Error: Cannot create local cache file: " + localPath, true);
        }
        ftp::ostream_adapter adapter(ofs);
        ftpClient.download_file(adapter, remotePath);
        ofs.close();
      }

      // Read the downloaded content
      ifstream ifs(localPath, ios::binary);
      if (!ifs.is_open()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: Cannot read downloaded file: " + localPath, true);
      }
      ostringstream contentStream;
      contentStream << ifs.rdbuf();
      ifs.close();
      string content = contentStream.str();

      // Size info
      auto fileSize = fs::file_size(localPath);
      int lineCount = 1;
      for (char c : content) {
        if (c == '\n')
          lineCount++;
      }

      ostringstream oss;
      oss << "📄 FTP File: " << remotePath << "\n";
      oss << "Cached at: " << localPath << "\n";
      oss << "Size: " << fileSize << " bytes | Lines: " << lineCount << "\n";
      if (useBase64) {
        oss << "Encoding: base64\n";
        oss << string(60, '-') << "\n";
        oss << base64Encode(content);
      } else {
        oss << string(60, '-') << "\n";
        oss << content;
      }

      result = makeTextResult(oss.str());
    }

    // ==================== UPLOAD ====================
    else if (command == "upload") {
      string remotePath = args.value("remote_path", "");
      string content = args.value("content", "");

      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for upload command", true);
      }
      if (content.empty()) {
        ftpClient.disconnect();
        return makeTextResult("Error: 'content' is required for upload command",
                              true);
      }

      if (useBase64) {
        string decoded;
        if (!base64Decode(content, decoded)) {
          ftpClient.disconnect();
          return makeTextResult("Error: 'content' is not valid Base64 "
                                "(encoding=base64 was set)",
                                true);
        }
        content = decoded;
      }

      // Save to local mirror first
      string localPath = toLocalMirrorPath(remotePath);
      ensureParentDir(localPath);
      {
        ofstream ofs(localPath, ios::binary | ios::trunc);
        if (!ofs.is_open()) {
          ftpClient.disconnect();
          return makeTextResult("Error: Cannot create local file: " + localPath,
                                true);
        }
        ofs.write(content.c_str(), content.size());
        ofs.close();
      }

      // Upload to FTP
      {
        istringstream iss(content);
        ftp::istream_adapter adapter(iss);
        ftpClient.upload_file(adapter, remotePath);
      }

      ostringstream oss;
      oss << "✅ File uploaded to FTP: " << remotePath << "\n";
      oss << "Size: " << content.size() << " bytes\n";
      oss << "Local cache: " << localPath;

      result = makeTextResult(oss.str());
    }

    // ==================== EDIT ====================
    else if (command == "edit") {
      string remotePath = args.value("remote_path", "");
      string search = args.value("search", "");
      string replacement = args.value("replacement", "");

      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for edit command", true);
      }
      if (search.empty()) {
        ftpClient.disconnect();
        return makeTextResult("Error: 'search' is required for edit command",
                              true);
      }

      if (useBase64) {
        string ds, dr;
        if (!base64Decode(search, ds)) {
          ftpClient.disconnect();
          return makeTextResult("Error: 'search' is not valid Base64 "
                                "(encoding=base64 was set)",
                                true);
        }
        if (!base64Decode(replacement, dr)) {
          ftpClient.disconnect();
          return makeTextResult("Error: 'replacement' is not valid Base64 "
                                "(encoding=base64 was set)",
                                true);
        }
        search = ds;
        replacement = dr;
      }

      // Download the file
      string localPath = toLocalMirrorPath(remotePath);
      ensureParentDir(localPath);

      {
        ofstream ofs(localPath, ios::binary | ios::trunc);
        if (!ofs.is_open()) {
          ftpClient.disconnect();
          return makeTextResult(
              "Error: Cannot create local cache file: " + localPath, true);
        }
        ftp::ostream_adapter adapter(ofs);
        ftpClient.download_file(adapter, remotePath);
        ofs.close();
      }

      // Read content
      ifstream ifs(localPath, ios::binary);
      ostringstream contentStream;
      contentStream << ifs.rdbuf();
      ifs.close();
      string content = contentStream.str();

      // Count and perform replacements
      int replaced = 0;
      string editedContent;
      size_t pos = 0;
      size_t searchLen = search.size();

      while (pos < content.size()) {
        size_t found = content.find(search, pos);
        if (found == string::npos) {
          editedContent += content.substr(pos);
          break;
        }
        editedContent += content.substr(pos, found - pos);
        editedContent += replacement;
        replaced++;
        pos = found + searchLen;
      }

      if (replaced == 0) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: Search text not found in remote file: " + remotePath, true);
      }

      // Save edited content locally
      {
        ofstream ofs(localPath, ios::binary | ios::trunc);
        ofs.write(editedContent.c_str(), editedContent.size());
        ofs.close();
      }

      // Re-upload to FTP
      {
        istringstream iss(editedContent);
        ftp::istream_adapter adapter(iss);
        ftpClient.upload_file(adapter, remotePath);
      }

      ostringstream oss;
      oss << "✅ File edited and re-uploaded: " << remotePath << "\n";
      oss << "Replacements made: " << replaced << "\n";
      oss << "New size: " << editedContent.size() << " bytes\n";
      oss << "Local cache: " << localPath;

      result = makeTextResult(oss.str());
    }

    // ==================== DELETE ====================
    else if (command == "delete") {
      string remotePath = args.value("remote_path", "");
      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for delete command", true);
      }

      ftpClient.remove_file(remotePath);

      // Also remove from local cache if exists
      string localPath = toLocalMirrorPath(remotePath);
      if (fs::exists(localPath)) {
        fs::remove(localPath);
      }

      ostringstream oss;
      oss << "✅ File deleted from FTP: " << remotePath;

      result = makeTextResult(oss.str());
    }

    // ==================== DELETE_DIR ====================
    else if (command == "delete_dir") {
      string remotePath = args.value("remote_path", "");
      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for delete_dir command", true);
      }

      ftpClient.remove_directory(remotePath);

      // Also remove from local cache if exists
      string localPath = toLocalMirrorPath(remotePath);
      if (fs::exists(localPath) && fs::is_directory(localPath)) {
        fs::remove_all(localPath);
      }

      ostringstream oss;
      oss << "✅ Directory deleted from FTP: " << remotePath;

      result = makeTextResult(oss.str());
    }

    // ==================== RENAME ====================
    else if (command == "rename") {
      string remotePath = args.value("remote_path", "");
      string newPath = args.value("new_path", "");

      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for rename command", true);
      }
      if (newPath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'new_path' is required for rename command", true);
      }

      ftpClient.rename(remotePath, newPath);

      // Update local cache
      string localOld = toLocalMirrorPath(remotePath);
      string localNew = toLocalMirrorPath(newPath);
      if (fs::exists(localOld)) {
        ensureParentDir(localNew);
        try {
          fs::rename(localOld, localNew);
        } catch (...) {
          // Non-fatal if local rename fails
        }
      }

      ostringstream oss;
      oss << "✅ File renamed on FTP\n";
      oss << "From: " << remotePath << "\n";
      oss << "To:   " << newPath;

      result = makeTextResult(oss.str());
    }

    // ==================== MKDIR ====================
    else if (command == "mkdir") {
      string remotePath = args.value("remote_path", "");
      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for mkdir command", true);
      }

      ftpClient.create_directory(remotePath);

      // Also create in local cache
      string localPath = toLocalMirrorPath(remotePath);
      fs::create_directories(localPath);

      ostringstream oss;
      oss << "✅ Directory created on FTP: " << remotePath << "\n";
      oss << "Local cache: " << localPath;

      result = makeTextResult(oss.str());
    }

    // ==================== INFO ====================
    else if (command == "info") {
      string remotePath = args.value("remote_path", "");
      if (remotePath.empty()) {
        ftpClient.disconnect();
        return makeTextResult(
            "Error: 'remote_path' is required for info command", true);
      }

      ostringstream oss;
      oss << "📋 FTP File Info: " << remotePath << "\n";
      oss << "Host: " << creds.hostname << "\n";
      oss << string(40, '-') << "\n";

      // Get file size
      try {
        auto sizeReply = ftpClient.get_file_size(remotePath);
        const auto &sizeOpt = sizeReply.get_size();
        if (sizeOpt.has_value()) {
          oss << "Size: " << sizeOpt.value() << " bytes\n";
        } else {
          oss << "Size: (unavailable)\n";
        }
      } catch (const exception &e) {
        oss << "Size: (unavailable - " << e.what() << ")\n";
      }

      // Get modification time
      try {
        auto timeReply = ftpClient.get_file_modified_time(remotePath);
        const auto &dtOpt = timeReply.get_datetime();
        if (dtOpt.has_value()) {
          const auto &dt = dtOpt.value();
          char timebuf[64];
          snprintf(timebuf, sizeof(timebuf), "%04d-%02d-%02d %02d:%02d:%02d",
                   (int)dt.year, (int)dt.month, (int)dt.day, (int)dt.hour,
                   (int)dt.minute, (int)dt.second);
          oss << "Modified: " << timebuf << "\n";
        } else {
          oss << "Modified: (unavailable)\n";
        }
      } catch (const exception &e) {
        oss << "Modified: (unavailable - " << e.what() << ")\n";
      }

      // Check local cache status
      string localPath = toLocalMirrorPath(remotePath);
      if (fs::exists(localPath)) {
        oss << "Local cache: " << localPath << " (cached)\n";
      } else {
        oss << "Local cache: not cached yet\n";
      }

      result = makeTextResult(oss.str());
    }

    // ==================== UNKNOWN COMMAND ====================
    else {
      ftpClient.disconnect();
      return makeTextResult(
          "Error: Unknown command '" + command +
              "'. Available commands: list, read, upload, edit, "
              "delete, delete_dir, rename, mkdir, info",
          true);
    }

    // Disconnect gracefully
    ftpClient.disconnect();
    return result;

  } catch (const ftp::ftp_exception &e) {
    return makeTextResult("FTP Error (" + command + "): " + string(e.what()),
                          true);
  } catch (const exception &e) {
    return makeTextResult(
        "Error in ftp_handler (" + command + "): " + string(e.what()), true);
  }
}

} // namespace MCP
