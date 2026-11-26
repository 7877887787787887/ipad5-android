#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 ]]; then
    echo "Usage: bash $0 AOSP_HARDWARE_INTERFACES" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
interfaces=$(cd "$1" && pwd)
primary=$interfaces/audio/aidl/default/primary
patch=$repo/android-stage/audio-primary/j72s-primary-aidl.patch
routes=$repo/android-stage/audio-primary/j72s-routes-aidl.patch
sources=(android-stage/alsa_endpoint.c android-stage/alsa_endpoint.h
    android-stage/audio-primary/alsa_controls.c android-stage/audio-primary/alsa_controls.h)

for source in "${sources[@]}"; do
    name=${source##*/}
    if [[ ( -e $primary/$name || -L $primary/$name ) ]] &&
        ! cmp -s "$repo/$source" "$primary/$name"; then
        echo "Existing ALSA source differs: $primary/$name" >&2
        exit 1
    fi
done

if ! git -C "$interfaces" apply --reverse --check "$routes" 2>/dev/null; then
    if ! git -C "$interfaces" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$interfaces" apply --check "$patch"
        git -C "$interfaces" apply "$patch"
    fi
    git -C "$interfaces" apply --check "$routes"
    git -C "$interfaces" apply "$routes"
fi

for source in "${sources[@]}"; do
    name=${source##*/}
    if [[ ! -e $primary/$name ]]; then
        cp "$repo/$source" "$primary/$name"
    fi
done
