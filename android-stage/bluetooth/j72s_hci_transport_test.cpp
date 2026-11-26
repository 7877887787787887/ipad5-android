// SPDX-License-Identifier: Apache-2.0
#include "j72s_hci_transport.h"
#include "../../android-hal/test_directory.h"

#include <cassert>
#include <cerrno>
#include <fstream>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using namespace j72s::bluetooth;
using namespace std::chrono_literals;

void writeCompatible(const std::filesystem::path& path, std::string_view bytes) {
    const auto node = path / "device/of_node";
    std::filesystem::create_directories(node);
    std::ofstream compatible(node / "compatible", std::ios::binary);
    compatible.write(bytes.data(), bytes.size());
    compatible.close();
    assert(compatible);
}

void testDiscovery() {
    using namespace std::string_view_literals;
    j72s::test::Directory fixture;
    const auto bus = fixture.path() / "class";
    const auto device = fixture.path() / "board";
    std::filesystem::create_directory(bus);
    Controller controller{};
    assert(findController(bus, controller) == -ENODEV);
    writeCompatible(bus / "hci0", "brcm,bcm4355c0-bt\0"sv);
    writeCompatible(bus / "hci3", "apple,j71t-bcm4355c0-bt-extended\0"sv);
    writeCompatible(bus / "hci5", "apple,j71t-bcm4355c0-bt"sv);
    writeCompatible(bus / "hci65535", "apple,j71t-bcm4355c0-bt\0"sv);
    writeCompatible(device, "apple,j71t-bcm4355c0-bt\0brcm,bcm4355c0-bt\0"sv);
    std::filesystem::create_directory_symlink(device, bus / "hci7");
    assert(findController(bus, controller) == 0 && controller.index == 7);
    writeCompatible(bus / "hci2", "apple,j71t-bcm4355c0-bt\0"sv);
    assert(findController(bus, controller) == -ENOTUNIQ);
    assert(openController(bus, 20ms) == -ENOTUNIQ);
    assert(openController(bus / "missing", 20ms) == -ENOENT);
}

constexpr Address kAddress{0xaa, 0xbb, 0x01, 0x02, 0x03, 0x02};

// A packet socket preserves the same read boundaries as a Linux HCI user channel.
int checkResponse(const Address& returned, const std::optional<Address>& expected,
                  uint8_t status = 0, bool respond = true) {
    int channel[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channel) == 0);
    if (respond) {
        const std::array<uint8_t, 3> unrelated{0x04, 0xff, 0x00};
        assert(send(channel[1], unrelated.data(), unrelated.size(), 0) ==
               static_cast<ssize_t>(unrelated.size()));
        const std::array<uint8_t, 13> response{
            0x04, 0x0e, 0x0a, 0x01, 0x09, 0x10, status, returned[0], returned[1],
            returned[2], returned[3], returned[4], returned[5],
        };
        assert(send(channel[1], response.data(), response.size(), 0) ==
               static_cast<ssize_t>(response.size()));
    }
    const int result = verifyAddress(channel[0], expected, respond ? 250ms : 20ms);
    std::array<uint8_t, 4> command{};
    assert(recv(channel[1], command.data(), command.size(), MSG_DONTWAIT | MSG_TRUNC) == 4);
    assert((command == std::array<uint8_t, 4>{0x01, 0x09, 0x10, 0x00}));
    close(channel[0]);
    close(channel[1]);
    return result;
}

}  // namespace

int main() {
    testDiscovery();
    assert(checkResponse(kAddress, std::nullopt) == 0);
    assert(checkResponse(kAddress, kAddress) == 0);
    assert(checkResponse(kAddress, Address{1, 2, 3, 4, 5, 6}) == -EADDRNOTAVAIL);
    assert(checkResponse(Address{}, std::nullopt) == -EADDRNOTAVAIL);
    assert(checkResponse(Address{0xac, 0x1f, 0, 0xc0, 0x55, 0x43}, std::nullopt) == -EADDRNOTAVAIL);
    assert(checkResponse(Address{1, 2, 3, 4, 5, 7}, std::nullopt) == -EADDRNOTAVAIL);
    assert(checkResponse(kAddress, kAddress, 1) == -EPROTO);
    assert(checkResponse(kAddress, kAddress, 0, false) == -ETIMEDOUT);
}
