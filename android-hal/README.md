# J72s Android 16 HAL integration

## J72s vendor product

Place this checkout at `device/apple/j72s-port` in an Android 16 source
tree. The `j72s_vendor-trunk_staging-userdebug` target builds the vendor
filesystem for integration with the Android 16 ARM64 system image. The
kernel and DTB are built separately. Use the matching kernel and DTB
for the modules in the vendor image.

The product consumes the following source-tree layout.

```text
device/apple/j72s-port/
  vendor-inputs/
    modules/*.ko
    firmware/apple/j72s-fwstart.z2fw
    firmware/apple/gfx-s8003.bin
    firmware/apple/s8000-smartio.bin
    firmware/apple/aop-ipad6f.bin
    firmware/apple/aop-ipad6f-ext.bin
    firmware/apple/vxd-deva.bin
    firmware/brcm/...
  vendor-vulkan/
    Android.bp
    vulkan.j72s.so
```

Use a local Android source tree for the firmware bytes. Orchard's
v0.3.8 recipe for J72s pins the 20H350 Apple restore image and all
19 outputs. Its host firmware-only command obtains the required byte
ranges without a device session. With `ORCHARD` pointing to a local
checkout at `116004fb11f91cedc52db718ed5c6a6492d6577f` and
`ORCHARD_HOME` set and exported as a private cache directory, run:

```sh
PYTHONPATH="$ORCHARD/src" python3 - "$ORCHARD" <<'PY'
from pathlib import Path
import sys
from orchard import firmware
from orchard.progress import Clock, Progress

print(firmware.ensure("j72s", Clock(), Progress(), hardware=Path(sys.argv[1])))
PY
```

From the port checkout in that local build tree, stage the cache
directory printed above and the matching kernel's installed modules.

```sh
mkdir -p vendor-inputs
python3 scripts/stage_j72s_firmware.py stage /path/to/orchard/firmware/j72s/outputs \
    vendor-inputs/firmware
python3 scripts/vendor_modules.py /path/to/lib/modules/KERNEL_RELEASE \
    vendor-inputs/modules --audio --wireless --coprocessors
bash scripts/stage-j72s-audio.sh /path/to/aosp/hardware/interfaces
bash scripts/stage-j72s-bluetooth.sh /path/to/aosp/hardware/interfaces
```

Stage the prebuilt PowerVR Vulkan module and apply the pinned ADBE
kernel, composer and minigbm patches described below. The vendor product
installs the touchscreen IDC, imports the Lights ueventd rule, and
includes the Android 16 graphics and Lights services. Keep Apple
firmware in the local build inputs outside the synchronized checkout.

From the AOSP root, the build entry point is:

```sh
source build/envsetup.sh
lunch j72s_vendor-trunk_staging-userdebug
m vendorimage
```

The product packages the Android speaker route, the four-sensor AIDL
service, Bluetooth HCI, and Android Wi-Fi over nl80211. It stages the
SmartIO, AOP sensor, CT819 ambient-light, VXD video, audio and wireless
kernel modules.
Wired jack detection, cellular integration, the Android media
service, and a full platform build remain open. Track hardware support
against `integration/acceptance.json`.
Only use this target for code and image integration.

Run the host checks from the repository root in Linux or WSL with CMake,
Ninja, C/C++ compilers, Python 3, OpenSSL, pkg-config, and libdrm headers:

```sh
cmake -S android-hal -B ../j72s-host-tests -G Ninja
cmake --build ../j72s-host-tests
ctest --test-dir ../j72s-host-tests --output-on-failure
```

For an Android 16 AOSP build, put this repository under
`device/apple/j72s-port`. In the J72s product makefile, inherit
`device/apple/j72s-port/android-hal/j72s-hal.mk`. Include
`device/apple/j72s-port/android-hal/BoardConfigJ72sHal.mk` from the product's
board configuration. Check that inherited products declare only one
provider for each of `ILights/default` and `IHealth/default`.

Import `/vendor/etc/ueventd.j72s-lights.rc` from the board's installed
`/vendor/etc/ueventd.rc` to grant the Lights service group write access to the
DWI brightness attribute when the kernel announces the device.

The product fragment also installs the IDC file for Orchard's Apple Z2
multitouch driver. J72s binds the J71t driver entry, which reports its evdev
name as `iPad6,11 Touchscreen`. The kernel supplies the axis ranges and already
applies Y inversion. The J72s device tree requests `apple/j72s-fwstart.z2fw`,
which must be supplied before touch can initialize. Set `J72S_TOUCH_FIRMWARE`
to a board-matched firmware build input before inheriting the product
fragment to install it under `/vendor/firmware/apple/`. The kernel and
userspace firmware loader must be configured so the driver can retrieve it
at probe time. Confirm the evdev identity, rotation, and multitouch behavior
on the target.

The pinned J72s device tree requests `apple/gfx-s8003.bin` for PowerVR.
Set `J72S_GPU_FIRMWARE` to the board-matched firmware input to package it
under the same vendor firmware directory. To build the Android ARM64 Vulkan
library locally, run `bash scripts/build-android-gpu.sh` with a build-only
checkout of the pinned Orchard Mesa source, a Linux NDK with API 35, and a
build directory. The script applies the source fixes in `android-stage/`,
builds the host shader tools and ARM64 driver, and uses patchelf to stage
`vulkan.j72s.so` with the matching SONAME. It checks the architecture,
16 KB segment alignment, and the Android Vulkan HAL export.

Place the generated `vendor-vulkan` directory in the AOSP device tree
and set `J72S_ENABLE_VENDOR_VULKAN := true` before inheriting the product
fragment. This installs the 64-bit HAL and sets the Vulkan loader property.
The board still needs matching GPU firmware, modules, and an Android
vendor build. Run the probe on the target before declaring hardware
Vulkan features. Panel presentation also needs a working composer and
allocator.

The module staging script resolves dependencies from the matching kernel's
`modules.dep`. The board fragment adds the driver's roots to
`modules.load`, and second-stage Android init runs the vendor modprobe
service to load them. Use the same build's kernel image and device tree.
The external kernel boot configuration needs
`firmware_class.path=/vendor/firmware` so Linux can find the packaged
firmware.

Debug products include `j72s-native-interfaces`. Its graphics check
enumerates the Vulkan devices Android can open. Its audio check opens an
AAudio output stream and reports the stream parameters. This diagnostic
can also be compiled separately with the Android NDK for ARM64.

Debug products also include `j72s-vulkan-probe`. It submits a buffer fill,
waits for completion, and checks every word after reading the result back.
The Android build requires a hardware Vulkan device. The optional desktop
test can exercise the same submission and readback on a software Vulkan
driver when Vulkan development files and an ICD are available. This test
covers command submission and memory visibility. Panel presentation and
graphics throughput require separate checks.

## ADBE display allocation

Orchard's display driver reports the DRM name `adbe`. The candidate
kernel applies `adbe-argb-primary.patch` to accept linear ARGB8888
alongside the existing XRGB8888 layout. Both have the same color bytes.
The panel's single primary plane ignores alpha. The minigbm patches
add ADBE using the existing DRM dumb-buffer allocator and PRIME import
path, allow both formats for scanout, and keep explicit RGBA offscreen.

Run `bash scripts/test-adbe-allocator.sh <build-only minigbm checkout> <build directory>`
with Linux C build tools and libdrm headers. It applies the two patches to Android
16 minigbm commit `10d2dbeec1d248e7a040ecab225a075caa34b2bf` and verifies format
selection through minigbm using a synthetic DRM device name.

Apply both `drm-hwc-adbe-*.patch` files to Android 16 drm_hwcomposer
commit `002126e4cbf3e57c5375bf4106cde90a6977eeb0`. The composer requests a
BGRA8888 client target for the ADBE display, and minigbm maps BGRA8888
to DRM ARGB8888. The composer uses client composition for ADBE so the
primary plane receives the finished frame. In the device product,
set `J72S_ENABLE_ADBE_DISPLAY := true` before inheriting the product
fragment, using the patched kernel, minigbm, and composer together.
Remove competing composer and allocator providers from the product.
If the product also enables the packaged PowerVR Vulkan HAL, the fragment
selects the threaded Skia Vulkan RenderEngine for this display path.

The kernel enables the system DMA-BUF heap for shared buffers. The
allocator tests and kernel build run on the desktop. Soong must still
build the composer and allocator in the device product. GPU buffer import,
display fences, and panel scanout require target validation.

## Audio bring-up

Orchard describes the iPad 5 sound card as `iPad5 Speakers`, with the
CS42L81 codec on Apple MCA0. Pass `--audio` to `scripts/vendor_modules.py`
to include the Apple MCA driver, CS42L81 codec, simple audio card driver,
and their dependencies in the matching module stage. Enable
`J72S_ENABLE_AUDIO_MODULES := true` in the board and product makefiles
before including the J72s fragments to add the audio roots to the vendor
module load list and include the diagnostic in debug products.

`j72s-alsa-probe` reads ALSA control device information. It selects the
board sound card by name instead of assuming that it is card zero,
checks playback and capture independently, and rejects ambiguous
endpoints. It needs permission to read the sound control nodes. It does
not open PCM streams, change mixer controls, or play or record samples.
The Android primary HAL uses this resolver for speaker playback and
built-in microphone capture. The wired headphone port uses the same
playback endpoint after an external connection event. See the
[primary audio notes](../android-stage/audio-primary/README.md).

After selecting the J72s product, build the services with
`m android.hardware.light-service.j72s android.hardware.health-service.j72s`,
then build the vendor image and run the product's VINTF and SELinux checks.
The vendor image step requires the device product, kernel-specific sysfs
permissions, and the rest of its board configuration.

The kernel interfaces for the other subsystems are tracked in
`integration/acceptance.json`. Host tests exercise synthetic backlight and
battery trees. Service registration, device permissions, and the hardware
acceptance criteria require separate validation.
