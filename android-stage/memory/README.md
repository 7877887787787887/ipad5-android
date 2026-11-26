# Android memory on the 2 GB iPad 5

The vendor image enables Android's low-RAM mode. It uses the 128 MiB
ordinary app heap limit and 256 MiB large heap limit from Android 16's
Go defaults. Android's low-memory killer uses the kernel's pressure
stall information to reclaim background apps under pressure.

The vendor init action sets up one 768 MiB logical zram swap device.
Its compressed pages consume physical RAM as needed. The kernel's
existing zstd backend is used, and swap has no disk backing device.
The stock Android 16 QPR1 AOSP system image is the bring-up baseline.
Keep system and vendor images on block-backed storage and keep the
initial ramdisk small to leave RAM available to Android.

On the iPad, verify the low-RAM property, Android's RAM feature, the
active swap device and the memory pressure readings:

```sh
getprop ro.config.low_ram
pm list features | grep android.hardware.ram
cat /proc/meminfo /proc/swaps /proc/pressure/memory
cat /sys/block/zram0/mm_stat
dumpsys meminfo
```

Measure after cold boot, at idle with the launcher visible, and while
switching between Settings and a browser with Wi-Fi enabled. Record
minimum available RAM, peak zram physical memory and whether the
launcher or foreground app was killed. Verify display refresh, touch
response and audio during that workload. Compare those same measurements
across builds before changing the heap or swap sizes.

On an authorized Android debug connection, collect 60 one-second samples
while using the tablet:

```sh
adb shell sh -s 60 < validation/j72s-memory.sh > j72s-memory.tsv
```

The report includes available RAM, swap usage, physical memory used by
zram, and full memory-stall percentage over the last 10 seconds.
`NA` indicates a kernel counter that the current shell cannot read.
