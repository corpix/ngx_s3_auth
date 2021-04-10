{ pkgs ? import ./nixpkgs.nix {} }:
with pkgs;
{ src = nix-gitignore.gitignoreSourcePure [./../.gitignore] ./../.; }
