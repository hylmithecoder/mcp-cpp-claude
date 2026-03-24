#include "../include/server.hpp"
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

using namespace std;

using namespace MCP;

// Build HTTP response string
string HttpResponse::build() const {
  ostringstream oss;
  oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
  for (auto &[key, val] : headers) {
    oss << key << ": " << val << "\r\n";
  }
  if (!keepAlive && !body.empty() &&
      headers.find("Content-Length") == headers.end()) {
    oss << "Content-Length: " << body.size() << "\r\n";
  }
  oss << "\r\n";
  oss << body;
  return oss.str();
}

Server::Server(int port) : port_(port) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    cerr << "WSAStartup failed" << endl;
    exit(EXIT_FAILURE);
  }
#endif
}

Server::~Server() {
  stop();
#ifdef _WIN32
  WSACleanup();
#endif
}

void Server::route(const string &method, const string &path,
                   RouteHandler handler) {
  routes_[method][path] = handler;
}

void Server::start() {
  struct sockaddr_in address;
  int opt = 1;

  // Create socket
  if ((server_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
#ifdef _WIN32
    cerr << "socket failed: " << WSAGetLastError() << endl;
#else
    perror("socket failed");
#endif
    exit(EXIT_FAILURE);
  }

  // Allow port reuse
#ifdef _WIN32
  // On Windows SO_REUSEADDR doesn't behave like Linux SO_REUSEPORT
  // We use it but it's slightly different
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
                 sizeof(opt)) == SOCKET_ERROR) {
    cerr << "setsockopt failed: " << WSAGetLastError() << endl;
#else
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) == -1) {
    perror("setsockopt failed");
#endif
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (::bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) ==
      SOCKET_ERROR) {
#ifdef _WIN32
    cerr << "bind failed: " << WSAGetLastError() << endl;
#else
    perror("bind failed");
#endif
    exit(EXIT_FAILURE);
  }

  if (::listen(server_fd_, 10) == SOCKET_ERROR) {
#ifdef _WIN32
    cerr << "listen failed: " << WSAGetLastError() << endl;
#else
    perror("listen failed");
#endif
    exit(EXIT_FAILURE);
  }

  running_ = true;
  cout << "🚀 MCP Server listening on http://0.0.0.0:" << port_ << "/mcp"
       << endl;
  cout << "   Press Ctrl+C to stop." << endl;

  while (running_) {
    struct sockaddr_in client_addr;
#ifdef _WIN32
    int client_len = sizeof(client_addr);
#else
    socklen_t client_len = sizeof(client_addr);
#endif
    socket_t client_fd =
        ::accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd == INVALID_SOCKET) {
      if (running_) {
#ifdef _WIN32
        cerr << "accept failed: " << WSAGetLastError() << endl;
#else
        perror("accept failed");
#endif
      }
      continue;
    }

    // Spawn thread for each client
    thread(&Server::handleClient, this, client_fd).detach();
  }
}

void Server::stop() {
  running_ = false;
  if (server_fd_ != INVALID_SOCKET) {
    CLOSE_SOCKET(server_fd_);
    server_fd_ = INVALID_SOCKET;
  }
}

HttpRequest Server::parseRequest(const string &raw) {
  HttpRequest req;
  istringstream stream(raw);
  string line;

  // Parse request line: METHOD /path HTTP/1.1
  if (getline(stream, line)) {
    // Remove trailing \r
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    istringstream reqLine(line);
    reqLine >> req.method >> req.path;
  }

  // Parse headers
  while (getline(stream, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      break; // Empty line = end of headers

    auto colon = line.find(':');
    if (colon != string::npos) {
      string key = line.substr(0, colon);
      string val = line.substr(colon + 1);

      // Lowercase key for easy lookup
      for (auto &c : key)
        c = tolower(c);

      // Trim leading spaces from value
      size_t start = val.find_first_not_of(' ');
      if (start != string::npos)
        val = val.substr(start);
      req.headers[key] = val;
    }
  }

  // Parse body (rest of the data)
  string bodyPart;
  while (getline(stream, bodyPart)) {
    req.body += bodyPart;
    if (!stream.eof())
      req.body += "\n";
  }

  // Remove trailing newline if present
  if (!req.body.empty() && req.body.back() == '\n') {
    req.body.pop_back();
  }

  return req;
}

void Server::handleClient(socket_t client_fd) {
  char buffer[BUFFER_SIZE] = {0};
  string rawData;

  // Read data — may need multiple reads for large bodies
  ssize_t total = 0;
  ssize_t bytes_read;

  // First read
  bytes_read = ::recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
  if (bytes_read == SOCKET_ERROR || bytes_read == 0) {
    CLOSE_SOCKET(client_fd);
    return;
  }
  buffer[bytes_read] = '\0';
  rawData = string(buffer, bytes_read);

  // Check if we need to read more (Content-Length)
  auto headerEnd = rawData.find("\r\n\r\n");
  if (headerEnd != string::npos) {
    // Parse Content-Length from headers
    string headerSection = rawData.substr(0, headerEnd);
    size_t clPos = headerSection.find("Content-Length:");
    if (clPos == string::npos)
      clPos = headerSection.find("content-length:");

    if (clPos != string::npos) {
      size_t valStart = headerSection.find(':', clPos) + 1;
      size_t valEnd = headerSection.find("\r\n", valStart);
      if (valEnd == string::npos)
        valEnd = headerSection.size();
      int contentLength =
          stoi(headerSection.substr(valStart, valEnd - valStart));

      size_t bodyStart = headerEnd + 4;
      size_t bodyReceived = rawData.size() - bodyStart;

      // Read remaining body if needed
      while ((int)bodyReceived < contentLength) {
        std::memset(buffer, 0, BUFFER_SIZE);
        bytes_read = ::recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read == SOCKET_ERROR || bytes_read == 0)
          break;
        rawData.append(buffer, bytes_read);
        bodyReceived += bytes_read;
      }
    }
  }

  HttpRequest req = parseRequest(rawData);

  // Find route early to check for OAuth endpoints
  string routePath = req.path;
  size_t qmark = routePath.find('?');
  if (qmark != string::npos) {
    routePath = routePath.substr(0, qmark);
  }

  // Check Authentication if apiToken_ is set (but skip for OAuth flow)
  bool isOAuthPath = (routePath == "/authorize" || routePath == "/token");
  if (!apiToken_.empty() && !isOAuthPath) {
    string authHeader = req.headers.count("authorization")
                            ? req.headers.at("authorization")
                            : "";
    string xClientId =
        req.headers.count("x-client-id") ? req.headers.at("x-client-id") : "";
    string xClientSecret = req.headers.count("x-client-secret")
                               ? req.headers.at("x-client-secret")
                               : "";
    string xApiKey =
        req.headers.count("x-api-key") ? req.headers.at("x-api-key") : "";

    bool authenticated = false;

    // 1. Extract Bearer token if present
    string bearerToken;
    if (authHeader.size() > 7 && (authHeader.substr(0, 7) == "Bearer " ||
                                  authHeader.substr(0, 7) == "bearer ")) {
      bearerToken = authHeader.substr(7);
      bearerToken.erase(0, bearerToken.find_first_not_of(" \t\r\n"));
      size_t last = bearerToken.find_last_not_of(" \t\r\n");
      if (last != string::npos)
        bearerToken.erase(last + 1);
      else
        bearerToken.clear();
    }

    // 2. Determine provided ID and Secret
    string providedId = xClientId;
    string providedSecret =
        !xClientSecret.empty() ? xClientSecret
                               : (!bearerToken.empty() ? bearerToken : xApiKey);

    // 3. Simple trimming and comparison
    auto trim = [](string s) {
      s.erase(0, s.find_first_not_of(" \t\r\n"));
      size_t last = s.find_last_not_of(" \t\r\n");
      if (last != string::npos)
        s.erase(last + 1);
      else
        s.clear();
      return s;
    };

    providedId = trim(providedId);
    providedSecret = trim(providedSecret);

    // Match logic:
    // - Secret must match apiToken_
    if (!apiToken_.empty() && providedSecret == apiToken_) {
      authenticated = true;
    }

    if (!authenticated) {
      HttpResponse res;
      res.statusCode = 401;
      res.statusText = "Unauthorized";
      res.body = "{\"error\":\"Unauthorized: Invalid or missing Client ID / "
                 "Client Secret\"}";
      res.headers["Content-Type"] = "application/json";
      addCorsHeaders(res);
      string response = res.build();
      ::send(client_fd, response.c_str(), response.size(), 0);
      CLOSE_SOCKET(client_fd);
      return;
    }
  }

  // Handle CORS preflight
  if (req.method == "OPTIONS") {
    HttpResponse res = handleCors(req);
    string response = res.build();
    send(client_fd, response.c_str(), response.size(), 0);
    CLOSE_SOCKET(client_fd);
    return;
  }

  HttpResponse res;
  auto methodIt = routes_.find(req.method);
  if (methodIt != routes_.end()) {
    auto pathIt = methodIt->second.find(routePath);
    if (pathIt != methodIt->second.end()) {
      res = pathIt->second(req, client_fd);
    } else {
      res.statusCode = 404;
      res.statusText = "Not Found";
      res.body = "{\"error\":\"Not Found\"}";
      res.headers["Content-Type"] = "application/json";
    }
  } else {
    res.statusCode = 405;
    res.statusText = "Method Not Allowed";
    res.body = "{\"error\":\"Method Not Allowed\"}";
    res.headers["Content-Type"] = "application/json";
  }

  addCorsHeaders(res);
  string response = res.build();
  send(client_fd, response.c_str(), response.size(), 0);

  if (!res.keepAlive) {
    CLOSE_SOCKET(client_fd);
  }
}

HttpResponse Server::handleCors(const HttpRequest &req) {
  HttpResponse res;
  res.statusCode = 204;
  res.statusText = "No Content";
  addCorsHeaders(res);
  res.headers["Access-Control-Max-Age"] = "86400";
  return res;
}

void Server::addCorsHeaders(HttpResponse &res) {
  res.headers["Access-Control-Allow-Origin"] = "*";
  res.headers["Access-Control-Allow-Methods"] = "GET, POST, DELETE, OPTIONS";
  res.headers["Access-Control-Allow-Headers"] =
      "Content-Type, Accept, Authorization, Mcp-Session-Id, Last-Event-ID, "
      "X-Client-ID, X-Client-Secret, X-API-Key";
  res.headers["Access-Control-Expose-Headers"] = "Mcp-Session-Id";
}
