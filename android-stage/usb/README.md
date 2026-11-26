# J72s Lightning USB

The vendor product installs `init.j72s.usb.rc` for first-boot ADB. It uses
the DWC2 gadget controller at `20c100000.usbdev` from the J72s device tree.
Android mounts ConfigFS before `post-fs`. The board action creates the
standard `g1` gadget and mounts FunctionFS for ADB before enabling the
platform ConfigFS rules. Those rules bind the controller only after
Android selects ADB and `adbd` reports that its endpoints are ready.
Host authorization remains with Android. The descriptor uses the AOSP
ADB vendor and product IDs for host driver matching.
[AOSP USB init rules](https://android.googlesource.com/platform/system/core/+/android-16.0.0_r1/rootdir/init.usb.configfs.rc)

The vendor product also enables Android's USB host framework through the
standard host feature declaration. The pinned kernel owns accessory
detection and role changes through Tristar and the S8000 role switch. The
device tree supplies separate EHCI and OHCI host controllers.

This wiring covers the ADB-only gadget and host framework. Android 16 r1
uses the older Android USB class to discover its gadget settings interface.
The Orchard kernel exposes the standard Linux UDC class. The staged GSI is
a user build with `ro.adb.secure=1` and `ro.debuggable=0`. A first-boot ADB
test requires Android to select the ADB function and the user to authorize
the host. USB file transfer, gadget settings, and enumeration in either
direction still need validation on the iPad.
[AOSP USB service](https://android.googlesource.com/platform/frameworks/base/+/android-16.0.0_r1/services/usb/java/com/android/server/usb/UsbService.java)
