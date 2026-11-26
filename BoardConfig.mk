J72S_DEVICE_PATH := device/apple/j72s-port
include $(J72S_DEVICE_PATH)/vendor-inputs.mk

TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := generic
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv7-a-neon
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a15
TARGET_SUPPORTS_64_BIT_APPS := true
TARGET_SUPPORTS_32_BIT_APPS := true
TARGET_BOARD_PLATFORM := s8000

TARGET_NO_BOOTLOADER := true
TARGET_NO_KERNEL := true
TARGET_COPY_OUT_VENDOR := vendor
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
DEVICE_MANIFEST_FILE := $(J72S_DEVICE_PATH)/manifest.xml

# The product uses the J72s ALSA implementation in the AIDL audio APEX.
j72s_audio_sources := hardware/interfaces/audio/aidl/default/primary/alsa_endpoint.c \
    hardware/interfaces/audio/aidl/default/primary/alsa_endpoint.h \
    hardware/interfaces/audio/aidl/default/primary/alsa_controls.c \
    hardware/interfaces/audio/aidl/default/primary/alsa_controls.h
ifneq ($(wildcard $(j72s_audio_sources)),$(j72s_audio_sources))
$(error Stage the J72s audio sources with scripts/stage-j72s-audio.sh)
endif

ifeq ($(J72S_ENABLE_WIRELESS_MODULES),true)
j72s_bluetooth_sources := hardware/interfaces/bluetooth/aidl/default/j72s_hci_transport.cpp \
    hardware/interfaces/bluetooth/aidl/default/j72s_hci_transport.h
ifneq ($(wildcard $(j72s_bluetooth_sources)),$(j72s_bluetooth_sources))
$(error Stage the J72s Bluetooth sources with scripts/stage-j72s-bluetooth.sh)
endif
endif

j72s_required_inputs := \
    $(J72S_TOUCH_FIRMWARE) \
    $(J72S_GPU_FIRMWARE) \
    $(addprefix $(J72S_INPUT_DIR)/firmware/,$(J72S_WIRELESS_FIRMWARE_PATHS) $(J72S_COPROCESSOR_FIRMWARE_PATHS)) \
    $(J72S_DEVICE_PATH)/vendor-vulkan/Android.bp \
    $(J72S_DEVICE_PATH)/vendor-vulkan/vulkan.j72s.so
j72s_missing_inputs := $(filter-out $(wildcard $(j72s_required_inputs)),$(j72s_required_inputs))
ifneq ($(strip $(j72s_missing_inputs)),)
$(error Missing J72s vendor inputs: $(j72s_missing_inputs))
endif

include $(J72S_DEVICE_PATH)/android-hal/BoardConfigJ72sHal.mk
BOARD_VENDOR_SEPOLICY_DIRS += $(J72S_DEVICE_PATH)/android-stage/sensors/sepolicy
