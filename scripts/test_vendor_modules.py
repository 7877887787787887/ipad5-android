import tempfile
import unittest
import subprocess
import sys
from pathlib import Path

from vendor_modules import select_modules


class VendorModulesTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)

    def tree(self, index, paths):
        (self.root / "modules.dep").write_text(index, encoding="utf-8")
        for path in paths:
            module = self.root / path
            module.parent.mkdir(parents=True, exist_ok=True)
            module.touch()

    def test_picks_driver_dependencies_and_omits_unrelated_modules(self):
        self.tree(
            "kernel/touch/apple_z2.ko: kernel/i2c/provider.ko\n"
            "kernel/gpu/powervr.ko: kernel/drm/drm.ko\n"
            "kernel/i2c/provider.ko: kernel/drm/drm.ko\n"
            "kernel/drm/drm.ko:\n"
            "kernel/debug.ko:\n",
            ["kernel/touch/apple_z2.ko", "kernel/gpu/powervr.ko",
             "kernel/i2c/provider.ko", "kernel/drm/drm.ko", "kernel/debug.ko"],
        )
        self.assertEqual(
            [path.name for path in select_modules(self.root, ("apple_z2", "powervr"))],
            ["drm.ko", "provider.ko", "apple_z2.ko", "powervr.ko"],
        )

    def test_rejects_missing_dependency(self):
        self.tree("kernel/powervr.ko: kernel/drm.ko\n", ["kernel/powervr.ko"])
        with self.assertRaisesRegex(ValueError, "kernel/drm.ko"):
            select_modules(self.root, ("powervr",))

    def test_rejects_duplicate_flattened_names(self):
        self.tree("kernel/a/powervr.ko: kernel/b/powervr.ko\n"
                  "kernel/b/powervr.ko:\n",
                  ["kernel/a/powervr.ko", "kernel/b/powervr.ko"])
        with self.assertRaisesRegex(ValueError, "powervr"):
            select_modules(self.root, ("powervr",))

    def test_audio_option_stages_the_three_board_audio_drivers(self):
        names = ("apple_z2", "powervr", "snd-soc-apple-s8000-mca",
                 "snd-soc-cs42l81", "snd-soc-simple-card")
        paths = [f"kernel/{name}.ko" for name in names]
        self.tree("".join(f"{path}:\n" for path in paths), paths)
        stage = self.root / "stage"
        subprocess.run(
            [sys.executable, str(Path(__file__).with_name("vendor_modules.py")),
             str(self.root), str(stage), "--audio"],
            check=True, capture_output=True, text=True,
        )
        self.assertEqual(sorted(p.name for p in stage.iterdir()), sorted(f"{n}.ko" for n in names))

    def test_wireless_option_stages_wcc_and_bluetooth_dependencies(self):
        paths = [
            "kernel/apple_z2.ko", "kernel/powervr.ko",
            "kernel/brcmfmac.ko", "kernel/brcmfmac-wcc.ko", "kernel/hci_uart.ko",
            "kernel/cfg80211.ko", "kernel/btbcm.ko", "kernel/brcmfmac-cyw.ko",
        ]
        links = {
            "kernel/brcmfmac.ko": "kernel/cfg80211.ko",
            "kernel/brcmfmac-wcc.ko": "kernel/brcmfmac.ko",
            "kernel/hci_uart.ko": "kernel/btbcm.ko",
        }
        self.tree("".join(f"{path}: {links.get(path, '')}\n" for path in paths), paths)
        stage = self.root / "stage"
        subprocess.run(
            [sys.executable, str(Path(__file__).with_name("vendor_modules.py")),
             str(self.root), str(stage), "--wireless"],
            check=True, capture_output=True, text=True,
        )
        self.assertEqual(
            {p.name for p in stage.iterdir()},
            {Path(p).name for p in paths if not p.endswith("brcmfmac-cyw.ko")},
        )
