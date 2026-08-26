let
  nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/archive/nixos-unstable.tar.gz";
  pkgs = import nixpkgs {};
in
pkgs.pkgsStatic.mkShell {
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.pkg-config
    pkgs.git
    pkgs.cacert
    pkgs.binutils
  ];
  buildInputs = with pkgs.pkgsStatic; [
    nlohmann_json
    sqlite
    libxml2
    boost
    openssl
  ];
}
