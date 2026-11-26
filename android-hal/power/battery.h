// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "power_supply.h"

namespace j72s {
struct BatteryReading {
    int capacity = -1;
    std::string status;
    int voltageMicrovolts = -1;
    int temperatureDeciC = -1;
};

class Battery {
    std::filesystem::path node_;

public:
    explicit Battery(const std::filesystem::path& root = power::kPowerSupplyRoot) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec) && !ec && power::readText(it->path() / "type") == "Battery") {
                if (!node_.empty()) {
                    node_.clear();
                    return;
                }
                node_ = it->path();
            }
        }
        if (ec) node_.clear();
    }

    bool available() const { return !node_.empty(); }

    bool read(BatteryReading& result) const {
        if (!available()) return false;
        const auto capacity = power::readInt(node_ / "capacity");
        if (!capacity || *capacity < 0 || *capacity > 100)
            return false;
        result = {*capacity, power::readText(node_ / "status").value_or(""),
                  power::readInt(node_ / "voltage_now").value_or(-1),
                  power::readInt(node_ / "temp").value_or(-1)};
        return true;
    }
};
} // namespace j72s
