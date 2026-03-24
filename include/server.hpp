#pragma once

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    // Block POSIX headers from redefining timeval/fd_set
    #define _TIMEVAL_DEFINED
    #define _STRUCT_TIMEVAL
    #define _SYS_SELECT_H
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
    // ssize_t is defined by Mingw, but not by MSVC.
    #if defined(_MSC_VER) && !defined(ssize_t)
    #include <BaseTsd.h>
    typedef SSIZE_T ssize_t;
    #endif
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    typedef int socket_t;
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET (socket_t)-1
    #define SOCKET_ERROR -1
#endif

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstring>

namespace MCP {

    const int PORT = 5500;
    const int BUFFER_SIZE = 8192;

    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    struct HttpResponse {
        int statusCode = 200;
        std::string statusText = "OK";
        std::map<std::string, std::string> headers;
        std::string body;
        bool keepAlive = false;

        std::string build() const;
    };

    // Callback type for route handlers
    using RouteHandler = std::function<HttpResponse(const HttpRequest&, socket_t)>;

    class Server {
    public:
        Server(int port);
        ~Server();

        void start();
        void stop();
        void route(const std::string& method, const std::string& path, RouteHandler handler);
        void setToken(const std::string& token) { apiToken_ = token; }
        void setCredentials(const std::string& id, const std::string& secret) { 
            apiClientId_ = id; 
            apiToken_ = secret; 
        }

    private:
        int port_;
        socket_t server_fd_ = INVALID_SOCKET;
        bool running_ = false;
        std::mutex mutex_;
        std::string apiClientId_;
        std::string apiToken_;
        std::map<std::string, std::map<std::string, RouteHandler>> routes_;

        void handleClient(socket_t client_fd);
        HttpRequest parseRequest(const std::string& raw);
        void sendResponse(socket_t client_fd, const HttpResponse& res);
        HttpResponse handleCors(const HttpRequest& req);
        void addCorsHeaders(HttpResponse& res);
    };

} // namespace MCP