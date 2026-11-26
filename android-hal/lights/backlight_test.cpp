// SPDX-License-Identifier: Apache-2.0
#include "backlight.h"
#include "../test_directory.h"
#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static void write(const fs::path& p, const char* value) {
    std::ofstream out(p);
    out << value;
}

int main() {
    assert(j72s::Backlight::scale(0xff0000, 100) == 30);
    assert(j72s::Backlight::scale(0x00ff00, 100) == 59);
    assert(j72s::Backlight::scale(0x0000ff, 100) == 11);
    assert(j72s::Backlight::scale(0xffffffff, 100) == 100);
    assert(j72s::Backlight::scale(0, 0) == 0);

    const j72s::test::Directory fixture;
    const auto& root = fixture.path();
    const auto other = root / "other-display";
    fs::create_directory(other);
    write(other / "max_brightness", "100\n");
    write(other / "brightness", "33\n");
    j72s::Backlight backlight(root);
    assert(!backlight.available());
    assert(backlight.set(0xffffffff) == ENODEV);

    const auto panel = root / "20e200080.backlight";
    fs::create_directory(panel);
    write(panel / "max_brightness", "100\n");
    write(panel / "brightness", "0\n");
    assert(backlight.available());
    assert(backlight.set(0xffffffff) == 0);
    std::ifstream brightness(panel / "brightness");
    int value = -1;
    brightness >> value;
    assert(value == 100);
    brightness.close();
    fs::remove(panel / "brightness");
    assert(backlight.set(0) == EIO);
    assert(!fs::exists(panel / "brightness"));
    std::ifstream otherBrightness(other / "brightness");
    otherBrightness >> value;
    assert(value == 33);
    return 0;
}
