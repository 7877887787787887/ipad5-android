# J72s AIDL lights HAL

This directory contains the candidate Android AIDL `ILights` service for the
J72s display backlight. It targets the Orchard DWI backlight node, maps
the RGB color field to luminance using integer Rec. 601 weights, clamps to the
kernel-reported maximum, and returns service-specific errors for I/O failures.

`Android.bp`, the init fragment, and the VINTF fragment target Android 16
Lights AIDL v2. The service uses the platform's existing `hal_light_service`
label and its standard Lights server permissions.

The expected sysfs name follows the pinned Orchard device tree and Linux
platform device naming. The policy labels the DWI node with `sysfs_leds`.
The ueventd fragment grants the service's system group write access to
`brightness` on that node. Import the installed fragment in the board's
vendor ueventd configuration. The host test covers a late driver probe and
an unrelated backlight. Verify the canonical path, permissions, and
brightness changes on the target.
