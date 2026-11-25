#!/bin/sh
# Inventory only. Does not record audio, enable radios, charge, write storage or change clocks.
export LC_ALL=C
report() { printf '\n=== %s ===\n' "$1"; shift; "$@" 2>&1 || true; }
report "ALSA playback and recording endpoints" cat /proc/asound/pcm
report "ALSA cards" cat /proc/asound/cards
report "Audio playback device list" aplay -l
report "Audio capture device list" arecord -l
report "Wireless radio capabilities" iw list
report "Wi-Fi link information (existing connection only)" iw dev
report "Bluetooth controller capabilities" btmgmt info
report "Camera and video decoder devices" v4l2-ctl --list-devices
report "Sensor types" sh -c 'for p in /sys/bus/iio/devices/iio:device*/name; do [ ! -r "$p" ] || cat "$p"; done'
report "USB controllers" lsusb -t
report "Power and thermal state" sh -c 'for p in /sys/class/power_supply/*/type /sys/class/power_supply/*/status /sys/class/power_supply/*/capacity /sys/class/thermal/thermal_zone*/type /sys/class/thermal/thermal_zone*/temp; do [ ! -r "$p" ] || { printf "%s " "$p"; cat "$p"; }; done'
printf '\nInventory is not a functional test. Speaker sound, microphone capture, cameras, radio throughput, touch response, charging and suspend require physical testing.\n'
