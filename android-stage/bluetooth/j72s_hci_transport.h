// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace j72s::bluetooth {

// The controller and device tree expose Bluetooth addresses least-significant byte first.
using Address = std::array<uint8_t, 6>;

struct Controller {
    uint16_t index;
    std::filesystem::path ofNode;
};

// These return a negative errno on failure. openController returns an owned socket.
int findController(const std::filesystem::path& sysfs, Controller& controller);
int verifyAddress(int fd, const std::optional<Address>& expected, std::chrono::milliseconds timeout);
int openController(const std::filesystem::path& sysfs, std::chrono::milliseconds discoveryTimeout);

}  // namespace j72s::bluetooth
