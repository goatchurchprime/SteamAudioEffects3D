#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steam_source="$repo_dir/thirdparty/steam-audio/core"
steam_build="$repo_dir/build/steam-audio-arm64/cmake"
steam_install="$repo_dir/build/steam-audio-arm64/install"
addon_bin="$repo_dir/addons/steam_spatial/bin/arm64"

for required in CROSS_CC CROSS_CXX CROSS_AR PFFFT_INCLUDE_DIR PFFFT_LIBRARY MYSOFA_INCLUDE_DIR MYSOFA_LIBRARY ZLIB_INCLUDE_DIR ZLIB_LIBRARY FLATBUFFERS_INCLUDE_DIR FLATBUFFERS_EXECUTABLE; do
    if [[ -z "${!required:-}" ]]; then
        echo "$required is not set. Run: nix develop .#steam-frame --command scripts/build_steam_frame.sh" >&2
        exit 1
    fi
done

cmake --fresh -S "$steam_source" -B "$steam_build" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER="$CROSS_CC" \
    -DCMAKE_CXX_COMPILER="$CROSS_CXX" \
    -DCMAKE_AR="$CROSS_AR" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUILD_SHARED_LIBS=ON \
    -DSTEAMAUDIO_ENABLE_IPP=OFF \
    -DSTEAMAUDIO_ENABLE_EMBREE=OFF \
    -DSTEAMAUDIO_BUILD_TESTS=OFF \
    -DSTEAMAUDIO_BUILD_BENCHMARKS=OFF \
    -DSTEAMAUDIO_BUILD_SAMPLES=OFF \
    -DSTEAMAUDIO_STATIC_RUNTIME=OFF \
    -DPFFFT_INCLUDE_DIR="$PFFFT_INCLUDE_DIR" \
    -DPFFFT_LIBRARY="$PFFFT_LIBRARY" \
    -DMySOFA_INCLUDE_DIR="$MYSOFA_INCLUDE_DIR" \
    -DMySOFA_LIBRARY="$MYSOFA_LIBRARY" \
    -DZLIB_INCLUDE_DIR="$ZLIB_INCLUDE_DIR" \
    -DZLIB_LIBRARY="$ZLIB_LIBRARY" \
    -DFlatBuffers_INCLUDE_DIR="$FLATBUFFERS_INCLUDE_DIR" \
    -DFlatBuffers_EXECUTABLE="$FLATBUFFERS_EXECUTABLE"
cmake --build "$steam_build" -j2

install -Dm755 "$steam_build/src/core/libphonon.so" "$steam_install/lib/libphonon.so"
install -Dm644 "$steam_source/src/core/phonon.h" "$steam_install/include/phonon.h"
install -Dm644 "$steam_build/src/core/phonon_version.h" "$steam_install/include/phonon_version.h"
cmake -E remove_directory "$repo_dir/thirdparty/steam-audio/unity/lib"

mkdir -p "$addon_bin"
cd "$repo_dir"
for target in template_debug template_release; do
    # SteamVR and the headset provide a compatible libstdc++. Linking a second
    # static copy into the extension interposes std::locale symbols across
    # vrclient.so and causes an invalid free during xrCreateInstance.
    STEAM_AUDIO_ROOT="$steam_install" \
    STEAM_SPATIAL_OUTPUT_DIR="$addon_bin" \
    scons platform=linux arch=arm64 target="$target" use_static_cpp=false \
        -j2
done
install -Dm755 "$steam_install/lib/libphonon.so" "$addon_bin/libphonon.so"
patchelf --set-rpath '$ORIGIN' "$addon_bin/libphonon.so"
for extension in "$addon_bin"/libsteam_spatial.linux.*.arm64.so; do
    patchelf --set-rpath '$ORIGIN' "$extension"
done

echo "Built Steam Frame ARM64 libraries in $addon_bin"
