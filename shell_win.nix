let
  nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/archive/nixos-unstable.tar.gz";
  pkgs = import nixpkgs {};
in
pkgs.mkShell {
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.pkgsCross.mingwW64.stdenv.cc
  ];
  buildInputs = [
    pkgs.pkgsCross.mingwW64.libxml2
    pkgs.pkgsCross.mingwW64.boost
    pkgs.pkgsCross.mingwW64.openssl
    pkgs.pkgsCross.mingwW64.libiconv
    pkgs.pkgsCross.mingwW64.zlib
  ];
}
