#!/bin/bash

# MCP Server for Claude - Installation Script
# https://github.com/hylmithecoder/mcp-cpp-claude

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

VERSION="release-0.1-stable"
REPO="hylmithecoder/mcp-cpp-claude"
BASE_URL="https://github.com/$REPO/releases/download/$VERSION"

echo -e "${BLUE}---------------------------------------${NC}"
echo -e "${BLUE}   🖥️  MCP C++ Server Installation      ${NC}"
echo -e "${BLUE}---------------------------------------${NC}"

# Detect OS and Architecture
OS="$(uname -s)"
ARCH="$(uname -m)"
IS_ANDROID=false

if [[ "$OS" == "Linux" ]] && [[ "$(uname -o 2>/dev/null)" == "Android" ]]; then
    IS_ANDROID=true
    echo -e "${YELLOW}📱 Android (Termux) detected.${NC}"
fi

BINARY_NAME="mcp"
DOWNLOAD_URL=""

if [[ "$OS" == "Darwin" ]]; then
    if [[ "$ARCH" == "arm64" ]]; then
        DOWNLOAD_URL="$BASE_URL/mcp-mac-arm64"
    else
        DOWNLOAD_URL="$BASE_URL/mcp-mac-x86_64"
    fi
elif [[ "$IS_ANDROID" == true ]]; then
    if [[ "$ARCH" == "aarch64" ]]; then
        DOWNLOAD_URL="$BASE_URL/mcp-android-arm64-v8a"
    elif [[ "$ARCH" == "armv7l" || "$ARCH" == "armv8l" ]]; then
        DOWNLOAD_URL="$BASE_URL/mcp-android-armeabi-v7a"
    fi
elif [[ "$OS" == "Linux" ]]; then
    DOWNLOAD_URL="$BASE_URL/mcp-linux-x86_64"
fi

install_binary() {
    if [[ -z "$DOWNLOAD_URL" ]]; then
        return 1
    fi

    echo -e "${YELLOW}🚀 Downloading pre-built binary for $OS ($ARCH)...${NC}"
    if curl -L --progress-bar "$DOWNLOAD_URL" -o "$BINARY_NAME"; then
        chmod +x "$BINARY_NAME"
        echo -e "${GREEN}✅ Download successful!${NC}"
        
        INSTALL_PATH="/usr/local/bin"
        if [[ "$IS_ANDROID" == true ]]; then
            INSTALL_PATH="$PREFIX/bin"
        fi

        read -p "Do you want to install '$BINARY_NAME' to $INSTALL_PATH? (y/N) " -n 1 -r REPLY </dev/tty
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            if [ -w "$INSTALL_PATH" ]; then
                cp "$BINARY_NAME" "$INSTALL_PATH/mcp"
            else
                echo -e "Requires sudo to copy to $INSTALL_PATH"
                sudo cp "$BINARY_NAME" "$INSTALL_PATH/mcp"
            fi
            echo -e "${GREEN}✅ Installed! You can now run 'mcp' from anywhere.${NC}"
        else
            echo -e "Binary is available at: ${BLUE}$(pwd)/$BINARY_NAME${NC}"
        fi
        return 0
    else
        echo -e "${RED}❌ Download failed.${NC}"
        return 1
    fi
}

build_from_source() {
    echo -e "${YELLOW}🛠️ Building from source...${NC}"
    
    # Check for dependencies
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}❌ cmake not found. Please install it (e.g., sudo apt install cmake).${NC}"
        exit 1
    fi

    mkdir -p build && cd build
    echo -e "${YELLOW}⚙️ Configuring with CMake...${NC}"
    
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"
    if [[ "$IS_ANDROID" == true ]]; then
        # For Termux native build, we don't need a cross-toolchain
        CMAKE_FLAGS="$CMAKE_FLAGS"
    elif [[ "$OS" == "Darwin" ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_OSX_ARCHITECTURES=$ARCH"
    fi

    if ! cmake $CMAKE_FLAGS ..; then
        echo -e "${RED}❌ CMake configuration failed.${NC}"
        exit 1
    fi

    echo -e "${YELLOW}🔨 Compiling...${NC}"
    if ! cmake --build . --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2); then
        echo -e "${RED}❌ Compilation failed.${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✨ Build successful!${NC}"
    
    INSTALL_PATH="/usr/local/bin"
    if [[ "$IS_ANDROID" == true ]]; then
        INSTALL_PATH="$PREFIX/bin"
    fi

    read -p "Do you want to install '$BINARY_NAME' to $INSTALL_PATH? (y/N) " -n 1 -r REPLY </dev/tty
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        if [ -w "$INSTALL_PATH" ]; then
            cp "$BINARY_NAME" "$INSTALL_PATH/mcp"
        else
            sudo cp "$BINARY_NAME" "$INSTALL_PATH/mcp"
        fi
        echo -e "${GREEN}✅ Installed! You can now run 'mcp' from anywhere.${NC}"
    else
        echo -e "Binary is available at: ${BLUE}$(pwd)/$BINARY_NAME${NC}"
    fi
}

if [[ -n "$DOWNLOAD_URL" ]]; then
    if ! install_binary; then
        build_from_source
    fi
else
    build_from_source
fi

echo -e "${BLUE}---------------------------------------${NC}"
echo -e "${GREEN}🎉 MCP Server is ready!${NC}"
echo -e "Refer to README.md for configuration and connection steps."
