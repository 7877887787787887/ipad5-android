#!/usr/bin/env python3
"""Verify and name local J72s firmware from Orchard's hash-addressed cache."""

import argparse
import shutil
import stat
import tempfile
from pathlib import Path

from j72s_wireless import EXPECTED as WIRELESS_FIRMWARE, check_file, is_reparse_point


# Apple coprocessor outputs in Orchard v0.3.8 devices/j72s/firmware.json.
APPLE_FIRMWARE = {
    "apple/j72s-fwstart.z2fw": "d1ba8bc78e4bf67db163db426f11d22323057705aa66954b8785e9a57495da27",
    "apple/gfx-s8003.bin": "33ae20cbbe2498e4f119ba4e9a55467e4e0192d4c86d5bf1903adce63ffa5dca",
    "apple/s8000-smartio.bin": "da53095ed3364689ffd56f6576f036e4741a98836147a181c335626389ef5a36",
    "apple/aop-ipad6f.bin": "af5d51a368ceb89dc5c359e7c84e54cb6082e308956be742c17622eb4a6047c8",
    "apple/aop-ipad6f-ext.bin": "2528d2686ed1262de9910c2d60e45b68137e50aa601a0f8d54ea7d1347b9de22",
    "apple/vxd-deva.bin": "bf5167652e81540471aaca141943fd33d71c3990c137f145471305f5f5ade83b",
}
FIRMWARE = {**APPLE_FIRMWARE, **WIRELESS_FIRMWARE}


def check_directory(path):
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or is_reparse_point(info):
        raise ValueError(f"Expected a real directory: {path}")


def require_verified(path, digest, label):
    result = check_file(path, digest)
    if result["status"] != "verified":
        raise ValueError(f"{label}: {result['status']} (expected SHA-256 {digest})")


def validate(firmware_root, expected=FIRMWARE):
    check_directory(firmware_root)
    directories = {entry.name: entry for entry in firmware_root.iterdir()}
    file_tables = {}
    for relative, digest in expected.items():
        group, name = relative.split("/", 1)
        if group not in file_tables:
            directory = directories.get(group)
            if directory is None:
                raise ValueError(f"{group}: missing directory")
            check_directory(directory)
            file_tables[group] = {entry.name: entry for entry in directory.iterdir()}
        require_verified(file_tables[group].get(name), digest, relative)


def stage(cache, destination, expected=FIRMWARE):
    check_directory(cache)
    if destination.exists() or destination.is_symlink():
        validate(destination, expected)
        return

    with tempfile.TemporaryDirectory(prefix=".j72s-firmware-", dir=destination.parent) as temp:
        staging = Path(temp) / "firmware"
        for relative, digest in expected.items():
            source = cache / digest
            require_verified(source, digest, relative)
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        validate(staging, expected)
        staging.rename(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    install = commands.add_parser("stage", help="Stage all firmware into a local build directory")
    install.add_argument("cache", type=Path, help="Orchard firmware/j72s/outputs directory")
    install.add_argument("destination", type=Path, help="Firmware directory under the local inputs directory")
    verify = commands.add_parser("validate", help="Verify all named inputs")
    verify.add_argument("root", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "stage":
            stage(args.cache, args.destination)
        else:
            validate(args.root)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Verified {len(FIRMWARE)} J72s firmware files")


if __name__ == "__main__":
    main()
