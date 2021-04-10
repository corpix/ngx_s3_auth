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

  anyOutput = attrs: o: attrByPath
    [ (head attrs) ]
    (if (length attrs) > 1
     then anyOutput (tail attrs) o
     else o)
    o;

  # some derivations don't have dev output
  # will mitigate this inconsistency with anyOutput helper
  buildInputPkgConfig = lib.concatMapStringsSep
    ":" (p: "${anyOutput ["dev"] p}/lib/pkgconfig/")
    [openssl zlib pcre libxml2 libxslt gd geoip];
  buildInputLib = lib.concatMapStringsSep
    " -L" (p: "${p.out}/lib/")
    [openssl zlib pcre libxml2 libxslt gd geoip];
  buildInputInclude = lib.concatMapStringsSep
    " -I" (p: "${anyOutput ["dev"] p}/include/")
    [openssl zlib pcre libxml2 libxslt gd geoip];

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

    ngx_objs="$(find ${nginx}/include/objs/ -name 'ngx_*.o' | xargs)"
    export PKG_CONFIG_PATH="${buildInputPkgConfig}"
    export CFLAGS="-g -I${cmocka}/include/ -I${buildInputInclude} -I${nginxInclude} -I$root/"
    export LDFLAGS="-L${buildInputLib} -lcmocka -ldl -lpthread -lcrypt -lssl -lpcre -lcrypto -lGeoIP -lz -lxml2 -lxslt -lexslt -lgd $ngx_objs"

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
    gcc pkgconfig valgrind cmocka clang-analyzer

    minio s3cmd
    nginx
  ];

  inherit shellHook;
}
