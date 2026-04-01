#include <gsusb/gsusb.hpp>

#include "libusb_dyn.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gsusb {

namespace {

using detail::LibUsbApi;
using detail::build_request_type;
using detail::endpoint_direction;
using detail::endpoint_type;
using detail::libusb_config_descriptor;
using detail::libusb_context;
using detail::libusb_device;
using detail::libusb_device_descriptor;
using detail::libusb_device_handle;
using detail::LIBUSB_ENDPOINT_IN;
using detail::LIBUSB_TRANSFER_TYPE_BULK;

constexpr std::uint8_t REQ_HOST_FORMAT = 0;
constexpr std::uint8_t REQ_BITTIMING = 1;
constexpr std::uint8_t REQ_MODE = 2;
constexpr std::uint8_t REQ_BT_CONST = 4;
constexpr std::uint8_t REQ_DEVICE_CONFIG = 5;
constexpr std::uint8_t REQ_DATA_BITTIMING = 10;
constexpr std::uint8_t REQ_BT_CONST_EXT = 11;
constexpr std::uint8_t REQ_SET_TERMINATION = 12;
constexpr std::uint8_t REQ_GET_TERMINATION = 13;

constexpr std::uint32_t GS_CAN_MODE_RESET = 0;
constexpr std::uint32_t GS_CAN_MODE_START = 1;

constexpr std::uint32_t GS_CAN_MODE_LISTEN_ONLY = 1u << 0;
constexpr std::uint32_t GS_CAN_MODE_HW_TIMESTAMP = 1u << 4;
constexpr std::uint32_t GS_CAN_MODE_FD = 1u << 8;

constexpr std::uint32_t GS_CAN_FEATURE_HW_TIMESTAMP = 1u << 4;
constexpr std::uint32_t GS_CAN_FEATURE_FD = 1u << 8;
constexpr std::uint32_t GS_CAN_FEATURE_TERMINATION = 1u << 11;

constexpr std::uint8_t GS_CAN_FLAG_FD = 1u << 1;
constexpr std::uint8_t GS_CAN_FLAG_BRS = 1u << 2;
constexpr std::uint8_t GS_CAN_FLAG_ESI = 1u << 3;

constexpr std::uint32_t CAN_EFF_FLAG = 0x80000000u;
constexpr std::uint32_t CAN_RTR_FLAG = 0x40000000u;
constexpr std::uint32_t CAN_ERR_FLAG = 0x20000000u;
constexpr std::uint32_t CAN_EFF_MASK = 0x1FFFFFFFu;
constexpr std::uint32_t CAN_SFF_MASK = 0x000007FFu;

constexpr unsigned int CTRL_TIMEOUT_MS = 500;
constexpr unsigned int BULK_TIMEOUT_MS = 5;
constexpr int LIBUSB_SUCCESS = 0;
constexpr int LIBUSB_ERROR_TIMEOUT = -7;

class WinMutex {
public:
    WinMutex() {
        InitializeCriticalSection(&cs_);
    }

    ~WinMutex() {
        DeleteCriticalSection(&cs_);
    }

    WinMutex(const WinMutex&) = delete;
    WinMutex& operator=(const WinMutex&) = delete;

    void lock() {
        EnterCriticalSection(&cs_);
    }

    void unlock() {
        LeaveCriticalSection(&cs_);
    }

private:
    CRITICAL_SECTION cs_{};
};

WinMutex g_registry_lock;
std::map<std::string, std::weak_ptr<class SharedUsbSession>> g_session_registry;

class ExclusiveLock {
public:
    explicit ExclusiveLock(WinMutex& lock) : lock_(lock) {
        lock_.lock();
    }

    ~ExclusiveLock() {
        lock_.unlock();
    }

    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;

private:
    WinMutex& lock_;
};

class MessageSink {
public:
    virtual ~MessageSink() = default;
    virtual void on_message_received(const CanMessage& message) = 0;
};

std::string format_libusb_error(const char* what, int code) {
    std::ostringstream oss;
    oss << what << " failed with libusb error " << code;
    return oss.str();
}

bool is_timeout(int code) {
    return code == LIBUSB_ERROR_TIMEOUT;
}

std::vector<std::uint32_t> bytes_to_u32_list(const std::vector<std::uint8_t>& data) {
    if ((data.size() % 4u) != 0u) {
        throw std::runtime_error("buffer length must be multiple of 4");
    }
    std::vector<std::uint32_t> result;
    result.reserve(data.size() / 4u);
    for (std::size_t index = 0; index < data.size(); index += 4u) {
        std::uint32_t value = 0;
        std::memcpy(&value, data.data() + index, sizeof(value));
        result.push_back(value);
    }
    return result;
}

int dlc_to_len(std::uint8_t dlc) {
    static constexpr std::array<int, 16> kDlcMap = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    return kDlcMap.at(std::min<std::size_t>(dlc, kDlcMap.size() - 1));
}

std::uint8_t len_to_dlc(int len) {
    if (len <= 8) {
        return static_cast<std::uint8_t>(std::max(len, 0));
    }
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

int calc_rx_size(const ChannelCapabilities& cap) {
    if (cap.fd_supported) {
        return cap.hardware_timestamp ? 80 : 76;
    }
    return cap.hardware_timestamp ? 24 : 20;
}

int calc_tx_size(const ChannelCapabilities& cap) {
    return cap.fd_supported ? 76 : 20;
}

std::pair<std::optional<int>, std::optional<int>> parse_selector_bus_address(const std::string& selector, std::optional<int>& interface_number, std::string& serial_number) {
    if (selector.empty() || selector == "auto") {
        return {std::nullopt, std::nullopt};
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= selector.size()) {
        const std::size_t pos = selector.find(':', start);
        if (pos == std::string::npos) {
            parts.push_back(selector.substr(start));
            break;
        }
        parts.push_back(selector.substr(start, pos - start));
        start = pos + 1;
    }
    if ((parts.size() == 2u || parts.size() == 3u) && std::all_of(parts.begin(), parts.end(), [](const std::string& value) {
            return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
        })) {
        const int bus = std::stoi(parts[0]);
        const int address = std::stoi(parts[1]);
        if (parts.size() == 3u) {
            interface_number = std::stoi(parts[2]);
        }
        return {bus, address};
    }
    serial_number = selector;
    return {std::nullopt, std::nullopt};
}

std::string read_string_descriptor(libusb_device* device, std::uint8_t descriptor_index) {
    if (descriptor_index == 0) {
        return {};
    }
    auto& api = LibUsbApi::instance();
    libusb_device_handle* handle = nullptr;
    if (api.open(device, &handle) != LIBUSB_SUCCESS || handle == nullptr) {
        return {};
    }
    std::array<unsigned char, 256> buffer{};
    const int length = api.get_string_descriptor_ascii(handle, descriptor_index, buffer.data(), static_cast<int>(buffer.size()));
    api.close(handle);
    if (length <= 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(length));
}

struct BulkInterfaceInfo {
    int interface_number = 0;
    std::uint8_t in_ep = 0;
    std::uint8_t out_ep = 0;
};

BulkInterfaceInfo find_bulk_interface(libusb_device* device, std::optional<int> interface_hint) {
    auto& api = LibUsbApi::instance();
    libusb_config_descriptor* config = nullptr;
    int rc = api.get_active_config_descriptor(device, &config);
    if (rc != LIBUSB_SUCCESS || config == nullptr) {
        rc = api.get_config_descriptor(device, 0, &config);
    }
    if (rc != LIBUSB_SUCCESS || config == nullptr) {
        throw std::runtime_error(format_libusb_error("libusb_get_config_descriptor", rc));
    }

    BulkInterfaceInfo result{};
    bool found = false;
    for (std::uint8_t interface_index = 0; interface_index < config->bNumInterfaces && !found; ++interface_index) {
        const auto& interface_desc = config->interface[interface_index];
        for (int alt_index = 0; alt_index < interface_desc.num_altsetting && !found; ++alt_index) {
            const auto& alt = interface_desc.altsetting[alt_index];
            if (interface_hint.has_value() && alt.bInterfaceNumber != interface_hint.value()) {
                continue;
            }
            std::optional<std::uint8_t> in_ep;
            std::optional<std::uint8_t> out_ep;
            for (std::uint8_t endpoint_index = 0; endpoint_index < alt.bNumEndpoints; ++endpoint_index) {
                const auto& endpoint = alt.endpoint[endpoint_index];
                if (endpoint_type(endpoint.bmAttributes) != LIBUSB_TRANSFER_TYPE_BULK) {
                    continue;
                }
                if (endpoint_direction(endpoint.bEndpointAddress) == LIBUSB_ENDPOINT_IN) {
                    in_ep = endpoint.bEndpointAddress;
                } else {
                    out_ep = endpoint.bEndpointAddress;
                }
            }
            if (in_ep.has_value() && out_ep.has_value()) {
                result.interface_number = alt.bInterfaceNumber;
                result.in_ep = in_ep.value();
                result.out_ep = out_ep.value();
                found = true;
            }
        }
    }

    api.free_config_descriptor(config);
    if (!found) {
        throw std::runtime_error("bulk endpoints not found");
    }
    return result;
}

DeviceSummary build_summary(libusb_device* device, const libusb_device_descriptor& descriptor, int interface_number) {
    auto& api = LibUsbApi::instance();
    DeviceSummary summary;
    summary.bus = api.get_bus_number(device);
    summary.address = api.get_device_address(device);
    summary.vendor_id = descriptor.idVendor;
    summary.product_id = descriptor.idProduct;
    summary.interface_number = interface_number;
    summary.serial_number = read_string_descriptor(device, descriptor.iSerialNumber);
    summary.manufacturer = read_string_descriptor(device, descriptor.iManufacturer);
    summary.product = read_string_descriptor(device, descriptor.iProduct);

    std::ostringstream key;
    key.fill('0');
    key << std::setw(3) << summary.bus << ':' << std::setw(3) << summary.address << ':'
        << std::uppercase << std::hex << std::setw(4) << summary.vendor_id << ':' << std::setw(4) << summary.product_id << ':'
        << std::dec << interface_number;
    summary.key = key.str();

    std::ostringstream label;
    label.fill('0');
    label << "USB " << std::setw(3) << summary.bus << ':' << std::setw(3) << summary.address << " ["
          << std::uppercase << std::hex << std::setw(4) << summary.vendor_id << ':' << std::setw(4) << summary.product_id << "] "
          << (summary.product.empty() ? "GSUSB" : summary.product);
    summary.label = label.str();
    return summary;
}

struct EnumeratedDevice {
    libusb_device* device = nullptr;
    DeviceSummary summary;
};

std::vector<EnumeratedDevice> enumerate_matching_devices(std::uint16_t vendor_id, std::uint16_t product_id) {
    auto& api = LibUsbApi::instance();
    api.ensure_loaded();

    libusb_context* context = nullptr;
    const int init_rc = api.init(&context);
    if (init_rc != LIBUSB_SUCCESS) {
        throw std::runtime_error(format_libusb_error("libusb_init", init_rc));
    }

    libusb_device** list = nullptr;
    const std::intptr_t count = api.get_device_list(context, &list);
    if (count < 0) {
        api.exit(context);
        throw std::runtime_error(format_libusb_error("libusb_get_device_list", static_cast<int>(count)));
    }

    std::vector<EnumeratedDevice> result;
    result.reserve(static_cast<std::size_t>(count));

    for (std::intptr_t index = 0; index < count; ++index) {
        libusb_device* device = list[index];
        libusb_device_descriptor descriptor{};
        if (api.get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != vendor_id || descriptor.idProduct != product_id) {
            continue;
        }
        try {
            const auto bulk = find_bulk_interface(device, std::nullopt);
            result.push_back(EnumeratedDevice{device, build_summary(device, descriptor, bulk.interface_number)});
        } catch (...) {
            continue;
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.summary.bus != rhs.summary.bus) return lhs.summary.bus < rhs.summary.bus;
        if (lhs.summary.address != rhs.summary.address) return lhs.summary.address < rhs.summary.address;
        return lhs.summary.interface_number < rhs.summary.interface_number;
    });

    api.free_device_list(list, 1);
    api.exit(context);
    return result;
}

ChannelCapabilities read_channel_capabilities(libusb_device_handle* handle, int interface_number, int channel_index) {
    auto& api = LibUsbApi::instance();
    std::vector<unsigned char> ext_data(72);
    int rc = api.control_transfer(handle, build_request_type(true), REQ_BT_CONST_EXT, static_cast<std::uint16_t>(channel_index), static_cast<std::uint16_t>(interface_number), ext_data.data(), static_cast<std::uint16_t>(ext_data.size()), CTRL_TIMEOUT_MS);
    if (rc < 0) {
        ext_data.clear();
    } else {
        ext_data.resize(static_cast<std::size_t>(rc));
    }

    ChannelCapabilities caps;
    caps.index = channel_index;
    if (ext_data.size() >= 72u) {
        const auto words = bytes_to_u32_list(std::vector<std::uint8_t>(ext_data.begin(), ext_data.begin() + 72));
        caps.feature_flags = words[0];
        caps.nominal_limits = TimingLimits{static_cast<int>(words[1]), static_cast<int>(words[2]), static_cast<int>(words[3]), static_cast<int>(words[4]), static_cast<int>(words[5]), static_cast<int>(words[6]), static_cast<int>(words[7]), static_cast<int>(words[8]), static_cast<int>(words[9])};
        caps.data_limits = TimingLimits{static_cast<int>(words[1]), static_cast<int>(words[10]), static_cast<int>(words[11]), static_cast<int>(words[12]), static_cast<int>(words[13]), static_cast<int>(words[14]), static_cast<int>(words[15]), static_cast<int>(words[16]), static_cast<int>(words[17])};
    } else {
        std::vector<unsigned char> std_data(40);
        rc = api.control_transfer(handle, build_request_type(true), REQ_BT_CONST, static_cast<std::uint16_t>(channel_index), static_cast<std::uint16_t>(interface_number), std_data.data(), static_cast<std::uint16_t>(std_data.size()), CTRL_TIMEOUT_MS);
        if (rc < 40) {
            throw std::runtime_error("failed to read timing constants");
        }
        const auto words = bytes_to_u32_list(std::vector<std::uint8_t>(std_data.begin(), std_data.begin() + 40));
        caps.feature_flags = words[0];
        caps.nominal_limits = TimingLimits{static_cast<int>(words[1]), static_cast<int>(words[2]), static_cast<int>(words[3]), static_cast<int>(words[4]), static_cast<int>(words[5]), static_cast<int>(words[6]), static_cast<int>(words[7]), static_cast<int>(words[8]), static_cast<int>(words[9])};
    }
    caps.fd_supported = (caps.feature_flags & GS_CAN_FEATURE_FD) != 0u;
    caps.hardware_timestamp = (caps.feature_flags & GS_CAN_FEATURE_HW_TIMESTAMP) != 0u;
    caps.termination_supported = (caps.feature_flags & GS_CAN_FEATURE_TERMINATION) != 0u;
    if (caps.termination_supported) {
        std::vector<unsigned char> term_data(4);
        rc = api.control_transfer(handle, build_request_type(true), REQ_GET_TERMINATION, static_cast<std::uint16_t>(channel_index), static_cast<std::uint16_t>(interface_number), term_data.data(), static_cast<std::uint16_t>(term_data.size()), CTRL_TIMEOUT_MS);
        if (rc >= 4) {
            std::uint32_t value = 0;
            std::memcpy(&value, term_data.data(), sizeof(value));
            caps.termination_enabled = value == 120;
        }
    }
    return caps;
}

struct DeviceInfo {
    std::uint32_t sw_version = 0;
    std::uint32_t hw_version = 0;
    std::vector<ChannelCapabilities> channels;
};

DeviceInfo read_device_info(libusb_device_handle* handle, int interface_number) {
    auto& api = LibUsbApi::instance();
    std::vector<unsigned char> config(12);
    const int rc = api.control_transfer(handle, build_request_type(true), REQ_DEVICE_CONFIG, 1, static_cast<std::uint16_t>(interface_number), config.data(), static_cast<std::uint16_t>(config.size()), CTRL_TIMEOUT_MS);
    if (rc < 12) {
        throw std::runtime_error("failed to read device config");
    }
    DeviceInfo info;
    info.sw_version = *reinterpret_cast<std::uint32_t*>(config.data() + 4);
    info.hw_version = *reinterpret_cast<std::uint32_t*>(config.data() + 8);
    const int channel_count = static_cast<int>(config[3]) + 1;
    info.channels.reserve(static_cast<std::size_t>(channel_count));
    for (int channel_index = 0; channel_index < channel_count; ++channel_index) {
        info.channels.push_back(read_channel_capabilities(handle, interface_number, channel_index));
    }
    return info;
}

void send_host_format(libusb_device_handle* handle, int interface_number) {
    auto& api = LibUsbApi::instance();
    std::uint32_t value = 0x0000BEEF;
    std::array<unsigned char, 4> payload{};
    std::memcpy(payload.data(), &value, sizeof(value));
    api.control_transfer(handle, build_request_type(false), REQ_HOST_FORMAT, 1, static_cast<std::uint16_t>(interface_number), payload.data(), static_cast<std::uint16_t>(payload.size()), CTRL_TIMEOUT_MS);
}

std::vector<unsigned char> to_timing_payload(const BusTiming& timing) {
    std::vector<unsigned char> payload(20);
    const std::array<std::uint32_t, 5> values = {
        static_cast<std::uint32_t>(timing.prop_seg),
        static_cast<std::uint32_t>(timing.phase_seg1),
        static_cast<std::uint32_t>(timing.phase_seg2),
        static_cast<std::uint32_t>(timing.sjw),
        static_cast<std::uint32_t>(timing.brp),
    };
    for (std::size_t index = 0; index < values.size(); ++index) {
        std::memcpy(payload.data() + (index * 4u), &values[index], sizeof(std::uint32_t));
    }
    return payload;
}

class SharedUsbSession : public std::enable_shared_from_this<SharedUsbSession> {
public:
    explicit SharedUsbSession(const BusOptions& options)
        : selector_(options.selector.empty() ? "auto" : options.selector) {
        auto& api = LibUsbApi::instance();
        api.ensure_loaded();

        int init_rc = api.init(&context_);
        if (init_rc != LIBUSB_SUCCESS) {
            throw std::runtime_error(format_libusb_error("libusb_init", init_rc));
        }

        std::optional<int> wanted_interface = options.interface_number;
        std::string wanted_serial = options.serial_number;
        const auto [wanted_bus, wanted_address] = parse_selector_bus_address(selector_, wanted_interface, wanted_serial);

        libusb_device** list = nullptr;
        const std::intptr_t count = api.get_device_list(context_, &list);
        if (count < 0) {
            api.exit(context_);
            context_ = nullptr;
            throw std::runtime_error(format_libusb_error("libusb_get_device_list", static_cast<int>(count)));
        }

        bool found = false;
        for (std::intptr_t index = 0; index < count && !found; ++index) {
            libusb_device* candidate = list[index];
            libusb_device_descriptor descriptor{};
            if (api.get_device_descriptor(candidate, &descriptor) != LIBUSB_SUCCESS) {
                continue;
            }
            if (descriptor.idVendor != options.vendor_id || descriptor.idProduct != options.product_id) {
                continue;
            }
            const auto bulk = find_bulk_interface(candidate, wanted_interface);
            const auto summary = build_summary(candidate, descriptor, bulk.interface_number);
            if (wanted_bus.has_value() && summary.bus != wanted_bus.value()) continue;
            if (wanted_address.has_value() && summary.address != wanted_address.value()) continue;
            if (!wanted_serial.empty() && summary.serial_number != wanted_serial) continue;

            summary_ = summary;
            interface_number_ = bulk.interface_number;
            in_ep_ = bulk.in_ep;
            out_ep_ = bulk.out_ep;

            const int open_rc = api.open(candidate, &handle_);
            if (open_rc != LIBUSB_SUCCESS || handle_ == nullptr) {
                throw std::runtime_error(format_libusb_error("libusb_open", open_rc));
            }
            api.free_device_list(list, 1);
            found = true;
        }

        if (!found) {
            api.free_device_list(list, 1);
            api.exit(context_);
            context_ = nullptr;
            throw std::runtime_error("no GSUSB device found");
        }

        api.set_configuration(handle_, 1);
        const int claim_rc = api.claim_interface(handle_, interface_number_);
        if (claim_rc != LIBUSB_SUCCESS) {
            cleanup();
            throw std::runtime_error(format_libusb_error("libusb_claim_interface", claim_rc));
        }

        send_host_format(handle_, interface_number_);
        const auto info = read_device_info(handle_, interface_number_);
        sw_version_ = info.sw_version;
        hw_version_ = info.hw_version;
        channels_ = info.channels;
        for (const auto& channel : channels_) {
            rx_frame_size_ = std::max(rx_frame_size_, calc_rx_size(channel));
            tx_sizes_[channel.index] = calc_tx_size(channel);
        }

    }

    ~SharedUsbSession() {
        close();
    }

    SharedUsbSession(const SharedUsbSession&) = delete;
    SharedUsbSession& operator=(const SharedUsbSession&) = delete;

    void add_bus(MessageSink* bus, int logical_channel) {
        ExclusiveLock lock(bus_lock_);
        buses_[logical_channel].push_back(bus);
        ++refs_;
    }

    bool remove_bus(MessageSink* bus, int logical_channel) {
        ExclusiveLock lock(bus_lock_);
        auto it = buses_.find(logical_channel);
        if (it != buses_.end()) {
            auto& listeners = it->second;
            listeners.erase(std::remove(listeners.begin(), listeners.end(), bus), listeners.end());
            if (listeners.empty()) {
                buses_.erase(it);
            }
        }
        refs_ = std::max(refs_ - 1, 0);
        return refs_ == 0;
    }

    void close() {
        if (closed_.exchange(true)) {
            return;
        }
        cleanup();
    }

    void configure_channel(int channel_index, const BusTiming& nominal, const std::optional<BusTiming>& data, bool fd_enabled, bool listen_only, bool termination_enabled, bool start) {
        close_channel(channel_index);
        write_bittiming(channel_index, REQ_BITTIMING, nominal);

        const auto& cap = channels_.at(static_cast<std::size_t>(channel_index));
        if (fd_enabled) {
            if (!cap.fd_supported || !cap.data_limits.has_value()) {
                throw std::runtime_error("channel does not support CAN FD");
            }
            if (!data.has_value()) {
                throw std::runtime_error("FD enabled but data timing missing");
            }
            write_bittiming(channel_index, REQ_DATA_BITTIMING, data.value());
        }

        if (cap.termination_supported) {
            std::uint32_t value = termination_enabled ? 120u : 0u;
            std::array<unsigned char, 4> payload{};
            std::memcpy(payload.data(), &value, sizeof(value));
            write_control(false, REQ_SET_TERMINATION, channel_index, payload.data(), static_cast<std::uint16_t>(payload.size()));
        }

        if (start) {
            std::uint32_t flags = 0;
            if (listen_only) flags |= GS_CAN_MODE_LISTEN_ONLY;
            if (fd_enabled) flags |= GS_CAN_MODE_FD;
            if (cap.hardware_timestamp) flags |= GS_CAN_MODE_HW_TIMESTAMP;
            std::array<unsigned char, 8> payload{};
            std::uint32_t mode = GS_CAN_MODE_START;
            std::memcpy(payload.data(), &mode, sizeof(mode));
            std::memcpy(payload.data() + 4, &flags, sizeof(flags));
            write_control(false, REQ_MODE, channel_index, payload.data(), static_cast<std::uint16_t>(payload.size()));
        }
    }

    void close_channel(int channel_index) {
        std::array<unsigned char, 8> payload{};
        std::uint32_t mode = GS_CAN_MODE_RESET;
        std::memcpy(payload.data(), &mode, sizeof(mode));
        write_control(false, REQ_MODE, channel_index, payload.data(), static_cast<std::uint16_t>(payload.size()));
    }

    void send_frame(int channel_index, const CanMessage& msg) {
        const auto tx_it = tx_sizes_.find(channel_index);
        if (tx_it == tx_sizes_.end()) {
            throw std::runtime_error("invalid channel index");
        }

        std::vector<unsigned char> payload(static_cast<std::size_t>(tx_it->second), 0);
        const std::uint32_t echo_id = 0xFFFFFFFFu;
        std::memcpy(payload.data(), &echo_id, sizeof(echo_id));

        std::uint32_t raw_can_id = msg.arbitration_id;
        if (msg.is_extended_id) raw_can_id |= CAN_EFF_FLAG;
        if (msg.is_remote_frame) raw_can_id |= CAN_RTR_FLAG;
        std::memcpy(payload.data() + 4, &raw_can_id, sizeof(raw_can_id));

        const int payload_len = static_cast<int>(msg.data.size());
        const std::uint8_t dlc_code = msg.is_fd ? len_to_dlc(payload_len) : static_cast<std::uint8_t>(std::min(payload_len, 8));
        const int copy_len = msg.is_fd ? dlc_to_len(dlc_code) : std::min(payload_len, 8);
        payload[8] = dlc_code;
        payload[9] = static_cast<unsigned char>(channel_index);
        std::uint8_t flags = 0;
        if (msg.is_fd) flags |= GS_CAN_FLAG_FD;
        if (msg.bitrate_switch) flags |= GS_CAN_FLAG_BRS;
        if (msg.error_state_indicator) flags |= GS_CAN_FLAG_ESI;
        payload[10] = flags;
        if (!msg.is_remote_frame && copy_len > 0) {
            std::memcpy(payload.data() + 12, msg.data.data(), static_cast<std::size_t>(std::min(copy_len, payload_len)));
        }

        auto& api = LibUsbApi::instance();
        ExclusiveLock lock(write_lock_);
        int transferred = 0;
        const int rc = api.bulk_transfer(handle_, out_ep_, payload.data(), static_cast<int>(payload.size()), &transferred, CTRL_TIMEOUT_MS);
        if (rc != LIBUSB_SUCCESS || transferred <= 0) {
            throw std::runtime_error(format_libusb_error("libusb_bulk_transfer(write)", rc));
        }
    }

    bool poll_once(unsigned int timeout_ms) {
        if (handle_ == nullptr || closed_.load()) {
            return false;
        }

        auto& api = LibUsbApi::instance();
        std::vector<unsigned char> buffer(static_cast<std::size_t>(rx_frame_size_), 0);
        int transferred = 0;
        const int rc = api.bulk_transfer(handle_, in_ep_, buffer.data(), static_cast<int>(buffer.size()), &transferred, timeout_ms);
        if (rc < 0) {
            if (is_timeout(rc)) {
                return false;
            }
            Sleep(1);
            return false;
        }
        if (transferred <= 0) {
            return false;
        }

        try {
            auto [message, logical_channel] = parse_frame(buffer.data(), static_cast<std::size_t>(transferred));
            std::vector<MessageSink*> listeners;
            {
                ExclusiveLock lock(bus_lock_);
                auto it = buses_.find(logical_channel);
                if (it != buses_.end()) {
                    listeners = it->second;
                }
            }
            for (auto* listener : listeners) {
                if (listener != nullptr) {
                    listener->on_message_received(message);
                }
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    const DeviceSummary& summary() const { return summary_; }
    const std::vector<ChannelCapabilities>& channels() const { return channels_; }

private:
    void write_bittiming(int channel_index, std::uint8_t request, const BusTiming& timing) {
        auto payload = to_timing_payload(timing);
        write_control(false, request, channel_index, payload.data(), static_cast<std::uint16_t>(payload.size()));
    }

    void write_control(bool direction_in, std::uint8_t request, int value, unsigned char* data, std::uint16_t length) {
        auto& api = LibUsbApi::instance();
        ExclusiveLock lock(write_lock_);
        const int rc = api.control_transfer(handle_, build_request_type(direction_in), request, static_cast<std::uint16_t>(value), static_cast<std::uint16_t>(interface_number_), data, length, CTRL_TIMEOUT_MS);
        if (rc < 0) {
            throw std::runtime_error(format_libusb_error("libusb_control_transfer", rc));
        }
    }

    std::pair<CanMessage, int> parse_frame(const unsigned char* raw, std::size_t size) {
        if (size < 20u) {
            throw std::runtime_error("short frame");
        }
        std::uint32_t echo_id = 0;
        std::uint32_t raw_can_id = 0;
        std::memcpy(&echo_id, raw, sizeof(echo_id));
        std::memcpy(&raw_can_id, raw + 4, sizeof(raw_can_id));
        const std::uint8_t dlc_code = raw[8];
        const int channel_index = raw[9];
        const std::uint8_t flags = raw[10];
        const bool is_fd = (flags & GS_CAN_FLAG_FD) != 0u;
        const int payload_len = is_fd ? dlc_to_len(dlc_code) : std::min<int>(dlc_code, 8);
        CanMessage message;
        message.timestamp_s = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        message.arbitration_id = (raw_can_id & CAN_EFF_FLAG) != 0u ? (raw_can_id & CAN_EFF_MASK) : (raw_can_id & CAN_SFF_MASK);
        message.is_extended_id = (raw_can_id & CAN_EFF_FLAG) != 0u;
        message.is_remote_frame = (raw_can_id & CAN_RTR_FLAG) != 0u;
        message.is_error_frame = (raw_can_id & CAN_ERR_FLAG) != 0u;
        message.is_fd = is_fd;
        message.bitrate_switch = (flags & GS_CAN_FLAG_BRS) != 0u;
        message.error_state_indicator = (flags & GS_CAN_FLAG_ESI) != 0u;
        message.is_rx = echo_id == 0xFFFFFFFFu;
        message.channel = channel_index;
        message.data.assign(raw + 12, raw + 12 + std::min<std::size_t>(payload_len, size > 12u ? size - 12u : 0u));
        return {message, channel_index};
    }

    void cleanup() {
        auto& api = LibUsbApi::instance();
        if (handle_ != nullptr) {
            api.release_interface(handle_, interface_number_);
            api.close(handle_);
            handle_ = nullptr;
        }
        if (context_ != nullptr) {
            api.exit(context_);
            context_ = nullptr;
        }
    }

    std::string selector_;
    DeviceSummary summary_;
    libusb_context* context_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    int interface_number_ = 0;
    unsigned char in_ep_ = 0;
    unsigned char out_ep_ = 0;
    std::uint32_t sw_version_ = 0;
    std::uint32_t hw_version_ = 0;
    std::vector<ChannelCapabilities> channels_;
    int rx_frame_size_ = 80;
    std::map<int, int> tx_sizes_;
    WinMutex write_lock_;
    WinMutex bus_lock_;
    std::map<int, std::vector<MessageSink*>> buses_;
    int refs_ = 0;
    std::atomic<bool> closed_{false};
};

std::shared_ptr<SharedUsbSession> acquire_session(const BusOptions& options) {
    const std::string key = std::to_string(options.vendor_id) + ":" + std::to_string(options.product_id) + ":" + (options.selector.empty() ? "auto" : options.selector) + ":" + (options.interface_number.has_value() ? std::to_string(options.interface_number.value()) : "") + ":" + options.serial_number;
    ExclusiveLock lock(g_registry_lock);
    if (auto it = g_session_registry.find(key); it != g_session_registry.end()) {
        if (auto session = it->second.lock()) {
            return session;
        }
    }
    auto session = std::make_shared<SharedUsbSession>(options);
    g_session_registry[key] = session;
    return session;
}

}  // namespace

std::vector<DeviceSummary> enumerate_devices(std::uint16_t vendor_id, std::uint16_t product_id) {
    std::vector<DeviceSummary> summaries;
    for (const auto& item : enumerate_matching_devices(vendor_id, product_id)) {
        summaries.push_back(item.summary);
    }
    return summaries;
}

BusTiming calculate_timing(const TimingLimits& limits, int bitrate, double sample_point_percent) {
    if (bitrate <= 0) {
        throw std::runtime_error("bitrate must be > 0");
    }
    bool found = false;
    std::tuple<double, int, int, int, int> best_score{};
    BusTiming best_timing{};
    const int step = std::max(limits.brp_inc, 1);
    for (int brp = limits.brp_min; brp <= limits.brp_max; brp += step) {
        const long long numerator = limits.fclk_hz;
        const long long denominator = static_cast<long long>(brp) * bitrate;
        if (denominator <= 0 || (numerator % denominator) != 0) {
            continue;
        }
        const int total_tq = static_cast<int>(numerator / denominator);
        if (total_tq < 4) {
            continue;
        }
        const int tseg_total = total_tq - 1;
        const int tseg1_target = static_cast<int>(std::llround((sample_point_percent / 100.0) * total_tq - 1.0));
        for (const int tseg1 : {tseg1_target, tseg1_target - 1, tseg1_target + 1}) {
            const int tseg2 = tseg_total - tseg1;
            if (tseg1 < limits.tseg1_min || tseg1 > limits.tseg1_max) continue;
            if (tseg2 < limits.tseg2_min || tseg2 > limits.tseg2_max) continue;
            const double actual_sp = (static_cast<double>(1 + tseg1) / static_cast<double>(total_tq)) * 100.0;
            const auto score = std::make_tuple(std::abs(actual_sp - sample_point_percent), std::abs(brp), tseg2, tseg1, total_tq);
            if (!found || score < best_score) {
                found = true;
                best_score = score;
                best_timing = BusTiming{brp, 0, tseg1, tseg2, std::min(tseg2, limits.sjw_max)};
            }
        }
    }
    if (!found) {
        throw std::runtime_error("cannot derive timing from channel limits");
    }
    return best_timing;
}

class GsUsbBus::Impl : public MessageSink {
public:
    explicit Impl(const BusOptions& options)
        : options_(options), session_(acquire_session(options_)), channel_info_(session_->summary().label + "/gsusb_channel=" + std::to_string(options_.gsusb_channel)) {
        session_->add_bus(this, options_.gsusb_channel);
        capabilities_ = session_->channels().at(static_cast<std::size_t>(options_.gsusb_channel));

        try {
            std::optional<BusTiming> nominal = options_.nominal_timing;
            if (!nominal.has_value()) {
                if (!options_.bitrate.has_value()) {
                    throw std::runtime_error("bitrate or nominal_timing is required");
                }
                nominal = calculate_timing(capabilities_.nominal_limits, options_.bitrate.value(), 80.0);
            }

            std::optional<BusTiming> data = options_.data_timing;
            if (options_.fd) {
                if (!data.has_value()) {
                    if (!options_.data_bitrate.has_value()) {
                        throw std::runtime_error("data_bitrate or data_timing is required when fd=true");
                    }
                    if (!capabilities_.data_limits.has_value()) {
                        throw std::runtime_error("channel does not support CAN FD");
                    }
                    data = calculate_timing(capabilities_.data_limits.value(), options_.data_bitrate.value(), 80.0);
                }
            }

            session_->configure_channel(options_.gsusb_channel, nominal.value(), data, options_.fd, options_.listen_only, options_.termination_enabled, true);
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~Impl() {
        shutdown();
    }

    void send(const CanMessage& message) {
        session_->send_frame(options_.gsusb_channel, message);
    }

    std::optional<CanMessage> recv(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            {
                ExclusiveLock lock(queue_lock_);
                if (!queue_.empty()) {
                    CanMessage message = std::move(queue_.front());
                    queue_.pop_front();
                    return message;
                }
                if (stopped_) {
                    return std::nullopt;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return std::nullopt;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            const auto slice = static_cast<unsigned int>(std::max<long long>(1, std::min<long long>(remaining.count(), BULK_TIMEOUT_MS)));
            if (session_ == nullptr) {
                return std::nullopt;
            }
            if (!session_->poll_once(slice)) {
                Sleep(1);
            }
        }
    }

    void on_message_received(const CanMessage& message) override {
        ExclusiveLock lock(queue_lock_);
        if (stopped_) {
            return;
        }
        queue_.push_back(message);
    }

    void shutdown() {
        bool expected = false;
        if (!shutdown_called_.compare_exchange_strong(expected, true)) {
            return;
        }
        {
            ExclusiveLock lock(queue_lock_);
            stopped_ = true;
        }
        if (session_ != nullptr) {
            try {
                session_->close_channel(options_.gsusb_channel);
            } catch (...) {
            }
            bool last = false;
            {
                ExclusiveLock lock(g_registry_lock);
                last = session_->remove_bus(this, options_.gsusb_channel);
                if (last) {
                    for (auto it = g_session_registry.begin(); it != g_session_registry.end(); ) {
                        if (it->second.expired() || it->second.lock() == session_) {
                            it = g_session_registry.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
            if (last) {
                session_->close();
            }
            session_.reset();
        }
    }

    const std::string& channel_info() const { return channel_info_; }
    const DeviceSummary& device_summary() const { return session_summary(); }
    const ChannelCapabilities& capabilities() const { return capabilities_; }

private:
    const DeviceSummary& session_summary() const {
        static DeviceSummary empty;
        return session_ != nullptr ? session_->summary() : empty;
    }

    BusOptions options_;
    std::shared_ptr<SharedUsbSession> session_;
    ChannelCapabilities capabilities_;
    std::string channel_info_;
    WinMutex queue_lock_;
    std::deque<CanMessage> queue_;
    std::atomic<bool> shutdown_called_{false};
    bool stopped_ = false;
};

GsUsbBus::GsUsbBus(const BusOptions& options) : impl_(std::make_unique<Impl>(options)) {}
GsUsbBus::~GsUsbBus() = default;
GsUsbBus::GsUsbBus(GsUsbBus&&) noexcept = default;
GsUsbBus& GsUsbBus::operator=(GsUsbBus&&) noexcept = default;

void GsUsbBus::send(const CanMessage& message) {
    impl_->send(message);
}

std::optional<CanMessage> GsUsbBus::recv(std::chrono::milliseconds timeout) {
    return impl_->recv(timeout);
}

void GsUsbBus::shutdown() {
    if (impl_) {
        impl_->shutdown();
    }
}

const std::string& GsUsbBus::channel_info() const {
    return impl_->channel_info();
}

const DeviceSummary& GsUsbBus::device_summary() const {
    return impl_->device_summary();
}

const ChannelCapabilities& GsUsbBus::capabilities() const {
    return impl_->capabilities();
}

}  // namespace gsusb