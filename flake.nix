{
  description = "AudioStreamEffects3D Steam Audio GDExtension";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/f4a03c008967841f2a0b3cb7c436fc6fc31671f6";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          cmake
          flatbuffers
          gcc
          godot_4
          libmysofa
          ninja
          pffft
          pkg-config
          scons
          zlib
        ];

        PFFFT_INCLUDE_DIR = "${pkgs.pffft}/include";
        PFFFT_LIBRARY = "${pkgs.pffft}/lib/libpffft.a";
        MYSOFA_INCLUDE_DIR = "${pkgs.libmysofa.dev}/include";
        MYSOFA_LIBRARY = "${pkgs.libmysofa}/lib/libmysofa.a";
        ZLIB_INCLUDE_DIR = "${pkgs.zlib.dev}/include";
        ZLIB_LIBRARY = "${pkgs.zlib.static}/lib/libz.a";
        FLATBUFFERS_INCLUDE_DIR = "${pkgs.flatbuffers}/include";
        FLATBUFFERS_EXECUTABLE = "${pkgs.flatbuffers}/bin/flatc";
      };
    };
}
