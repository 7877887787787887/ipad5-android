#!/usr/bin/env python3
"""Report J72s sensor build evidence from a compiled DTB, .config, and modules.tar.gz."""
import argparse
import json
import re
import subprocess
import tarfile
from pathlib import Path


class DeviceTree:
    def __init__(self, path):
        self.path = path

    def get(self, node, prop, kind="s", optional=False):
        options = ["-t", kind]
        if optional:
            options += ["-d", ""]
        result = subprocess.run(
            ["fdtget", *options, str(self.path), node, prop],
            capture_output=True, text=True,
        )
        if result.returncode:
            raise ValueError(f"fdtget {node} {prop}: {result.stderr.strip()}")
        return result.stdout.strip()

    def enabled(self, node):
        while node:
            if self.get(node, "status", optional=True) not in ("", "okay", "ok"):
                return False
            node = node.rpartition("/")[0]
        return self.get("/", "status", optional=True) in ("", "okay", "ok")


def read_config(path):
    config = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if match := re.fullmatch(r"(CONFIG_\w+)=(.*)", line):
            config[match[1]] = match[2]
        elif match := re.fullmatch(r"# (CONFIG_\w+) is not set", line):
            config[match[1]] = "n"
    if not config:
        raise ValueError(f"{path}: no Kconfig entries found")
    return config


def read_modules(path):
    with tarfile.open(path, "r:gz") as archive:
        members = {
            member.name.removeprefix("./"): member
            for member in archive if member.isfile()
        }
        depfiles = [
            name for name in members
            if re.fullmatch(r"lib/modules/[^/]+/modules\.dep", name)
        ]
        if len(depfiles) != 1:
            raise ValueError(f"{path}: expected one lib/modules/<release>/modules.dep")
        root = depfiles[0].rsplit("/", 1)[0]

        def lines(name):
            member = members.get(f"{root}/{name}")
            if member is None:
                raise ValueError(f"{path}: missing {root}/{name}")
            with archive.extractfile(member) as stream:
                return stream.read().decode("utf-8").splitlines()

        files = {name.removeprefix(root + "/") for name in members
                 if name.startswith(root + "/")}
        aliases = set(lines("modules.alias"))
        dependencies = {}
        for line in lines("modules.dep"):
            module, separator, deps = line.partition(":")
            if not separator:
                raise ValueError(f"{path}: invalid modules.dep entry: {line}")
            dependencies[module] = deps.split()
    return root.rsplit("/", 1)[1], files, aliases, dependencies


def audit(dtb, config_path, modules_path):
    tree = DeviceTree(dtb)
    board = tree.get("/", "compatible").split()
    if "apple,j72s" not in board:
        raise ValueError(f"expected apple,j72s in root compatible, found {board}")
    config = read_config(config_path)
    release, module_files, aliases, dependencies = read_modules(modules_path)

    def driver(node, compatible, symbol, module):
        compatibles = tree.get(node, "compatible").split()
        if compatible not in compatibles:
            raise ValueError(f"{node}: expected {compatible}, found {compatibles}")
        module_name = Path(module).stem.replace("-", "_")
        driver_deps = dependencies.get(module)
        return {
            "node": node,
            "compatible": compatibles,
            "dt_enabled": tree.enabled(node),
            "config_symbol": symbol,
            "config": config.get(symbol),
            "module": module,
            "module_present": module in module_files,
            "module_dependencies": driver_deps,
            "missing_dependency_files": [dep for dep in driver_deps or [] if dep not in module_files],
            "of_alias_matches": [
                value for value in compatibles
                if f"alias of:N*T*C{value} {module_name}" in aliases
            ],
        }

    aop_path = tree.get("/aliases", "aop")
    aop = driver(aop_path, "apple,s8000-aop", "CONFIG_APPLE_AOP_T8010",
                 "kernel/drivers/soc/apple/apple-aop.ko")
    aop["firmware"] = tree.get(aop_path, "firmware-name", optional=True).split()
    mailbox = f"{aop_path.rsplit('/', 1)[0]}/mbox@210804000"
    phandle = tree.get(mailbox, "phandle", "x")
    mboxes = tree.get(aop_path, "mboxes", "x").split()
    aop["mailbox"] = {
        "node": mailbox,
        "dt_enabled": tree.enabled(mailbox),
        "phandle_matches": bool(mboxes) and mboxes[0] == phandle,
    }

    als = tree.get("/aliases", "als0")
    light = driver(als, "apple,ct819", "CONFIG_TSL2583", "kernel/drivers/iio/light/tsl2583.ko")
    light["i2c_address"] = "0x" + tree.get(als, "reg", "x")
    sensors = {"ambient_light": light}
    for name, child, category, symbol in (
        ("accelerometer", "accel", "accel", "CONFIG_APPLE_AOP_ACCEL"),
        ("gyroscope", "gyro", "gyro", "CONFIG_APPLE_AOP_GYRO"),
        ("pressure", "pressure", "pressure", "CONFIG_APPLE_AOP_PRESSURE"),
    ):
        node = f"{aop_path}/{child}"
        sensor = driver(node, f"apple,s8000-aop-{child}", symbol,
                        f"kernel/drivers/iio/{category}/apple_aop_{child}.ko")
        if child in ("accel", "gyro"):
            sensor["mount_matrix"] = tree.get(node, "mount-matrix", optional=True).split()
        sensors[name] = sensor

    return {
        "board_compatible": board,
        "kernel_release": release,
        "aop": aop,
        "sensors": sensors,
        "sensor_config": {symbol: config.get(symbol) for symbol in (
            "CONFIG_IIO", "CONFIG_IIO_BUFFER", "CONFIG_IIO_KFIFO_BUF",
            "CONFIG_I2C_APPLE", "CONFIG_APPLE_MAILBOX", "CONFIG_APPLE_RTKIT",
        )},
        "media_config": {symbol: config.get(symbol) for symbol in (
            "CONFIG_MEDIA_SUPPORT", "CONFIG_MEDIA_CAMERA_SUPPORT", "CONFIG_MEDIA_CONTROLLER",
            "CONFIG_VIDEO_DEV", "CONFIG_VIDEO_V4L2_SUBDEV_API", "CONFIG_MEDIA_USB_SUPPORT",
        )},
        "video_decoder": driver("/soc/video-codec@207f00000", "apple,s8000-vxd",
                                "CONFIG_VIDEO_APPLE_VXD",
                                "kernel/drivers/media/platform/apple/vxd/apple-vxd.ko"),
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, epilog="Requires fdtget (device-tree-compiler). Reads build files only."
    )
    parser.add_argument("dtb", type=Path, help="compiled s8000-j72s.dtb")
    parser.add_argument("config", type=Path, help="kernel .config or candidate.config")
    parser.add_argument("modules", type=Path, help="matching build's modules.tar.gz")
    args = parser.parse_args()
    try:
        report = audit(args.dtb.resolve(), args.config, args.modules)
    except (OSError, ValueError, EOFError, tarfile.TarError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
