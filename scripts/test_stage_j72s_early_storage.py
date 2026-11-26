import hashlib
import io
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import stage_j72s_early_storage as boot


class StageEarlyStorageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.kernel = Path(self.temp.name) / "kernel"
        self.output = Path(self.temp.name) / "overlay"
        self.kernel.mkdir()
        (self.kernel / "kernel-release.txt").write_text("7.3.0-rc1\n")
        config = [f"CONFIG_{option}=y" for option in boot.BUILT_IN]
        config += [f"CONFIG_{option}={'m' if option in {'SCSI', 'USB_STORAGE'} else 'y'}"
                   for option in boot.STORAGE]
        (self.kernel / "candidate.config").write_text("\n".join(config) + "\n")
        self.files = {
            "modules.dep": (
                "kernel/drivers/usb/storage/usb-storage.ko: kernel/drivers/scsi/scsi_mod.ko\n"
                "kernel/drivers/scsi/scsi_mod.ko:\n"
                "kernel/drivers/input/unrelated.ko:\n"
            ),
            "modules.alias": "alias usb:* usb_storage\nalias input:* unrelated\n",
            "modules.softdep": "",
            "kernel/drivers/usb/storage/usb-storage.ko": "usb",
            "kernel/drivers/scsi/scsi_mod.ko": "scsi",
            "kernel/drivers/input/unrelated.ko": "input",
        }

    def write_archive(self):
        with tarfile.open(self.kernel / "modules.tar.gz", "w:gz") as modules:
            for path, value in self.files.items():
                data = value.encode()
                item = tarfile.TarInfo(f"./lib/modules/7.3.0-rc1/{path}")
                item.size = len(data)
                modules.addfile(item, io.BytesIO(data))
        names = ["candidate.config", "modules.tar.gz"]
        (self.kernel / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256((self.kernel / name).read_bytes()).hexdigest()}  {name}\n"
            for name in names
        ))

    def configure_internal_nvme(self, missing_option=None):
        config = [f"CONFIG_{option}=y" for option in boot.NVME_BUILT_IN
                  if option != missing_option]
        config += [f"CONFIG_{option}=m" for option in boot.NVME_STORAGE]
        (self.kernel / "candidate.config").write_text("\n".join(config) + "\n")
        driver = "kernel/drivers/nvme/host"
        self.files["modules.dep"] += (
            f"{driver}/nvme-core.ko:\n"
            f"{driver}/nvme.ko: {driver}/nvme-core.ko\n"
            f"{driver}/nvme-apple.ko: {driver}/nvme-core.ko\n"
        )
        self.files["modules.alias"] += (
            "alias pci:v0000106Bd00002002* nvme\n"
            "alias of:N*T*Capple,nvme-ans2 nvme_apple\n"
        )
        for name in ("nvme-core", "nvme", "nvme-apple"):
            self.files[f"{driver}/{name}.ko"] = name

    @mock.patch.object(boot, "verify_module")
    def test_stages_only_storage_dependencies_and_metadata(self, verify_module):
        self.write_archive()
        release, modules = boot.stage_storage(self.kernel, self.output)
        self.assertEqual(release, "7.3.0-rc1")
        self.assertEqual(modules, ["scsi_mod.ko", "usb-storage.ko"])
        root = self.output / "lib/modules/7.3.0-rc1_16k"
        self.assertEqual((root / "modules.load").read_text(), "scsi_mod.ko\nusb-storage.ko\n")
        self.assertEqual((root / "modules.alias").read_text(), "alias usb:* usb_storage\n")
        self.assertNotIn("unrelated", (root / "modules.dep").read_text())
        self.assertEqual({p.name for p in root.rglob("*.ko")}, set(modules))
        self.assertEqual(verify_module.call_count, 2)

    def test_rejects_corrupt_or_missing_inputs_without_an_overlay(self):
        self.write_archive()
        with (self.kernel / "modules.tar.gz").open("ab") as invalid_archive:
            invalid_archive.write(b"modified")
        with self.assertRaisesRegex(ValueError, "checksum failed"):
            boot.stage_storage(self.kernel, self.output)
        self.assertFalse(self.output.exists())

        del self.files["kernel/drivers/scsi/scsi_mod.ko"]
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "Missing or invalid module"):
            boot.stage_storage(self.kernel, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_unreviewed_soft_dependency(self):
        self.files["modules.softdep"] = "softdep usb_storage pre: unrelated\n"
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "unreviewed soft dependency"):
            boot.stage_storage(self.kernel, self.output)
        self.assertFalse(self.output.exists())

    def test_requires_early_controller_and_storage_config(self):
        path = self.kernel / "candidate.config"
        config = path.read_text()
        for flag in ("USB_ROLES_APPLE_S8000", "USB_STORAGE"):
            with self.subTest(flag=flag):
                path.write_text("\n".join(line for line in config.splitlines()
                                          if not line.startswith(f"CONFIG_{flag}=")) + "\n")
                self.write_archive()
                with self.assertRaisesRegex(ValueError, f"CONFIG_{flag}"):
                    boot.stage_storage(self.kernel, self.output)
                self.assertFalse(self.output.exists())

    @mock.patch.object(boot, "verify_module")
    def test_internal_nvme_loads_pci_storage_and_its_core_only(self, verify_module):
        self.configure_internal_nvme()
        self.write_archive()
        release, modules = boot.stage_storage(self.kernel, self.output, transport="internal-nvme")
        self.assertEqual(release, "7.3.0-rc1")
        self.assertEqual(modules, ["nvme-core.ko", "nvme.ko"])
        root = self.output / "lib/modules/7.3.0-rc1_16k"
        self.assertEqual((root / "modules.load").read_text(), "nvme-core.ko\nnvme.ko\n")
        self.assertEqual((root / "modules.alias").read_text(), "alias pci:v0000106Bd00002002* nvme\n")
        self.assertEqual({p.name for p in root.rglob("*.ko")}, set(modules))
        self.assertEqual(verify_module.call_count, 2)

    def test_internal_nvme_requires_builtin_h9p(self):
        self.configure_internal_nvme(missing_option="PCIE_APPLE_H9P")
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "CONFIG_PCIE_APPLE_H9P"):
            boot.stage_storage(self.kernel, self.output, transport="internal-nvme")
        self.assertFalse(self.output.exists())

    def test_internal_nvme_rejects_unreviewed_dependencies(self):
        self.configure_internal_nvme()
        self.files["modules.dep"] = self.files["modules.dep"].replace(
            "nvme.ko: kernel/drivers/nvme/host/nvme-core.ko\n",
            "nvme.ko: kernel/drivers/nvme/host/nvme-core.ko kernel/drivers/input/unrelated.ko\n",
        )
        self.write_archive()
        with self.assertRaisesRegex(ValueError, "PCI NVMe module dependency closure"):
            boot.stage_storage(self.kernel, self.output, transport="internal-nvme")
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
