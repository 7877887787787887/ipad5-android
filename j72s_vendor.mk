J72S_DEVICE_PATH := device/apple/j72s-port
include $(J72S_DEVICE_PATH)/vendor-inputs.mk

$(call inherit-product, $(SRC_TARGET_DIR)/product/base_vendor.mk)
$(call inherit-product, $(J72S_DEVICE_PATH)/android-hal/j72s-hal.mk)

PRODUCT_NAME := j72s_vendor
PRODUCT_DEVICE := j72s-port
PRODUCT_BRAND := Android
PRODUCT_MANUFACTURER := Apple
PRODUCT_MODEL := iPad (5th generation) J72s
PRODUCT_SHIPPING_API_LEVEL := 36

PRODUCT_MAX_PAGE_SIZE_SUPPORTED := 16384
PRODUCT_NO_BIONIC_PAGE_SIZE_MACRO := true
PRODUCT_CHECK_PREBUILT_MAX_PAGE_SIZE := true

# Use Android's low-RAM framework mode and AOSP Go's per-app ART heap limits.
PRODUCT_VENDOR_PROPERTIES += \
    ro.config.low_ram=true \
    dalvik.vm.heapgrowthlimit=128m \
    dalvik.vm.heapsize=256m

# Size the host-built ext4 image from its contents.
PRODUCT_USE_DYNAMIC_PARTITION_SIZE := true
PRODUCT_BUILD_VENDOR_IMAGE := true
PRODUCT_BUILD_SYSTEM_IMAGE := false
PRODUCT_BUILD_SYSTEM_EXT_IMAGE := false
PRODUCT_BUILD_PRODUCT_IMAGE := false
PRODUCT_BUILD_BOOT_IMAGE := false
PRODUCT_BUILD_INIT_BOOT_IMAGE := false
PRODUCT_BUILD_VENDOR_BOOT_IMAGE := false
PRODUCT_BUILD_RECOVERY_IMAGE := false
PRODUCT_BUILD_USERDATA_IMAGE := false
PRODUCT_BUILD_VBMETA_IMAGE := false

PRODUCT_PACKAGES += \
    android.hardware.sensors-service.j72s \
    com.android.hardware.audio \
    toolbox_vendor

ifeq ($(J72S_ENABLE_WIRELESS_MODULES),true)
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.default \
    android.hardware.wifi-service \
    libwifi-hal-j72s \
    wpa_supplicant \
    hostapd
# One kernel netdev can be a client or an access point at any given time.
j72s_wifi_concurrency := {{{STA, AP}, 1}}
$(call soong_config_set,wifi,hal_interface_combinations,$(j72s_wifi_concurrency))
$(call soong_config_set_bool,wpa_supplicant_8,wifi_hidl_unified_supplicant_service_rc_entry,true)
$(call soong_config_set_bool,wpa_supplicant_8,wpa_build_hostapd,true)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    $(J72S_DEVICE_PATH)/android-stage/wifi/j72s-vendor-hal.xml:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/vendor_hals/j72s.xml \
    $(J72S_DEVICE_PATH)/android-stage/wifi/init.j72s.wifi.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.j72s.wifi.rc \
    external/wpa_supplicant_8/wpa_supplicant/wpa_supplicant_template.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(J72S_DEVICE_PATH)/android-stage/wifi/regdb/regulatory.db.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/regulatory.db \
    $(J72S_DEVICE_PATH)/android-stage/wifi/regdb/regulatory.db.p7s:$(TARGET_COPY_OUT_VENDOR)/firmware/regulatory.db.p7s \
    $(J72S_DEVICE_PATH)/android-stage/wifi/regdb/LICENSE:$(TARGET_COPY_OUT_VENDOR)/firmware/regulatory.db.LICENSE
endif

PRODUCT_COPY_FILES += \
    $(J72S_DEVICE_PATH)/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc \
    $(J72S_DEVICE_PATH)/android-stage/memory/fstab.j72s.zram:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.j72s.zram \
    $(J72S_DEVICE_PATH)/android-stage/memory/init.j72s.memory.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.j72s.memory.rc \
    $(J72S_DEVICE_PATH)/android-stage/sensors/ueventd.j72s-sensors.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.j72s-sensors.rc \
    $(foreach kind,accelerometer gyroscope barometer light,\
        frameworks/native/data/etc/android.hardware.sensor.$(kind).xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.$(kind).xml) \
    $(J72S_DEVICE_PATH)/init.j72s.modules.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.j72s.modules.rc \
    $(foreach file,audio_policy_configuration.xml primary_audio_policy_configuration.xml r_submix_audio_policy_configuration.xml bluetooth_audio_policy_configuration_7_0.xml,\
        $(J72S_DEVICE_PATH)/android-stage/audio-primary/$(file):$(TARGET_COPY_OUT_VENDOR)/etc/$(file)) \
    hardware/interfaces/audio/aidl/default/audio_effects_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects_config.xml \
    $(foreach file,audio_policy_volumes.xml default_volume_tables.xml,\
        frameworks/av/services/audiopolicy/config/$(file):$(TARGET_COPY_OUT_VENDOR)/etc/$(file)) \
    $(foreach file,$(J72S_WIRELESS_FIRMWARE_PATHS) $(J72S_COPROCESSOR_FIRMWARE_PATHS),\
        $(J72S_INPUT_DIR)/firmware/$(file):$(TARGET_COPY_OUT_VENDOR)/firmware/$(file))
