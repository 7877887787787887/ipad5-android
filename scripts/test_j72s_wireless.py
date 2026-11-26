import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import j72s_wireless as wireless


WIFI = "brcm/brcmfmac4355-pcie.apple,fearb.bin"
BLUETOOTH = "brcm/BCM.apple,j72s.hcd"
SCRIPT = Path(__file__).with_name("j72s_wireless.py")


class WirelessTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def put(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def cli(self, *args):
        result = subprocess.run(
            [sys.executable, "-B", str(SCRIPT), *map(str, args)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.stderr, "")
        return result.returncode, json.loads(result.stdout)

    def test_inventory_has_exact_j72s_paths(self):
        code, report = self.cli("inventory")
        self.assertEqual(code, 0)
        self.assertEqual(report["board"], "apple,j72s")
        self.assertEqual(report["wifi_board_type"], "apple,fearb")
        self.assertEqual(report["bluetooth_compatible"], [
            "apple,j71t-bcm4355c0-bt", "brcm,bcm4355c0-bt",
        ])
        entries = {row["path"]: row["sha256"] for row in report["files"]}
        prefix = "brcm/brcmfmac4355-pcie.apple,fearb"
        nvram = {"m": ("1.1", "2.1", "2.3", "3.1"),
                 "u": ("1.1", "2.1", "3.1", "3.3", "4.1")}
        self.assertEqual(set(entries), {
            WIFI, BLUETOOTH, prefix + ".clm_blob", prefix + ".txcap_blob",
            *(f"{prefix}-YSBU-{variant}-{revision}.txt"
              for variant, revisions in nvram.items() for revision in revisions),
        })
        self.assertEqual(len(report["files"]), 13)
        self.assertEqual(entries[BLUETOOTH],
                         "85db6624fe5e9fe63a5dc0a5244be8326e2e861e974d519e79b8c34b8e7dc822")

    def test_cli_reports_missing_mismatched_and_empty_inputs(self):
        self.put(WIFI, b"synthetic Wi-Fi input")
        self.put(BLUETOOTH, b"")
        self.put("brcm/brcmfmac4355-pcie.bin", b"generic fallback")
        code, report = self.cli("validate", self.root)
        self.assertEqual(code, 1)
        statuses = {row["path"]: row["status"] for row in report["files"]}
        self.assertEqual(statuses[WIFI], "digest_mismatch")
        self.assertEqual(statuses[BLUETOOTH], "empty")
        self.assertEqual(list(statuses.values()).count("missing"), 11)

    def test_accepts_matching_files_and_ignores_unrelated_files(self):
        payload = b"synthetic fixture"
        expected = {name: hashlib.sha256(payload).hexdigest() for name in (WIFI, BLUETOOTH)}
        for name in expected:
            self.put(name, payload)
        self.put("brcm/unrelated.bin", b"ignored")
        with patch.object(wireless, "EXPECTED", expected):
            with patch("sys.argv", [str(SCRIPT), "validate", str(self.root)]):
                with patch("builtins.print") as output:
                    self.assertEqual(wireless.main(), 0)
        report = json.loads(output.call_args.args[0])
        self.assertEqual([row["status"] for row in report["files"]], ["verified"] * 2)

    def test_rejects_wrong_case_even_on_case_insensitive_filesystems(self):
        self.put(BLUETOOTH.lower(), b"synthetic input")
        rows = {row["path"]: row for row in wireless.inventory(self.root)}
        self.assertEqual(rows[BLUETOOTH]["status"], "missing")

    def test_rejects_directory_in_place_of_firmware(self):
        (self.root / WIFI).mkdir(parents=True)
        rows = {row["path"]: row for row in wireless.inventory(self.root)}
        self.assertEqual(rows[WIFI]["status"], "invalid_type")

    def test_rejects_symlink_to_firmware_file(self):
        target = self.put("fixture", b"synthetic input")
        link = self.root / BLUETOOTH
        link.parent.mkdir()
        try:
            link.symlink_to(target)
        except OSError as error:
            self.skipTest(str(error))
        rows = {row["path"]: row for row in wireless.inventory(self.root)}
        self.assertEqual(rows[BLUETOOTH]["status"], "invalid_type")

    def test_cli_rejects_missing_root(self):
        code, report = self.cli("validate", self.root / "missing")
        self.assertEqual(code, 1)
        self.assertIn("error", report)


if __name__ == "__main__":
    unittest.main()
