// SPDX-License-Identifier: Apache-2.0
#include "backlight.h"
#include <aidl/android/hardware/light/BnLights.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <string>
#include <vector>

namespace light = aidl::android::hardware::light;
class Lights final : public light::BnLights {
    static constexpr int32_t kDisplayId = 1;
    j72s::Backlight backlight_;
public:
    ndk::ScopedAStatus getLights(std::vector<light::HwLight>* result) override {
        // The framework caches this list, so keep the board's display ID stable
        // even if its kernel driver is still probing.
        light::HwLight display;
        display.id = kDisplayId;
        display.ordinal = 0;
        display.type = light::LightType::BACKLIGHT;
        result->assign(1, display);
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setLightState(int32_t id, const light::HwLightState& state) override {
        if (id != kDisplayId || state.flashMode != light::FlashMode::NONE ||
            state.brightnessMode == light::BrightnessMode::LOW_PERSISTENCE)
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        int error = backlight_.set(static_cast<uint32_t>(state.color));
        return error ? ndk::ScopedAStatus::fromServiceSpecificError(error) : ndk::ScopedAStatus::ok();
    }
};
int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto service = ndk::SharedRefBase::make<Lights>();
    std::string name = std::string(light::ILights::descriptor) + "/default";
    if (AServiceManager_addService(service->asBinder().get(), name.c_str()) != STATUS_OK)
        return 1;
    ABinderProcess_joinThreadPool();
    return 0;
}
