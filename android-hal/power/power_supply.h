// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace j72s::power {

inline constexpr char kPowerSupplyRoot[] = "/sys/class/power_supply";
inline constexpr char kBatteryName[] = "battery";
inline constexpr char kLightningName[] = "lightning";

inline std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string line;
    if (!std::getline(input, line)) {
        return std::nullopt;
    }
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return std::nullopt;
    }
    return line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
}

inline std::optional<int> readInt(const std::filesystem::path& path) {
    std::ifstream input(path);
    int value;
    std::string extra;
    if (!(input >> value) || (input >> extra) || !input.eof()) {
        return std::nullopt;
    }
    return value;
}

enum class Source { Offline, Usb, Dedicated, Dock };

struct LightningReading {
    Source source;
    // The source grant from current_max, in microamps.
    int maxCurrentMicroamps;
};

inline Source classifyLightningType(std::string_view types) {
    // The power_supply ABI brackets the active USB type in the supported-types list.
    const auto first = types.find('[');
    const auto last = types.find(']');
    if (first != std::string_view::npos && last != std::string_view::npos && first < last &&
        types.find('[', first + 1) == std::string_view::npos &&
        types.find(']', last + 1) == std::string_view::npos) {
        const auto active = types.substr(first + 1, last - first - 1);
        if (active == "DCP" || active == "BrickID") {
            return Source::Dedicated;
        }
        if (active == "ACA") {
            return Source::Dock;
        }
    }
    return Source::Usb;
}

class PowerSupplies {
  public:
    explicit PowerSupplies(std::filesystem::path root = kPowerSupplyRoot)
        : battery_(root / kBatteryName), lightning_(root / kLightningName) {}

    std::optional<int> batteryInt(const char* property) const {
        if (readText(battery_ / "type") != "Battery" || readInt(battery_ / "present") != 1) {
            return std::nullopt;
        }
        return readInt(battery_ / property);
    }

    std::optional<int> capacity() const {
        const auto capacity = batteryInt("capacity");
        return capacity && *capacity >= 0 && *capacity <= 100 ? capacity : std::nullopt;
    }

    std::optional<int> chargeUah() const {
        const auto charge = batteryInt("charge_now");
        return charge && *charge >= 0 ? charge : std::nullopt;
    }

    std::optional<LightningReading> lightning() const {
        if (readText(lightning_ / "type") != "USB") {
            return std::nullopt;
        }
        const auto online = readInt(lightning_ / "online");
        if (online == 0) {
            return LightningReading{Source::Offline, 0};
        }
        if (online != 1) {
            return std::nullopt;
        }
        const auto type = readText(lightning_ / "usb_type");
        const auto current = readInt(lightning_ / "current_max");
        if (readInt(lightning_ / "online") != 1) {
            return std::nullopt;
        }
        return LightningReading{type ? classifyLightningType(*type) : Source::Usb,
                                current && *current > 0 ? *current : 0};
    }

  private:
    std::filesystem::path battery_;
    std::filesystem::path lightning_;
};

}  // namespace j72s::power
