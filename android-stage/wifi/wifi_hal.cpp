// SPDX-License-Identifier: Apache-2.0
#include "wifi_interface.h"

#include <hardware_legacy/wifi_hal.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <thread>

struct wifi_interface_info {
    unsigned index;
};

struct wifi_info {
    explicit wifi_info(unsigned index) : iface{index} {}

    wifi_interface_info iface;
    wifi_interface_handle list[1]{&iface};
    std::mutex mutex;
    std::condition_variable stop;
    bool stopping = false;
    wifi_cleaned_up_handler afterStop = nullptr;
};

namespace {

using j72s::wifi::kInterfaceName;
using namespace std::chrono_literals;
using NlSocket = std::unique_ptr<nl_sock, decltype(&nl_socket_free)>;
using NlMessage = std::unique_ptr<nl_msg, decltype(&nlmsg_free)>;

bool interfacePresent(unsigned index) {
    return index != 0 && if_nametoindex(kInterfaceName) == index;
}

wifi_error waitForDriver() {
    const auto end = std::chrono::steady_clock::now() + 5s;
    do {
        const auto iface = j72s::wifi::findInterface("/sys/class/net");
        if (iface && interfacePresent(*iface)) {
            return WIFI_SUCCESS;
        }
        std::this_thread::sleep_until(std::min(end, std::chrono::steady_clock::now() + 100ms));
    } while (std::chrono::steady_clock::now() < end);
    return WIFI_ERROR_TIMED_OUT;
}

wifi_error initialize(wifi_handle* handleOut) {
    if (!handleOut) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    *handleOut = nullptr;
    const auto iface = j72s::wifi::findInterface("/sys/class/net");
    if (!iface || !interfacePresent(*iface)) {
        return WIFI_ERROR_NOT_AVAILABLE;
    }
    *handleOut = new (std::nothrow) wifi_info(*iface);
    return *handleOut ? WIFI_SUCCESS : WIFI_ERROR_OUT_OF_MEMORY;
}

void cleanup(wifi_handle handle, wifi_cleaned_up_handler callback) {
    if (!handle) {
        return;
    }
    std::lock_guard lock(handle->mutex);
    if (!handle->stopping) {
        handle->afterStop = callback;
        handle->stopping = true;
        handle->stop.notify_one();
    }
}

void eventLoop(wifi_handle handle) {
    if (!handle) {
        return;
    }
    std::unique_ptr<wifi_info> session(handle);
    std::unique_lock lock(handle->mutex);
    handle->stop.wait(lock, [&] { return handle->stopping; });
    const auto callback = handle->afterStop;
    lock.unlock();
    if (callback) {
        callback(handle);
    }
}

wifi_error getInterfaces(wifi_handle handle, int* count, wifi_interface_handle** list) {
    if (!handle || !count || !list) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    *count = 1;
    *list = handle->list;
    return WIFI_SUCCESS;
}

wifi_error getInterfaceName(wifi_interface_handle iface, char* name, size_t length) {
    if (!iface || !name || length < sizeof(kInterfaceName)) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    std::memcpy(name, kInterfaceName, sizeof(kInterfaceName));
    return WIFI_SUCCESS;
}

bool supportedType(uint32_t type) {
    return type == WIFI_INTERFACE_TYPE_STA || type == WIFI_INTERFACE_TYPE_AP;
}

wifi_error getSupportedInterfaceName(wifi_handle handle, uint32_t type, char* name, size_t length) {
    if (!handle) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    if (!supportedType(type)) {
        return WIFI_ERROR_NOT_SUPPORTED;
    }
    return getInterfaceName(&handle->iface, name, length);
}

wifi_error existingInterface(wifi_handle handle, const char* name) {
    if (!handle || !name || std::string_view(name) != kInterfaceName) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    return interfacePresent(handle->iface.index) ? WIFI_SUCCESS : WIFI_ERROR_NOT_AVAILABLE;
}

wifi_error claimInterface(wifi_handle handle, const char* name, wifi_interface_type type) {
    // cfg80211 owns wlan0. Supplicant and hostapd configure its role via nl80211,
    // and Android allocates at most one of the two roles at a time.
    return supportedType(type) ? existingInterface(handle, name) : WIFI_ERROR_NOT_SUPPORTED;
}

template <typename Fill>
wifi_error request(int command, int flags, Fill fill, nl_recvmsg_msg_cb_t callback = nullptr,
                   void* data = nullptr) {
    NlSocket socket(nl_socket_alloc(), nl_socket_free);
    NlMessage message(nlmsg_alloc(), nlmsg_free);
    if (!socket || !message) {
        return WIFI_ERROR_OUT_OF_MEMORY;
    }
    if (genl_connect(socket.get()) < 0) {
        return WIFI_ERROR_NOT_AVAILABLE;
    }
    const timeval timeout{1, 0};
    if (setsockopt(nl_socket_get_fd(socket.get()), SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) != 0) {
        return WIFI_ERROR_UNKNOWN;
    }
    const int family = genl_ctrl_resolve(socket.get(), "nl80211");
    if (family < 0) {
        return WIFI_ERROR_NOT_AVAILABLE;
    }
    if (!genlmsg_put(message.get(), NL_AUTO_PORT, NL_AUTO_SEQ, family, 0, flags, command, 0) ||
        fill(message.get()) < 0) {
        return WIFI_ERROR_UNKNOWN;
    }
    if (callback && nl_socket_modify_cb(socket.get(), NL_CB_VALID, NL_CB_CUSTOM, callback, data) < 0) {
        return WIFI_ERROR_UNKNOWN;
    }
    if (nl_send_auto(socket.get(), message.get()) < 0) {
        return WIFI_ERROR_UNKNOWN;
    }
    const int status = nl_recvmsgs_default(socket.get());
    return status >= 0 ? WIFI_SUCCESS : WIFI_ERROR_UNKNOWN;
}

int onWiphy(nl_msg* message, void* opaque) {
    auto& features = *static_cast<feature_set*>(opaque);
    nlattr* attrs[NL80211_ATTR_MAX + 1]{};
    if (genlmsg_parse(nlmsg_hdr(message), 0, attrs, NL80211_ATTR_MAX, nullptr) < 0) {
        return NL_SKIP;
    }
    if (attrs[NL80211_ATTR_WIPHY_BANDS]) {
        nlattr* band;
        int remaining;
        nla_for_each_nested(band, attrs[NL80211_ATTR_WIPHY_BANDS], remaining) {
            if (nla_type(band) == NL80211_BAND_5GHZ) {
                features |= WIFI_FEATURE_INFRA_5G;
            }
        }
    }
    if (attrs[NL80211_ATTR_SUPPORTED_IFTYPES]) {
        nlattr* type;
        int remaining;
        nla_for_each_nested(type, attrs[NL80211_ATTR_SUPPORTED_IFTYPES], remaining) {
            if (nla_type(type) == NL80211_IFTYPE_STATION) {
                features |= WIFI_FEATURE_INFRA;
            } else if (nla_type(type) == NL80211_IFTYPE_AP) {
                features |= WIFI_FEATURE_SOFT_AP;
            }
        }
    }
    return NL_OK;
}

wifi_error getSupportedFeatures(wifi_interface_handle iface, feature_set* out) {
    if (!iface || !out) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    if (!interfacePresent(iface->index)) {
        return WIFI_ERROR_NOT_AVAILABLE;
    }
    feature_set features = 0;
    const auto status = request(NL80211_CMD_GET_WIPHY, NLM_F_DUMP, [&](nl_msg* message) {
        const int result = nla_put_u32(message, NL80211_ATTR_IFINDEX, iface->index);
        if (result < 0) {
            return result;
        }
        return nla_put_flag(message, NL80211_ATTR_SPLIT_WIPHY_DUMP);
    }, onWiphy, &features);
    if (status != WIFI_SUCCESS) {
        return status;
    }
    if ((features & WIFI_FEATURE_INFRA) == 0) {
        return WIFI_ERROR_NOT_AVAILABLE;
    }
    *out = features;
    return WIFI_SUCCESS;
}

wifi_error setCountry(wifi_interface_handle iface, const char* code) {
    if (!iface || !code) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    const std::string_view country(code);
    const auto isUpper = [](char letter) { return letter >= 'A' && letter <= 'Z'; };
    if (country != "00" && (country.size() != 2 ||
                           !std::all_of(country.begin(), country.end(), isUpper))) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    if (!interfacePresent(iface->index)) {
        return WIFI_ERROR_NOT_AVAILABLE;
    }
    return request(NL80211_CMD_REQ_SET_REG, 0, [code](nl_msg* message) {
        return nla_put_string(message, NL80211_ATTR_REG_ALPHA2, code);
    });
}

}  // namespace

// The AOSP Wi-Fi service initializes its optional entries with explicit
// NOT_SUPPORTED callbacks before loading this shared library.
extern "C" __attribute__((visibility("default")))
wifi_error init_wifi_vendor_hal_func_table(wifi_hal_fn* table) {
    if (!table) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    table->wifi_wait_for_driver_ready = waitForDriver;
    table->wifi_initialize = initialize;
    table->wifi_cleanup = cleanup;
    table->wifi_event_loop = eventLoop;
    table->wifi_get_ifaces = getInterfaces;
    table->wifi_get_iface_name = getInterfaceName;
    table->wifi_get_supported_iface_name = getSupportedInterfaceName;
    table->wifi_virtual_interface_create = claimInterface;
    table->wifi_virtual_interface_delete = existingInterface;
    table->wifi_get_supported_feature_set = getSupportedFeatures;
    table->wifi_set_country_code = setCountry;
    return WIFI_SUCCESS;
}
