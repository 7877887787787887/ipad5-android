// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace j72s::test {
class Directory {
    std::filesystem::path path_;

public:
    Directory() {
        const auto pattern = (std::filesystem::temp_directory_path() / "j72s-hal-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const char* created = mkdtemp(buffer.data());
        if (!created) std::abort();
        path_ = created;
    }

    ~Directory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    const std::filesystem::path& path() const { return path_; }
};
} // namespace j72s::test
