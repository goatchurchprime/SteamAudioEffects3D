#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:?usage: package_release.sh <version> [output-directory]}"
output_dir="${2:-$repo_dir/dist}"
stage="$repo_dir/build/release-stage"
archive_name="steam-audio-effects-3d-${version}.zip"
archive="$output_dir/$archive_name"

rm -rf "$stage"
mkdir -p "$stage/addons" "$stage/licenses" "$output_dir"
cp -R "$repo_dir/addons/steam_spatial" "$stage/addons/"
cp "$repo_dir/thirdparty/steam-audio/LICENSE.md" "$stage/licenses/Steam-Audio-LICENSE.md"
cp "$repo_dir/thirdparty/steam-audio/TRADEMARK_RIGHTS.md" "$stage/licenses/Steam-Audio-TRADEMARK_RIGHTS.md"
cp "$repo_dir/thirdparty/steam-audio/core/THIRDPARTY.md" "$stage/licenses/Steam-Audio-THIRDPARTY.md"

required_files=(
    "linux-x86_64/libphonon.so"
    "linux-x86_64/libsteam_spatial.linux.template_debug.x86_64.so"
    "linux-x86_64/libsteam_spatial.linux.template_release.x86_64.so"
    "linux-arm64/libphonon.so"
    "linux-arm64/libsteam_spatial.linux.template_debug.arm64.so"
    "linux-arm64/libsteam_spatial.linux.template_release.arm64.so"
    "windows-x86_64/phonon.dll"
    "windows-x86_64/libsteam_spatial.windows.template_debug.x86_64.dll"
    "windows-x86_64/libsteam_spatial.windows.template_release.x86_64.dll"
    "macos/libphonon.dylib"
    "macos/libsteam_spatial.macos.template_debug.universal.dylib"
    "macos/libsteam_spatial.macos.template_release.universal.dylib"
    "android-arm64/libphonon.so"
    "android-arm64/libsteam_spatial.android.template_debug.arm64.so"
    "android-arm64/libsteam_spatial.android.template_release.arm64.so"
)
for relative_path in "${required_files[@]}"; do
    if [[ ! -f "$stage/addons/steam_spatial/bin/$relative_path" ]]; then
        echo "Missing release artifact: $relative_path" >&2
        exit 1
    fi
done

rm -f "$archive"
(
    cd "$stage"
    zip -9 -qr "$archive" addons licenses
)
(
    cd "$output_dir"
    sha256sum "$archive_name" > "$archive_name.sha256"
)
echo "$archive"
