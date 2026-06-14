let
  nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/archive/nixos-unstable.tar.gz";
  pkgs = import nixpkgs {
    config = {
      allowBroken = true;
      allowUnsupportedSystem = true;
    };
  };
  mingwPkgs = pkgs.pkgsCross.mingwW64;
in
mingwPkgs.mkShell {
  name = "mcp-cross-shell";
  
  nativeBuildInputs = [
    mingwPkgs.buildPackages.cmake
    mingwPkgs.buildPackages.ninja
    pkgs.pkg-config
    pkgs.msitools # Includes wixl compiler for .msi files
    pkgs.p7zip
    # Host Qt6 for host tools (moc, uic)
    pkgs.qt6.qtbase
    # Python environment for running aqtinstall
    (pkgs.python3.withPackages (ps: with ps; [
      pip
      virtualenv
      py7zr
    ]))
  ];

  buildInputs = [
    mingwPkgs.libxml2.bin
    mingwPkgs.libxml2.dev
    mingwPkgs.boost
    mingwPkgs.openssl.bin
    mingwPkgs.openssl.dev
    mingwPkgs.libiconv
    mingwPkgs.zlib
    mingwPkgs.windows.pthreads
  ];

  shellHook = ''
    export HOST_QT_PATH="${pkgs.qt6.qtbase}"

    echo "=========================================================="
    echo "            MCP Windows Cross-Compilation Shell           "
    echo "=========================================================="
    echo "Available commands:"
    echo "  1. download-qt6  : Download Windows Qt6 MinGW to vendor/qt6"
    echo "=========================================================="

    download-qt6() {
      echo "Setting up Python virtual environment..."
      if [ ! -d .venv ]; then
        python3 -m venv .venv
      fi
      source .venv/bin/activate
      pip install -U aqtinstall py7zr

      echo "Downloading Qt 6.10.2 for Windows Desktop MinGW..."
      mkdir -p vendor/qt6
      aqt install-qt -O vendor/qt6 windows desktop 6.10.2 win64_mingw
      
      echo "Qt6 Windows MinGW binaries downloaded successfully to vendor/qt6!"
      deactivate
    }
  '';
}
