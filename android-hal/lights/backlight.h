// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <string>
#include <unistd.h>

namespace j72s {
class Backlight {
    const std::filesystem::path node_;
    std::mutex mutex_;
    static bool readValue(const std::filesystem::path& path, int& value) {
        std::ifstream in(path);
        std::string extra;
        return bool(in >> value) && !(in >> extra) && value >= 0;
    }
public:
    explicit Backlight(const std::filesystem::path& root =
                           "/sys/devices/platform/soc/20e200080.backlight/backlight")
        : node_(root / "20e200080.backlight") {}

    bool available() const {
        std::error_code ec;
        int maximum = 0;
        return readValue(node_ / "max_brightness", maximum) && maximum > 0 &&
            std::filesystem::is_regular_file(node_ / "brightness", ec);
    }
    static int scale(uint32_t argb, int maximum) {
        if (maximum <= 0) return 0;
        const uint64_t red = (argb >> 16) & 255, green = (argb >> 8) & 255, blue = argb & 255;
        const uint64_t intensity = 77*red + 150*green + 29*blue;
        return static_cast<int>((intensity * static_cast<uint64_t>(maximum) + 32640) / 65280);
    }
    int set(uint32_t argb) {
        std::lock_guard<std::mutex> lock(mutex_);
        int max = 0;
        if (!readValue(node_ / "max_brightness", max) || max <= 0) {
            std::error_code ec;
            return !std::filesystem::exists(node_, ec) && !ec ? ENODEV : EIO;
        }
        const int requested = std::clamp(scale(argb, max), 0, max);
        const std::string value = std::to_string(requested) + '\n';
        const int fd = open((node_ / "brightness").c_str(),
                            O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) return EIO;
        ssize_t written;
        do {
            written = write(fd, value.data(), value.size());
        } while (written < 0 && errno == EINTR);
        const int closed = close(fd);
        return written == static_cast<ssize_t>(value.size()) && closed == 0 ? 0 : EIO;
    }
};
} // namespace j72s
