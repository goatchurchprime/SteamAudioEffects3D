#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steam_source="$repo_dir/thirdparty/steam-audio/core"
steam_build="$repo_dir/build/steam-audio/cmake"
steam_install="$repo_dir/build/steam-audio/install"
addon_bin="$repo_dir/addons/steam_spatial/bin"

for required in PFFFT_INCLUDE_DIR PFFFT_LIBRARY MYSOFA_INCLUDE_DIR MYSOFA_LIBRARY ZLIB_INCLUDE_DIR ZLIB_LIBRARY FLATBUFFERS_INCLUDE_DIR FLATBUFFERS_EXECUTABLE; do
    if [[ -z "${!required:-}" ]]; then
        echo "$required is not set. Run this script with: nix develop --command scripts/build_linux.sh" >&2
        exit 1
    fi
done

cmake -S "$steam_source" -B "$steam_build" -G Ninja \
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
# Steam Audio's upstream post-build rule also copies this artifact into its Unity tree.
# It is generated output, not a modification we want to leave inside the pinned submodule.
cmake -E remove_directory "$repo_dir/thirdparty/steam-audio/unity/lib"

cd "$repo_dir"
STEAM_AUDIO_ROOT="$steam_install" scons platform=linux target=template_debug use_static_cpp=false -j2
install -Dm755 "$steam_install/lib/libphonon.so" "$addon_bin/libphonon.so"

echo "Built Steam Audio and the GDExtension in $addon_bin"
