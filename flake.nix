{
  description = "Open source implementation of AliceSoft's System 4 game engine for unix-like operating systems";

  # Nixpkgs / NixOS version to use.
  inputs.nixpkgs.url = "nixpkgs/nixos-25.05";

  outputs = { self, nixpkgs }:
    let

      # System types to support.
      supportedSystems = [ "x86_64-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin" ];

      # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

      # Nixpkgs instantiated for supported system types.
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });

    in {

      # Set up xsystem4 development environment:
      #     nix develop # create shell environment with dependencies available
      devShell = forAllSystems (system:
        let pkgs = nixpkgsFor.${system}; in with pkgs; pkgs.mkShell {
          nativeBuildInputs = [ meson ninja pkg-config ];
          buildInputs = [
            cglm
            SDL2
            freetype
            libffi
            libjpeg
            libwebp
            libsndfile
            glew
            readline
            chibi
            flex
            bison
            ffmpeg
          ];
        }
      );

      # Build xsystem4:
      #     nix build .?submodules=1 # build xsystem4 (outputs to ./result/)
      #     nix shell .?submodules=1 # create shell environment with xsystem4 available
      # Install xsystem4 to user profile:
      #     nix profile install .?submodules=1
      packages = forAllSystems (system:
        let pkgs = nixpkgsFor.${system}; in {
          default = with pkgs; stdenv.mkDerivation rec {
            name = "xsystem4";
            src = ./.;
            mesonAutoFeatures = "auto";
            nativeBuildInputs = [ meson ninja pkg-config ];
            buildInputs = [
              cglm
              SDL2
              freetype
              libffi
              libjpeg
              libwebp
              libsndfile
              glew
              readline
              chibi
              flex
              bison
              ffmpeg
            ];
          };
        }
      );
    };
}
