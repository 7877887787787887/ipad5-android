#!/usr/bin/env python3
"""Verify a regular-file J72s boot-media image without mounting or writing it."""

import argparse
import hashlib
import json
import os
import stat
import subprocess
from contextlib import contextmanager
from pathlib import Path
from uuid import UUID


PARTITIONS = {"system": "j72s-system", "vendor": "j72s-vendor",
              "data": "j72s-data", "metadata": "j72s-metadata"}
CHUNK = 1024 * 1024


@contextmanager
def regular_file(path):
    # O_PATH opens the directory entry without opening a device at that path.
    descriptor = os.open(path, os.O_PATH | os.O_NOFOLLOW)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(f"Expected a regular file: {path}")
        with open(f"/proc/self/fd/{descriptor}", "rb") as source:
            yield source
    finally:
        os.close(descriptor)


def fd_path(source):
    return f"/proc/self/fd/{source.fileno()}"


def inspect(command, source, *, check=True):
    return subprocess.run([*command, fd_path(source)], check=check, capture_output=True,
                          pass_fds=(source.fileno(),), text=True)


def verify_dtb(source):
    path = fd_path(source)
    compatible = subprocess.run(["fdtget", "-t", "s", path, "/", "compatible"], check=True,
                                capture_output=True, pass_fds=(source.fileno(),), text=True)
    if compatible.stdout.split()[:1] != ["apple,j72s"]:
        raise ValueError("Expected a J72s device tree")
    for request in (["-p", path, "/firmware/android/fstab"],
                    ["-t", "s", path, "/chosen", "bootargs"]):
        property_result = subprocess.run(["fdtget", *request], capture_output=True,
                                         pass_fds=(source.fileno(),), text=True)
        if property_result.returncode == 0 or "FDT_ERR_NOTFOUND" not in property_result.stderr:
            raise ValueError("Unexpected DTB first-stage fstab or boot arguments")


def read_layout(media, disk_guid):
    verification = inspect(["sfdisk", "--verify"], media)
    if verification.stderr.strip():
        raise ValueError(f"GPT verification reported diagnostics: {verification.stderr.strip()}")
    table = json.loads(inspect(["sfdisk", "--json"], media).stdout)["partitiontable"]
    if table.get("label") != "gpt" or UUID(table["id"]) != disk_guid:
        raise ValueError("Expected GPT with the specified dedicated disk GUID")
    sector_size = table["sectorsize"]
    if sector_size not in (512, 4096):
        raise ValueError(f"Unexpected media sector size: {sector_size}")

    entries = {}
    uuids = set()
    end = table["firstlba"]
    media_sectors = os.fstat(media.fileno()).st_size // sector_size
    limit = min(media_sectors, table["lastlba"] + 1)
    for entry in sorted(table["partitions"], key=lambda part: part["start"]):
        name = entry.get("name")
        guid = UUID(entry["uuid"])
        if name not in PARTITIONS.values() or name in entries or guid in uuids or not guid.int:
            raise ValueError("Use unique GUIDs and only dedicated J72s Android partition names")
        if entry["start"] < end or entry["size"] <= 0 or entry["start"] + entry["size"] > limit:
            raise ValueError(f"Overlapping or out-of-range GPT extent: {name}")
        end = entry["start"] + entry["size"]
        entries[name] = (entry["start"] * sector_size, entry["size"] * sector_size, guid)
        uuids.add(guid)
    for name in ("system", "vendor"):
        if PARTITIONS[name] not in entries:
            raise ValueError(f"Missing dedicated J72s Android {name} partition")
    return entries


def source_properties(source, name):
    source.seek(0)
    if source.read(4) == b"\x3a\xff\x26\xed":
        raise ValueError(f"{name} is Android-sparse, expected the raw ext4 build artifact")
    source.seek(1080)
    if source.read(2) != b"\x53\xef":
        raise ValueError(f"Expected a raw ext filesystem image for {name}")
    prop_path = "/system/build.prop" if name == "system" else "/build.prop"
    data = inspect(["debugfs", "-R", f"cat {prop_path}"], source).stdout.splitlines()
    props = dict(line.split("=", 1) for line in data if "=" in line and not line.startswith("#"))
    if name == "system":
        if (props.get("ro.system.build.version.sdk") != "36"
                or "arm64-v8a" not in props.get("ro.system.product.cpu.abilist", "").split(",")):
            raise ValueError("Expected Android 16 ARM64 system image properties")
    elif props.get("ro.product.vendor.device") != "j72s-port":
        raise ValueError("Expected J72s Android vendor image properties")


def match_partition(media, extent, source, name):
    start, capacity, _ = extent
    size = os.fstat(source.fileno()).st_size
    if size <= 0 or size > capacity:
        raise ValueError(f"{name} image does not fit the GPT extent")
    source.seek(0)
    expected = hashlib.file_digest(source, "sha256").hexdigest()
    actual = hashlib.sha256()
    media.seek(start)
    remaining = size
    while remaining:
        chunk = media.read(min(remaining, CHUNK))
        if not chunk:
            raise ValueError(f"Unexpected end of {name} GPT extent")
        actual.update(chunk)
        remaining -= len(chunk)
    if actual.hexdigest() != expected:
        raise ValueError(f"{name} partition does not match the supplied image")
    return expected


def validate(dtb, media, system, vendor, disk_guid):
    with (regular_file(dtb) as device_tree, regular_file(media) as media_file,
          regular_file(system) as system_file, regular_file(vendor) as vendor_file):
        verify_dtb(device_tree)
        entries = read_layout(media_file, disk_guid)
        images = {}
        for name, source in (("system", system_file), ("vendor", vendor_file)):
            source_properties(source, name)
            extent = entries[PARTITIONS[name]]
            images[name] = {"sha256": match_partition(media_file, extent, source, name),
                            "partition_uuid": str(extent[2]), "name": PARTITIONS[name]}
    return {
        "board": "apple,j72s",
        "disk_guid": str(disk_guid),
        "gpt_and_images_verified_offline": True,
        "images": images,
        "candidate_boot_part_uuid": images["system"]["partition_uuid"],
        "dedicated_data_partition_present": PARTITIONS["data"] in entries,
        "dedicated_userdata_partitions": {
            role: {"name": PARTITIONS[role], "partition_uuid": str(entries[PARTITIONS[role]][2])}
            for role in ("data", "metadata") if PARTITIONS[role] in entries
        },
        "hardware_tested": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dtb", required=True, type=Path)
    parser.add_argument("--media-image", required=True, type=Path,
                        help="Regular-file GPT disk image, never /dev")
    parser.add_argument("--disk-guid", required=True, type=UUID,
                        help="Independently recorded GPT disk GUID for the dedicated Android media")
    parser.add_argument("--system", required=True, type=Path, help="Raw Android 16 ARM64 GSI")
    parser.add_argument("--vendor", required=True, type=Path, help="Raw J72s vendor image")
    args = parser.parse_args()
    try:
        result = validate(args.dtb, args.media_image, args.system, args.vendor, args.disk_guid)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
