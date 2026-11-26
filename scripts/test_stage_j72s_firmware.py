import hashlib
import tempfile
import unittest
from pathlib import Path

from stage_j72s_firmware import stage, validate


class StageFirmwareTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        self.cache = root / "outputs"
        self.cache.mkdir()
        self.dest = root / "firmware"
        self.files = {"apple/sensor.bin": b"sensor image", "brcm/radio.bin": b"radio image"}
        self.expected = {}
        for name, contents in self.files.items():
            digest = hashlib.sha256(contents).hexdigest()
            self.expected[name] = digest
            (self.cache / digest).write_bytes(contents)

    def test_stages_named_files_and_verifies_an_existing_stage(self):
        stage(self.cache, self.dest, self.expected)
        stage(self.cache, self.dest, self.expected)
        validate(self.dest, self.expected)
        self.assertEqual(
            {name: (self.dest / name).read_bytes() for name in self.files},
            self.files,
        )

    def test_invalid_cache_leaves_no_destination(self):
        (self.cache / self.expected["brcm/radio.bin"]).write_bytes(b"invalid")
        with self.assertRaisesRegex(ValueError, "brcm/radio.bin: digest_mismatch"):
            stage(self.cache, self.dest, self.expected)
        self.assertFalse(self.dest.exists())

    def test_modified_destination_is_preserved(self):
        stage(self.cache, self.dest, self.expected)
        changed = self.dest / "apple/sensor.bin"
        changed.write_bytes(b"user copy")
        with self.assertRaisesRegex(ValueError, "apple/sensor.bin: digest_mismatch"):
            stage(self.cache, self.dest, self.expected)
        self.assertEqual(changed.read_bytes(), b"user copy")
