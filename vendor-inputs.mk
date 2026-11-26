# This product stages kernel-built modules and locally supplied firmware.
J72S_INPUT_DIR ?= $(J72S_DEVICE_PATH)/vendor-inputs
J72S_VENDOR_MODULES_DIR := $(J72S_INPUT_DIR)/modules
J72S_TOUCH_FIRMWARE := $(J72S_INPUT_DIR)/firmware/apple/j72s-fwstart.z2fw
J72S_GPU_FIRMWARE := $(J72S_INPUT_DIR)/firmware/apple/gfx-s8003.bin
J72S_COPROCESSOR_FIRMWARE_PATHS := apple/s8000-smartio.bin apple/aop-ipad6f.bin \
    apple/aop-ipad6f-ext.bin apple/vxd-deva.bin
J72S_BRCM_FIRMWARE_PREFIX := brcm/brcmfmac4355-pcie.apple,fearb
J72S_WIRELESS_FIRMWARE_PATHS := \
    $(addprefix $(J72S_BRCM_FIRMWARE_PREFIX),.bin .clm_blob .txcap_blob) \
    $(addsuffix .txt,$(addprefix $(J72S_BRCM_FIRMWARE_PREFIX)-YSBU-m-,1.1 2.1 2.3 3.1)) \
    $(addsuffix .txt,$(addprefix $(J72S_BRCM_FIRMWARE_PREFIX)-YSBU-u-,1.1 2.1 3.1 3.3 4.1)) \
    brcm/BCM.apple,j72s.hcd

J72S_ENABLE_ADBE_DISPLAY := true
J72S_ENABLE_VENDOR_VULKAN := true
J72S_ENABLE_AUDIO_MODULES := true
J72S_ENABLE_WIRELESS_MODULES := true
J72S_ENABLE_COPROCESSOR_MODULES := true
