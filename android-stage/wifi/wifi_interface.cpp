// SPDX-License-Identifier: Apache-2.0
#include "wifi_interface.h"

#include <array>
#include <fstream>
#include <limits>
#include <string_view>

namespace j72s::wifi {
namespace {

bool firstPropertyStringEquals(const std::filesystem::path& path, std::string_view value) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 64> text{};
    input.getline(text.data(), text.size(), '\0');
    return input && !input.eof() && std::string_view(text.data()) == value;
}

}  // namespace

std::optional<unsigned> findInterface(const std::filesystem::path& sysfsNet) {
    const auto iface = sysfsNet / kInterfaceName;
    const auto ofNode = iface / "device/of_node";
    std::error_code error;
    if (!std::filesystem::is_directory(iface / "phy80211", error) ||
        !firstPropertyStringEquals(ofNode / "compatible", "pci14e4,43dc") ||
        !firstPropertyStringEquals(ofNode / "brcm,board-type", "apple,fearb")) {
        return std::nullopt;
    }
    std::ifstream indexFile(iface / "ifindex");
    unsigned index = 0;
    if (!(indexFile >> index) || index == 0 ||
        index > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    indexFile >> std::ws;
    return indexFile.eof() ? std::optional(index) : std::nullopt;
}

}  // namespace j72s::wifi
