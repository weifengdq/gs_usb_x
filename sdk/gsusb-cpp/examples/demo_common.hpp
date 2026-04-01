#pragma once

#include <gsusb/gsusb.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace demo {

enum class WorkerStyle {
    Threading,
    Async,
};

struct CommonOptions {
    std::string selector = "auto";
    int bitrate = 1'000'000;
    int data_bitrate = 5'000'000;
    double period_s = 1.0;
    double duration_s = 3.0;
    double rx_timeout_s = 0.2;
    bool classic = false;
    bool no_termination = false;
    int tx_channel = 0;
    int rx_channel = 1;
    int channel_count = 8;
    int payload_len = 12;
};

struct ChannelStats {
    std::atomic<std::uint64_t> tx_count{0};
    std::atomic<std::uint64_t> rx_count{0};
};

using BusFactory = std::function<std::unique_ptr<gsusb::GsUsbBus>(int channel_index)>;
using MessageFactory = std::function<gsusb::CanMessage(int channel_index, std::uint64_t counter)>;

CommonOptions parse_common_options(int argc, char** argv, const std::string& description);
std::pair<gsusb::BusTiming, gsusb::BusTiming> get_demo_custom_timings();
std::string build_pair_topology_text(const std::vector<int>& channels);
std::vector<std::uint8_t> make_payload(const std::vector<std::uint8_t>& prefix, std::size_t total_length, std::uint8_t fill_value);
void print_stats_summary(const std::string& title, const std::vector<int>& channels, const std::vector<ChannelStats>& stats, double duration_s);
int run_multi_summary_demo(const std::string& run_title, const std::string& summary_title, const std::vector<int>& channels, const CommonOptions& options, WorkerStyle style, const BusFactory& bus_factory, const MessageFactory& message_factory);
gsusb::BusOptions make_bus_options(const CommonOptions& options, int channel_index, bool fd, std::optional<gsusb::BusTiming> nominal = std::nullopt, std::optional<gsusb::BusTiming> data = std::nullopt);
void print_message(const std::string& prefix, const gsusb::CanMessage& message);

}  // namespace demo