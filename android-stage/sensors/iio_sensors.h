// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace j72s::sensors {

enum class Kind { Accel, Gyro, Pressure, Light };

// Sampling bounds for the pinned Apple AOP IIO drivers.
inline constexpr int64_t kAopMinPeriodNs = 10000000;
inline constexpr int64_t kAopMaxPeriodNs = 1000000000;

struct Device {
    Kind kind;
    std::filesystem::path attributes;
    std::filesystem::path node;
    double scale;
    std::array<int, 9> orientation;
};

struct Sample {
    int64_t timestampNs;
    std::array<float, 3> values;
};

std::vector<Device> discover(const std::filesystem::path& sysfs, const std::filesystem::path& dev);
std::optional<Sample> decodeScan(const Device& device, const uint8_t* bytes, size_t size);
std::optional<Sample> readLight(const Device& device);

class Buffer {
public:
    explicit Buffer(Device device);
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    bool start(int64_t periodNs);
    bool setPeriod(int64_t periodNs);
    bool stop();
    bool read(std::vector<Sample>& samples);

private:
    bool applyAttribute(const std::filesystem::path& path, const std::string& value);
    Device device_;
    int fd_ = -1;
    std::vector<std::pair<std::filesystem::path, std::string>> changes_;
};

}  // namespace j72s::sensors
