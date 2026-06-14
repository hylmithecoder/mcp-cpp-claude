{
  description = "mcp - C++ Project Flake";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = forAllSystems (system: import nixpkgs { inherit system; });
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = pkgsFor.${system};

          mcp-source = pkgs.stdenv.mkDerivation {
            pname = "mcp-source";
            version = "2026.0.2"; # Matches CMakeLists.txt
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = with pkgs; [
              nlohmann_json
              sqlite
              libxml2
              boost
            ];
          };

          mcp-bin = if system == "x86_64-linux" then
            pkgs.stdenv.mkDerivation rec {
              pname = "mcp-bin";
              version = "release-0.3-stable";

              src = pkgs.fetchurl {
                url = "https://github.com/hylmithecoder/mcp-cpp-claude/releases/download/${version}/mcp-linux-x86_64";
                sha256 = "1riihvr35rvh2brrm29yjphqrxw9vj1lr4jjmwmaskxaxlqxhfv7";
              };

              dontUnpack = true;

              nativeBuildInputs = with pkgs; [ autoPatchelfHook ];

              buildInputs = with pkgs; [
                stdenv.cc.cc.lib
                sqlite
              ];

              installPhase = ''
                mkdir -p $out/bin
                cp $src $out/bin/mcp
                chmod +x $out/bin/mcp
              '';
            }
          else
            mcp-source; # Fallback to source build for other platforms
        in
        {
          inherit mcp-source mcp-bin;
          
          # Use binary download by default on linux for fast installation
          default = mcp-bin;
        }
      );

      devShells = forAllSystems (system:
        let
          pkgs = pkgsFor.${system};
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              cmake
              ninja       # for faster builds
              pkg-config
              clang-tools # for clangd/formatting
              gdb         # for debugging
            ];

            buildInputs = with pkgs; [
              nlohmann_json
              sqlite
              libxml2
              boost
            ];
          };
        }
      );
    };
}
