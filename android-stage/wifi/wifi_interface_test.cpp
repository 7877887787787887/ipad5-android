// SPDX-License-Identifier: Apache-2.0
#include "wifi_interface.h"
#include "../../android-hal/test_directory.h"

#include <cassert>
#include <fstream>
#include <string_view>

namespace {

void write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), bytes.size());
    output.close();
    assert(output);
}

}  // namespace

int main() {
    using namespace std::string_view_literals;
    using j72s::wifi::findInterface;
    j72s::test::Directory fixture;
    const auto iface = fixture.path() / "wlan0";
    const auto node = fixture.path() / "j72s-pci-node";
    std::filesystem::create_directories(iface / "device");
    std::filesystem::create_directory(node);
    std::filesystem::create_directory_symlink(node, iface / "device/of_node");
    assert(!findInterface(fixture.path()));
    write(iface / "ifindex", "7\n");
    write(node / "compatible", "pci14e4,43dc\0pciclass,028000\0"sv);
    write(node / "brcm,board-type", "apple,fearb\0"sv);
    assert(!findInterface(fixture.path()));
    std::filesystem::create_directory(iface / "phy80211");
    assert(findInterface(fixture.path()) == 7);
    write(node / "brcm,board-type", "apple,fearb-test\0"sv);
    assert(!findInterface(fixture.path()));
    write(node / "brcm,board-type", "apple,fearb"sv);
    assert(!findInterface(fixture.path()));
    write(node / "brcm,board-type", "apple,fearb\0"sv);
    write(node / "compatible", "pci14e4,43dd\0"sv);
    assert(!findInterface(fixture.path()));
    write(node / "compatible", "pci14e4,43dc\0"sv);
    for (const auto invalid : {"0\n", "-7\n", "7garbage\n", "4294967296\n", ""}) {
        write(iface / "ifindex", invalid);
        assert(!findInterface(fixture.path()));
    }
}
