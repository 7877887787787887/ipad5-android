#!/usr/bin/env bash
# Local build of the experimental PowerVR Vulkan library for Android.
set -euo pipefail
if [[ $# != 3 ]]; then
    echo "Usage: bash $0 MESA_SOURCE NDK_ROOT BUILD_DIRECTORY" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
source_dir=$(realpath "$1")
ndk=$(realpath "$2")/toolchains/llvm/prebuilt/linux-x86_64/bin
build_dir=$(realpath -m "$3")
expected=7df85604e5097fef98442796965e321e1cf2a0e7
if [[ $(git -C "$source_dir" rev-parse HEAD) != "$expected" ]]; then
    echo "Expected OrchardLinux/mesa commit $expected" >&2
    exit 2
fi
if [[ ! -x $ndk/aarch64-linux-android35-clang ]]; then
    echo "The supplied Linux NDK must provide the Android API 35 ARM64 compiler." >&2
    exit 2
fi
for suffix in const-tex drm-dependency pass-callback spill-log; do
    patch=$repo/android-stage/mesa-pvr-$suffix.patch
    if ! git -C "$source_dir" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$source_dir" apply "$patch"
    fi
done
setup() {
    local directory=$1
    shift
    local mode=()
    if [[ -f $directory/meson-private/coredata.dat ]]; then
        mode+=(--reconfigure)
    fi
    meson setup "${mode[@]}" "$directory" "$source_dir" "$@"
}

# PowerVR's generated shaders need programs that execute on the build host.
# Build those against host LLVM before configuring the Android target.
host_tools=$build_dir/host-tools
jobs=${BUILD_JOBS:-2}
setup "$host_tools" --buildtype=release -Dplatforms= \
    -Dgallium-drivers= -Dvulkan-drivers=imagination -Dglx=disabled \
    -Degl=disabled -Dgbm=disabled -Dmesa-clc=enabled -Dinstall-mesa-clc=true \
    -Dinstall-precomp-compiler=true
meson compile -C "$host_tools" -j "$jobs" mesa_clc vtn_bindgen2 pco_clc pdsc_precomp:executable

export PATH="$host_tools/src/compiler/clc:$host_tools/src/compiler/spirv:$host_tools/src/imagination/pco/uscgen:$host_tools/src/imagination/pdsc:$ndk:$PATH"
setup "$build_dir/target" \
    --cross-file "$repo/android-stage/powervr-aarch64.ini" \
    --buildtype=release \
    -Dplatforms=android -Dplatform-sdk-version=35 -Dandroid-stub=true \
    -Dandroid-libbacktrace=disabled -Dandroid-libperfetto=disabled \
    -Dvulkan-drivers=imagination -Dgallium-drivers= -Dglx=disabled \
    -Degl=disabled -Dgbm=disabled -Dllvm=disabled -Dmesa-clc=system \
    -Dprecomp-compiler=system \
    -Dallow-fallback-for=libdrm -Dlibdrm:default_library=static

if [[ ${ANDROID_GPU_CONFIGURE_ONLY:-0} == 1 ]]; then
    exit 0
fi
meson compile -C "$build_dir/target" -j "$jobs" vulkan_powervr_mesa
library=$build_dir/target/src/imagination/vulkan/libvulkan_powervr_mesa.so
test -s "$library"
stage=$build_dir/vendor-vulkan
mkdir -p "$stage"
staged=$stage/vulkan.j72s.so
patchelf --page-size 16384 --set-soname vulkan.j72s.so --output "$staged" "$library"
elf=$("$ndk/llvm-readelf" -W -h -l -d --dyn-syms "$staged")
printf '%s\n' "$elf"
if ! grep -Eq '^ *Machine: *AArch64' <<< "$elf"; then
    echo "PowerVR library is not AArch64." >&2
    exit 1
fi
mapfile -t alignments < <(awk '$1 == "LOAD" { print $NF }' <<< "$elf")
if (( ${#alignments[@]} == 0 )); then
    echo "PowerVR library has no load segments." >&2
    exit 1
fi
for align in "${alignments[@]}"; do
    if [[ ! $align =~ ^0x[0-9a-fA-F]+$ ]] || (( align < 0x4000 )); then
        echo "PowerVR library has a load segment aligned below 16 KB." >&2
        exit 1
    fi
done
if ! awk '$4 == "OBJECT" && $5 == "GLOBAL" && $7 != "UND" && $8 == "HMI" { found = 1 }
           END { exit !found }' <<< "$elf"; then
    echo "PowerVR library does not export Android's Vulkan HAL module symbol." >&2
    exit 1
fi
if [[ $(patchelf --print-soname "$staged") != vulkan.j72s.so ]]; then
    echo "PowerVR library SONAME does not match Android's module filename." >&2
    exit 1
fi
cp "$repo/android-stage/vulkan-prebuilt.bp.in" "$stage/Android.bp"
printf 'Verified Android Vulkan staging directory: %s\n' "$stage"
