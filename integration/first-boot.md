# J72s first Android boot

The validated J72s kernel and device tree use 16 KiB pages. The available
Android 16 QPR1 GSI is ARM64, API 36, and 1,968,115,712 bytes. It is a raw
ext4 system filesystem containing `/system/bin/init`. Keep system and
vendor on block-backed storage so their contents are paged in as needed
on the 2 GB iPad.

## First-stage storage modules

The candidate builds the J72s Lightning role controller and its EHCI
and OHCI host controllers into the kernel. USB mass storage, USB Attached
SCSI, and the SCSI disk driver are modules. Android needs these before
first-stage init can find system and vendor on a USB disk. Keep the
storage module closure in the ramdisk and the remaining Android driver
modules on vendor.

From the repository root on a Linux build host with Python 3.11 or newer
and `modinfo` from kmod, stage an overlay using the artifact directory
produced by `scripts/build.sh`.

```sh
python3 scripts/stage_j72s_early_storage.py /path/to/kernel-artifacts /path/to/new-overlay
```

The overlay contains `lib/modules/<kernel-release>_16k/` with the USB
storage drivers, their dependency closure, and the metadata read by
Android 16 first-stage init. Module hashes are anchored to the kernel
artifact's `SHA256SUMS`. The staging script checks the built-in
controller prerequisites, each module's architecture and kernel
release, and rejects unresolved dependencies. Merge the overlay at
the root of the eventual Android first-stage ramdisk.

## Validate the proposed external media offline

The GitHub vendor build is configured to compile Android's
`init_first_stage` separately from the vendor image. Combine the resulting
first-stage executable and storage overlay only after selecting and validating
dedicated Android media.

On Linux with `sfdisk`, `debugfs` and `fdtget`, validate a regular-file
copy of the proposed external GPT media against the raw system and vendor
artifacts. Supply the disk GUID recorded independently for that image.
The candidate layout uses `j72s-system` and `j72s-vendor`.
The preflight also recognizes `j72s-data` and `j72s-metadata` on that disk.

```sh
python3 validation/check_j72s_boot_media.py \
    --dtb /path/to/s8000-j72s.dtb \
    --media-image /path/to/dedicated-media.img \
    --disk-guid DISK_GUID \
    --system /path/to/system.img \
    --vendor /path/to/vendor.img
```

The check verifies the J72s device-tree identity, explicit GPT disk GUID,
unique named partitions, Android image properties and a byte-for-byte
match to the system and vendor images. It reports the selected
partition's UUID for boot-device binding. The report lists the verified
GPT identities of any dedicated data and metadata partitions. Their
contents and runtime encryption are separate tests.

Android 16 first-stage init uses `androidboot.boot_part_uuid` to select an
exact block-device parent from Linux partition add events. Binding it to
the validated external system partition's GPT UUID would let Android
identify the underlying SCSI disk rather than relying on a changing
`sdX` name or a USB-controller path shared by several disks. The
selected disk receives the global `/dev/block/by-name/` aliases.
The temporary loader must deliver that property accurately, and
`androidboot.fstab_suffix=j72s` can select the eventual root
`fstab.j72s`. The current DTB has no Android Fstab subtree or boot
arguments to select the media. Verify that the runtime DTB and boot
properties preserve disk binding and that the eventual first-stage
fstab includes read-only system and vendor entries. The second-stage
data configuration requires a separate review and physical verification.

[Android 16 block device selection](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/init/block_dev_initializer.cpp)
[Android 16 first-stage fstab lookup](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/fs_mgr/libfstab/fstab.cpp)

## Package the read-only first-stage diagnostic

Once the matching vendor image and compiled `init_first_stage` are
available, assemble a first-stage mount diagnostic from Linux. Use the
first-stage build output that contains `init` and its `SHA256SUMS`.
The packager also uses `modinfo` and `readelf`.

```sh
python3 validation/package_j72s_first_stage.py \
    --kernel /path/to/kernel-artifacts \
    --init /path/to/first-stage/init \
    --early-storage /path/to/j72s-early-storage-overlay.tar.gz \
    --media-image /path/to/dedicated-media.img \
    --disk-guid DISK_GUID \
    --system /path/to/system.img \
    --vendor /path/to/vendor.img \
    --output /path/to/new-diagnostic
```

The output contains a small gzip-compressed ramdisk, its board fstab,
the boot properties for the eventual temporary loader, and a report and
checksums. The ramdisk includes only the validated ARM64 first-stage
binary, matching storage modules, mount-point directories, and fstab.
Android mounts the GSI at `/system` before switching root to it and
then mounts `/vendor`. Both entries are read-only with journal loading
disabled. Android fs_mgr can make a temporary writable mount when it
detects ext4 recovery conditions, so the packager rejects dirty or
quota-enabled filesystems and selected fstab overrides. A physical
attempt also requires independently verified write-protection of the
dedicated external media and verification that the loader supplies
the expected boot properties.

This artifact is for observing first-stage mounts and Android's attempt
to hand off to second-stage init. It leaves `/data` unconfigured and UI
boot unverified. The static checks and manifest describe the selected
regular-file image. Runtime behavior, the loader, and recovery still
require hardware validation.

[Android 16 mount and switch-root behavior](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/init/first_stage_mount.cpp)
[Android 16 filesystem preparation](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/fs_mgr/fs_mgr.cpp)

## Encrypted data for home-screen boot

Android 16 second-stage `ueventd` re-applies `androidboot.boot_part_uuid`
and generates global by-name aliases for the selected SCSI disk.
A later writable configuration can use the dedicated data and metadata
aliases on that same parent. Verify the aliases against the selected
media before enabling either filesystem.

Stock Android init starts encryption HALs during `late-fs`. The board
needs a vendor-side late mount action and persistent fstab visible after
the GSI switch-root. Metadata encryption needs an early dedicated
`/metadata` filesystem for its key material, the kernel's fscrypt and
`dm-default-key` support, and KeyMint ready before encrypted data mounts.
External USB storage also needs the software inline-encryption fallback.
The supplied GSI provides the Android init, vold and Keystore framework
pieces. The J72s vendor build must provide and start the appropriate
KeyMint service. [Android 16 second-stage device handling](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/init/ueventd.cpp)
[AOSP metadata encryption](https://source.android.com/docs/security/features/encryption/metadata)

The diagnostic report inventories those kernel options, both dedicated GPT
identities and any visible vendor KeyMint default declaration. A VINTF
declaration does not establish that the service runs or protects keys.
The original local candidate lacked `CONFIG_FS_ENCRYPTION` and
`CONFIG_DM_DEFAULT_KEY`, so inspect the exact rebuilt kernel before
attempting writable data. The diagnostic config keeps both data
filesystems unmounted. A UI candidate requires a verified encrypted mount
and successful key creation across reboot.

## Gates before a hardware attempt

- [ ] Build the complete J72s vendor image with matching kernel modules and board firmware.
- [ ] Build the ARM64 Android first-stage init and produce the small J72s ramdisk with the storage module overlay.
- [ ] Specify a dedicated Android storage layout and resolve its stable block identities in the board fstab. Keep system and vendor read-only and put Android data on dedicated storage. Check all paths against the intended media before a boot test.
- [ ] Verify that the selected temporary loader can deliver the matching kernel, J72s DTB and Android ramdisk. Verify the planned recovery path to iPadOS.
- [ ] Confirm Android first-stage mounts and second-stage init, then the first graphical boot. Measure RAM and swap during an app-switching test.

If the first boot uses a Lightning USB storage adapter in host mode,
plan logs that can be collected without switching that port to USB device
mode. The [display-console preflight](../android-stage/usb/first-boot-console.md)
checks the effective device tree and kernel configuration. Confirm the
hardware can resume stock iPadOS before treating the temporary setup as
ready for independent boot work.

[Android generic boot and first-stage init](https://source.android.com/docs/core/architecture/partitions/generic-boot)
