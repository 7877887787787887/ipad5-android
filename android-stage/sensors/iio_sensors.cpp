// SPDX-License-Identifier: Apache-2.0
#include "iio_sensors.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <locale>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace j72s::sensors {
namespace {

constexpr std::array<int, 9> kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
constexpr std::array<const char*, 4> kNames = {
    "apple-aop-accel", "apple-aop-gyro", "apple-aop-pressure", "ct819",
};
constexpr std::array<const char*, 3> kPrefixes = {"in_accel", "in_anglvel", "in_pressure"};

size_t index(Kind kind) {
    return static_cast<size_t>(kind);
}

std::optional<std::string> attribute(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value) || value.empty() || value.size() > 256) {
        return std::nullopt;
    }
    const auto start = value.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        return std::nullopt;
    }
    return value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
}

bool writeAttribute(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path);
    output << value << '\n';
    output.close();
    return !output.fail();
}

std::optional<double> number(const std::filesystem::path& path) {
    const auto value = attribute(path);
    if (!value) {
        return std::nullopt;
    }
    std::istringstream input(*value);
    input.imbue(std::locale::classic());
    double result;
    if (!(input >> result) || !input.eof() || !std::isfinite(result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::array<int, 9>> orientation(const std::filesystem::path& path) {
    auto value = attribute(path);
    if (!value) {
        return std::nullopt;
    }
    std::replace(value->begin(), value->end(), ',', ' ');
    std::replace(value->begin(), value->end(), ';', ' ');
    std::istringstream fields(*value);
    std::array<int, 9> matrix;
    for (int& entry : matrix) {
        if (!(fields >> entry) || entry < -1 || entry > 1) {
            return std::nullopt;
        }
    }
    fields >> std::ws;
    if (!fields.eof()) {
        return std::nullopt;
    }
    for (int i = 0; i < 3; ++i) {
        if (std::abs(matrix[i * 3]) + std::abs(matrix[i * 3 + 1]) +
                    std::abs(matrix[i * 3 + 2]) != 1 ||
            std::abs(matrix[i]) + std::abs(matrix[i + 3]) + std::abs(matrix[i + 6]) != 1) {
            return std::nullopt;
        }
    }
    return matrix;
}

std::vector<std::string> channels(Kind kind) {
    const std::string prefix = kPrefixes[index(kind)];
    if (kind == Kind::Pressure) {
        return {prefix, "in_timestamp"};
    }
    return {prefix + "_x", prefix + "_y", prefix + "_z", "in_timestamp"};
}

size_t frameSize(Kind kind) {
    // IIO aligns the 64-bit timestamp after one or three 32-bit channels.
    return kind == Kind::Pressure ? 16 : 24;
}

std::string frequency(int64_t periodNs) {
    constexpr int64_t kMicroHzPerHz = 1000000;
    const auto bounded = std::clamp(periodNs, kAopMinPeriodNs, kAopMaxPeriodNs);
    const auto rate = 1000000000000000LL / bounded;
    const auto fraction = std::to_string(rate % kMicroHzPerHz + kMicroHzPerHz);
    return std::to_string(rate / kMicroHzPerHz) + "." + fraction.substr(1);
}

}  // namespace

std::vector<Device> discover(const std::filesystem::path& sysfs, const std::filesystem::path& dev) {
    std::array<std::vector<Device>, kNames.size()> found;
    std::error_code error;
    for (std::filesystem::directory_iterator it(sysfs, error), end; !error && it != end;
         it.increment(error)) {
        const auto path = it->path();
        const auto id = path.filename().string();
        constexpr size_t kPrefixSize = sizeof("iio:device") - 1;
        if (id.compare(0, kPrefixSize, "iio:device") != 0 || id.size() == kPrefixSize ||
            id.find_first_not_of("0123456789", kPrefixSize) != std::string::npos) {
            continue;
        }
        const auto name = attribute(path / "name");
        if (!name) {
            continue;
        }
        const auto match = std::find(kNames.begin(), kNames.end(), *name);
        if (match == kNames.end()) {
            continue;
        }
        const auto kind = static_cast<Kind>(match - kNames.begin());
        Device device{kind, path, dev / id, 1, kIdentity};
        if (kind == Kind::Light) {
            if (!std::filesystem::is_regular_file(path / "in_illuminance_input", error)) {
                error.clear();
                continue;
            }
        } else {
            const std::string prefix = kPrefixes[index(kind)];
            const auto scale = number(path / (prefix + "_scale"));
            if (!scale || *scale <= 0 || *scale > 1) {
                continue;
            }
            device.scale = *scale;
            if (kind != Kind::Pressure) {
                const auto matrix = orientation(path / (prefix + "_mount_matrix"));
                if (!matrix) {
                    continue;
                }
                device.orientation = *matrix;
            }
        }
        found[index(kind)].push_back(std::move(device));
    }
    std::vector<Device> devices;
    for (auto& group : found) {
        if (group.size() == 1) {
            devices.push_back(std::move(group.front()));
        }
    }
    return devices;
}

std::optional<Sample> decodeScan(const Device& device, const uint8_t* bytes, size_t size) {
    if (device.kind == Kind::Light || size != frameSize(device.kind)) {
        return std::nullopt;
    }
    Sample result{};
    std::memcpy(&result.timestampNs, bytes + size - sizeof(result.timestampNs),
                sizeof(result.timestampNs));
    if (result.timestampNs <= 0) {
        return std::nullopt;
    }
    if (device.kind == Kind::Pressure) {
        uint32_t raw;
        std::memcpy(&raw, bytes, sizeof(raw));
        // IIO reports kPa. Android Sensor.TYPE_PRESSURE reports hPa.
        result.values[0] = static_cast<float>(raw * device.scale * 10);
        return result;
    }
    std::array<int32_t, 3> raw;
    std::memcpy(raw.data(), bytes, sizeof(raw));
    for (size_t axis = 0; axis < raw.size(); ++axis) {
        double converted = 0;
        for (size_t source = 0; source < raw.size(); ++source) {
            // Each row gives the corresponding sensor axis in the device frame.
            converted += static_cast<double>(raw[source]) * device.orientation[source * 3 + axis];
        }
        result.values[axis] = static_cast<float>(converted * device.scale);
    }
    return result;
}

std::optional<Sample> readLight(const Device& device) {
    if (device.kind != Kind::Light) {
        return std::nullopt;
    }
    const auto lux = number(device.attributes / "in_illuminance_input");
    timespec timestamp{};
    if (!lux || *lux < 0 || *lux > std::numeric_limits<int>::max() ||
        clock_gettime(CLOCK_BOOTTIME, &timestamp) != 0) {
        return std::nullopt;
    }
    return Sample{timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec,
                  {static_cast<float>(*lux), 0, 0}};
}

Buffer::Buffer(Device device) : device_(std::move(device)) {}

Buffer::~Buffer() {
    stop();
}

bool Buffer::applyAttribute(const std::filesystem::path& path, const std::string& value) {
    auto before = attribute(path);
    if (!before) {
        return false;
    }
    if (*before != value) {
        if (std::none_of(changes_.begin(), changes_.end(),
                         [&](const auto& change) { return change.first == path; })) {
            changes_.emplace_back(path, std::move(*before));
        }
        return writeAttribute(path, value);
    }
    return true;
}

bool Buffer::start(int64_t periodNs) {
    if (device_.kind == Kind::Light || fd_ != -1 || !changes_.empty()) {
        return false;
    }
    const auto& root = device_.attributes;
    const auto modern = root / "buffer0";
    const bool hasModern = attribute(modern / "in_timestamp_index").has_value();
    const auto config = hasModern ? modern : root / "buffer";
    const auto scan = hasModern ? modern : root / "scan_elements";
    if (attribute(config / "enable") != "0") {
        return false;
    }

    const auto names = channels(device_.kind);
    for (size_t i = 0; i < names.size(); ++i) {
        const auto type = i + 1 == names.size() ? "le:s64/64>>0"
            : device_.kind == Kind::Pressure ? "le:u32/32>>0" : "le:s32/32>>0";
        if (attribute(scan / (names[i] + "_index")) != std::to_string(i) ||
            attribute(scan / (names[i] + "_type")) != type) {
            return false;
        }
    }
    std::error_code error;
    for (std::filesystem::directory_iterator it(scan, error), end; !error && it != end;
         it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name.size() > 3 && name.compare(name.size() - 3, 3, "_en") == 0 &&
            std::find(names.begin(), names.end(), name.substr(0, name.size() - 3)) == names.end() &&
            attribute(it->path()) != "0") {
            return false;
        }
    }
    if (error) {
        return false;
    }

    fd_ = open(device_.node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info {};
    if (fd_ < 0 || fstat(fd_, &info) != 0 || !S_ISCHR(info.st_mode)) {
        stop();
        return false;
    }

    if (!applyAttribute(root / "current_timestamp_clock", "boottime") ||
        !applyAttribute(root / "sampling_frequency", frequency(periodNs))) {
        stop();
        return false;
    }
    for (const auto& name : names) {
        if (!applyAttribute(scan / (name + "_en"), "1")) {
            stop();
            return false;
        }
    }
    if (!applyAttribute(config / "enable", "1")) {
        stop();
        return false;
    }
    return true;
}

bool Buffer::stop() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
    bool restored = true;
    while (!changes_.empty()) {
        const auto& [path, previous] = changes_.back();
        if (!writeAttribute(path, previous)) {
            restored = false;
        }
        changes_.pop_back();
    }
    return restored;
}

bool Buffer::setPeriod(int64_t periodNs) {
    return fd_ != -1 &&
           applyAttribute(device_.attributes / "sampling_frequency", frequency(periodNs));
}

bool Buffer::read(std::vector<Sample>& samples) {
    std::array<uint8_t, 16 * 24> bytes;
    const auto stride = frameSize(device_.kind);
    ssize_t count;
    do {
        count = ::read(fd_, bytes.data(), stride * 16);
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
    if (count == 0 || static_cast<size_t>(count) % stride != 0) {
        return false;
    }
    for (size_t offset = 0; offset < static_cast<size_t>(count); offset += stride) {
        if (auto sample = decodeScan(device_, bytes.data() + offset, stride)) {
            samples.push_back(*sample);
        }
    }
    return true;
}

}  // namespace j72s::sensors
