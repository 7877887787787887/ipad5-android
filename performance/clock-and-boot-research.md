# J72s clock and boot research

## Clock evidence

- The compiled J72s DTB audit (run 45722805438) has seven CPU operating points ending at 1,800,000,000 Hz.
- Upstream S8000 source lists a disabled 1,844,000,000 Hz turbo state and says CPU deep sleep is required: https://android-kvm.googlesource.com/linux/+/refs/heads/for-android/pkvm-mainline/arch/arm64/boot/dts/apple/s8000.dtsi
- The driver uses hardware performance-state indices (opp-level), not an arbitrary programmable MHz value: https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/cpufreq/apple%2Ccluster-cpufreq.yaml
- A 2 GHz target is research only. Never relabel an existing hardware state as 2 GHz or assume an additional state exists.
- Before any turbo experiment, establish working deep idle and suspend/resume, real clock measurement, temperature monitoring and recovery on the exact Samsung S8000 board. Retain stock voltage and thermal limits.

## Android integration

The native_interfaces executable is an Android/Bionic API-level Vulkan and AAudio probe. It is not a vendor HAL and cannot replace missing device-specific drivers. Validate Android userspace startup first, then real display/composer, graphics allocator, audio routing, input, wireless, sensor, power and camera services. Do not advertise unavailable hardware features.

## Independent boot

The upstream J72s compatibility row identifies checkm8-pongo only: https://github.com/OrchardLinux/orchard/blob/main/docs/compatibility.md
No independent cold-boot backend has been established for this device. Keep stock iPadOS and its partitions untouched. The current menu prototype is policy only, not boot-chain code. Require a documented, exact-board-compatible boot method, a tested recovery path, and repeatable standalone boot before enabling an Android boot selection. No persistent boot modifications are authorized by this document.
