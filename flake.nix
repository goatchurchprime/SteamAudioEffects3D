{
  description = "AudioStreamEffects3D Steam Audio GDExtension";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/f4a03c008967841f2a0b3cb7c436fc6fc31671f6";
  # Keep the Steam Frame ABI below the headset's glibc 2.43. The normal
  # development shell can move independently with desktop Godot.
  inputs.nixpkgs-frame.url = "github:NixOS/nixpkgs/b6018f87da91d19d0ab4cf979885689b469cdd41";

  outputs = { self, nixpkgs, nixpkgs-frame }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      framePkgs = import nixpkgs-frame { inherit system; };
      frameCross = framePkgs.pkgsCross.aarch64-multiplatform;
    in {
      devShells.${system} = {
      default = pkgs.mkShell {
        packages = with pkgs; [
          cmake
          curl
          flatbuffers
          gcc
          godot_4
          libmysofa
          ninja
          patchelf
          pffft
          pkg-config
          scons
          unzip
          zip
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

      steam-frame = framePkgs.mkShell {
        packages = with framePkgs; [
          cmake
          flatbuffers
          ninja
          patchelf
          pkg-config
          scons
          frameCross.stdenv.cc
        ];

        CROSS_CC = "${frameCross.stdenv.cc}/bin/${frameCross.stdenv.cc.targetPrefix}cc";
        CROSS_CXX = "${frameCross.stdenv.cc}/bin/${frameCross.stdenv.cc.targetPrefix}c++";
        CROSS_AR = "${frameCross.stdenv.cc.bintools.bintools}/bin/${frameCross.stdenv.cc.targetPrefix}ar";
        PFFFT_INCLUDE_DIR = "${frameCross.pffft}/include";
        PFFFT_LIBRARY = "${frameCross.pffft}/lib/libpffft.a";
        MYSOFA_INCLUDE_DIR = "${frameCross.libmysofa.dev}/include";
        MYSOFA_LIBRARY = "${frameCross.libmysofa}/lib/libmysofa.a";
        ZLIB_INCLUDE_DIR = "${frameCross.zlib.dev}/include";
        ZLIB_LIBRARY = "${frameCross.zlib.static}/lib/libz.a";
        FLATBUFFERS_INCLUDE_DIR = "${framePkgs.flatbuffers}/include";
        FLATBUFFERS_EXECUTABLE = "${framePkgs.flatbuffers}/bin/flatc";
      };
      };
    };
}
