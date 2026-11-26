// SPDX-License-Identifier: Apache-2.0
#include "battery.h"
#include "../test_directory.h"
#include <cassert>
#include <filesystem>
#include <fstream>

static void writeFile(const std::filesystem::path& path, const char* text) {
    std::ofstream out(path);
    out << text;
}

static void testBoardPowerSupplies() {
    using j72s::power::Source;
    const j72s::test::Directory fixture;
    const auto& root = fixture.path();
    const j72s::power::PowerSupplies supplies(root);
    assert(!supplies.capacity());
    assert(!supplies.lightning());

    const auto battery = root / "battery";
    std::filesystem::create_directory(battery);
    writeFile(battery / "type", "Battery\n");
    writeFile(battery / "present", "1\n");
    writeFile(battery / "capacity", "63\n");
    writeFile(battery / "charge_now", "4600000\n");
    writeFile(battery / "current_now", "-340000\n");
    assert(supplies.capacity() == 63);
    assert(supplies.chargeUah() == 4600000);
    assert(supplies.batteryInt("current_now") == -340000);
    writeFile(battery / "capacity", "0\n");
    assert(supplies.capacity() == 0);
    writeFile(battery / "capacity", "101\n");
    assert(!supplies.capacity());
    writeFile(battery / "present", "0\n");
    assert(!supplies.chargeUah());
    std::filesystem::rename(battery, root / "other-battery");
    assert(!supplies.batteryInt("current_now"));

    const auto lightning = root / "lightning";
    std::filesystem::create_directory(lightning);
    writeFile(lightning / "type", "USB\n");
    writeFile(lightning / "online", "1\n");
    writeFile(lightning / "current_max", "2100000\n");
    const auto checkSource = [&](const char* types, Source expected) {
        writeFile(lightning / "usb_type", types);
        const auto reading = supplies.lightning();
        assert(reading && reading->source == expected && reading->maxCurrentMicroamps == 2100000);
    };
    checkSource("Unknown [SDP] DCP CDP ACA BrickID\n", Source::Usb);
    checkSource("Unknown SDP [DCP] CDP ACA BrickID\n", Source::Dedicated);
    checkSource("Unknown SDP DCP CDP ACA [BrickID]\n", Source::Dedicated);
    checkSource("Unknown SDP DCP [CDP] ACA BrickID\n", Source::Usb);
    checkSource("Unknown SDP DCP CDP [ACA] BrickID\n", Source::Dock);
    checkSource("[DCP] [BrickID]\n", Source::Usb);
    writeFile(lightning / "current_max", "-10\n");
    assert(supplies.lightning()->maxCurrentMicroamps == 0);
    writeFile(lightning / "current_max", "2100000\n");
    writeFile(lightning / "online", "0\n");
    const auto offline = supplies.lightning();
    assert(offline && offline->source == Source::Offline && offline->maxCurrentMicroamps == 0);
    writeFile(lightning / "online", "2\n");
    assert(!supplies.lightning());
}

int main() {
    testBoardPowerSupplies();
    const j72s::test::Directory fixture;
    const auto& root = fixture.path();
    std::filesystem::create_directories(root / "second");
    writeFile(root / "second" / "capacity", "75\n");
    writeFile(root / "second" / "type", "UPS\n");
    assert(!j72s::Battery(root).available());

    std::filesystem::create_directories(root / "battery");
    writeFile(root / "battery" / "type", "Battery\n");
    writeFile(root / "battery" / "capacity", "73\n");
    writeFile(root / "battery" / "status", "Not charging\n");
    writeFile(root / "battery" / "voltage_now", "3820000 invalid\n");
    writeFile(root / "battery" / "temp", "285\n");

    j72s::Battery battery(root);
    assert(battery.available());
    j72s::BatteryReading reading;
    assert(battery.read(reading));
    assert(reading.capacity == 73 && reading.status == "Not charging");
    assert(reading.voltageMicrovolts == -1 && reading.temperatureDeciC == 285);

    writeFile(root / "battery" / "voltage_now", "3820000\n");
    assert(battery.read(reading));
    assert(reading.voltageMicrovolts == 3820000);

    assert(j72s::Battery(root).available());
    writeFile(root / "second" / "type", "Battery\n");
    assert(!j72s::Battery(root).available());

    writeFile(root / "battery" / "capacity", "101\n");
    assert(!battery.read(reading));
    return 0;
}
