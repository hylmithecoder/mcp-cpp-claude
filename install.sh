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

BINARY_NAME="mcp"
DOWNLOAD_URL=""

if [[ "$OS" == "Darwin" ]]; then
    if [[ "$ARCH" == "arm64" ]]; then
        DOWNLOAD_URL="$BASE_URL/mcp-mac-arm64"
    else
        DOWNLOAD_URL="$BASE_URL/mcp-mac-x86_64"
    fi
elif [[ "$OS" == "Linux" ]]; then
    DOWNLOAD_URL="$BASE_URL/mcp"
fi

install_binary() {
    echo -e "${YELLOW}🚀 Downloading pre-built binary for $OS ($ARCH)...${NC}"
    if curl -L --progress-bar "$DOWNLOAD_URL" -o "$BINARY_NAME"; then
        chmod +x "$BINARY_NAME"
        echo -e "${GREEN}✅ Download successful!${NC}"
        
        read -p "Do you want to install '$BINARY_NAME' to /usr/local/bin? (y/N) " -n 1 -r REPLY </dev/tty
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            if [ -w /usr/local/bin ]; then
                cp "$BINARY_NAME" /usr/local/bin/mcp
            else
                echo -e "Requires sudo to copy to /usr/local/bin"
                sudo cp "$BINARY_NAME" /usr/local/bin/mcp
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
    echo -e "${YELLOW}🛠️ Falling back to building from source...${NC}"
    
    # Check if we are in the source directory
    if [[ ! -f "CMakeLists.txt" ]]; then
        echo -e "${YELLOW}📂 Cloning repository to temporary directory...${NC}"
        TEMP_DIR="$(mktemp -d)"
        git clone "https://github.com/$REPO.git" "$TEMP_DIR"
        cd "$TEMP_DIR"
    fi

    # Check for dependencies
    if ! command -v cmake &> /dev/null || ! command -v g++ &> /dev/null; then
        echo -e "${RED}❌ cmake or g++ not found. Please install them and try again.${NC}"
        exit 1
    fi

    mkdir -p build && cd build
    if [[ "$OS" == "Darwin" ]]; then
        cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" ..
    else
        cmake -DCMAKE_BUILD_TYPE=Release ..
    fi
    cmake --build . --config Release --parallel $(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)
    
    echo -e "${GREEN}✨ Build successful!${NC}"
    
    # Prompt for installation
    read -p "Do you want to install '$BINARY_NAME' to /usr/local/bin? (y/N) " -n 1 -r REPLY </dev/tty
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        if [ -w /usr/local/bin ]; then
            cp "$BINARY_NAME" /usr/local/bin/mcp
        else
            echo -e "Requires sudo to copy to /usr/local/bin"
            sudo cp "$BINARY_NAME" /usr/local/bin/mcp
        fi
        echo -e "${GREEN}✅ Installed! You can now run 'mcp' from anywhere.${NC}"
    else
        echo -e "Binary is available at: ${BLUE}$(pwd)/$BINARY_NAME${NC}"
    fi

    # Cleanup if we cloned
    if [[ -n "$TEMP_DIR" ]]; then
        echo -e "${YELLOW}🧹 Cleaning up temporary files...${NC}"
        # We don't remove if they chose not to install, because they need the binary
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$TEMP_DIR"
        fi
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
