#!/usr/bin/env python3
"""Stage iPad 5 block-storage modules for the Android first-stage ramdisk."""

import argparse
import hashlib
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

from vendor_modules import select_modules


BUILT_IN = (
    "ARM64_16K_PAGES", "BLK_DEV_INITRD", "USB_EHCI_HCD_PLATFORM", "USB_OHCI_HCD_PLATFORM",
    "USB_ROLES_APPLE_S8000", "EXT4_FS",
)
STORAGE = {"SCSI": "scsi_mod", "BLK_DEV_SD": "sd_mod", "USB_STORAGE": "usb-storage",
           "USB_UAS": "uas"}
NVME_BUILT_IN = (
    "ARM64_16K_PAGES", "BLK_DEV_INITRD", "PCI", "PCI_MSI", "PCIE_APPLE_H9P",
    "APPLE_DART", "IOMMU_IO_PGTABLE_DART", "EFI_PARTITION", "EXT4_FS",
)
# The J72s ANS uses the PCI NVMe host driver.
NVME_STORAGE = {"NVME_CORE": "nvme-core", "BLK_DEV_NVME": "nvme"}
PROFILES = {"usb": (BUILT_IN, STORAGE), "internal-nvme": (NVME_BUILT_IN, NVME_STORAGE)}


def verify_inputs(kernel):
    wanted = {"candidate.config", "modules.tar.gz"}
    checksums = {}
    for line in (kernel / "SHA256SUMS").read_text(encoding="ascii").splitlines():
        digest, name = line.split(maxsplit=1)
        if name not in wanted:
            continue
        if name in checksums or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"Invalid or duplicate SHA256SUMS entry for {name}")
        checksums[name] = digest
    if checksums.keys() != wanted:
        raise ValueError("SHA256SUMS must cover candidate.config and modules.tar.gz")
    for name, digest in checksums.items():
        with (kernel / name).open("rb") as artifact:
            if hashlib.file_digest(artifact, "sha256").hexdigest() != digest:
                raise ValueError(f"Kernel artifact checksum failed: {name}")


def module_roots(config, *, transport="usb"):
    values = dict(line.split("=", 1) for line in config.splitlines()
                  if line.startswith("CONFIG_") and "=" in line)
    built_in, storage = PROFILES[transport]
    for option in built_in:
        if values.get(f"CONFIG_{option}") != "y":
            raise ValueError(f"Early {transport} storage requires built-in CONFIG_{option}")
    roots = []
    for option, module in storage.items():
        value = values.get(f"CONFIG_{option}")
        if value not in {"y", "m"}:
            raise ValueError(f"Early {transport} storage requires CONFIG_{option}")
        if value == "m":
            roots.append(module)
    if transport == "internal-nvme" and roots == ["nvme-core"]:
        raise ValueError("Built-in PCI NVMe requires built-in CONFIG_NVME_CORE")
    return roots


def extract_module_inputs(archive, release, destination):
    prefix = ("lib", "modules", release)
    wanted = {"modules.dep", "modules.alias", "modules.softdep"}
    found = set()
    with tarfile.open(archive, "r:gz") as modules:
        for item in modules:
            parts = PurePosixPath(item.name).parts
            if parts[:3] != prefix or len(parts) < 4 or item.isdir():
                continue
            relative = PurePosixPath(*parts[3:])
            if relative.suffix != ".ko" and str(relative) not in wanted:
                continue
            if not item.isfile() or ".." in relative.parts or relative in found:
                raise ValueError(f"Invalid module archive path: {item.name}")
            path = destination / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            with modules.extractfile(item) as source, path.open("xb") as target:
                shutil.copyfileobj(source, target)
            found.add(relative)
    missing = wanted - {str(path) for path in found}
    if missing:
        raise ValueError(f"Module archive for {release} is missing {', '.join(sorted(missing))}")


def verify_module(path, release):
    with path.open("rb") as module:
        header = module.read(20)
    if (header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[18:20], "little") != 183):
        raise ValueError(f"Expected an ARM64 Linux module: {path.name}")
    result = subprocess.run(["modinfo", "-F", "vermagic", str(path)], check=True,
                            text=True, capture_output=True)
    if result.stdout.split()[:1] != [release]:
        raise ValueError(f"Kernel release mismatch in {path.name}: {result.stdout.strip()}")


def copy_modules(source, target, roots, release, *, transport="usb"):
    selected = select_modules(source, roots)
    relative_paths = [path.relative_to(source).as_posix() for path in selected]
    if transport == "internal-nvme":
        expected = [f"kernel/drivers/nvme/host/{module}.ko" for module in roots]
        if relative_paths != expected:
            raise ValueError("Unexpected J72s PCI NVMe module dependency closure")
    paths = set(relative_paths)
    names = {path.stem.replace("-", "_") for path in selected}
    softdep = (source / "modules.softdep").read_text(encoding="utf-8").splitlines()
    if any(line.split()[1].replace("-", "_") in names for line in softdep
           if line.startswith("softdep ")):
        raise ValueError("Selected early storage module has an unreviewed soft dependency")

    target.mkdir(parents=True)
    for module in selected:
        verify_module(module, release)
        output = target / module.relative_to(source)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(module, output)

    entries = (source / "modules.dep").read_text(encoding="utf-8").splitlines()
    deps = [line for line in entries if line.partition(":")[0] in paths]
    aliases = (source / "modules.alias").read_text(encoding="utf-8").splitlines()
    aliases = [line for line in aliases
               if line.startswith("alias ") and line.split()[-1].replace("-", "_") in names]
    metadata = {"modules.dep": deps, "modules.alias": aliases, "modules.softdep": [],
                "modules.load": [f"{root}.ko" for root in roots]}
    for name, lines in metadata.items():
        (target / name).write_text("".join(f"{line}\n" for line in lines), encoding="utf-8")
    return [path.name for path in selected]


def stage_storage(kernel, destination, *, transport="usb"):
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Use a new output directory: {destination}")
    verify_inputs(kernel)
    release = (kernel / "kernel-release.txt").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9][0-9A-Za-z._+-]*", release):
        raise ValueError(f"Unexpected kernel release: {release}")
    roots = module_roots((kernel / "candidate.config").read_text(encoding="utf-8"),
                         transport=transport)

    with tempfile.TemporaryDirectory(prefix=".j72s-early-", dir=destination.parent) as temp:
        temp = Path(temp).resolve()
        overlay = temp / "overlay"
        overlay.mkdir()
        selected = []
        if roots:
            source = temp / "source"
            extract_module_inputs(kernel / "modules.tar.gz", release, source)
            selected = copy_modules(source, overlay / "lib/modules" / f"{release}_16k",
                                    roots, release, transport=transport)
        overlay.rename(destination)
    return release, selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path, help="Verified J72s kernel build artifact directory")
    parser.add_argument("destination", type=Path, help="New first-stage ramdisk overlay directory")
    parser.add_argument("--transport", choices=PROFILES, default="usb",
                        help="Block storage transport (default: %(default)s)")
    args = parser.parse_args()
    try:
        release, selected = stage_storage(args.kernel, args.destination.absolute(),
                                          transport=args.transport)
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
    name = "USB" if args.transport == "usb" else "internal NVMe"
    print(f"Staged iPad 5 {release} early {name} storage: {', '.join(selected) or 'built-in drivers'}")


if __name__ == "__main__":
    main()
