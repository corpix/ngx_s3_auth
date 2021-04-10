let nixpkgs = ./nix/nixpkgs.nix;
    config = {};
in with import nixpkgs { inherit config; };
with builtins;
with lib;
let
  shellWrapper = writeScript "shell-wrapper" ''
    #! ${stdenv.shell}
    set -e

    exec -a shell ${fish}/bin/fish --login --interactive --init-command='
      set -x root '"$root"'
      set config $root/.fish.conf
      set personal_config $root/.personal.fish.conf
      if test -e $personal_config
        source $personal_config
      end
      if test -e $config
        source $config
      end
    ' "$@"
  '';

  nginx = pkgs.callPackage ./nix/nginx.nix {};

  shellHook = ''
    export root=$(pwd)

    if [ -f "$root/.env" ]
    then
      source "$root/.env"
    fi

    export LANG="en_US.UTF-8"
    export NIX_PATH="nixpkgs=${nixpkgs}"
    export CFLAGS="-I${cmocka}/include/ -I${lib.concatMapStringsSep " -I" (p: "${p.dev}/include/") [openssl zlib pcre libxml2 libxslt gd]}"
    export NGX_PATH="${nginx}/include"
    export MAKEFLAGS="--no-print-directory"

    if [ ! -z "$PS1" ]
    then
      export SHELL="${shellWrapper}"
      exec "$SHELL"
    fi
  '';
in stdenv.mkDerivation rec {
  name = "nix-shell";

  buildInputs = [
    glibcLocales bashInteractive man
    nix cacert curl utillinux coreutils
    git jq tmux findutils gnumake
    exa ripgrep
    gcc valgrind cmocka

    nginx
  ];

  inherit shellHook;
}
