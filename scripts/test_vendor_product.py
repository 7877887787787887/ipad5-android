import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from xml.etree import ElementTree

from stage_j72s_firmware import FIRMWARE
from vendor_modules import AUDIO_DRIVERS, COPROCESSOR_DRIVERS, DRIVERS, WIRELESS_DRIVERS


REPO = Path(__file__).resolve().parent.parent
DEVICE = Path("device/apple/j72s-port")
MODULES = DRIVERS + COPROCESSOR_DRIVERS + AUDIO_DRIVERS + WIRELESS_DRIVERS
AUDIO_PRIMARY = Path("hardware/interfaces/audio/aidl/default/primary")
BLUETOOTH_DEFAULT = Path("hardware/interfaces/bluetooth/aidl/default")
SENSORS = Path("android-stage/sensors")
WIFI = Path("android-stage/wifi")
REGDB = {"regulatory.db", "regulatory.db.p7s", "regulatory.db.LICENSE"}


@unittest.skipUnless(shutil.which("make"), "GNU Make is required")
class VendorProductTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.device = self.root / DEVICE
        for path in (
            "AndroidProducts.mk", "j72s_vendor.mk", "BoardConfig.mk", "vendor-inputs.mk",
            "manifest.xml", "ueventd.rc", "init.j72s.modules.rc",
            "android-hal/BoardConfigJ72sHal.mk", "android-hal/j72s-hal.mk",
        ):
            destination = self.device / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO / path, destination)
        for path in (
            "vendor-vulkan/Android.bp", "vendor-vulkan/vulkan.j72s.so",
            *(f"vendor-inputs/modules/{name}.ko" for name in MODULES),
            *(f"vendor-inputs/firmware/{name}" for name in FIRMWARE),
        ):
            destination = self.device / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text("fixture\n", encoding="utf-8")
        base = self.root / "aosp/product/base_vendor.mk"
        base.parent.mkdir(parents=True)
        base.write_text("PRODUCT_PACKAGES += selinux_policy_vendor\n", encoding="utf-8")
        primary = self.root / AUDIO_PRIMARY
        primary.mkdir(parents=True)
        for file in ("alsa_endpoint.c", "alsa_endpoint.h"):
            shutil.copyfile(REPO / "android-stage" / file, primary / file)
        for file in ("alsa_controls.c", "alsa_controls.h"):
            shutil.copyfile(REPO / "android-stage/audio-primary" / file, primary / file)
        bluetooth = self.root / BLUETOOTH_DEFAULT
        bluetooth.mkdir(parents=True)
        for file in ("j72s_hci_transport.cpp", "j72s_hci_transport.h"):
            shutil.copyfile(REPO / "android-stage/bluetooth" / file, bluetooth / file)
        (self.root / "check.mk").write_text(
            f"LOCAL_DIR := {DEVICE.as_posix()}\n"
            "SRC_TARGET_DIR := aosp\n"
            "my-dir = $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))\n"
            "inherit-product = $(eval include $(strip $(1)))\n"
            "soong_config_set = $(eval soong_$(1)_$(2) := $(3))\n"
            "soong_config_set_bool = $(eval soong_$(1)_$(2) := $(3))\n"
            "include $(LOCAL_DIR)/AndroidProducts.mk\n"
            "include $(LOCAL_DIR)/BoardConfig.mk\n"
            "include $(PRODUCT_MAKEFILES)\n"
            "check:\n"
            "\t@printf '%s\\n' '$(COMMON_LUNCH_CHOICES)' '$(PRODUCT_PACKAGES)' "
            "'$(BOARD_VENDOR_KERNEL_MODULES_LOAD)' '$(PRODUCT_COPY_FILES)' "
            "'$(DEVICE_MANIFEST_FILE)' '$(PRODUCT_MAX_PAGE_SIZE_SUPPORTED)' "
            "'$(PRODUCT_BUILD_VENDOR_IMAGE) $(PRODUCT_BUILD_BOOT_IMAGE)' "
            "'$(BOARD_VENDOR_SEPOLICY_DIRS)' "
            "'$(soong_wifi_hal_interface_combinations)' "
            "'$(soong_wpa_supplicant_8_wifi_hidl_unified_supplicant_service_rc_entry) "
            "$(soong_wpa_supplicant_8_wpa_build_hostapd)' '$(PRODUCT_VENDOR_PROPERTIES)'\n",
            encoding="utf-8",
        )

    def read_target(self):
        return subprocess.run(
            ["make", "--no-print-directory", "-f", "check.mk", "check"],
            cwd=self.root, capture_output=True, text=True,
        )

    def test_product_wires_vendor_modules_and_verified_firmware_inventory(self):
        result = self.read_target()
        self.assertEqual(result.returncode, 0, result.stderr)
        lunch, packages, modules, copies, manifest, pages, images, *_ = result.stdout.splitlines()
        self.assertEqual(lunch, "j72s_vendor-trunk_staging-userdebug")
        self.assertTrue({
            "selinux_policy_vendor", "android.hardware.light-service.j72s",
            "android.hardware.composer.hwc3-service.drm",
            "android.hardware.graphics.allocator-service.minigbm", "mapper.minigbm", "vulkan.j72s",
            "com.android.hardware.audio",
            "toolbox_vendor",
        }.issubset(packages.split()))
        self.assertEqual([Path(item).stem for item in modules.split()], list(MODULES))
        paths = dict(value.split(":") for value in copies.split())
        self.assertEqual(
            {value.removeprefix("vendor/firmware/") for value in paths.values()
             if value.startswith("vendor/firmware/")},
            set(FIRMWARE) | REGDB,
        )
        self.assertEqual(paths[(DEVICE / "ueventd.rc").as_posix()], "vendor/etc/ueventd.rc")
        loader = "init.j72s.modules.rc"
        self.assertEqual(paths[(DEVICE / loader).as_posix()], f"vendor/etc/init/{loader}")
        rc = (REPO / loader).read_text(encoding="utf-8")
        self.assertIn("exec_start vendor.j72s-modules", rc)
        self.assertIn("--all=/vendor/lib/modules/modules.load", rc)
        policy = REPO / "android-stage/audio-primary/audio_policy_configuration.xml"
        includes = {
            element.get("href")
            for element in ElementTree.parse(policy).iter("{http://www.w3.org/2001/XInclude}include")
        }
        self.assertEqual(includes, {
            "primary_audio_policy_configuration.xml", "r_submix_audio_policy_configuration.xml",
            "bluetooth_audio_policy_configuration_7_0.xml", "audio_policy_volumes.xml",
            "default_volume_tables.xml",
        })
        self.assertTrue({
            f"vendor/etc/{name}" for name in includes | {"audio_policy_configuration.xml",
                                                        "audio_effects_config.xml"}
        }.issubset(paths.values()))
        for name in ("r_submix_audio_policy_configuration.xml",
                     "bluetooth_audio_policy_configuration_7_0.xml"):
            relative = DEVICE / "android-stage/audio-primary" / name
            self.assertEqual(paths[relative.as_posix()], f"vendor/etc/{name}")
            profiles = ElementTree.parse(REPO / "android-stage/audio-primary" / name).iter("profile")
            self.assertTrue(all("name" not in profile.attrib for profile in profiles))
        self.assertEqual(ElementTree.parse(self.root / manifest).getroot().get("target-level"), "202504")
        self.assertEqual(pages, "16384")
        self.assertEqual(images, "true false")

    def test_low_ram_settings_and_zram_are_packaged_in_vendor(self):
        result = self.read_target()
        self.assertEqual(result.returncode, 0, result.stderr)
        _, _, _, copies, *_, properties = result.stdout.splitlines()
        self.assertTrue({
            "ro.config.low_ram=true", "dalvik.vm.heapgrowthlimit=128m", "dalvik.vm.heapsize=256m",
        }.issubset(properties.split()))
        paths = dict(value.split(":") for value in copies.split())
        memory = Path("android-stage/memory")
        fstab = "fstab.j72s.zram"
        rc = "init.j72s.memory.rc"
        self.assertEqual(paths[(DEVICE / memory / fstab).as_posix()], f"vendor/etc/{fstab}")
        self.assertEqual(paths[(DEVICE / memory / rc).as_posix()], f"vendor/etc/init/{rc}")
        entries = [line.split() for line in (REPO / memory / fstab).read_text().splitlines()
                   if line.strip() and not line.startswith("#")]
        self.assertEqual(len(entries), 1)
        device, mount_point, fs_type, mount_flags, fs_mgr_flags = entries[0]
        self.assertEqual((device, mount_point, fs_type, mount_flags),
                         ("/dev/block/zram0", "none", "swap", "defaults"))
        self.assertEqual(set(fs_mgr_flags.split(",")), {"wait", "zramsize=805306368", "swapprio=100"})
        self.assertIn(f"on post-fs-data\n    swapon_all /vendor/etc/{fstab}",
                      (REPO / memory / rc).read_text())

    def test_sensor_service_is_packaged_with_discovery_permissions(self):
        result = self.read_target()
        self.assertEqual(result.returncode, 0, result.stderr)
        _, packages, _, copies, _, _, _, policies, *_ = result.stdout.splitlines()
        service = "android.hardware.sensors-service.j72s"
        self.assertIn(service, packages.split())
        self.assertIn((DEVICE / SENSORS / "sepolicy").as_posix(), policies.split())
        paths = dict(value.split(":") for value in copies.split())
        rules = "ueventd.j72s-sensors.rc"
        self.assertEqual(paths[(DEVICE / SENSORS / rules).as_posix()], f"vendor/etc/{rules}")
        self.assertIn(f"import /vendor/etc/{rules}", (REPO / "ueventd.rc").read_text())
        for kind in ("accelerometer", "gyroscope", "barometer", "light"):
            feature = f"android.hardware.sensor.{kind}.xml"
            self.assertEqual(paths[f"frameworks/native/data/etc/{feature}"],
                             f"vendor/etc/permissions/{feature}")
        hal = ElementTree.parse(REPO / SENSORS / f"{service}.xml").getroot().find("hal")
        self.assertEqual(hal.get("format"), "aidl")
        self.assertEqual(hal.findtext("name"), "android.hardware.sensors")
        self.assertEqual(hal.findtext("version"), "3")
        self.assertEqual(hal.findtext("fqname"), "ISensors/default")
        rc = (REPO / SENSORS / f"{service}.rc").read_text()
        self.assertIn(f"/vendor/bin/hw/{service}", rc)
        self.assertIn("interface aidl android.hardware.sensors.ISensors/default", rc)

    def test_missing_wireless_firmware_fails_before_packaging(self):
        file = "brcm/BCM.apple,j72s.hcd"
        (self.device / "vendor-inputs/firmware" / file).unlink()
        result = self.read_target()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(file, result.stderr)

    def test_health_service_packages_one_battery_and_charger_registration(self):
        result = self.read_target()
        self.assertEqual(result.returncode, 0, result.stderr)
        _, packages, _, _, _, _, _, policies, *_ = result.stdout.splitlines()
        service = "android.hardware.health-service.j72s"
        self.assertIn(service, packages.split())
        self.assertNotIn("android.hardware.health-service.example", packages.split())
        power = Path("android-hal/power")
        self.assertIn((DEVICE / power / "sepolicy").as_posix(), policies.split())
        hal = ElementTree.parse(REPO / power / f"{service}.xml").getroot().find("hal")
        self.assertEqual(hal.get("format"), "aidl")
        self.assertEqual(hal.findtext("name"), "android.hardware.health")
        self.assertEqual(hal.findtext("version"), "4")
        self.assertEqual(hal.findtext("fqname"), "IHealth/default")
        rc = (REPO / power / f"{service}.rc").read_text()
        path = f"/vendor/bin/hw/{service}"
        self.assertIn(f"service vendor.health-default {path}\n", rc)
        self.assertIn("interface aidl android.hardware.health.IHealth/default", rc)
        self.assertIn(f"service vendor.charger {path} --charger\n", rc)

    def test_bluetooth_service_and_radio_features_are_packaged(self):
        result = self.read_target()
        self.assertEqual(result.returncode, 0, result.stderr)
        _, packages, _, copies, *_ = result.stdout.splitlines()
        self.assertIn("android.hardware.bluetooth-service.default", packages.split())
        paths = dict(value.split(":") for value in copies.split())
        for kind in ("bluetooth", "bluetooth_le"):
            feature = f"android.hardware.{kind}.xml"
            self.assertEqual(paths[f"frameworks/native/data/etc/{feature}"],
                             f"vendor/etc/permissions/{feature}")

    def test_missing_bluetooth_sources_fail_before_packaging(self):
        (self.root / BLUETOOTH_DEFAULT / "j72s_hci_transport.h").unlink()
        result = self.read_target()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("scripts/stage-j72s-bluetooth.sh", result.stderr)

    def test_wifi_services_use_one_kernel_netdev_and_signed_regdb(self):
        result = self.read_target()
        self.assertEqual(result.returncode, 0, result.stderr)
        _, packages, _, copies, _, _, _, _, combinations, daemons, _ = result.stdout.splitlines()
        self.assertTrue({"android.hardware.wifi-service", "libwifi-hal-j72s",
                         "wpa_supplicant", "hostapd"}.issubset(packages.split()))
        self.assertEqual(combinations, "{{{STA, AP}, 1}}")
        self.assertEqual(daemons, "true true")
        paths = dict(value.split(":") for value in copies.split())
        self.assertEqual(paths[(DEVICE / WIFI / "j72s-vendor-hal.xml").as_posix()],
                         "vendor/etc/wifi/vendor_hals/j72s.xml")
        self.assertEqual(paths[(DEVICE / WIFI / "init.j72s.wifi.rc").as_posix()],
                         "vendor/etc/init/init.j72s.wifi.rc")
        self.assertEqual(paths["frameworks/native/data/etc/android.hardware.wifi.xml"],
                         "vendor/etc/permissions/android.hardware.wifi.xml")
        for source, target in (("regulatory.db.bin", "regulatory.db"),
                               ("regulatory.db.p7s", "regulatory.db.p7s")):
            path = WIFI / "regdb" / source
            self.assertEqual(paths[(DEVICE / path).as_posix()], f"vendor/firmware/{target}")
            self.assertGreater((REPO / path).stat().st_size, 0)
        loader = ElementTree.parse(REPO / WIFI / "j72s-vendor-hal.xml").getroot()
        self.assertEqual(loader.findtext("path"), "/vendor/lib64/libwifi-hal-j72s.so")
        self.assertEqual(loader.findtext("primary"), "1")

    def test_missing_audio_sources_fails_before_packaging(self):
        (self.root / AUDIO_PRIMARY / "alsa_controls.h").unlink()
        result = self.read_target()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("scripts/stage-j72s-audio.sh", result.stderr)

    def test_mic_is_attached_and_headphones_wait_for_an_external_connection(self):
        path = REPO / "android-stage/audio-primary/primary_audio_policy_configuration.xml"
        module = ElementTree.parse(path).getroot()
        attached = {item.text for item in module.findall("./attachedDevices/item")}
        self.assertEqual(attached, {"Speaker", "Built-In Mic"})
        devices = {port.get("tagName"): port for port in module.findall("./devicePorts/devicePort")}
        self.assertEqual(devices["Built-In Mic"].get("address"), "j72s-mic")
        self.assertEqual(devices["Wired Headphones"].get("type"), "AUDIO_DEVICE_OUT_WIRED_HEADPHONE")
        for name, mask in (("Built-In Mic", "AUDIO_CHANNEL_IN_STEREO"),
                           ("Wired Headphones", "AUDIO_CHANNEL_OUT_STEREO")):
            self.assertEqual(devices[name].find("profile").attrib, {
                "format": "AUDIO_FORMAT_PCM_16_BIT", "samplingRates": "48000", "channelMasks": mask,
            })
        routes = {route.get("sink"): route.get("sources") for route in module.findall("./routes/route")}
        self.assertEqual(routes, {"Speaker": "primary output", "Wired Headphones": "primary output",
                                  "primary input": "Built-In Mic"})

    def test_missing_coprocessor_firmware_fails_before_packaging(self):
        file = "apple/aop-ipad6f-ext.bin"
        (self.device / "vendor-inputs/firmware" / file).unlink()
        result = self.read_target()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(file, result.stderr)
