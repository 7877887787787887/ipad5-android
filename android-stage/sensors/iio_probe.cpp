// SPDX-License-Identifier: Apache-2.0
#include "iio_sensors.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>

namespace {
using namespace j72s::sensors;
constexpr std::array<std::string_view, 4> kNames = {"accel", "gyro", "pressure", "light"};

std::string_view name(Kind kind) {
    return kNames[static_cast<size_t>(kind)];
}

bool capture(const Device& device) {
    std::vector<Sample> samples;
    if (device.kind == Kind::Light) {
        if (auto light = readLight(device)) {
            samples.push_back(*light);
        }
    } else {
        Buffer source(device);
        if (!source.start(100000000)) {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (samples.empty() && std::chrono::steady_clock::now() < deadline) {
            if (!source.read(samples)) {
                return false;
            }
            if (samples.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (!source.stop()) {
            return false;
        }
    }
    for (const auto& sample : samples) {
        std::printf("%lld %.6g %.6g %.6g\n", static_cast<long long>(sample.timestampNs),
                    sample.values[0], sample.values[1], sample.values[2]);
    }
    return !samples.empty();
}
}  // namespace

int main(int argc, char** argv) {
    const auto devices = discover("/sys/bus/iio/devices", "/dev");
    if (argc == 1) {
        for (const auto& device : devices) {
            const auto label = name(device.kind);
            std::printf("%.*s %s\n", static_cast<int>(label.size()), label.data(), device.node.c_str());
        }
        return devices.empty() ? 1 : 0;
    }
    if (argc != 2 || std::find(kNames.begin(), kNames.end(), argv[1]) == kNames.end()) {
        std::fputs("Usage: j72s-iio-probe [accel|gyro|pressure|light]\n", stderr);
        return 2;
    }
    const auto device = std::find_if(devices.begin(), devices.end(),
                                     [&](const Device& entry) { return name(entry.kind) == argv[1]; });
    if (device == devices.end() || !capture(*device)) {
        std::fputs("No valid IIO samples\n", stderr);
        return 1;
    }
    return 0;
}
