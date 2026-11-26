// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>

namespace j72s::wifi {

// Android's InterfaceTool starts and stops this kernel-provided interface.
inline constexpr char kInterfaceName[] = "wlan0";

std::optional<unsigned> findInterface(const std::filesystem::path& sysfsNet);

}  // namespace j72s::wifi
