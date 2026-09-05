{
  description = "Moss Extensions";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    vulkan-helper = {
      url = "github:brightprogrammer/VulkanHelper";
      flake = false;
    };
  };

  outputs = { nixpkgs, vulkan-helper, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      # Re-root vulkan-helper's include dir under a "Vkh" namespace
      # so consumers write #include "Vkh/Vulkan.hpp"
      vulkanHelperNamespaced = pkgs.runCommand "vulkan-helper-namespaced" {} ''
        mkdir -p $out/Vkh
        cp -r ${vulkan-helper}/include/. $out/Vkh/
        chmod -R u+w $out/Vkh
        find $out/Vkh -type f \( -name '*.hpp' -o -name '*.h' -o -name '*.cpp' \) \
        -exec sed -i 's|#include <Vulkan\.hpp>|#include "Vulkan.hpp"|g' {} +
      ''; ## Namespacing VKH and changing angled brackets to qoutes.
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          clang
          raylib
          vulkan-loader
          vulkan-headers
          vulkan-validation-layers
        ];

        VULKAN_HELPER_DIR = "${vulkanHelperNamespaced}";
      };
    };
}
