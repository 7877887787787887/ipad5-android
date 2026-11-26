# Inherit from the J72s product after removing competing default instances.
PRODUCT_PACKAGES += \
    android.hardware.light-service.j72s \
    android.hardware.health-service.j72s

PRODUCT_PACKAGES_DEBUG += j72s-native-interfaces j72s-vulkan-probe
ifeq ($(J72S_ENABLE_COPROCESSOR_MODULES),true)
PRODUCT_PACKAGES_DEBUG += j72s-iio-probe
endif
ifeq ($(J72S_ENABLE_AUDIO_MODULES),true)
PRODUCT_PACKAGES_DEBUG += j72s-alsa-probe
endif

ifeq ($(J72S_ENABLE_VENDOR_VULKAN),true)
PRODUCT_PACKAGES += vulkan.j72s
PRODUCT_VENDOR_PROPERTIES += ro.hardware.vulkan=j72s
endif

# Requires the patched Android 16 composer and minigbm with the ARGB kernel.
ifeq ($(J72S_ENABLE_ADBE_DISPLAY),true)
PRODUCT_PACKAGES += \
    android.hardware.composer.hwc3-service.drm \
    android.hardware.graphics.allocator-service.minigbm \
    mapper.minigbm
ifeq ($(J72S_ENABLE_VENDOR_VULKAN),true)
PRODUCT_SYSTEM_PROPERTIES += debug.renderengine.backend=skiavkthreaded
endif
endif

PRODUCT_COPY_FILES += \
    device/apple/j72s-port/android-hal/input/iPad6_11_Touchscreen.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/iPad6_11_Touchscreen.idc \
    device/apple/j72s-port/android-hal/lights/ueventd.j72s-lights.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.j72s-lights.rc

ifneq ($(strip $(J72S_TOUCH_FIRMWARE)),)
PRODUCT_COPY_FILES += \
    $(J72S_TOUCH_FIRMWARE):$(TARGET_COPY_OUT_VENDOR)/firmware/apple/j72s-fwstart.z2fw
endif

ifneq ($(strip $(J72S_GPU_FIRMWARE)),)
PRODUCT_COPY_FILES += \
    $(J72S_GPU_FIRMWARE):$(TARGET_COPY_OUT_VENDOR)/firmware/apple/gfx-s8003.bin
endif
