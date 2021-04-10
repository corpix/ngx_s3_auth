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
    cp ./objs/*.{h,c} $out/include/objs
  '';
})
