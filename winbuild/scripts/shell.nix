# Linux build-time tools only. None of these are required by Windows users.
let
  locked = (builtins.fromJSON (builtins.readFile ../../flake.lock)).nodes.nixpkgs.locked;
  pkgs = import (builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/${locked.rev}.tar.gz";
    sha256 = locked.narHash;
  }) { system = "x86_64-linux"; };
  cross = pkgs.pkgsCross.mingwW64;
  qtHost = pkgs.buildEnv {
    name = "deskport-windows-qt-host";
    paths = with pkgs.qt6; [
      qtbase qtbase.dev qtdeclarative qtdeclarative.dev
      qtshadertools qtshadertools.dev qtsvg qtsvg.dev qttools qttools.dev
    ];
    ignoreCollisions = true;
  };
in pkgs.mkShell {
  packages = [
    cross.buildPackages.gcc cross.buildPackages.binutils
    pkgs.cmake pkgs.ninja pkgs.pkg-config pkgs.python3 pkgs.perl pkgs.git
    pkgs.nodejs pkgs.meson pkgs.nasm pkgs.yasm pkgs.wget pkgs.curl
    pkgs.p7zip pkgs.unzip pkgs.zip pkgs.file pkgs.which pkgs.gnumake
    pkgs.gnupatch pkgs.autoconf pkgs.automake pkgs.libtool pkgs.util-linux
    pkgs.coreutils pkgs.nsis pkgs.msitools pkgs.osslsigncode pkgs.xxd
    pkgs.imagemagick
  ];
  shellHook = ''
    export QT_HOST_PATH=${qtHost}
    export NIX_HOST_QT=${qtHost}
  '';
}
