# J72s Industrial I/O sensors

The pinned J72s kernel exposes the AOP accelerometer, gyroscope and barometer
as buffered IIO devices. Its CT819 ambient-light node binds the TSL2583 IIO
module, which returns processed lux. The vendor product loads all four kernel
drivers and includes `j72s-iio-probe` in debug builds.

`libj72s_iio_sensors` identifies the expected kernel devices by name, resolves
their current IIO node numbers, and reads the scale and axis mount matrix from
sysfs. It checks the scan indices and data types before starting a buffer,
selects the boot-time clock, and reads coherent timestamped records. It converts
IIO pressure from kPa to Android hPa and restores the buffer settings on stop.
The light path uses the kernel's processed lux attribute.

The host fixture checks axis transforms, pressure units, light values, invalid
and duplicate devices, scan layout mismatch, and rollback after a failed
buffer setup. The read-only audit checks the compiled device tree, Kconfig
and module archive.

```sh
python3 android-stage/sensors/audit_j72s.py s8000-j72s.dtb candidate.config modules.tar.gz
ctest --test-dir /path/to/j72s-host-tests -R iio_sensors --output-on-failure
```

The Android 16 Sensors AIDL v3 service registers as
`android.hardware.sensors.ISensors/default`. The J72s vendor target installs
the service, feature declarations, ueventd rules and vendor SELinux policy.
The service uses stable handles and publishes real events through Android's
Fast Message Queue. The three AOP sensors support a 1 to 100 Hz sampling
request and can change rate without closing their IIO buffer. The CT819 is
polled separately at intervals of 400 ms or longer and reports changes in lux.
Motion metadata uses the representable IIO range and marks calibration status
unreliable pending physical measurements. The HAL reports no hardware FIFO,
wake-up sensors, direct reports or data injection.

The vendor image build, SELinux policy link, Sensors VTS and iPad checks
remain open. On hardware, verify the sysfs paths and node access, sampling
timing, calibration, suspend/resume and recovery if the HAL process exits
while a kernel buffer is still enabled.
