// SPDX-License-Identifier: Apache-2.0
#include "j72s_hci_transport.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <poll.h>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace j72s::bluetooth {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// Linux's AF_BLUETOOTH HCI socket UAPI is not exported by Bionic.
constexpr int kHciProtocol = 1;
constexpr uint16_t kUserChannel = 1;
constexpr uint16_t kNoController = 0xffff;
struct HciSocketAddress {
    sa_family_t family;
    uint16_t controller;
    uint16_t channel;
};

constexpr std::string_view kBoardController = "apple,j71t-bcm4355c0-bt";
constexpr Address kBcm4355C0DefaultAddress{0xac, 0x1f, 0x00, 0xc0, 0x55, 0x43};
constexpr std::array<uint8_t, 4> kReadAddressCommand{0x01, 0x09, 0x10, 0x00};

bool isBoardController(const std::filesystem::path& ofNode) {
    std::ifstream compatible(ofNode / "compatible", std::ios::binary);
    std::array<char, 512> value{};
    compatible.read(value.data(), value.size());
    if (!compatible.eof() || compatible.bad()) {
        return false;
    }
    std::string_view entries(value.data(), static_cast<size_t>(compatible.gcount()));
    while (!entries.empty()) {
        const auto end = entries.find('\0');
        if (end == std::string_view::npos) {
            return false;
        }
        if (entries.substr(0, end) == kBoardController) {
            return true;
        }
        entries.remove_prefix(end + 1);
    }
    return false;
}

bool validAddress(const Address& address) {
    return address != Address{} && address != kBcm4355C0DefaultAddress && !(address.back() & 1);
}

int readExpectedAddress(const Controller& controller, std::optional<Address>& expected) {
    const auto path = controller.ofNode / "local-bd-address";
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return error ? -error.value() : 0;
    }
    std::ifstream input(path, std::ios::binary);
    Address address{};
    input.read(reinterpret_cast<char*>(address.data()), address.size());
    if (!input || input.peek() != std::char_traits<char>::eof() || input.bad()) {
        return -EINVAL;
    }
    if (!validAddress(address)) {
        return -EADDRNOTAVAIL;
    }
    expected = address;
    return 0;
}

int bindController(uint16_t index) {
    const int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, kHciProtocol);
    if (fd < 0) {
        return -errno;
    }
    const HciSocketAddress address{AF_BLUETOOTH, index, kUserChannel};
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        const int error = errno;
        close(fd);
        return -error;
    }
    return fd;
}

}  // namespace

int findController(const std::filesystem::path& sysfs, Controller& controller) {
    std::optional<Controller> found;
    std::error_code error;
    for (std::filesystem::directory_iterator it(sysfs, error), end; !error && it != end;
         it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (name.compare(0, 3, "hci") != 0 || name.size() == 3 ||
            (name.size() > 4 && name[3] == '0')) {
            continue;
        }
        uint16_t index;
        const char* endOfName = name.data() + name.size();
        const auto result = std::from_chars(name.data() + 3, endOfName, index);
        if (result.ec != std::errc{} || result.ptr != endOfName || index == kNoController) {
            continue;
        }
        auto ofNode = it->path() / "device/of_node";
        if (isBoardController(ofNode)) {
            if (found) {
                return -ENOTUNIQ;
            }
            found = Controller{index, std::move(ofNode)};
        }
    }
    if (error) {
        return -error.value();
    }
    if (!found) {
        return -ENODEV;
    }
    controller = std::move(*found);
    return 0;
}

int verifyAddress(int fd, const std::optional<Address>& expected, std::chrono::milliseconds timeout) {
    const auto end = Clock::now() + timeout;
    ssize_t sent;
    do {
        sent = send(fd, kReadAddressCommand.data(), kReadAddressCommand.size(),
                    MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (sent < 0 && errno == EINTR && Clock::now() < end);
    if (sent < 0) {
        return -errno;
    }
    if (sent != static_cast<ssize_t>(kReadAddressCommand.size())) {
        return -EIO;
    }

    while (Clock::now() < end) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(end - Clock::now());
        pollfd event{fd, POLLIN, 0};
        const int pollResult = poll(&event, 1, static_cast<int>(std::max(remaining, 0ms).count()));
        if (pollResult < 0 && errno != EINTR) {
            return -errno;
        }
        if (!(event.revents & POLLIN)) {
            if (event.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return -EIO;
            }
            continue;
        }
        std::array<uint8_t, 13> packet{};
        const auto size = recv(fd, packet.data(), packet.size(), MSG_DONTWAIT | MSG_TRUNC);
        if (size < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return -errno;
        }
        if (size == 0) {
            return -EIO;
        }
        if (size < 6 || packet[0] != 0x04 || packet[1] != 0x0e ||
            packet[4] != kReadAddressCommand[1] || packet[5] != kReadAddressCommand[2]) {
            continue;
        }
        if (size != static_cast<ssize_t>(packet.size()) || packet[2] != 10 || packet[6] != 0) {
            return -EPROTO;
        }
        Address received{};
        std::copy_n(packet.begin() + 7, received.size(), received.begin());
        return validAddress(received) && (!expected || *expected == received) ? 0 : -EADDRNOTAVAIL;
    }
    return -ETIMEDOUT;
}

int openController(const std::filesystem::path& sysfs, std::chrono::milliseconds discoveryTimeout) {
    const auto end = Clock::now() + discoveryTimeout;
    int result;
    do {
        Controller controller{};
        result = findController(sysfs, controller);
        if (result == 0) {
            std::optional<Address> expected;
            result = readExpectedAddress(controller, expected);
            if (result != 0) {
                return result;
            }
            result = bindController(controller.index);
            if (result >= 0) {
                const int status = verifyAddress(result, expected, 2s);
                if (status == 0) {
                    return result;
                }
                close(result);
                return status;
            }
        }
        // Controller registration and early kernel setup can overlap Android init.
        if (result != -ENOENT && result != -ENODEV && result != -EBUSY) {
            return result;
        }
        std::this_thread::sleep_until(std::min(end, Clock::now() + 100ms));
    } while (Clock::now() < end);
    return result;
}

}  // namespace j72s::bluetooth
