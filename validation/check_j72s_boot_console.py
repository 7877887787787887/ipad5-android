#!/usr/bin/env python3
"""Read-only console preflight for a J72s kernel config, DTB and optional command line."""

import argparse
import json
import shlex
import subprocess
from pathlib import Path


PANEL_FLAGS = (
    "PRINTK", "TTY", "VT", "VT_CONSOLE", "FRAMEBUFFER_CONSOLE",
    "DRM_SIMPLEDRM", "DRM_APPLE_ADBE", "DRM_FBDEV_EMULATION",
    "DRM_CLIENT_SETUP", "DRM_CLIENT_DEFAULT_FBDEV",
)
PSTORE_FLAGS = ("PSTORE", "PSTORE_RAM", "PSTORE_CONSOLE")


class DeviceTree:
    def __init__(self, path):
        self.path = Path(path).resolve()

    def _get(self, *args):
        return subprocess.run(
            ["fdtget", *args], check=True, capture_output=True, text=True
        ).stdout.strip()

    def strings(self, node, key):
        return self._get("-t", "s", "-d", "", str(self.path), node, key)

    def cells(self, node, key):
        return [int(cell, 16) for cell in self._get("-t", "x", "-d", "", str(self.path), node, key).split()]

    def children(self, node):
        return self._get("-l", str(self.path), node).splitlines()

    def enabled(self, node):
        return self.strings(node, "status") in {"", "ok", "okay"}

    def reg_region(self, node):
        parent = node.rsplit("/", 1)[0] or "/"
        address_cells = (self.cells(parent, "#address-cells") or [2])[0]
        size_cells = (self.cells(parent, "#size-cells") or [1])[0]
        reg = self.cells(node, "reg")
        if address_cells not in {1, 2} or size_cells not in {1, 2} or len(reg) < address_cells + size_cells:
            return None

        def combine(cells):
            return int.from_bytes(b"".join(n.to_bytes(4, "big") for n in cells), "big")

        return combine(reg[:address_cells]), combine(reg[address_cells:address_cells + size_cells])


def read_config(path):
    return dict(
        line.split("=", 1)
        for line in Path(path).read_text(encoding="utf-8").splitlines()
        if line.startswith("CONFIG_") and "=" in line
    )


def missing(config, flags):
    return [f"CONFIG_{name}=y" for name in flags if config.get("CONFIG_" + name) != "y"]


def framebuffer_report(tree, root_children, reserved):
    candidates = [
        "/chosen/" + child for child in (tree.children("/chosen") if "chosen" in root_children else [])
        if "simple-framebuffer" in tree.strings("/chosen/" + child, "compatible").split()
    ]
    node = next((node for node in candidates if tree.enabled(node)), None)
    if not node:
        reason = "The simple framebuffer is disabled." if candidates else "No /chosen simple framebuffer."
        return {"node": candidates[0] if candidates else None, "valid": False, "issue": reason}

    dimensions = {
        name: (tree.cells(node, name) or [0])[0]
        for name in ("width", "height", "stride")
    }
    width, height, stride = (dimensions[key] for key in ("width", "height", "stride"))
    pixel_format = tree.strings(node, "format")
    handle = tree.cells(node, "memory-region")[:1]
    region_node = next((
        region for region in reserved
        if handle and handle in (tree.cells(region, "phandle"), tree.cells(region, "linux,phandle"))
        and tree.enabled(region)
    ), None)
    region = tree.reg_region(region_node) if region_node else None
    region = region or tree.reg_region(node)
    valid = (
        32 <= width <= 4096 and 32 <= height <= 4096
        and width * 4 <= stride <= 32768 and stride % 64 == 0
        and pixel_format == "x8r8g8b8" and region is not None
        and region[0] > 0 and region[1] >= stride * height
    )
    return {
        "node": node, "valid": valid, **dimensions, "format": pixel_format,
        "region_size": region[1] if region else None,
        "issue": None if valid else "The enabled framebuffer lacks a usable ADBE scanout description.",
    }


def kernel_console_report(config, command_line):
    args = shlex.split(command_line)
    if "--" in args:
        args = args[:args.index("--")]
    consoles = [arg.split("=", 1)[1].split(",")[0] for arg in args if arg.startswith("console=")]
    first_vt = next((name for name in consoles if name.startswith("tty") and name[3:].isdigit()), "")
    level = int(config.get("CONFIG_CONSOLE_LOGLEVEL_DEFAULT", "4"))
    for arg in args:
        if arg == "quiet":
            level = int(config.get("CONFIG_CONSOLE_LOGLEVEL_QUIET", "4"))
        elif arg == "debug":
            level = 10
        elif arg.startswith("loglevel=") and arg[9:].isdigit():
            level = int(arg[9:])
    return {
        "consoles": consoles, "tty0_selected": first_vt == "tty0" and "null" not in consoles,
        "console_loglevel": level, "info_visible": "ignore_loglevel" in args or level >= 7,
    }


def inspect(config, tree, command_line=None):
    if "apple,j72s" not in tree.strings("/", "compatible").split():
        raise ValueError("Expected a DTB with apple,j72s in the root compatible list")
    children = tree.children("/")
    reserved = [
        "/reserved-memory/" + node
        for node in (tree.children("/reserved-memory") if "reserved-memory" in children else [])
    ]

    framebuffer = framebuffer_report(tree, children, reserved)
    flags_missing = missing(config, PANEL_FLAGS)
    display = "/soc/display-controller@206200000"
    display_enabled = (
        "apple,s8000-display-pipe" in tree.strings(display, "compatible").split()
        and tree.enabled(display)
    )
    configured_args = config.get("CONFIG_CMDLINE", "").strip('"')
    if command_line is None:
        command_line = tree.strings("/chosen", "bootargs")
        if config.get("CONFIG_CMDLINE_EXTEND") == "y":
            command_line += " " + configured_args
        elif not command_line:
            command_line = configured_args
    if config.get("CONFIG_CMDLINE_FORCE") == "y":
        command_line = configured_args
    console = kernel_console_report(config, command_line)
    ready = not flags_missing and display_enabled and framebuffer["valid"] and (
        console["tty0_selected"] and console["info_visible"]
    )

    stdout = tree.strings("/chosen", "stdout-path")
    stdout_name = stdout.split(":", 1)[0]
    uart = stdout_name if stdout_name.startswith("/") else (
        tree.strings("/aliases", stdout_name) if stdout_name else ""
    )
    serial_config = not missing(config, ("PRINTK", "TTY", "SERIAL_SAMSUNG", "SERIAL_SAMSUNG_CONSOLE"))
    serial_node = (
        bool(uart)
        and "apple,s5l-uart" in tree.strings(uart, "compatible").split()
        and tree.enabled(uart)
    )
    ramoops = {
        node: (tree.cells(node, "console-size") or [0])[0]
        for node in reserved
        if "ramoops" in tree.strings(node, "compatible").split() and tree.enabled(node)
    }
    pstore_console = False
    for node, console_size in ramoops.items():
        region = tree.reg_region(node)
        reserved_sizes = [
            (tree.cells(node, key) or [0])[0]
            for key in ("record-size", "ftrace-size", "pmsg-size")
        ]
        if region and region[0] > 0 and console_size > 0 and console_size + sum(reserved_sizes) <= region[1]:
            pstore_console = True
    return {
        "board": "apple,j72s",
        "panel": {
            "preflight_passed": ready, "missing_kernel_options": flags_missing,
            "display_node_enabled": display_enabled, "framebuffer": framebuffer, **console,
        },
        "uart": {"stdout_path": stdout, "node": uart, "configured_in_artifacts": serial_config and serial_node},
        "pstore": {
            "missing_kernel_options": missing(config, PSTORE_FLAGS),
            "ramoops_nodes": ramoops,
            "console_region_configured": not missing(config, PSTORE_FLAGS) and pstore_console,
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-config", required=True, type=Path)
    parser.add_argument("--dtb", required=True, type=Path)
    parser.add_argument("--kernel-args", type=Path, help="Optional effective command-line text file")
    args = parser.parse_args()
    try:
        report = inspect(
            read_config(args.kernel_config), DeviceTree(args.dtb),
            args.kernel_args.read_text(encoding="utf-8") if args.kernel_args else None,
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"Cannot inspect J72s console artifacts: {error}\n")
    print(json.dumps(report, indent=2))
    return 0 if report["panel"]["preflight_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
