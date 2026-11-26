// SPDX-License-Identifier: Apache-2.0
#include "backlight.h"
#include <aidl/android/hardware/light/BnLights.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

namespace light = aidl::android::hardware::light;
class Lights final : public light::BnLights {
    j72s::Backlight backlight_;
public:
    ndk::ScopedAStatus getLights(std::vector<light::HwLight>* result) override {
        result->clear();
        if (backlight_.available()) {
            light::HwLight display;
            display.id = 1;
            display.ordinal = 0;
            display.type = light::LightType::BACKLIGHT;
            result->push_back(display);
        }
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setLightState(int32_t id, const light::HwLightState& state) override {
        if (id != 1 || !backlight_.available() || state.flashMode != light::FlashMode::NONE)
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
    return 1;
}
