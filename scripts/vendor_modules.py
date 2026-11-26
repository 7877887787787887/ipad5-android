#!/usr/bin/env python3
"""Stage J72s vendor drivers with their kernel module dependencies."""
import argparse
import shutil
from pathlib import Path, PurePosixPath

DRIVERS = ("apple_z2", "powervr")
COPROCESSOR_DRIVERS = (
    "apple-sio-dma", "apple_aop_accel", "apple_aop_gyro", "apple_aop_pressure", "apple-vxd",
    "tsl2583",
)
AUDIO_DRIVERS = ("snd-soc-apple-s8000-mca", "snd-soc-cs42l81", "snd-soc-simple-card")
WIRELESS_DRIVERS = ("brcmfmac", "brcmfmac-wcc", "hci_uart")


def select_modules(root, names):
    root = root.resolve()
    graph = {}
    for line in (root / "modules.dep").read_text(encoding="utf-8").splitlines():
        module, colon, dependencies = line.partition(":")
        if not colon or module in graph:
            raise ValueError(f"Invalid modules.dep entry: {line}")
        graph[module] = dependencies.split()

    ordered = {}
    visiting = set()

    def visit(entry):
        path = PurePosixPath(entry)
        if path.is_absolute() or ".." in path.parts or path.suffix != ".ko":
            raise ValueError(f"Expected a relative, uncompressed module: {entry}")
        if path.name in ordered:
            if ordered[path.name] != root.joinpath(*path.parts):
                raise ValueError(f"Duplicate module basename: {path.name}")
            return
        if entry in visiting:
            raise ValueError(f"Cyclic module dependency: {entry}")
        file = root.joinpath(*path.parts)
        if entry not in graph or not file.is_file() or not file.resolve().is_relative_to(root):
            raise ValueError(f"Missing or invalid module: {entry}")
        visiting.add(entry)
        for dependency in graph[entry]:
            visit(dependency)
        visiting.remove(entry)
        ordered[path.name] = file

    for name in names:
        matches = [entry for entry in graph if PurePosixPath(entry).name == name + ".ko"]
        if len(matches) != 1:
            raise ValueError(f"Expected one {name}.ko in modules.dep, found {len(matches)}")
        visit(matches[0])
    return list(ordered.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module_tree", type=Path, help="Installed lib/modules/<kernel-release> directory")
    parser.add_argument("destination", type=Path, help="New directory for Android vendor modules")
    parser.add_argument("--coprocessors", action="store_true", help="Include SmartIO, board sensors and VXD")
    parser.add_argument("--audio", action="store_true", help="Include the J72s ALSA audio card")
    parser.add_argument("--wireless", action="store_true", help="Include the J72s Wi-Fi and Bluetooth drivers")
    args = parser.parse_args()
    roots = DRIVERS
    if args.coprocessors:
        roots += COPROCESSOR_DRIVERS
    if args.audio:
        roots += AUDIO_DRIVERS
    if args.wireless:
        roots += WIRELESS_DRIVERS
    try:
        modules = select_modules(args.module_tree, roots)
        args.destination.mkdir(parents=True)
        for module in modules:
            shutil.copyfile(module, args.destination / module.name)
            print(module.name)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
