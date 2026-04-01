#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gsusb {

constexpr std::uint16_t DEFAULT_VENDOR_ID = 0x1D50;
constexpr std::uint16_t DEFAULT_PRODUCT_ID = 0x606F;

struct BusTiming {
    int brp = 0;
    int prop_seg = 0;
    int phase_seg1 = 0;
    int phase_seg2 = 0;
    int sjw = 0;
};

struct TimingLimits {
    int fclk_hz = 0;
    int tseg1_min = 0;
    int tseg1_max = 0;
    int tseg2_min = 0;
    int tseg2_max = 0;
    int sjw_max = 0;
    int brp_min = 0;
    int brp_max = 0;
    int brp_inc = 0;
};

struct DeviceSummary {
    std::string key;
    int bus = 0;
    int address = 0;
    int vendor_id = 0;
    int product_id = 0;
    int interface_number = 0;
    std::string serial_number;
    std::string manufacturer;
    std::string product;
    std::string label;
};

struct ChannelCapabilities {
    int index = 0;
    std::uint32_t feature_flags = 0;
    TimingLimits nominal_limits;
    std::optional<TimingLimits> data_limits;
    bool fd_supported = false;
    bool hardware_timestamp = false;
    bool termination_supported = false;
    bool termination_enabled = false;
};

struct CanMessage {
    std::uint32_t arbitration_id = 0;
    bool is_extended_id = false;
    bool is_remote_frame = false;
    bool is_error_frame = false;
    bool is_fd = false;
    bool bitrate_switch = false;
    bool error_state_indicator = false;
    bool is_rx = true;
    int channel = -1;
    double timestamp_s = 0.0;
    std::vector<std::uint8_t> data;
};

struct BusOptions {
    std::string selector = "auto";
    int gsusb_channel = 0;
    std::optional<int> bitrate;
    bool fd = false;
    std::optional<int> data_bitrate;
    std::optional<BusTiming> nominal_timing;
    std::optional<BusTiming> data_timing;
    bool listen_only = false;
    bool termination_enabled = true;
    std::uint16_t vendor_id = DEFAULT_VENDOR_ID;
    std::uint16_t product_id = DEFAULT_PRODUCT_ID;
    std::optional<int> interface_number;
    std::string serial_number;
};

std::vector<DeviceSummary> enumerate_devices(std::uint16_t vendor_id = DEFAULT_VENDOR_ID, std::uint16_t product_id = DEFAULT_PRODUCT_ID);
BusTiming calculate_timing(const TimingLimits& limits, int bitrate, double sample_point_percent = 80.0);

class GsUsbBus {
public:
    explicit GsUsbBus(const BusOptions& options = {});
    ~GsUsbBus();

    GsUsbBus(const GsUsbBus&) = delete;
    GsUsbBus& operator=(const GsUsbBus&) = delete;
    GsUsbBus(GsUsbBus&&) noexcept;
    GsUsbBus& operator=(GsUsbBus&&) noexcept;

    void send(const CanMessage& message);
    std::optional<CanMessage> recv(std::chrono::milliseconds timeout);
    void shutdown();

    const std::string& channel_info() const;
    const DeviceSummary& device_summary() const;
    const ChannelCapabilities& capabilities() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gsusb