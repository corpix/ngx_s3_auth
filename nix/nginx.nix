{ pkgs ? import ./nixpkgs.nix {} }:
with pkgs;
(nginx.override (_: {
  modules = [
    (import ./nginx-s3-auth.nix { inherit pkgs; })
  ];
})).overrideAttrs (_: {
  postInstall = ''
    mv $out/sbin $out/bin
    mkdir -p $out/include/{src,objs}
    cp -r ./src $out/include
    cp -r ./objs/* $out/include/objs
    strip -N main -o $out/include/objs/src/core/ngx_nginx.o $out/include/objs/src/core/nginx.o
  '';
})
