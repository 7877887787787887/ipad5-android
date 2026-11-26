// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace j72s {
class Backlight {
    std::filesystem::path node_;
    std::mutex mutex_;
    static bool readValue(const std::filesystem::path& path, int& value) {
        std::ifstream in(path);
        std::string extra;
        return bool(in >> value) && !(in >> extra) && value >= 0;
    }
public:
    explicit Backlight(const std::filesystem::path& root = "/sys/class/backlight") {
        std::error_code ec;
        std::vector<std::filesystem::path> candidates;
        std::filesystem::directory_iterator it(root, ec), end;
        while (!ec && it != end) {
            int maximum = 0;
            if (readValue(it->path() / "max_brightness", maximum) && maximum > 0 &&
                std::filesystem::exists(it->path() / "brightness", ec) && !ec)
                candidates.push_back(it->path());
            it.increment(ec);
        }
        // Do not guess which device to control when enumeration is ambiguous.
        if (!ec && candidates.size() == 1) node_ = candidates.front();
    }
    bool available() const { return !node_.empty(); }
    static int scale(uint32_t argb, int maximum) {
        if (maximum <= 0) return 0;
        const uint64_t red = (argb >> 16) & 255, green = (argb >> 8) & 255, blue = argb & 255;
        const uint64_t intensity = 77*red + 150*green + 29*blue;
        return static_cast<int>((intensity * static_cast<uint64_t>(maximum) + 32640) / 65280);
    }
    int set(uint32_t argb) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!available()) return ENODEV;
        int max = 0;
        if (!readValue(node_ / "max_brightness", max) || max <= 0) return EIO;
        const int requested = std::clamp(scale(argb, max), 0, max);
        std::ofstream out(node_ / "brightness");
        if (!out) return EIO;
        out << requested << '\n';
        out.close();
        return out.fail() ? EIO : 0;
    }
};
} // namespace j72s
