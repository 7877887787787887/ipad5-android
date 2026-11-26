#!/usr/bin/env bash
# Host test of the ADBE minigbm backend against the pinned Android 16 source.
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: bash $0 MINIGBM_SOURCE BUILD_DIRECTORY" >&2
    exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
source_dir=$(realpath "$1")
out=$(realpath -m "$2")
expected=10d2dbeec1d248e7a040ecab225a075caa34b2bf
if [[ $(git -C "$source_dir" rev-parse HEAD) != "$expected" ]]; then
    echo "Expected Android 16 minigbm commit $expected" >&2
    exit 2
fi
base=$repo/android-stage/minigbm-adbe.patch
scanout=$repo/android-stage/minigbm-adbe-bgra.patch
if ! git -C "$source_dir" apply --reverse --check "$scanout" 2>/dev/null; then
    if ! git -C "$source_dir" apply --reverse --check "$base" 2>/dev/null; then
        git -C "$source_dir" apply "$base"
    fi
    git -C "$source_dir" apply "$scanout"
fi
mkdir -p "$out"
make -C "$source_dir" -j"${BUILD_JOBS:-2}" OUT="$out" PKG_CONFIG=pkg-config all
read -ra cflags <<< "$(pkg-config --cflags libdrm)"
read -ra libraries <<< "$(pkg-config --libs libdrm)"
"${CC:-cc}" -std=c17 -Wall -Wextra -Werror -I"$source_dir" "${cflags[@]}" \
    "$repo/android-stage/adbe_allocator_test.c" "$out"/*.pic.o "${libraries[@]}" \
    -pthread -ldl -o "$out/adbe_allocator_test"
"$out/adbe_allocator_test"
echo "ADBE allocation format policy passed."
