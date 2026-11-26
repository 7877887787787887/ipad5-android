# J72s Android Bluetooth HCI

The J72s vendor product builds Android 16's
`android.hardware.bluetooth-service.default`, with its Linux transport
adapted for the board. Run `scripts/stage-j72s-bluetooth.sh` on the Android
16 r1 `hardware/interfaces` checkout before evaluating the vendor target.
The service retains Android's H4 packet handling and AIDL v1 declaration.

The Orchard kernel owns the Broadcom BCM4355C0 UART. The HAL identifies
the controller through the serdev parent's `apple,j71t-bcm4355c0-bt`
device-tree compatible. It opens the kernel HCI user channel, whose
exclusive ownership ends on close, and leaves rfkill decisions to the
kernel. Startup retries transient registration failures for up to five
seconds between kernel operations. It requires a valid Read BD_ADDR
reply and matches `local-bd-address` when the bootloader supplied it.
The public address is never logged.

Use the matching J72s kernel modules and packaged
`brcm/BCM.apple,j72s.hcd`. The `bluetooth0` alias in the compiled device
tree lets Orchard's boot path inject the unit's own Bluetooth address.
The patched HAL uses Android's existing Bluetooth domain and NET_ADMIN
service capability for the user channel. SELinux access, controller
initialization, toggling, pairing, HID, audio and reconnect still require
target testing. The host test runs discovery on a fixture and exercises
the HCI address exchange through a packet socket.
