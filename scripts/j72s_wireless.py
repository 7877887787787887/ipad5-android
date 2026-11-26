#!/usr/bin/env python3
"""Inventory or verify local J72s Wi-Fi and Bluetooth firmware inputs.

Paths are relative to a firmware directory, such as lib/firmware in an
Orchard output tree. Validation checks the complete pinned recipe inventory,
including all NVRAM variants. Reports are written as JSON to stdout.
"""

import argparse
import hashlib
import json
from pathlib import Path
import stat


# Orchard v0.3.8 devices/j72s/firmware.json (iPad6,12, build 20H350).
ORCHARD_REVISION = "116004fb11f91cedc52db718ed5c6a6492d6577f"
WIFI_PREFIX = "brcm/brcmfmac4355-pcie.apple,fearb"
EXPECTED = {
    WIFI_PREFIX + ".bin": "e695aabcf313eddac486a1468a53382c44ef6fcae424f77598a4a79c90393d54",
    WIFI_PREFIX + ".clm_blob": "18a84c9ffdfe50e52f23a661a7e855a1aa114506364134eb8d511c1cb8aef3fb",
    WIFI_PREFIX + ".txcap_blob": "7d036b6b063bcbf2643f689dd97f9949ebce06926dc5d05e4811e0f738c3aa1f",
    **{
        f"{WIFI_PREFIX}-YSBU-m-{revision}.txt":
            "c570c2155119f4d01175839943158d92dffb2c1783dbfbaaec8f2b86cce8eaaa"
        for revision in ("1.1", "2.1", "2.3", "3.1")
    },
    **{
        f"{WIFI_PREFIX}-YSBU-u-{revision}.txt":
            "265750811a782ebbb5c82495a32174a122602fb71e6452cb615158bb8b299789"
        for revision in ("1.1", "2.1", "3.1", "3.3", "4.1")
    },
    "brcm/BCM.apple,j72s.hcd": "85db6624fe5e9fe63a5dc0a5244be8326e2e861e974d519e79b8c34b8e7dc822",
}


def is_reparse_point(file_stat):
    # Windows directory junctions and other reparse points can redirect reads.
    return bool(getattr(file_stat, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def check_file(path, expected):
    if path is None:
        return {"status": "missing"}
    try:
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or is_reparse_point(info):
            return {"status": "invalid_type"}
        if info.st_size == 0:
            return {"status": "empty"}
        with path.open("rb") as stream:
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(64 * 1024), b""):
                digest.update(block)
        actual = digest.hexdigest()
    except FileNotFoundError:
        return {"status": "missing"}
    except OSError:
        return {"status": "unreadable"}
    return {
        "status": "verified" if actual == expected else "digest_mismatch",
        "actual_sha256": actual,
    }


def inventory(root=None):
    entries = {}
    if root is not None:
        # Enumerate exact names so case-insensitive hosts cannot mask a
        # filename that would be missing on Android.
        brcm = next((entry for entry in root.iterdir() if entry.name == "brcm"), None)
        if brcm is not None:
            info = brcm.lstat()
            if not stat.S_ISDIR(info.st_mode) or is_reparse_point(info):
                raise ValueError("brcm must be a real directory under the firmware root")
            entries = {f"brcm/{entry.name}": entry for entry in brcm.iterdir()}
    rows = []
    for path, digest in sorted(EXPECTED.items()):
        row = {"path": path, "sha256": digest}
        if root is not None:
            row.update(check_file(entries.get(path), digest))
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("inventory", help="List the pinned firmware paths and digests")
    validate = commands.add_parser("validate", help="Verify all 13 recipe outputs")
    validate.add_argument("root", type=Path, help="Firmware directory containing brcm/")
    args = parser.parse_args()

    # Wireless identity from the pinned s8000-j72s device tree and its includes.
    report = {
        "orchard_release": "v0.3.8",
        "orchard_revision": ORCHARD_REVISION,
        "recipe": "devices/j72s/firmware.json",
        "recipe_build": "20H350",
        "product": "iPad6,12",
        "board": "apple,j72s",
        "dtb": "s8000-j72s.dtb",
        "wifi_compatible": "pci14e4,43dc",
        "wifi_board_type": "apple,fearb",
        "bluetooth_compatible": ["apple,j71t-bcm4355c0-bt", "brcm,bcm4355c0-bt"],
    }
    exit_code = 0
    try:
        validating = args.command == "validate"
        report["files"] = inventory(args.root if validating else None)
        if validating and any(row["status"] != "verified" for row in report["files"]):
            exit_code = 1
    except (OSError, ValueError) as error:
        report["error"] = str(error)
        exit_code = 1
    print(json.dumps(report, indent=2))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
