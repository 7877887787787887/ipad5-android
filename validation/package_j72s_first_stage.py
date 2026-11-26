#!/usr/bin/env python3
"""Package checked J72s first-stage artifacts for an isolated read-only mount test."""

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath
from uuid import UUID

from check_j72s_boot_media import PARTITIONS, inspect, regular_file, validate


MAX_RAMDISK_BYTES = 32 * 1024 * 1024
REQUIRED_CONFIG = ("CONFIG_RD_GZIP=y", "CONFIG_BLK_DEV_DM=y", "CONFIG_SECURITY_SELINUX=y")
INIT_DIRECTORIES = ("dev", "proc", "sys", "mnt", "debug_ramdisk", "second_stage_resources", "system")
KERNEL_ARTIFACTS = ("Image.gz", "s8000-j72s.dtb", "candidate.config", "modules.tar.gz")
STAGE_STORAGE = Path(__file__).resolve().parents[1] / "scripts/stage_j72s_early_storage.py"
USERDATA_CONFIG = ("CONFIG_FS_ENCRYPTION=y", "CONFIG_DM_DEFAULT_KEY=y",
                   "CONFIG_BLK_INLINE_ENCRYPTION=y", "CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK=y")


def checksums(path, expected):
    matches = {}
    with regular_file(path) as source:
        for line in source.read().decode("ascii").splitlines():
            digest, name = line.split(maxsplit=1)
            if name in expected:
                if name in matches or not re.fullmatch(r"[0-9a-f]{64}", digest):
                    raise ValueError(f"Invalid or duplicate checksum for {name}")
                matches[name] = digest
    if matches.keys() != set(expected):
        raise ValueError(f"Missing checksums in {path}")
    return matches


def copy_verified(source_path, digest, destination=None):
    with regular_file(source_path) as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != digest:
            raise ValueError(f"Checksum mismatch: {source_path}")
        if destination is not None:
            source.seek(0)
            with destination.open("xb") as target:
                shutil.copyfileobj(source, target)


def verify_init(source):
    with regular_file(source) as image:
        if os.fstat(image.fileno()).st_size > MAX_RAMDISK_BYTES:
            raise ValueError("First-stage init exceeds the diagnostic ramdisk size budget")
        elf = inspect(["readelf", "-h", "-l", "-d", "-W"], image).stdout
        if not (re.search(r"Class:\s+ELF64", elf) and re.search(r"Machine:\s+AArch64", elf)
                and re.search(r"Type:\s+(EXEC|DYN)\b", elf)):
            raise ValueError("Expected an executable ARM64 first-stage init")
        if re.search(r"^\s*INTERP\s|\(NEEDED\)", elf, re.MULTILINE):
            raise ValueError("Expected statically linked first-stage init")
        segments = [line.split() for line in elf.splitlines() if line.strip().startswith("LOAD ")]
        if not segments or any(int(row[-1], 16) < 16384
                               or (int(row[1], 16) - int(row[2], 16)) % 16384
                               for row in segments):
            raise ValueError("First-stage init LOAD segments must support 16 KiB pages")
        if b"init first stage started!" not in image.read():
            raise ValueError("Expected the Android first-stage init startup marker")


def verify_clean_ext4(path, device_role):
    # fs_mgr can try a writable recovery mount before the final read-only mount.
    with regular_file(path) as source:
        source.seek(1024)
        sb = source.read(1024)
        if len(sb) != 1024 or sb[56:58] != b"\x53\xef":
            raise ValueError(f"Expected a raw ext4 image for {device_role}")
        state = struct.unpack_from("<H", sb, 58)[0]
        incompatible, ro_compatible = struct.unpack_from("<II", sb, 96)
        last_orphan = struct.unpack_from("<I", sb, 232)[0]
        recovery = incompatible & 0x4
        quota_or_orphans = ro_compatible & (0x100 | 0x10000)
        if state != 1 or recovery or quota_or_orphans or last_orphan:
            raise ValueError(f"{device_role} filesystem requires repair, quota tuning or orphan cleanup")
        paths = ("/system/etc/fstab.j72s", "/odm/etc/fstab.j72s") if device_role == "system" else (
            "/etc/fstab.j72s", "/odm/etc/fstab.j72s")
        for selected_fstab in paths:
            result = inspect(["debugfs", "-R", f"stat {selected_fstab}"], source)
            if "Inode:" in result.stdout or "File not found" not in result.stderr:
                raise ValueError(f"Possible second-stage fstab override in {device_role}: {selected_fstab}")


def visible_keymint_manifest(vendor_path):
    with regular_file(vendor_path) as vendor:
        manifests = ["/etc/vintf/manifest.xml", "/odm/etc/vintf/manifest.xml", "/manifest.xml"]
        for directory in ("/etc/vintf/manifest", "/odm/etc/vintf/manifest"):
            listing = inspect(["debugfs", "-R", f"ls -p {directory}"], vendor).stdout
            for line in listing.splitlines():
                columns = line.split("/")
                if len(columns) > 5 and re.fullmatch(r"[0-9A-Za-z_.-]+\.xml", columns[5]):
                    manifests.append(f"{directory}/{columns[5]}")
        for path in manifests:
            raw = inspect(["debugfs", "-R", f"cat {path}"], vendor).stdout
            if not raw:
                continue
            try:
                document = ET.fromstring(raw)
            except ET.ParseError:
                continue
            for hal in document.findall("./hal"):
                if (hal.get("format") != "aidl"
                        or hal.findtext("name") != "android.hardware.security.keymint"):
                    continue
                if (hal.find("fqname[.='IKeyMintDevice/default']") is not None
                        or hal.find("interface[name='IKeyMintDevice']/instance[.='default']") is not None):
                    return path
    return None


def userdata_preflight(kernel_config, vendor, media):
    partitions = media["dedicated_userdata_partitions"]
    return {
        "required_kernel_options_missing": [option for option in USERDATA_CONFIG
                                            if option not in kernel_config],
        "missing_dedicated_partitions": [role for role in ("data", "metadata") if role not in partitions],
        "visible_vendor_keymint_default_manifest": visible_keymint_manifest(vendor),
        "late_fs_mount_configured": False,
        "encryption_on_target_verified": False,
    }


def verify_storage_overlay(archive_path, ramdisk):
    expected = {p.relative_to(ramdisk).as_posix(): p for p in ramdisk.rglob("*")}
    found = set()
    root_seen = False
    with regular_file(archive_path) as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
        source.seek(0)
        with tarfile.open(fileobj=source, mode="r:gz") as archive:
            for member in archive:
                if member.name in (".", "./") and member.isdir() and not root_seen:
                    root_seen = True
                    continue
                name = PurePosixPath(member.name).as_posix()
                path = expected.get(name)
                if path is None or name in found:
                    raise ValueError(f"Unexpected or duplicate storage overlay entry: {member.name}")
                if member.isdir() and path.is_dir():
                    found.add(name)
                    continue
                if not member.isfile() or not path.is_file() or member.size != path.stat().st_size:
                    raise ValueError(f"Invalid storage overlay entry: {member.name}")
                with archive.extractfile(member) as data:
                    if data.read() != path.read_bytes():
                        raise ValueError(f"Storage overlay differs from this kernel: {member.name}")
                found.add(name)
    if found != expected.keys():
        raise ValueError("The storage overlay is missing matching kernel files")
    return digest


def add_newc_entry(archive, inode, name, mode, length, file_stream):
    path = name.encode("ascii") + b"\0"
    fields = (inode, mode, 0, 0, 1, 0, length, 0, 0, 0, 0, len(path), 0)
    header = b"070701" + "".join(f"{field:08x}" for field in fields).encode("ascii")
    archive.write(header + path)
    archive.write(b"\0" * (-(len(header) + len(path)) % 4))
    shutil.copyfileobj(file_stream, archive)
    archive.write(b"\0" * (-length % 4))


def write_ramdisk(root, target):
    entries = sorted(root.rglob("*"))
    total = sum(path.stat().st_size for path in entries if path.is_file())
    if total > MAX_RAMDISK_BYTES:
        raise ValueError("The diagnostic ramdisk exceeds its 32 MiB unpacked file budget")
    with target.open("xb") as output, gzip.GzipFile(filename="", fileobj=output, mode="wb", mtime=0) as gz:
        for inode, path in enumerate(entries, start=1):
            name = path.relative_to(root).as_posix()
            if path.is_dir():
                add_newc_entry(gz, inode, name, stat.S_IFDIR | 0o755, 0, io.BytesIO())
            else:
                mode = 0o750 if name == "init" else 0o644
                with path.open("rb") as source:
                    add_newc_entry(gz, inode, name, stat.S_IFREG | mode, path.stat().st_size, source)
        add_newc_entry(gz, len(entries) + 1, "TRAILER!!!", 0, 0, io.BytesIO())


def package(args):
    output = args.output.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError(f"Use a new output directory: {output}")
    kernel_hashes = checksums(args.kernel / "SHA256SUMS", KERNEL_ARTIFACTS)
    init_sha = checksums(args.init.parent / "SHA256SUMS", (args.init.name,))[args.init.name]
    with tempfile.TemporaryDirectory(prefix=".j72s-first-stage-", dir=output.parent) as temp:
        temp = Path(temp)
        kernel = temp / "kernel"
        kernel.mkdir()
        for name, digest in kernel_hashes.items():
            copy_verified(args.kernel / name, digest, None if name == "Image.gz" else kernel / name)
        for name in ("kernel-release.txt", "SHA256SUMS"):
            with regular_file(args.kernel / name) as source:
                (kernel / name).write_bytes(source.read())
        config = (kernel / "candidate.config").read_text(encoding="utf-8").splitlines()
        if not set(REQUIRED_CONFIG).issubset(config):
            raise ValueError("Missing candidate kernel gzip, device-mapper or SELinux support")

        media = validate(kernel / "s8000-j72s.dtb", args.media_image, args.system, args.vendor,
                         args.disk_guid)
        for role in ("system", "vendor"):
            verify_clean_ext4(getattr(args, role), role)

        root = temp / "ramdisk"
        subprocess.run([sys.executable, "-B", str(STAGE_STORAGE), str(kernel), str(root)],
                       check=True, capture_output=True, text=True)
        media["storage_overlay_sha256"] = verify_storage_overlay(args.early_storage, root)
        copy_verified(args.init, init_sha, root / "init")
        verify_init(root / "init")
        for path in INIT_DIRECTORIES:
            (root / path).mkdir()
        fstab = "".join(f"/dev/block/by-name/{PARTITIONS[role]} /{role} ext4 "
                        f"ro,noload wait,first_stage_mount\n" for role in ("system", "vendor"))
        (root / "fstab.j72s").write_text(fstab, encoding="ascii")

        result = temp / "result"
        result.mkdir()
        archive = result / "first-stage-initramfs.cpio.gz"
        write_ramdisk(root, archive)
        (result / "fstab.j72s").write_text(fstab, encoding="ascii")
        (result / "kernel-args.txt").write_text(
            f"androidboot.boot_part_uuid={media['candidate_boot_part_uuid']} "
            "androidboot.fstab_suffix=j72s androidboot.hardware=j72s\n", encoding="ascii")
        media.update({
            "scope": "first-stage read-only diagnostic, Android UI unverified",
            "init_sha256": init_sha,
            "kernel_artifacts": kernel_hashes,
            "runtime_requires_verified_read_only_media": True,
            "data_mount_configured": False,
            "encrypted_userdata_preflight": userdata_preflight(config, args.vendor, media),
        })
        (result / "first-stage.json").write_text(json.dumps(media, indent=2) + "\n", encoding="utf-8")
        hashes = []
        for path in sorted(result.iterdir()):
            with path.open("rb") as source:
                digest = hashlib.file_digest(source, "sha256").hexdigest()
            hashes.append(f"{digest}  {path.name}\n")
        (result / "SHA256SUMS").write_text("".join(hashes), encoding="ascii")
        result.rename(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path, help="Candidate kernel artifact directory")
    parser.add_argument("--init", required=True, type=Path, help="Android first-stage init with sibling SHA256SUMS")
    parser.add_argument("--early-storage", required=True, type=Path, help="Matching storage overlay .tar.gz")
    parser.add_argument("--media-image", required=True, type=Path, help="Dedicated regular-file GPT image")
    parser.add_argument("--disk-guid", required=True, type=UUID)
    parser.add_argument("--system", required=True, type=Path, help="Raw ARM64 Android 16 GSI")
    parser.add_argument("--vendor", required=True, type=Path, help="Raw J72s vendor image")
    parser.add_argument("--output", required=True, type=Path, help="New first-stage artifact output directory")
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        parser.exit(1, f"{error}\n")
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"{error}: {error.stderr.strip()}\n")
    print(f"Created first-stage read-only diagnostic files in {args.output}")


if __name__ == "__main__":
    main()
