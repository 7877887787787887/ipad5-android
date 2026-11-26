// SPDX-License-Identifier: Apache-2.0
#include "iio_sensors.h"
#include "../../android-hal/test_directory.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>

namespace {
using namespace j72s::sensors;

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << text << '\n';
    stream.close();
    assert(stream);
}

std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::string text;
    assert(std::getline(stream, text));
    return text;
}

void scan(const std::filesystem::path& root, const std::string& channel, int index,
          const std::string& type) {
    write(root / (channel + "_en"), "0");
    write(root / (channel + "_index"), std::to_string(index));
    write(root / (channel + "_type"), type);
}

bool near(float actual, double expected) {
    return std::abs(actual - expected) < 0.001;
}
}  // namespace

int main() {
    j72s::test::Directory fixture;
    const auto sysfs = fixture.path();
    const auto accel = sysfs / "iio:device7";
    const auto gyro = sysfs / "iio:device8";
    const auto pressure = sysfs / "iio:device9";
    const auto light = sysfs / "iio:device2";
    write(accel / "name", "apple-aop-accel");
    write(accel / "in_accel_scale", "0.000149637");
    write(accel / "in_accel_mount_matrix", "1, 0, 0; 0, 1, 0; 0, 0, -1");
    write(gyro / "name", "apple-aop-gyro");
    write(gyro / "in_anglvel_scale", "0.000000266");
    write(gyro / "in_anglvel_mount_matrix", "0, 1, 0; -1, 0, 0; 0, 0, 1");
    write(pressure / "name", "apple-aop-pressure");
    write(pressure / "in_pressure_scale", "0.0000152587890625");
    write(light / "name", "ct819");
    write(light / "in_illuminance_input", "75");
    write(sysfs / "iio:device_bad/name", "ct819");
    write(sysfs / "iio:device_bad/in_illuminance_input", "900");
    auto devices = discover(sysfs, "/dev");
    assert(devices.size() == 4);

    std::array<uint8_t, 24> frame{};
    const std::array<int32_t, 3> axes = {65536, -65536, 65536};
    const int64_t timestamp = 1555555555;
    std::memcpy(frame.data(), axes.data(), sizeof(axes));
    std::memcpy(frame.data() + 16, &timestamp, sizeof(timestamp));
    const auto motion = decodeScan(devices[0], frame.data(), frame.size());
    assert(motion && motion->timestampNs == timestamp);
    assert(near(motion->values[0], 9.80661) && near(motion->values[1], -9.80661));
    assert(near(motion->values[2], -9.80661));
    const auto rotated = decodeScan(devices[1], frame.data(), frame.size());
    assert(rotated && near(rotated->values[0], 0.017432576) &&
           near(rotated->values[1], 0.017432576));
    assert(!decodeScan(devices[0], frame.data(), frame.size() - 1));

    std::array<uint8_t, 16> pressureFrame{};
    const uint32_t raw = 6553600;
    std::memcpy(pressureFrame.data(), &raw, sizeof(raw));
    std::memcpy(pressureFrame.data() + 8, &timestamp, sizeof(timestamp));
    const auto barometer = decodeScan(devices[2], pressureFrame.data(), pressureFrame.size());
    assert(barometer && near(barometer->values[0], 1000));
    const auto lux = readLight(devices[3]);
    assert(lux && lux->timestampNs > 0 && near(lux->values[0], 75));
    write(light / "in_illuminance_input", "-1");
    assert(!readLight(devices[3]));

    const auto buffer = accel / "buffer0";
    scan(buffer, "in_accel_x", 0, "le:s32/32>>0");
    scan(buffer, "in_accel_y", 1, "le:s32/32>>0");
    scan(buffer, "in_accel_z", 2, "le:s32/32>>0");
    scan(buffer, "in_timestamp", 3, "le:s64/64>>0");
    write(buffer / "enable", "0");
    write(accel / "sampling_frequency", "10.000000");
    write(accel / "current_timestamp_clock", "monotonic");
    devices[0].node = "/dev/null";
    Buffer capture(devices[0]);
    assert(!capture.setPeriod(25000000));
    assert(capture.start(25000000));
    assert(read(buffer / "enable") == "1" && read(buffer / "in_timestamp_en") == "1");
    assert(read(accel / "sampling_frequency") == "40.000000");
    assert(read(accel / "current_timestamp_clock") == "boottime");
    assert(!capture.start(25000000));
    assert(capture.setPeriod(1));
    assert(read(accel / "sampling_frequency") == "100.000000");
    assert(capture.setPeriod(2000000000));
    assert(read(accel / "sampling_frequency") == "1.000000");
    assert(read(buffer / "enable") == "1" && read(buffer / "in_timestamp_en") == "1");
    assert(capture.stop());
    assert(read(buffer / "enable") == "0" && read(buffer / "in_timestamp_en") == "0");
    assert(read(accel / "sampling_frequency") == "10.000000");
    assert(read(accel / "current_timestamp_clock") == "monotonic");
    write(buffer / "in_accel_z_type", "le:s16/16>>0");
    assert(!capture.start(25000000));
    assert(read(buffer / "enable") == "0");
    write(buffer / "in_accel_z_type", "le:s32/32>>0");
    std::filesystem::remove(buffer / "in_accel_y_en");
    assert(!capture.start(25000000));
    assert(read(accel / "sampling_frequency") == "10.000000");
    assert(read(accel / "current_timestamp_clock") == "monotonic");
    assert(read(buffer / "in_accel_x_en") == "0" && read(buffer / "enable") == "0");

    write(gyro / "in_anglvel_mount_matrix", "1,1,0; 0,1,0; 0,0,1");
    write(sysfs / "iio:device21/name", "ct819");
    write(sysfs / "iio:device21/in_illuminance_input", "500");
    devices = discover(sysfs, "/dev");
    assert(devices.size() == 2 && devices[0].kind == Kind::Accel && devices[1].kind == Kind::Pressure);
}
