# J72s Android Wi-Fi

The vendor product uses Android 16's Wi-Fi AIDL v3, Supplicant AIDL v4,
and Hostapd AIDL v3. Its primary Wi-Fi implementation is
`libwifi-hal-j72s.so`, loaded by Android's standard legacy HAL loader.
It verifies `wlan0` against the J72s Broadcom PCI device tree before
the Android service brings the interface up. One kernel interface is
shared between station and AP roles.

The HAL queries wireless PHY capabilities via nl80211. The supplicant
and hostapd use cfg80211 for association and AP mode. Wificond reads
per-band channel availability from the kernel, and requests to change
country go through cfg80211. Firmware, antenna configuration, and
regulatory limits remain under the Orchard brcmfmac driver. Optional
Android vendor operations that have no implementation return
`WIFI_ERROR_NOT_SUPPORTED` from the AOSP function table.

The pinned kernel requires a signed regulatory database. Files under
`regdb/` are from the Linux wireless-regdb 2026.09.03 release, with
its accompanying ISC license. The pair was verified using the
`net/wireless/certs/wens.hex` certificate from the matching Orchard
kernel source. The source tree uses `regulatory.db.bin` to pass the
workspace sync filter. The build installs it as `regulatory.db` next
to `regulatory.db.p7s` under `/vendor/firmware`.
Keep `firmware_class.path=/vendor/firmware` in the kernel command line.

See [wireless-regdb releases](https://www.kernel.org/pub/software/network/wireless-regdb/)
and the [Android Wi-Fi HAL](https://source.android.com/docs/core/connect/wifi-hal).

Target checks cover station association on 2.4 and 5 GHz, negotiated PHY
rate, bidirectional throughput, reconnect and AP mode. The full vendor
image and these on-target checks remain open.
