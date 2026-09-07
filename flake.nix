{
  description = "Moss Extensions";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          clang
          raylib
          vulkan-loader
          vulkan-headers
          vulkan-validation-layers
          vk-bootstrap
          glfw3
        ];
      };
    };
}
