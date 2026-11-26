j72s_hal_dir := $(call my-dir)
BOARD_VENDOR_SEPOLICY_DIRS += \
    $(j72s_hal_dir)/lights/sepolicy \
    $(j72s_hal_dir)/power/sepolicy \
    $(j72s_hal_dir)/graphics/sepolicy

# Modules built for the same kernel as this product, loaded after vendor mounts.
ifneq ($(filter true,$(J72S_ENABLE_AUDIO_MODULES) $(J72S_ENABLE_WIRELESS_MODULES) $(J72S_ENABLE_COPROCESSOR_MODULES)),)
ifeq ($(strip $(J72S_VENDOR_MODULES_DIR)),)
$(error Enabled J72s module groups require J72S_VENDOR_MODULES_DIR)
endif
endif
ifneq ($(strip $(J72S_VENDOR_MODULES_DIR)),)
j72s_driver_names := apple_z2.ko powervr.ko
ifeq ($(J72S_ENABLE_COPROCESSOR_MODULES),true)
j72s_driver_names += apple-sio-dma.ko apple_aop_accel.ko apple_aop_gyro.ko apple_aop_pressure.ko apple-vxd.ko tsl2583.ko
endif
ifeq ($(J72S_ENABLE_AUDIO_MODULES),true)
j72s_driver_names += snd-soc-apple-s8000-mca.ko snd-soc-cs42l81.ko snd-soc-simple-card.ko
endif
ifeq ($(J72S_ENABLE_WIRELESS_MODULES),true)
j72s_driver_names += brcmfmac.ko brcmfmac-wcc.ko hci_uart.ko
endif
j72s_driver_modules := $(addprefix $(J72S_VENDOR_MODULES_DIR)/,$(j72s_driver_names))
j72s_missing_modules := $(filter-out $(wildcard $(j72s_driver_modules)),$(j72s_driver_modules))
ifneq ($(strip $(j72s_missing_modules)),)
$(error Missing J72s driver modules: $(notdir $(j72s_missing_modules)))
endif
BOARD_VENDOR_KERNEL_MODULES += $(sort $(wildcard $(J72S_VENDOR_MODULES_DIR)/*.ko))
BOARD_VENDOR_KERNEL_MODULES_LOAD += $(j72s_driver_modules)
BOARD_KERNEL_CMDLINE += firmware_class.path=/vendor/firmware
endif
