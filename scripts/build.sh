#!/usr/bin/env bash
# Build-only. Never accesses devices or writes firmware.
set -euo pipefail
cd "$(dirname "$0")/.."
repo=$PWD
board=${KERNEL_BOARD-j72s}
case "$board" in
  j72s)
    dtb_name=s8000-j72s
    default_output=$repo/artifacts
    ;;
  j72t)
    dtb_name=s8003-j72t
    default_output=$repo/artifacts/j72t
    ;;
  *)
    printf 'Unsupported kernel board %q (expected j72s or j72t)\n' "$board" >&2
    exit 1
    ;;
esac
output=${BUILD_OUTPUT_DIR:-"$default_output"}
mkdir -p "$output"
output=$(realpath "$output")
archive=${KERNEL_SOURCE_ARCHIVE:-}
work=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/var/tmp}}/$board-kernel.XXXXXX")
echo "Kernel build directory: $work"
mkdir -p "$work/src"
base=https://github.com/OrchardLinux/orchard/releases/download/v0.3.8
if [[ -z $archive ]]; then
  archive=$work/source.tar.gz
  curl --fail --location --retry 3 "$base/kernel-source-apple-16k.tar.gz" -o "$archive"
fi
printf '%s  %s\n' 2f4c537bbe415c4c44eb6a660ced546bab66e87c6b07a72a282996c0fb1512a7 "$archive" | sha256sum -c -
tar -xf "$archive" -C "$work/src"
mapfile -t candidates < <(find "$work/src" -type f -path '*/scripts/kconfig/merge_config.sh')
if [[ ${#candidates[@]} != 1 ]]; then echo 'Cannot uniquely locate kernel source tree'; exit 1; fi
src=${candidates[0]%/scripts/kconfig/merge_config.sh}
cd "$src"
patch --batch -p1 -i "$repo/android-stage/adbe-argb-primary.patch"
curl --fail --location --retry 3 https://raw.githubusercontent.com/OrchardLinux/orchard/v0.3.8/kernel/configs/apple-16k/config -o .config
cp .config "$output/original.config"
export ARCH=arm64
if [[ $(uname -m) != aarch64 ]]; then export CROSS_COMPILE=aarch64-linux-gnu-; fi
while IFS= read -r line; do
  if [[ $line =~ ^CONFIG_([A-Z0-9_]+)=y$ ]]; then scripts/config --enable "${BASH_REMATCH[1]}"; fi
done < <(tr -d '\r' < "$repo/scripts/android-candidate.fragment")
lsm=$(sed -n 's/^CONFIG_LSM="\(.*\)"$/\1/p' .config)
if [[ ,$lsm, != *,selinux,* ]]; then scripts/config --set-str LSM "${lsm:+$lsm,}selinux"; fi
make olddefconfig
cp .config "$output/candidate.config"
python3 "$repo/scripts/audit.py" .config | tee "$output/config-audit.json"
while IFS= read -r line; do
  if [[ $line =~ ^CONFIG_([A-Z0-9_]+)=y$ ]]; then grep -Fx "$line" .config >/dev/null || { echo "Missing candidate feature: $line"; exit 1; }; fi
done < <(tr -d '\r' < "$repo/scripts/android-candidate.fragment")
mapfile -t dts < <(find arch/arm64/boot/dts -name "$dtb_name.dts" -type f)
if [[ ${#dts[@]} != 1 ]]; then
  printf 'Exact %s device tree missing or ambiguous\n' "$board" >&2
  exit 1
fi
dtb=${dts[0]%.dts}.dtb
make -j"${BUILD_JOBS:-2}" Image.gz "${dtb#arch/arm64/boot/dts/}" modules
cp arch/arm64/boot/Image.gz "$dtb" "$output/"
make modules_install INSTALL_MOD_PATH="$work/modules" INSTALL_MOD_STRIP=1
find "$work/modules" -type l \( -name build -o -name source \) -delete
tar -czf "$output/modules.tar.gz" -C "$work/modules" .
make -s kernelrelease > "$output/kernel-release.txt"
(cd "$output" && sha256sum Image.gz "$dtb_name.dtb" modules.tar.gz candidate.config > SHA256SUMS)
echo 'Build completed. NOT boot-tested, NOT a flashable Android image.'
