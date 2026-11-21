#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steam_upstream="$repo_dir/thirdparty/steam-audio/core"
platform="${1:?usage: build_native_source.sh <linux|windows|macos|android> <arch>}"
arch="${2:?usage: build_native_source.sh <linux|windows|macos|android> <arch>}"

case "$platform:$arch" in
    linux:x86_64)
        valve_platform="linux"
        valve_arch="x64"
        valve_build="linux-x64-release"
        runtime_name="libphonon.so"
        output_dir="linux-x86_64"
        ;;
    windows:x86_64)
        valve_platform="windows"
        valve_arch="x64"
        valve_build="windows-vs2022-x64"
        runtime_name="phonon.dll"
        link_name="phonon.lib"
        output_dir="windows-x86_64"
        ;;
    macos:universal)
        valve_platform="osx"
        valve_arch="x64"
        valve_build="osx"
        runtime_name="libphonon.dylib"
        output_dir="macos"
        ;;
    android:arm64)
        valve_platform="android"
        valve_arch="arm64"
        valve_build="android-arm64-release"
        runtime_name="libphonon.so"
        output_dir="android-arm64"
        if [[ -z "${ANDROID_NDK:-}" ]]; then
            export ANDROID_NDK="${ANDROID_NDK_ROOT:-}"
        fi
        if [[ -z "${ANDROID_NDK:-}" ]]; then
            echo "ANDROID_NDK or ANDROID_NDK_ROOT must identify Android NDK r25c" >&2
            exit 1
        fi
        if command -v cygpath >/dev/null 2>&1; then
            # CMake persists this value in generated source using backslashes as
            # escapes. Use a Windows path with forward slashes instead.
            ANDROID_NDK="$(cygpath -m "$ANDROID_NDK")"
            export ANDROID_NDK ANDROID_NDK_ROOT="$ANDROID_NDK"
        fi
        ;;
    *)
        echo "Unsupported source target: $platform:$arch" >&2
        exit 2
        ;;
esac

steam_work="$repo_dir/build/steam-audio-source-$platform-$arch"
steam_source="$steam_work/source"
steam_root="$steam_work/install"
rm -rf "$steam_work"
mkdir -p "$steam_source"
git -C "$steam_upstream" archive HEAD | tar -x -C "$steam_source"
patch -d "$steam_source" -p1 < "$repo_dir/patches/steam-audio-build-compatibility.patch"

# Valve pins the dependency revisions in dependencies.json. Build only the four
# required dependencies; the optional simulator, GPU, tests, samples, and docs
# dependencies are deliberately outside this direct-path addon.
pushd "$steam_source/build" >/dev/null
for dependency in flatbuffers pffft zlib mysofa; do
    dependency_args=(
        --platform "$valve_platform"
        --architecture "$valve_arch"
        --dependency "$dependency"
    )
    if [[ "$platform" == "windows" ]]; then
        dependency_args+=(--toolchain vs2022)
    elif [[ "$platform" == "android" ]]; then
        dependency_args+=(--toolchain vs2022 --ndk "$ANDROID_NDK")
    fi
    python get_dependencies.py "${dependency_args[@]}"
done

build_args=(
    --platform "$valve_platform"
    --architecture "$valve_arch"
    --configuration release
    --operation default
    --minimal
    --parallel 4
)
if [[ "$platform" == "windows" ]]; then
    build_args+=(--toolchain vs2022)
fi
python build.py "${build_args[@]}"
popd >/dev/null

mkdir -p "$steam_root/include" "$steam_root/lib"
cp "$steam_source/src/core/phonon.h" "$steam_root/include/"
cp "$steam_source/build/$valve_build/src/core/phonon_version.h" "$steam_root/include/"
case "$platform" in
    windows)
        cp "$steam_source/build/$valve_build/src/core/Release/$runtime_name" "$steam_root/lib/"
        cp "$steam_source/build/$valve_build/src/core/Release/$link_name" "$steam_root/lib/"
        ;;
    macos)
        cp "$steam_source/build/$valve_build/src/core/Release/$runtime_name" "$steam_root/lib/"
        ;;
    *)
        cp "$steam_source/build/$valve_build/src/core/$runtime_name" "$steam_root/lib/"
        ;;
esac

addon_bin="$repo_dir/addons/steam_spatial/bin/$output_dir"
mkdir -p "$addon_bin"
cd "$repo_dir"
for target in template_debug template_release; do
    scons_args=(platform="$platform" arch="$arch" target="$target" use_static_cpp=false -j2)
    if [[ "$platform" == "android" ]]; then
        # godot-cpp otherwise prefers the runner's ANDROID_HOME over the exact
        # NDK selected above, even when ANDROID_NDK_ROOT is explicitly set.
        scons_args+=(ANDROID_HOME=)
    fi
    STEAM_AUDIO_ROOT="$steam_root" \
    STEAM_SPATIAL_OUTPUT_DIR="$addon_bin" \
    scons "${scons_args[@]}"
done
cp "$steam_root/lib/$runtime_name" "$addon_bin/$runtime_name"

if [[ "$platform" == "linux" ]]; then
    # shellcheck disable=SC2016 # The loader, not this shell, expands $ORIGIN.
    patchelf --set-rpath '$ORIGIN' "$addon_bin/$runtime_name"
    for extension in "$addon_bin"/libsteam_spatial.linux.*.x86_64.so; do
        # shellcheck disable=SC2016
        patchelf --set-rpath '$ORIGIN' "$extension"
    done
elif [[ "$platform" == "macos" ]]; then
    install_name_tool -id '@rpath/libphonon.dylib' "$addon_bin/$runtime_name"
fi

echo "Built Steam Audio and the GDExtension from source for $platform:$arch"
