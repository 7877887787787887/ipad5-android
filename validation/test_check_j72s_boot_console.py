"""Artifact and command-line fixtures for the J72s console preflight."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from check_j72s_boot_console import DeviceTree, PANEL_FLAGS, PSTORE_FLAGS, inspect


@unittest.skipUnless(shutil.which("dtc") and shutil.which("fdtget"), "requires dtc and fdtget")
class FirstBootConsoleTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.config = {
            **{f"CONFIG_{name}": "y" for name in PANEL_FLAGS + PSTORE_FLAGS},
            "CONFIG_CONSOLE_LOGLEVEL_DEFAULT": "4",
            "CONFIG_SERIAL_SAMSUNG": "y",
            "CONFIG_SERIAL_SAMSUNG_CONSOLE": "y",
        }

    def tree(
        self, *, board="apple,j72s", status="okay", command="console=tty0 loglevel=7",
        bounds="<0x8 0x10000000 0 0xc00000>", storage="", use_reserved=False,
    ):
        source = self.root / "fixture.dts"
        output = self.root / "fixture.dtb"
        memory = (
            "memory-region = <&staged>;"
            if use_reserved else f"reg = {bounds};"
        )
        source.write_text(f"""
/dts-v1/;
/ {{
    compatible = "{board}", "apple,s8000";
    #address-cells = <2>;
    #size-cells = <2>;
    chosen {{
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        stdout-path = "serial0";
        bootargs = "{command}";
        framebuffer@810000000 {{
            compatible = "apple,simple-framebuffer", "simple-framebuffer";
            status = "{status}";
            width = <2048>;
            height = <1536>;
            stride = <8192>;
            format = "x8r8g8b8";
            {memory}
        }};
    }};
    aliases {{
        serial0 = "/soc/serial@20a0c0000";
    }};
    reserved-memory {{
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        {storage}
    }};
    soc {{
        compatible = "simple-bus";
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        display-controller@206200000 {{
            compatible = "apple,s8000-display-pipe";
            reg = <0x2 0x06200000 0 0x100000>;
        }};
        serial@20a0c0000 {{
            compatible = "apple,s5l-uart";
            reg = <0x2 0x0a0c0000 0 0x4000>;
        }};
    }};
}};
""", encoding="ascii")
        subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", output, source], check=True)
        return DeviceTree(output)

    def test_handoff_and_console_pass_panel_preflight(self):
        report = inspect(self.config, self.tree())
        self.assertTrue(report["panel"]["preflight_passed"])
        self.assertEqual(report["panel"]["framebuffer"]["region_size"], 8192 * 1536)
        self.assertTrue(report["uart"]["configured_in_artifacts"])
        self.assertFalse(report["pstore"]["console_region_configured"])

    def test_disabled_or_undersized_framebuffer_fails(self):
        for properties in ({"status": "disabled"}, {"bounds": "<0x8 0x10000000 0 0x1000>"}):
            with self.subTest(properties=properties):
                report = inspect(self.config, self.tree(**properties))
                self.assertFalse(report["panel"]["framebuffer"]["valid"])
                self.assertFalse(report["panel"]["preflight_passed"])

    def test_memory_region_handoff(self):
        report = inspect(self.config, self.tree(
            use_reserved=True,
            storage="staged: framebuffer-mem@810000000 {reg = <0x8 0x10000000 0 0xc00000>;};",
        ))
        self.assertTrue(report["panel"]["preflight_passed"])

    def test_default_quiet_and_init_argument_boundary(self):
        tree = self.tree()
        report = inspect(self.config, tree, "console=tty0")
        self.assertFalse(report["panel"]["info_visible"])
        for command in (
            "console=tty0 loglevel=7 quiet",
            "-- console=tty0 loglevel=7",
            "console=tty1 console=tty0 loglevel=7",
        ):
            with self.subTest(command=command):
                self.assertFalse(inspect(self.config, tree, command)["panel"]["preflight_passed"])
        self.assertTrue(inspect(self.config, tree, "console=tty0 quiet loglevel=7")["panel"]["preflight_passed"])

    def test_forced_kernel_arguments_override_supplied_arguments(self):
        self.config["CONFIG_CMDLINE_FORCE"] = "y"
        self.config["CONFIG_CMDLINE"] = '"console=ttySAC0 loglevel=7"'
        report = inspect(self.config, self.tree(), "console=tty0 loglevel=7")
        self.assertEqual(report["panel"]["consoles"], ["ttySAC0"])
        self.assertFalse(report["panel"]["preflight_passed"])

    def test_built_in_configuration_is_required(self):
        self.config["CONFIG_DRM_APPLE_ADBE"] = "m"
        report = inspect(self.config, self.tree())
        self.assertEqual(report["panel"]["missing_kernel_options"], ["CONFIG_DRM_APPLE_ADBE=y"])
        self.assertFalse(report["panel"]["preflight_passed"])

    def test_ramoops_requires_a_console_region(self):
        for sizes, expected in (
            ("console-size = <0x10000>; record-size = <0x10000>;", True),
            ("record-size = <0x10000>;", False),
            ("console-size = <0x10000>; pmsg-size = <0x40000>;", False),
        ):
            with self.subTest(sizes=sizes):
                report = inspect(self.config, self.tree(
                    storage=f'ramoops@811000000 {{compatible = "ramoops"; reg = <0x8 0x11000000 0 0x40000>; {sizes} }};',
                ))
                self.assertEqual(report["pstore"]["console_region_configured"], expected)

    def test_wrong_board_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "apple,j72s"):
            inspect(self.config, self.tree(board="apple,j71s"))


if __name__ == "__main__":
    unittest.main()
