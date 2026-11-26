// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "j72s-sensors"

#include "iio_sensors.h"

#include <aidl/android/hardware/sensors/BnSensors.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <fmq/AidlMessageQueue.h>
#include <fmq/EventFlag.h>
#include <log/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

namespace j72s::sensors {
namespace {

using namespace std::chrono_literals;
using namespace aidl::android::hardware::sensors;
using aidl::android::hardware::common::fmq::MQDescriptor;
using aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using android::AidlMessageQueue;
using android::hardware::EventFlag;
using ndk::ScopedAStatus;
using Clock = std::chrono::steady_clock;
using EventQueue = AidlMessageQueue<Event, SynchronizedReadWrite>;
using WakeQueue = AidlMessageQueue<int32_t, SynchronizedReadWrite>;
using Payload = Event::EventPayload;

constexpr int64_t kDefaultPeriodNs = 100000000;
constexpr int64_t kLightMinPeriodNs = 400000000;
constexpr int64_t kLightMaxPeriodNs = 5000000000;
constexpr auto kBufferPoll = 5ms;
constexpr size_t kMaxFlushRequests = 128;

struct Description {
    const char* name;
    SensorType type;
    const char* typeName;
};

constexpr std::array<Description, 4> kDescriptions{{
    {"Apple AOP Accelerometer", SensorType::ACCELEROMETER, "android.sensor.accelerometer"},
    {"Apple AOP Gyroscope", SensorType::GYROSCOPE, "android.sensor.gyroscope"},
    {"Apple AOP Barometer", SensorType::PRESSURE, "android.sensor.pressure"},
    {"Apple CT819 Ambient Light", SensorType::LIGHT, "android.sensor.light"},
}};

SensorInfo sensorInfo(const Device& device) {
    const auto index = static_cast<size_t>(device.kind);
    const auto& description = kDescriptions[index];
    const bool light = device.kind == Kind::Light;
    const bool pressure = device.kind == Kind::Pressure;
    SensorInfo info{};
    info.sensorHandle = static_cast<int32_t>(index) + 1;
    info.name = description.name;
    info.vendor = "Apple";
    info.version = 1;
    info.type = description.type;
    info.typeAsString = description.typeName;
    // CT819 publishes a 5000-lux ceiling. The AOP drivers expose signed
    // vector or unsigned pressure counts without a separate range attribute.
    const double resolution = device.scale * (pressure ? 10 : 1);
    info.resolution = light ? 1 : static_cast<float>(resolution);
    const double countMax = pressure ? static_cast<double>(std::numeric_limits<uint32_t>::max())
                                     : static_cast<double>(std::numeric_limits<int32_t>::max());
    info.maxRange = light ? 5000 : static_cast<float>(resolution * countMax);
    info.minDelayUs = (light ? kLightMinPeriodNs : kAopMinPeriodNs) / 1000;
    info.maxDelayUs = (light ? kLightMaxPeriodNs : kAopMaxPeriodNs) / 1000;
    info.flags = light ? SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE
                       : SensorInfo::SENSOR_FLAG_BITS_CONTINUOUS_MODE;
    return info;
}

Event sampleEvent(const SensorInfo& info, const Sample& sample) {
    Event event{};
    event.timestamp = sample.timestampNs;
    event.sensorType = info.type;
    event.sensorHandle = info.sensorHandle;
    if (info.type == SensorType::ACCELEROMETER || info.type == SensorType::GYROSCOPE) {
        event.payload.set<Payload::Tag::vec3>(Payload::Vec3{
            sample.values[0], sample.values[1], sample.values[2], SensorStatus::UNRELIABLE});
    } else {
        event.payload.set<Payload::Tag::scalar>(sample.values[0]);
    }
    return event;
}

Event flushEvent(int32_t handle) {
    using Type = Payload::MetaData::MetaDataEventType;
    Event event{};
    event.sensorHandle = handle;
    event.sensorType = SensorType::META_DATA;
    event.payload.set<Payload::Tag::meta>(Payload::MetaData{Type::META_DATA_FLUSH_COMPLETE});
    return event;
}

ScopedAStatus badValue() {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ScopedAStatus unsupported() {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

class Sensors final : public BnSensors {
public:
    explicit Sensors(std::vector<Device> devices) {
        for (auto& device : devices) {
            states_.emplace_back(std::move(device));
        }
        worker_ = std::thread(&Sensors::work, this, false);
        lightWorker_ = std::thread(&Sensors::work, this, true);
    }

    ~Sensors() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            condition_.notify_all();
        }
        worker_.join();
        lightWorker_.join();
        if (eventFlag_) {
            EventFlag::deleteEventFlag(&eventFlag_);
        }
    }

    ScopedAStatus getSensorsList(std::vector<SensorInfo>* result) override {
        result->clear();
        for (const auto& state : states_) {
            result->push_back(state.info);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus initialize(const MQDescriptor<Event, SynchronizedReadWrite>& events,
                             const MQDescriptor<int32_t, SynchronizedReadWrite>& wakeLock,
                             const std::shared_ptr<ISensorsCallback>& callback) override {
        std::lock_guard lock(mutex_);
        // Validate both descriptors before touching any existing capture.
        auto wakeQueue = std::make_unique<WakeQueue>(wakeLock, false);
        auto queue = std::make_unique<EventQueue>(events, false);
        if (!callback || !wakeQueue->isValid() || !queue->isValid() ||
            !queue->getEventFlagWord()) {
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        EventFlag* flag = nullptr;
        if (EventFlag::createEventFlag(queue->getEventFlagWord(), &flag) != android::OK) {
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        for (auto& state : states_) {
            deactivate(state);
            state.periodNs = defaultPeriod(state);
        }
        if (eventFlag_) {
            EventFlag::deleteEventFlag(&eventFlag_);
        }
        eventQueue_ = std::move(queue);
        wakeQueue_ = std::move(wakeQueue);
        eventFlag_ = flag;
        callback_ = callback;
        condition_.notify_all();
        return ScopedAStatus::ok();
    }

    ScopedAStatus activate(int32_t handle, bool enabled) override {
        std::lock_guard lock(mutex_);
        State* state = find(handle);
        if (!state || !eventQueue_) {
            return badValue();
        }
        if (state->active == enabled) {
            return ScopedAStatus::ok();
        }
        if (enabled) {
            if (state->buffer && !state->buffer->start(state->periodNs)) {
                ALOGE("Cannot start IIO capture for %s", state->info.name.c_str());
                return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
            state->active = true;
            state->nextPoll = Clock::now();
        } else if (!deactivate(*state)) {
            condition_.notify_all();
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        condition_.notify_all();
        return ScopedAStatus::ok();
    }

    ScopedAStatus batch(int32_t handle, int64_t periodNs, int64_t latencyNs) override {
        std::lock_guard lock(mutex_);
        State* state = find(handle);
        if (!state || !eventQueue_ || periodNs < 0 || latencyNs < 0) {
            return badValue();
        }
        const auto period = std::clamp<int64_t>(periodNs, state->info.minDelayUs * 1000LL,
                                               state->info.maxDelayUs * 1000LL);
        if (period == state->periodNs) {
            return ScopedAStatus::ok();
        }
        if (state->active && state->buffer && !state->buffer->setPeriod(period)) {
            ALOGE("Cannot change IIO sampling rate for %s", state->info.name.c_str());
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        state->periodNs = period;
        if (!state->buffer && !state->flushes && state->lastPoll != Clock::time_point{}) {
            state->nextPoll = state->lastPoll + std::chrono::nanoseconds(period);
        }
        condition_.notify_all();
        return ScopedAStatus::ok();
    }

    ScopedAStatus flush(int32_t handle) override {
        std::lock_guard lock(mutex_);
        State* state = find(handle);
        if (!state || !state->active) {
            return badValue();
        }
        if (state->flushes == kMaxFlushRequests) {
            return ScopedAStatus::fromServiceSpecificError(ISensors::ERROR_NO_MEMORY);
        }
        ++state->flushes;
        state->nextPoll = Clock::now();
        condition_.notify_all();
        return ScopedAStatus::ok();
    }

    ScopedAStatus setOperationMode(OperationMode mode) override {
        if (mode == OperationMode::NORMAL) {
            return ScopedAStatus::ok();
        }
        return unsupported();
    }
    ScopedAStatus injectSensorData(const Event&) override { return unsupported(); }
    ScopedAStatus registerDirectChannel(const SharedMemInfo&, int32_t*) override {
        return unsupported();
    }
    ScopedAStatus configDirectReport(int32_t, int32_t, RateLevel, int32_t*) override {
        return unsupported();
    }
    ScopedAStatus unregisterDirectChannel(int32_t) override { return unsupported(); }

private:
    struct State {
        explicit State(Device input)
            : device(std::move(input)),
              info(sensorInfo(device)),
              buffer(device.kind == Kind::Light ? nullptr : std::make_unique<Buffer>(device)),
              periodNs(defaultPeriod(*this)) {}

        Device device;
        SensorInfo info;
        std::unique_ptr<Buffer> buffer;
        int64_t periodNs;
        bool active = false;
        uint64_t generation = 0;
        size_t flushes = 0;
        std::optional<float> lastLux;
        Clock::time_point lastPoll{};
        Clock::time_point nextPoll{};
        std::vector<Event> pending;
    };

    static int64_t defaultPeriod(const State& state) {
        return state.buffer ? kDefaultPeriodNs : kLightMinPeriodNs;
    }

    State* find(int32_t handle) {
        const auto found = std::find_if(states_.begin(), states_.end(), [handle](const State& s) {
            return s.info.sensorHandle == handle;
        });
        return found == states_.end() ? nullptr : &*found;
    }

    bool deactivate(State& state) {
        const bool restored = !state.buffer || state.buffer->stop();
        if (!restored) {
            ALOGE("Cannot restore IIO configuration for %s", state.info.name.c_str());
        }
        state.active = false;
        ++state.generation;
        state.flushes = 0;
        state.lastLux.reset();
        state.lastPoll = {};
        state.pending.clear();
        return restored;
    }

    void publish(State& state) {
        const auto count = std::min(state.pending.size(), eventQueue_->availableToWrite());
        if (count && eventQueue_->write(state.pending.data(), count)) {
            state.pending.erase(state.pending.begin(), state.pending.begin() + count);
            eventFlag_->wake(ISensors::EVENT_QUEUE_FLAG_BITS_READ_AND_PROCESS);
        }
    }

    void capture(State& state, std::unique_lock<std::mutex>& lock) {
        if (state.buffer) {
            std::vector<Sample> samples;
            if (!state.buffer->read(samples)) {
                ALOGE("Cannot read IIO capture for %s", state.info.name.c_str());
                deactivate(state);
                return;
            }
            for (const auto& sample : samples) {
                state.pending.push_back(sampleEvent(state.info, sample));
            }
            // Drain the IIO kernel queue before releasing the flush markers.
            if (!samples.empty()) {
                state.nextPoll = Clock::now();
                return;
            }
            state.nextPoll = Clock::now() + kBufferPoll;
        } else {
            const auto now = Clock::now();
            const auto due = state.lastPoll + std::chrono::nanoseconds(state.periodNs);
            if (state.lastPoll == Clock::time_point{} || now >= due) {
                const auto generation = state.generation;
                lock.unlock();
                const auto sample = readLight(state.device);
                lock.lock();
                if (generation != state.generation) {
                    return;
                }
                state.lastPoll = Clock::now();
                if (sample && sample->values[0] != state.lastLux) {
                    state.lastLux = sample->values[0];
                    state.pending.push_back(sampleEvent(state.info, *sample));
                }
            }
            state.nextPoll = state.lastPoll + std::chrono::nanoseconds(state.periodNs);
        }
        while (state.flushes) {
            state.pending.push_back(flushEvent(state.info.sensorHandle));
            --state.flushes;
        }
    }

    void work(bool light) {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            auto wake = Clock::time_point::max();
            for (auto& state : states_) {
                if (!state.active || light != (state.device.kind == Kind::Light)) {
                    continue;
                }
                publish(state);
                if (state.pending.empty() && state.nextPoll <= Clock::now()) {
                    capture(state, lock);
                    publish(state);
                }
                if (state.active) {
                    wake = std::min(wake, state.pending.empty() ? state.nextPoll
                                                               : Clock::now() + kBufferPoll);
                }
            }
            if (wake == Clock::time_point::max()) {
                condition_.wait(lock);
            } else if (wake > Clock::now()) {
                condition_.wait_until(lock, wake);
            } else {
                lock.unlock();
                std::this_thread::yield();
                lock.lock();
            }
        }
    }

    std::vector<State> states_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::unique_ptr<EventQueue> eventQueue_;
    std::unique_ptr<WakeQueue> wakeQueue_;
    EventFlag* eventFlag_ = nullptr;
    std::shared_ptr<ISensorsCallback> callback_;
    std::thread worker_;
    std::thread lightWorker_;
    bool stopping_ = false;
};

}  // namespace
}  // namespace j72s::sensors

int main() {
    using namespace j72s::sensors;
    const auto deadline = Clock::now() + 5s;
    std::vector<Device> devices;
    do {
        devices = discover("/sys/bus/iio/devices", "/dev");
        if (devices.size() == kDescriptions.size()) {
            break;
        }
        std::this_thread::sleep_for(100ms);
    } while (Clock::now() < deadline);
    if (devices.size() != kDescriptions.size()) {
        ALOGW("Found %zu of %zu expected J72s sensors", devices.size(), kDescriptions.size());
    }
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    auto service = ndk::SharedRefBase::make<Sensors>(std::move(devices));
    const std::string instance = std::string(Sensors::descriptor) + "/default";
    if (AServiceManager_addService(service->asBinder().get(), instance.c_str()) != STATUS_OK) {
        ALOGE("Failed to register %s", instance.c_str());
        return EXIT_FAILURE;
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
