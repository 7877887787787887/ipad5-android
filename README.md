# J72s Android candidate kernel

Target: iPad6,12 / Samsung A9 S8000 / J72s. Experimental kernel configuration, not a complete Android port. No device access, bootloader changes, flashing or partition changes.

The build workflow uses the pinned Orchard v0.3.8 source release, validates its archive digest, applies Android candidate Kconfig options, and compiles the kernel, J72s DTB and matching modules. Output is compile-only and not boot-tested. Android userspace, SELinux policy, HALs, hardware testing and independent boot remain unresolved.

Source: https://github.com/OrchardLinux/orchard/releases/tag/v0.3.8

Use Actions to view the build or manually rerun it. Do not flash this experimental output.
