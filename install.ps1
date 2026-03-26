# MCP Server for Claude - Windows Installation Script
# https://github.com/hylmithecoder/mcp-cpp-claude

$ErrorActionPreference = "Stop"

$VERSION = "release-0.2-stable"
$REPO = "hylmithecoder/mcp-cpp-claude"
$BASE_URL = "https://github.com/$REPO/releases/download/$VERSION"

Write-Host "---------------------------------------" -ForegroundColor Blue
Write-Host "   🖥️  MCP C++ Server Installation      " -ForegroundColor Blue
Write-Host "---------------------------------------" -ForegroundColor Blue

$ARCH = $env:PROCESSOR_ARCHITECTURE
if ($ARCH -eq "AMD64") { $ARCH = "x86_64" }

$BINARY_NAME = "mcp.exe"
$DOWNLOAD_URL = "$BASE_URL/mcp-win-x86_64.exe"

function Install-Binary {
    Write-Host "🚀 Downloading pre-built binary for Windows ($ARCH)..." -ForegroundColor Yellow
    
    try {
        Invoke-WebRequest -Uri $DOWNLOAD_URL -OutFile $BINARY_NAME
        Write-Host "✅ Download successful!" -ForegroundColor Green
        
        $choice = Read-Host "Do you want to add the current directory to your User PATH? (y/N)"
        if ($choice -eq "y") {
            $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
            $absPath = Get-Item . | Select-Object -ExpandProperty FullName
            if ($currentPath -notlike "*$absPath*") {
                [Environment]::SetEnvironmentVariable("Path", "$currentPath;$absPath", "User")
                Write-Host "✅ Added to PATH! You may need to restart your terminal." -ForegroundColor Green
            } else {
                Write-Host "ℹ️ Directory already in PATH." -ForegroundColor Cyan
            }
        }
        
        Write-Host "Binary is available at: $(Get-Location)\$BINARY_NAME" -ForegroundColor Blue
        return $true
    } catch {
        Write-Host "❌ Download failed or binary not available for this version yet." -ForegroundColor Red
        return $false
    }
}

function Build-From-Source {
    Write-Host "🛠️ Building from source..." -ForegroundColor Yellow
    
    if (!(Get-Command cmake -ErrorAction SilentlyContinue)) {
        Write-Host "❌ cmake not found. Please install CMake and a C++ compiler (like Visual Studio or MinGW)." -ForegroundColor Red
        exit 1
    }

    if (!(Test-Path build)) { New-Item -ItemType Directory -Path build }
    Set-Location build
    
    Write-Host "⚙️ Configuring with CMake..." -ForegroundColor Yellow
    cmake -DCMAKE_BUILD_TYPE=Release ..
    
    Write-Host "🔨 Compiling..." -ForegroundColor Yellow
    cmake --build . --config Release --parallel $env:NUMBER_OF_PROCESSORS
    
    Write-Host "✨ Build successful!" -ForegroundColor Green
    Write-Host "Binary is available at: $(Get-Location)\Release\$BINARY_NAME" -ForegroundColor Blue
    
    Set-Location ..
}

if (!(Install-Binary)) {
    Build-From-Source
}

Write-Host "---------------------------------------" -ForegroundColor Blue
Write-Host "🎉 MCP Server is ready!" -ForegroundColor Green
Write-Host "Refer to README.md for configuration and connection steps." -ForegroundColor White
