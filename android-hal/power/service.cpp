// SPDX-License-Identifier: Apache-2.0
#include "power_supply.h"

#include <memory>
#include <string_view>
#include <utility>

#include <android-base/logging.h>
#include <health-impl/ChargerUtils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

namespace {

using namespace aidl::android::hardware::health;
using j72s::power::Source;
using ndk::ScopedAStatus;

constexpr int kDefaultLightningVoltageMicrovolts = 5000000;

std::unique_ptr<healthd_config> makeConfig() {
    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    const auto setPath = [&](::android::String8 healthd_config::*path, const char* property) {
        ((*config).*path) = ::android::String8::format("%s/%s/%s",
                j72s::power::kPowerSupplyRoot, j72s::power::kBatteryName, property);
    };
    // The HDQ gauge can register after BatteryMonitor's one-time discovery.
    // These paths come from the pinned BQ27540 property table and supply name.
    setPath(&healthd_config::batteryStatusPath, "status");
    setPath(&healthd_config::batteryHealthPath, "health");
    setPath(&healthd_config::batteryPresentPath, "present");
    setPath(&healthd_config::batteryCapacityPath, "capacity");
    setPath(&healthd_config::batteryVoltagePath, "voltage_now");
    setPath(&healthd_config::batteryTemperaturePath, "temp");
    setPath(&healthd_config::batteryTechnologyPath, "technology");
    setPath(&healthd_config::batteryCurrentNowPath, "current_now");
    setPath(&healthd_config::batteryChargeCounterPath, "charge_now");
    setPath(&healthd_config::batteryFullChargePath, "charge_full");
    setPath(&healthd_config::batteryFullChargeDesignCapacityUahPath, "charge_full_design");
    setPath(&healthd_config::batteryCycleCountPath, "cycle_count");
    setPath(&healthd_config::batteryCapacityLevelPath, "capacity_level");
    return config;
}

ScopedAStatus scalar(std::optional<int> reading, int32_t* out) {
    if (!reading) {
        return ScopedAStatus::fromServiceSpecificError(IHealth::STATUS_UNKNOWN);
    }
    *out = *reading;
    return ScopedAStatus::ok();
}

class J72sHealth final : public Health {
  public:
    explicit J72sHealth(std::unique_ptr<healthd_config> config)
        : Health("default", std::move(config)) {}

    ScopedAStatus getCapacity(int32_t* out) override {
        return scalar(supplies_.capacity(), out);
    }

    ScopedAStatus getChargeCounterUah(int32_t* out) override {
        return scalar(supplies_.chargeUah(), out);
    }

    ScopedAStatus getCurrentNowMicroamps(int32_t* out) override {
        return scalar(supplies_.batteryInt("current_now"), out);
    }

    ScopedAStatus setChargingPolicy(BatteryChargingPolicy) override {
        // The Tristar driver owns source selection and current policy.
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

  protected:
    void UpdateHealthInfo(HealthInfo* info) override {
        if (const auto capacity = supplies_.capacity()) {
            info->batteryPresent = true;
            info->batteryLevel = *capacity;
        } else {
            // An unreadable gauge must not be seen as a present, empty battery.
            info->batteryPresent = false;
            info->batteryStatus = BatteryStatus::UNKNOWN;
            info->batteryCapacityLevel = BatteryCapacityLevel::UNSUPPORTED;
        }

        const auto input = supplies_.lightning().value_or(
                j72s::power::LightningReading{Source::Offline, 0});
        info->chargerAcOnline = input.source == Source::Dedicated;
        info->chargerUsbOnline = input.source == Source::Usb;
        info->chargerDockOnline = input.source == Source::Dock;
        info->maxChargingCurrentMicroamps = input.maxCurrentMicroamps;
        info->maxChargingVoltageMicrovolts =
                input.maxCurrentMicroamps > 0 ? kDefaultLightningVoltageMicrovolts : 0;
    }

  private:
    const j72s::power::PowerSupplies supplies_;
};

}  // namespace

int main(int argc, char** argv) {
    auto config = makeConfig();
    auto* loopConfig = config.get();
    const auto fast = config->periodic_chores_interval_fast;
    const auto slow = config->periodic_chores_interval_slow;
    auto service = ndk::SharedRefBase::make<J72sHealth>(std::move(config));
    // Health retains the configuration. Preserve polling when the monitor
    // initially sees no battery, so late registration is picked up.
    loopConfig->periodic_chores_interval_fast = fast;
    loopConfig->periodic_chores_interval_slow = slow;
    if (argc > 1 && std::string_view(argv[1]) == "--charger") {
        return charger::ChargerModeMain(
                service, std::make_shared<charger::ChargerCallback>(service));
    }
    LOG(INFO) << "Starting J72s Health HAL";
    auto loop = std::make_shared<HalHealthLoop>(service, service);
    return loop->StartLoop();
}
