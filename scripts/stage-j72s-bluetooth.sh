#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 ]]; then
    echo "Usage: bash $0 AOSP_HARDWARE_INTERFACES" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
interfaces=$(cd "$1" && pwd)
destination=$interfaces/bluetooth/aidl/default
source_dir=$repo/android-stage/bluetooth
patch=$source_dir/aosp-j72s-hci.patch

for name in j72s_hci_transport.cpp j72s_hci_transport.h; do
    if [[ ( -e $destination/$name || -L $destination/$name ) ]] &&
        ! cmp -s "$source_dir/$name" "$destination/$name"; then
        echo "Existing Bluetooth source differs: $destination/$name" >&2
        exit 1
    fi
done

if ! git -C "$interfaces" apply --reverse --check "$patch" 2>/dev/null; then
    git -C "$interfaces" apply --check "$patch"
    git -C "$interfaces" apply "$patch"
fi

for name in j72s_hci_transport.cpp j72s_hci_transport.h; do
    if [[ ! -e $destination/$name ]]; then
        cp "$source_dir/$name" "$destination/$name"
    fi
done
