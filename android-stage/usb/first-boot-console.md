# J72s first-boot console with Lightning storage

The Lightning controller is in USB host mode while it supplies Android's
system and vendor storage. For this test, the LCD is the proposed path for
kernel and first-stage Android init messages. Android 16 sends early init
messages to the kernel log before logcat is available.
[Android init reference](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/init/README.md)

Run the read-only preflight on the build host. Use the effective DTB
and effective command line for the proposed diagnostic boot. The host
needs `fdtget` from the device-tree compiler package.

```sh
python3 -B validation/check_j72s_boot_console.py \
    --kernel-config candidate.config --dtb effective-j72s.dtb \
    --kernel-args kernel-args.txt
```

The diagnostic console arguments to check are `console=tty0 loglevel=7
fbcon=nodefer`. They select the virtual console and show informational
kernel messages. `fbcon=nodefer` requests immediate framebuffer-console
takeover. Review the console output from power-on through Android init
using a separate camera once a device test is approved.
[Linux framebuffer console options](https://docs.kernel.org/fb/fbcon.html)
[Linux kernel logging parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html#the-kernels-command-line-parameters)

The current compiled J72s DTB carries a disabled simple-framebuffer
placeholder under `/chosen`, with zero base and size. Orchard's ADBE
driver adopts a usable boot framebuffer from that node before it
registers its DRM and fbdev interfaces. Supply an effective handoff DTB
with the board's actual framebuffer resource, geometry, stride and
format to verify this path. The candidate kernel has the required
console and display drivers built in and defaults to console log
level 4.

Orchard's ADBE handoff adopts the scanout that firmware left running.
The driver reads the enabled framebuffer resource and its width,
height, stride and format from the effective tree. It then checks the
declared base and stride against the live display plane. All five
local J72s DTB artifacts contain the same disabled zero resource.
The missing evidence is a readback from an authorized J72s handoff
showing the enabled node, its resolved physical base and size, and its
width, height, byte stride and format. The panel's nominal resolution
does not establish the live base or stride.

The report distinguishes the LCD path from two other possibilities:

- The configured `serial0` resolves to the built-in Samsung UART
  driver. Access to that UART while Lightning is in host mode needs
  its own hardware verification.
- The candidate kernel has ramoops console support. The compiled DTB
  has no ramoops reservation. An effective DTB with a valid console
  reservation still needs a warm-restart retention test.
  [Linux ramoops reference](https://docs.kernel.org/admin-guide/ramoops.html)

Exit status 0 passes the offline panel preflight. Status 1 reports
missing panel prerequisites. Status 2 reports invalid input or an
unavailable tool. An offline pass establishes the configuration checks.
Photographing readable output on the iPad is the physical acceptance test.
