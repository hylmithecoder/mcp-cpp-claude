#include "../include/utils.hpp"

using namespace MCP;

static void banner(){
    string banner = R"(
    ███╗   ███╗ ██████╗██████╗      ██████╗    ██████╗     ██╗   ██╗███████╗██████╗ 
    ████╗ ████║██╔════╝██╔══██╗    ██╔═████╗   ╚════██╗    ██║   ██║██╔════╝██╔══██╗
    ██╔████╔██║██║     ██████╔╝    ██║██╔██║    █████╔╝    ██║   ██║█████╗  ██████╔╝
    ██║╚██╔╝██║██║     ██╔═══╝     ████╔╝██║    ╚═══██╗    ╚██╗ ██╔╝██╔══╝  ██╔══██╗
    ██║ ╚═╝ ██║╚██████╗██║         ╚██████╔╝██╗██████╔╝     ╚████╔╝ ███████╗██║  ██║
    ╚═╝     ╚═╝ ╚═════╝╚═╝          ╚═════╝ ╚═╝╚═════╝       ╚═══╝  ╚══════╝╚═╝  ╚═╝                                                                                
    )";
    cout << banner << endl;
}

void Utils::printVersion() {
    banner();
    cout << "Support me https://github.com/hylmithecoder/mcp-cpp-claude" << endl;
    cout << "Author: Hylmi" << endl;
    cout << "License: MIT" << endl;
}

void Utils::printHelp() {
    cout << "Usage: mcp-server [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -h, --help    Show this help message" << endl;
    cout << "  -v, --version Show version information" << endl;
    cout << "  -p, --port    Set the port number" << endl;
    cout << "  -t, --token   Set the API token" << endl;
    cout << "  -c, --client  Set the client ID and secret" << endl;
}