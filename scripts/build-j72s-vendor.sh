#!/usr/bin/env bash
# Stage the board's verified inputs and build its Android 16 vendor image.
set -euo pipefail

stage_only=false
if [[ ${1:-} == --stage-only ]]; then
    stage_only=true
    shift
fi
if [[ $# != 4 ]]; then
    echo "Usage: bash $0 [--stage-only] AOSP_ROOT KERNEL_ARTIFACTS FIRMWARE_CACHE VULKAN_STAGE" >&2
    exit 2
fi

device=$(cd "$(dirname "$0")/.." && pwd -P)
aosp=$(realpath -e "$1")
kernel=$(realpath -e "$2")
firmware=$(realpath -e "$3")
vulkan=$(realpath -e "$4")

fail() {
    echo "$*" >&2
    exit 1
}

require_revision() {
    local name=$1 path=$2 expected=$3
    [[ -d $path ]] && [[ $(git -C "$path" rev-parse HEAD) == "$expected" ]] ||
        fail "Expected $name at Android 16 r1 commit $expected: $path"
}

apply_patch() {
    local project=$1 patch=$device/android-stage/$2
    if ! git -C "$project" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$project" apply --check "$patch"
        git -C "$project" apply "$patch"
    fi
}

[[ $(uname -m) == x86_64 ]] && [[ $(uname -s) == Linux ]] ||
    fail 'Android 16 requires a Linux x86-64 build host.'
[[ $device == "$aosp/device/apple/j72s-port" ]] && [[ -f $aosp/build/envsetup.sh ]] ||
    fail 'Put the port checkout at device/apple/j72s-port under the Android source root.'
[[ $aosp != *[[:space:]]* ]] || fail 'The Android source path must be free of whitespace.'

interfaces=$aosp/hardware/interfaces
minigbm=$aosp/external/minigbm
composer=$aosp/external/drm_hwcomposer
require_revision hardware/interfaces "$interfaces" 5688f7eb1e117ed26e642a695de300b7683acb87
require_revision minigbm "$minigbm" 10d2dbeec1d248e7a040ecab225a075caa34b2bf
require_revision drm_hwcomposer "$composer" 002126e4cbf3e57c5375bf4106cde90a6977eeb0

for name in Image.gz s8000-j72s.dtb modules.tar.gz candidate.config; do
    awk -v name="$name" '$2 == name { print; found++ } END { exit (found != 1) }' \
        "$kernel/SHA256SUMS" | (cd "$kernel" && sha256sum --check --strict)
done
release=$(cat "$kernel/kernel-release.txt")
[[ $release =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]] || fail 'Invalid kernel release.'
cmp -s "$vulkan/Android.bp" "$device/android-stage/vulkan-prebuilt.bp.in" &&
    [[ -s $vulkan/vulkan.j72s.so && ! -L $vulkan/vulkan.j72s.so ]] ||
    fail 'Supply the vendor-vulkan stage from scripts/build-android-gpu.sh.'

inputs=$device/vendor-inputs
[[ ! -L $inputs ]] || fail 'The vendor input stage must be a directory inside the port checkout.'
mkdir -p "$inputs"
python3 "$device/scripts/stage_j72s_firmware.py" stage "$firmware" "$inputs/firmware"

# Reject non-regular members before extracting the checked kernel bundle.
scratch=$(mktemp -d "$inputs/.modules-build.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
python3 - "$kernel/modules.tar.gz" "$scratch" <<'PY'
import sys
import tarfile

with tarfile.open(sys.argv[1], 'r:gz') as archive:
    members = archive.getmembers()
    if any(not (member.isdir() or member.isfile()) for member in members):
        raise SystemExit('Unexpected non-regular member in the kernel module bundle')
    archive.extractall(sys.argv[2], members=members, filter='data')
PY

[[ -f $scratch/lib/modules/$release/modules.dep ]] ||
    fail 'The module archive has no modules.dep for the supplied kernel release.'
python3 "$device/scripts/vendor_modules.py" "$scratch/lib/modules/$release" \
    "$scratch/staged" --audio --wireless --coprocessors
if [[ -e $inputs/modules || -L $inputs/modules ]]; then
    [[ -d $inputs/modules && ! -L $inputs/modules ]] &&
        diff -qr "$inputs/modules" "$scratch/staged" ||
        fail 'Existing vendor modules differ from this kernel build. Use a fresh build checkout.'
else
    mv "$scratch/staged" "$inputs/modules"
fi

install_vulkan=$device/vendor-vulkan
if [[ -e $install_vulkan || -L $install_vulkan ]]; then
    [[ -d $install_vulkan && ! -L $install_vulkan ]] &&
        cmp -s "$vulkan/Android.bp" "$install_vulkan/Android.bp" &&
        cmp -s "$vulkan/vulkan.j72s.so" "$install_vulkan/vulkan.j72s.so" ||
        fail 'Existing Vulkan files differ from the supplied build. Use a fresh build checkout.'
else
    mkdir "$install_vulkan"
    cp "$vulkan/Android.bp" "$vulkan/vulkan.j72s.so" "$install_vulkan/"
fi

# The second minigbm patch modifies the backend introduced by the first patch.
scanout=$device/android-stage/minigbm-adbe-bgra.patch
if ! git -C "$minigbm" apply --reverse --check "$scanout" 2>/dev/null; then
    apply_patch "$minigbm" minigbm-adbe.patch
    apply_patch "$minigbm" minigbm-adbe-bgra.patch
fi
apply_patch "$composer" drm-hwc-adbe-client-backend.patch
apply_patch "$composer" drm-hwc-adbe-client-target.patch
bash "$device/scripts/stage-j72s-audio.sh" "$interfaces"
bash "$device/scripts/stage-j72s-bluetooth.sh" "$interfaces"

if $stage_only; then
    echo 'Verified and staged the J72s vendor build inputs.'
    exit
fi

(
    cd "$aosp"
    # AOSP's environment script and lunch helpers assume unset variables are allowed.
    set +u
    source build/envsetup.sh
    lunch j72s_vendor-aosp_current-userdebug
    product_out=$(get_build_var PRODUCT_OUT)

    m init_first_stage
    first_stage=$product_out/ramdisk/init
    [[ -s $first_stage ]] || fail 'The Android first-stage init was not installed.'
    elf=$(LC_ALL=C readelf -h -l -d -W "$first_stage")
    grep -Eq 'Class:[[:space:]]+ELF64' <<< "$elf" &&
        grep -Eq 'Machine:[[:space:]]+AArch64' <<< "$elf" &&
        ! grep -Eq '^[[:space:]]*INTERP[[:space:]]|\(NEEDED\)' <<< "$elf" ||
        fail 'First-stage init must be a statically linked ARM64 executable.'
    mapfile -t alignments < <(awk '$1 == "LOAD" { print $NF }' <<< "$elf")
    (( ${#alignments[@]} > 0 )) || fail 'First-stage init has no load segments.'
    for align in "${alignments[@]}"; do
        [[ $align =~ ^0x[[:xdigit:]]+$ ]] && (( align >= 0x4000 )) ||
            fail 'First-stage init has load segments aligned below 16 KiB.'
    done
    sha256sum "$first_stage"
    if [[ -n ${J72S_FIRST_STAGE_OUTPUT_DIR:-} ]]; then
        mkdir -p "$J72S_FIRST_STAGE_OUTPUT_DIR"
        cp "$first_stage" "$J72S_FIRST_STAGE_OUTPUT_DIR/init"
        (cd "$J72S_FIRST_STAGE_OUTPUT_DIR" && sha256sum init > SHA256SUMS)
    fi

    m vendorimage
    [[ -s $product_out/vendor.img ]] ||
        fail 'Soong returned without producing the J72s vendor.img.'
    python3 - "$product_out/vendor.img" <<'PY'
import sys

with open(sys.argv[1], "rb") as image:
    image.seek(1080)
    if image.read(2) != b"\x53\xef":
        raise SystemExit("Expected a raw ext4 vendor image for the external boot media")
PY
    sha256sum "$product_out/vendor.img"
)
