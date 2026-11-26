# J72s Android battery and charging

The vendor product packages `android.hardware.health-service.j72s` as the
Android Health AIDL v4 service. It keeps AOSP's Health loop and offline
charger mode. The J72s configuration supplies stable paths for the BQ27540
HDQ gauge and keeps polling if the gauge registers after the HAL starts.

`/sys/class/power_supply/battery` supplies battery percentage, signed
current in microamps and remaining charge in microamp-hours. The kernel
exports the latter as `charge_now`. An unavailable gauge is reported as
absent, and the corresponding individual reads return a service error.

`/sys/class/power_supply/lightning` supplies online state, USB source
type and the kernel's current grant. The service reports DCP and Apple
BrickID as AC, ACA as a dock, and other online Lightning sources as USB.
It uses Android's default nominal 5 V when a positive current grant is
reported. The kernel owns charging policy and current limits.

The service uses Android's standard Health SELinux domain. The board
file contexts cover the J72s gauge and charger reporting attributes.
The PC test uses a temporary sysfs-shaped fixture for late registration,
invalid readings and source classification. Target validation still
needs the booted iPad for battery readings, cable events and offline charging.
