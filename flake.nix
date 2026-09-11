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
          vulkan-utility-libraries
          vulkan-memory-allocator
          vk-bootstrap
          glfw3
          shaderc
        ];
      };

      LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [
        pkgs.vulkan-loader
        pkgs.glfw
      ];

      VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
    };
}
