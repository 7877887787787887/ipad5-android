import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path
from uuid import UUID

import check_j72s_boot_media as boot


SECTOR = 512
SECTORS = 49152
DISK_GUID = UUID("713a7499-4316-4cdf-9b1a-f81e717b8ee6")
FILESYSTEM = UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")
SYSTEM_GUID = UUID("4226fe65-0f3b-4ce6-bf7b-c01bc3618cea")
VENDOR_GUID = UUID("5ebf27c3-1c56-433c-880d-eb1371dcf6ec")
DATA_GUID = UUID("c4805492-e142-43c4-a1de-0681c7ee6f8b")
METADATA_GUID = UUID("ea29e260-5f61-458a-9b9c-23120d45eaea")
SYSTEM_START = 2048
VENDOR_START = 12288
CAPACITY = 8192


def write_gpt_fixture(path, locations):
    entries = bytearray(128 * 128)
    for index, (name, guid, start) in enumerate(locations):
        struct.pack_into("<16s16sQQQ72s", entries, index * 128, FILESYSTEM.bytes_le,
                         guid.bytes_le, start, start + CAPACITY - 1, 0, name.encode("utf-16-le"))

    def header(current, backup, entry_lba):
        data = bytearray(SECTOR)
        struct.pack_into("<8sIIIIQQQQ16sQIII", data, 0, b"EFI PART", 0x10000, 92,
                         0, 0, current, backup, 34, SECTORS - 34, DISK_GUID.bytes_le,
                         entry_lba, 128, 128, zlib.crc32(entries))
        struct.pack_into("<I", data, 16, zlib.crc32(data[:92]))
        return data

    # This fixture is a new regular file in a temporary directory.
    with path.open("xb") as media:
        media.truncate(SECTORS * SECTOR)
        mbr = bytearray(SECTOR)
        struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\x00\x02\x00", 0xee,
                         b"\xff\xff\xff", 1, SECTORS - 1)
        mbr[510:512] = b"\x55\xaa"
        media.write(mbr)
        media.write(header(1, SECTORS - 1, 2))
        media.write(entries)
        media.seek((SECTORS - 33) * SECTOR)
        media.write(entries)
        media.write(header(SECTORS - 1, 1, SECTORS - 33))


@unittest.skipUnless(hasattr(os, "O_PATH") and all(
    shutil.which(tool) for tool in ("dtc", "fdtget", "debugfs", "mkfs.ext4", "sfdisk")),
    "Linux DTB, ext filesystem and GPT tools are required")
class CheckBootMediaTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dtb = self.root / "board.dtb"
        self.write_dtb("apple,j72s")
        self.system = self.write_ext_image(
            "system", "system/build.prop",
            "ro.system.build.version.sdk=36\nro.system.product.cpu.abilist=arm64-v8a,armeabi-v7a\n")
        self.vendor = self.write_ext_image(
            "vendor", "build.prop", "ro.product.vendor.device=j72s-port\n")
        self.media = self.root / "media.img"
        write_gpt_fixture(self.media, [
            ("j72s-system", SYSTEM_GUID, SYSTEM_START), ("j72s-vendor", VENDOR_GUID, VENDOR_START),
        ])
        with self.media.open("r+b") as media:
            for image, start in ((self.system, SYSTEM_START), (self.vendor, VENDOR_START)):
                media.seek(start * SECTOR)
                media.write(image.read_bytes())

    def write_dtb(self, compatible, chosen=""):
        source = f'/dts-v1/; / {{ compatible = "{compatible}"; {chosen} }};'
        subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", self.dtb, "-"],
                       input=source, check=True, capture_output=True, text=True)

    def write_ext_image(self, name, property_path, properties):
        source = self.root / name
        props = source / property_path
        props.parent.mkdir(parents=True)
        props.write_text(properties)
        image = self.root / f"{name}.img"
        with image.open("xb") as filesystem:
            filesystem.truncate(2 * 1024 * 1024)
        subprocess.run(["mkfs.ext4", "-q", "-O", "^has_journal", "-d", source, image],
                       check=True, capture_output=True)
        return image

    def validate(self, disk_guid=DISK_GUID):
        return boot.validate(self.dtb, self.media, self.system, self.vendor, disk_guid)

    def test_read_only_media_match(self):
        before = self.media.read_bytes()
        result = self.validate()
        self.assertEqual(result["candidate_boot_part_uuid"], str(SYSTEM_GUID))
        self.assertEqual(result["images"]["vendor"]["partition_uuid"], str(VENDOR_GUID))
        self.assertFalse(result["hardware_tested"])
        self.assertEqual(result["dedicated_userdata_partitions"], {})
        self.assertEqual(self.media.read_bytes(), before)

    def test_reports_same_disk_data_and_metadata_without_mounting_them(self):
        extra = self.root / "extra.img"
        write_gpt_fixture(extra, [
            ("j72s-system", SYSTEM_GUID, SYSTEM_START), ("j72s-vendor", VENDOR_GUID, VENDOR_START),
            ("j72s-data", DATA_GUID, 22528), ("j72s-metadata", METADATA_GUID, 32768),
        ])
        with extra.open("r+b") as media:
            for image, start in ((self.system, SYSTEM_START), (self.vendor, VENDOR_START)):
                media.seek(start * SECTOR)
                media.write(image.read_bytes())
        result = boot.validate(self.dtb, extra, self.system, self.vendor, DISK_GUID)
        self.assertEqual(result["dedicated_userdata_partitions"], {
            "data": {"name": "j72s-data", "partition_uuid": str(DATA_GUID)},
            "metadata": {"name": "j72s-metadata", "partition_uuid": str(METADATA_GUID)},
        })
        self.assertFalse(result["hardware_tested"])

    def test_reports_sparse_input_separately_from_raw_ext4(self):
        sparse = self.root / "sparse-vendor.img"
        sparse.write_bytes(b"\x3a\xff\x26\xed")
        with self.assertRaisesRegex(ValueError, "vendor is Android-sparse"):
            boot.validate(self.dtb, self.media, self.system, sparse, DISK_GUID)

    def test_refuses_different_disk_or_altered_partition(self):
        with self.assertRaisesRegex(ValueError, "specified dedicated disk GUID"):
            self.validate(VENDOR_GUID)
        with self.media.open("r+b") as media:
            media.seek(VENDOR_START * SECTOR)
            media.write(b"changed content")
        with self.assertRaisesRegex(ValueError, "vendor partition does not match"):
            self.validate()

    def test_rejects_the_wrong_board_and_dtb_boot_overrides(self):
        self.write_dtb("apple,j71s")
        with self.assertRaisesRegex(ValueError, "Expected a J72s device tree"):
            self.validate()
        self.write_dtb("apple,j72s", 'chosen { bootargs = "root=other"; };')
        with self.assertRaisesRegex(ValueError, "Unexpected DTB"):
            self.validate()

    def test_refuses_symlinks_and_other_partition_names(self):
        link = self.root / "media-link.img"
        link.symlink_to(self.media)
        with self.assertRaisesRegex(ValueError, "Expected a regular file"):
            boot.validate(self.dtb, link, self.system, self.vendor, DISK_GUID)
        other = self.root / "other.img"
        write_gpt_fixture(other, [
            ("ios", SYSTEM_GUID, SYSTEM_START), ("j72s-vendor", VENDOR_GUID, VENDOR_START),
        ])
        with self.assertRaisesRegex(ValueError, "only dedicated J72s Android partition names"):
            boot.validate(self.dtb, other, self.system, self.vendor, DISK_GUID)


if __name__ == "__main__":
    unittest.main()
