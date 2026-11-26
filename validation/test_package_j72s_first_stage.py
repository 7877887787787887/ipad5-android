import gzip
import hashlib
import json
import os
import shutil
import stat
import struct
import tarfile
import unittest
from types import SimpleNamespace

import package_j72s_first_stage as boot
import test_check_j72s_boot_media as media


def checksums(directory, names):
    records = (f"{hashlib.sha256((directory / name).read_bytes()).hexdigest()}  {name}\n"
               for name in names)
    (directory / "SHA256SUMS").write_text("".join(records))


def static_init_fixture(path, alignment=16384):
    # A fixture for ELF layout and archive validation. It is never executed.
    length = 16384
    program = bytearray(length)
    ident = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    struct.pack_into("<16sHHIQQQIHHHHHH", program, 0, ident, 2, 183, 1,
                     0x400080, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    struct.pack_into("<IIQQQQQQ", program, 64, 1, 5, 0, 0x400000, 0x400000,
                     length, length, alignment)
    marker = b"init first stage started!"
    program[128:128 + len(marker)] = marker
    path.write_bytes(program)


def read_newc(path):
    entries = {}
    data = gzip.decompress(path.read_bytes())
    cursor = 0
    while True:
        assert data[cursor:cursor + 6] == b"070701"
        fields = [int(data[i:i + 8], 16) for i in range(cursor + 6, cursor + 110, 8)]
        name_end = cursor + 110 + fields[11]
        assert data[name_end - 1] == 0
        name = data[cursor + 110:name_end - 1].decode("ascii")
        start = (name_end + 3) & ~3
        end = start + fields[6]
        if name == "TRAILER!!!":
            assert end == len(data)
            return entries
        entries[name] = (fields[1], data[start:end])
        cursor = (end + 3) & ~3


@unittest.skipUnless(hasattr(os, "O_PATH") and all(shutil.which(tool) for tool in
                     ("dtc", "fdtget", "debugfs", "mkfs.ext4", "sfdisk", "readelf")),
                     "Linux ELF, DTB, ext filesystem and GPT tools are required")
class PackageFirstStageTest(unittest.TestCase):
    def setUp(self):
        self.fixture = media.CheckBootMediaTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        root = self.fixture.root
        kernel = root / "kernel"
        kernel.mkdir()
        shutil.copyfile(self.fixture.dtb, kernel / "s8000-j72s.dtb")
        (kernel / "Image.gz").write_bytes(b"kernel fixture")
        (kernel / "kernel-release.txt").write_text("7.3.0-rc1\n")
        configs = {"ARM64_16K_PAGES", "BLK_DEV_INITRD", "USB_EHCI_HCD_PLATFORM", "USB_OHCI_HCD_PLATFORM",
                   "USB_ROLES_APPLE_S8000", "EXT4_FS", "SCSI", "BLK_DEV_SD", "USB_STORAGE", "USB_UAS"}
        enabled = [f"CONFIG_{option}=y" for option in sorted(configs)] + list(boot.REQUIRED_CONFIG)
        (kernel / "candidate.config").write_text("\n".join(enabled) + "\n")
        with tarfile.open(kernel / "modules.tar.gz", "w:gz"):
            pass
        checksums(kernel, boot.KERNEL_ARTIFACTS)
        overlay = root / "overlay.tar.gz"
        with tarfile.open(overlay, "w:gz"):
            pass
        init_dir = root / "first-stage"
        init_dir.mkdir()
        init = init_dir / "init"
        static_init_fixture(init)
        checksums(init_dir, ("init",))
        self.inputs = SimpleNamespace(kernel=kernel, init=init, early_storage=overlay,
                                      media_image=self.fixture.media, disk_guid=media.DISK_GUID,
                                      system=self.fixture.system, vendor=self.fixture.vendor,
                                      output=root / "output")

    def test_creates_readonly_deterministic_mount_artifact(self):
        before = self.inputs.media_image.read_bytes()
        boot.package(self.inputs)
        out = self.inputs.output
        first_artifact = out / "first-stage-initramfs.cpio.gz"
        first_bytes = first_artifact.read_bytes()
        entries = read_newc(first_artifact)
        init_mode, init = entries["init"]
        self.assertEqual(init, self.inputs.init.read_bytes())
        self.assertEqual(init_mode, stat.S_IFREG | 0o750)
        self.assertTrue(all(stat.S_ISDIR(entries[name][0]) for name in boot.INIT_DIRECTORIES))
        fstab = entries["fstab.j72s"][1].decode("ascii")
        self.assertEqual(fstab, (out / "fstab.j72s").read_text())
        self.assertEqual([line.split() for line in fstab.splitlines()], [
            ["/dev/block/by-name/j72s-system", "/system", "ext4", "ro,noload", "wait,first_stage_mount"],
            ["/dev/block/by-name/j72s-vendor", "/vendor", "ext4", "ro,noload", "wait,first_stage_mount"],
        ])
        config = json.loads((out / "first-stage.json").read_text())
        self.assertFalse(config["data_mount_configured"])
        self.assertFalse(config["hardware_tested"])
        self.assertTrue(config["runtime_requires_verified_read_only_media"])
        userdata = config["encrypted_userdata_preflight"]
        self.assertEqual(userdata["required_kernel_options_missing"], list(boot.USERDATA_CONFIG))
        self.assertEqual(userdata["missing_dedicated_partitions"], ["data", "metadata"])
        self.assertFalse(userdata["late_fs_mount_configured"])
        self.assertIsNone(userdata["visible_vendor_keymint_default_manifest"])
        self.assertIn(f"androidboot.boot_part_uuid={media.SYSTEM_GUID}", (out / "kernel-args.txt").read_text())
        self.assertEqual(self.inputs.media_image.read_bytes(), before)

        with self.assertRaisesRegex(ValueError, "Use a new output directory"):
            boot.package(self.inputs)
        self.inputs.output = out.with_name("second-output")
        boot.package(self.inputs)
        self.assertEqual((self.inputs.output / first_artifact.name).read_bytes(), first_bytes)

    def test_rejects_wrong_init_alignment_and_checksum(self):
        static_init_fixture(self.inputs.init, alignment=4096)
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            boot.package(self.inputs)
        checksums(self.inputs.init.parent, ("init",))
        with self.assertRaisesRegex(ValueError, "16 KiB"):
            boot.package(self.inputs)
        self.assertFalse(self.inputs.output.exists())

    def test_rejects_dirty_media_and_unexpected_overlay(self):
        with self.inputs.vendor.open("r+b") as vendor:
            vendor.seek(1024 + 58)
            vendor.write(b"\x00\x00")
        with self.assertRaisesRegex(ValueError, "filesystem requires repair"):
            boot.verify_clean_ext4(self.inputs.vendor, "vendor")
        with self.inputs.vendor.open("r+b") as vendor:
            vendor.seek(1024 + 58)
            vendor.write(b"\x01\x00")
        with tarfile.open(self.inputs.early_storage, "w:gz") as archive:
            entry = tarfile.TarInfo("fstab.j72s")
            archive.addfile(entry)
        with self.assertRaisesRegex(ValueError, "Unexpected or duplicate storage overlay entry"):
            boot.package(self.inputs)
        self.assertFalse(self.inputs.output.exists())

    def test_rejects_late_fstab_override(self):
        vendor = self.fixture.write_ext_image(
            "vendor-override", "etc/fstab.j72s", "/dev/example /data ext4 defaults formattable\n")
        with self.assertRaisesRegex(ValueError, "second-stage fstab override"):
            boot.verify_clean_ext4(vendor, "vendor")

    def test_accepts_standard_tar_root_and_checks_nested_files(self):
        overlay = self.fixture.root / "archive-root"
        metadata = overlay / "lib/modules/example/modules.load"
        metadata.parent.mkdir(parents=True)
        metadata.write_text("sd_mod.ko\n")
        with tarfile.open(self.inputs.early_storage, "w:gz") as archive:
            archive.add(overlay, arcname=".")
        expected = hashlib.sha256(self.inputs.early_storage.read_bytes()).hexdigest()
        self.assertEqual(boot.verify_storage_overlay(self.inputs.early_storage, overlay), expected)
        metadata.write_text("bd_mod.ko\n")
        with self.assertRaisesRegex(ValueError, "Storage overlay differs from this kernel"):
            boot.verify_storage_overlay(self.inputs.early_storage, overlay)
        with tarfile.open(self.inputs.early_storage, "w:gz") as archive:
            archive.add(overlay, arcname=".")
            archive.add(overlay, arcname=".")
        with self.assertRaisesRegex(ValueError, "Unexpected or duplicate storage overlay entry"):
            boot.verify_storage_overlay(self.inputs.early_storage, overlay)

    def test_reports_vendor_keymint_declaration_without_claiming_runtime_ready(self):
        manifest_path = "etc/vintf/manifest/keymint.xml"
        for declaration in (
            "<fqname>IKeyMintDevice/default</fqname>",
            "<interface><name>IKeyMintDevice</name><instance>default</instance></interface>",
        ):
            with self.subTest(declaration=declaration):
                vendor = self.fixture.write_ext_image(
                    f"vendor-keymint-{len(declaration)}", manifest_path,
                    '<manifest type="device" version="1.0"><hal format="aidl">'
                    "<name>android.hardware.security.keymint</name>"
                    f"{declaration}</hal></manifest>",
                )
                report = boot.userdata_preflight(
                    boot.USERDATA_CONFIG, vendor,
                    {"dedicated_userdata_partitions": {"data": {}, "metadata": {}}},
                )
                self.assertEqual(report["visible_vendor_keymint_default_manifest"], f"/{manifest_path}")
                self.assertEqual(report["required_kernel_options_missing"], [])
                self.assertEqual(report["missing_dedicated_partitions"], [])
                self.assertFalse(report["late_fs_mount_configured"])
                self.assertFalse(report["encryption_on_target_verified"])


if __name__ == "__main__":
    unittest.main()
