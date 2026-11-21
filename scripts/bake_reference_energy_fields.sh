#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steam_root="${STEAM_AUDIO_ROOT:-${repo_dir}/build/steam-audio/install}"
output_dir="${1:-${repo_dir}/experiments/reflections/reference_energy_fields}"
quality="${2:-full}"
tool_dir="${repo_dir}/build/tools"
tool="${tool_dir}/bake_reference_energy_fields"

mkdir -p "${tool_dir}" "${output_dir}"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra \
	-I"${steam_root}/include" \
	"${repo_dir}/tools/bake_reference_energy_fields.cpp" \
	-L"${steam_root}/lib" -Wl,-rpath,"${steam_root}/lib" -lphonon \
	-o "${tool}"

extra=()
if [[ "${quality}" == "quick" ]]; then
	extra=(--quick)
elif [[ "${quality}" != "full" ]]; then
	echo "quality must be 'full' or 'quick'" >&2
	exit 2
fi

"${tool}" absorbent_wall "${output_dir}/absorbent_wall.json" "${extra[@]}"
"${tool}" small_shoebox "${output_dir}/small_shoebox.json" "${extra[@]}"
"${tool}" far_wall_echo "${output_dir}/far_wall_echo.json" "${extra[@]}"
"${tool}" big_auditorium "${output_dir}/big_auditorium.json" "${extra[@]}"

echo "Generated Steam Audio reference fields in ${output_dir}"
