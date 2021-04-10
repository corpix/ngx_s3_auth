let nixpkgs = ./nix/nixpkgs.nix;
    config = {};
in with import nixpkgs { inherit config; };
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

  buildInputInclude = lib.concatMapStringsSep
    " -I" (p: "${p.dev}/include/")
    [openssl zlib pcre libxml2 libxslt gd];

  nginxInclude = lib.concatMapStringsSep
    " -I" (p: "${nginx}/include/${p}/")
    ["src/os/unix"
     "src/core"
     "src/http"
     "src/http/v2"
     "src/http/modules"
     "src/event"
     "objs"
    ];

  shellHook = ''
    export root=$(pwd)

    if [ -f "$root/.env" ]
    then
      source "$root/.env"
    fi

    export LANG="en_US.UTF-8"
    export NIX_PATH="nixpkgs=${nixpkgs}"
    export CFLAGS="-g -I${cmocka}/include/ -I${buildInputInclude} -I${nginxInclude} -I$root/"
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

    minio s3cmd
    nginx
  ];

  inherit shellHook;
}
