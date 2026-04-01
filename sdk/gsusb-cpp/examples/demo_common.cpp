#include "demo_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace demo {

namespace {

void print_usage(const std::string& description) {
    std::cout << description << "\n"
              << "  --selector <auto|BUS:ADDR|BUS:ADDR:IF|SERIAL>\n"
              << "  --bitrate <bps>\n"
              << "  --data-bitrate <bps>\n"
              << "  --period <seconds>\n"
              << "  --duration <seconds>\n"
              << "  --rx-timeout <seconds>\n"
              << "  --classic\n"
              << "  --no-termination\n"
              << "  --tx-channel <index>\n"
              << "  --rx-channel <index>\n"
              << "  --channel-count <4|8>\n"
              << "  --payload-len <bytes>\n";
}

double to_double(const std::string& value, const char* name) {
    try {
        return std::stod(value);
    } catch (...) {
        throw std::runtime_error(std::string("invalid value for ") + name + ": " + value);
    }
}

int to_int(const std::string& value, const char* name) {
    try {
        return std::stoi(value);
    } catch (...) {
        throw std::runtime_error(std::string("invalid value for ") + name + ": " + value);
    }
}

}  // namespace

CommonOptions parse_common_options(int argc, char** argv, const std::string& description) {
    CommonOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto next_value = [&](const char* name) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            ++index;
            return argv[index];
        };
        if (arg == "--help" || arg == "-h") {
            print_usage(description);
            std::exit(0);
        } else if (arg == "--selector") {
            options.selector = next_value("--selector");
        } else if (arg == "--bitrate") {
            options.bitrate = to_int(next_value("--bitrate"), "--bitrate");
        } else if (arg == "--data-bitrate") {
            options.data_bitrate = to_int(next_value("--data-bitrate"), "--data-bitrate");
        } else if (arg == "--period") {
            options.period_s = to_double(next_value("--period"), "--period");
        } else if (arg == "--duration") {
            options.duration_s = to_double(next_value("--duration"), "--duration");
        } else if (arg == "--rx-timeout") {
            options.rx_timeout_s = to_double(next_value("--rx-timeout"), "--rx-timeout");
        } else if (arg == "--classic") {
            options.classic = true;
        } else if (arg == "--no-termination") {
            options.no_termination = true;
        } else if (arg == "--tx-channel") {
            options.tx_channel = to_int(next_value("--tx-channel"), "--tx-channel");
        } else if (arg == "--rx-channel") {
            options.rx_channel = to_int(next_value("--rx-channel"), "--rx-channel");
        } else if (arg == "--channel-count") {
            options.channel_count = to_int(next_value("--channel-count"), "--channel-count");
        } else if (arg == "--payload-len") {
            options.payload_len = to_int(next_value("--payload-len"), "--payload-len");
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

std::pair<gsusb::BusTiming, gsusb::BusTiming> get_demo_custom_timings() {
    return {
        gsusb::BusTiming{2, 0, 31, 8, 8},
        gsusb::BusTiming{1, 0, 7, 2, 2},
    };
}

std::string build_pair_topology_text(const std::vector<int>& channels) {
    std::ostringstream oss;
    bool first = true;
    for (std::size_t index = 0; index + 1 < channels.size(); index += 2) {
        if (!first) {
            oss << " / ";
        }
        first = false;
        oss << "can" << channels[index] << "-can" << channels[index + 1];
    }
    return oss.str();
}

std::vector<std::uint8_t> make_payload(const std::vector<std::uint8_t>& prefix, std::size_t total_length, std::uint8_t fill_value) {
    std::vector<std::uint8_t> payload = prefix;
    if (payload.size() < total_length) {
        payload.resize(total_length, fill_value);
    }
    if (payload.size() > total_length) {
        payload.resize(total_length);
    }
    return payload;
}

void print_stats_summary(const std::string& title, const std::vector<int>& channels, const std::vector<ChannelStats>& stats, double duration_s) {
    const double effective_duration = duration_s > 0.0 ? duration_s : 1.0;
    std::uint64_t total_tx = 0;
    std::uint64_t total_rx = 0;
    std::cout << "\n" << title << "\n";
    std::cout << "CH    TX    TX/s      RX    RX/s\n";
    std::cout << "---------------------------------\n";
    for (int channel : channels) {
        const auto tx = stats[static_cast<std::size_t>(channel)].tx_count.load();
        const auto rx = stats[static_cast<std::size_t>(channel)].rx_count.load();
        total_tx += tx;
        total_rx += rx;
        std::cout << std::left << std::setw(2) << channel << "  "
                  << std::right << std::setw(6) << tx << "  "
                  << std::setw(6) << std::fixed << std::setprecision(1) << (static_cast<double>(tx) / effective_duration) << "  "
                  << std::setw(6) << rx << "  "
                  << std::setw(6) << (static_cast<double>(rx) / effective_duration) << "\n";
    }
    std::cout << "---------------------------------\n";
    std::cout << "SUM  "
              << std::right << std::setw(6) << total_tx << "  "
              << std::setw(6) << std::fixed << std::setprecision(1) << (static_cast<double>(total_tx) / effective_duration) << "  "
              << std::setw(6) << total_rx << "  "
              << std::setw(6) << (static_cast<double>(total_rx) / effective_duration) << "\n";
}

gsusb::BusOptions make_bus_options(const CommonOptions& options, int channel_index, bool fd, std::optional<gsusb::BusTiming> nominal, std::optional<gsusb::BusTiming> data) {
    gsusb::BusOptions bus_options;
    bus_options.selector = options.selector;
    bus_options.gsusb_channel = channel_index;
    bus_options.fd = fd;
    bus_options.termination_enabled = !options.no_termination;
    if (nominal.has_value()) {
        bus_options.nominal_timing = nominal;
    } else {
        bus_options.bitrate = options.bitrate;
    }
    if (fd) {
        if (data.has_value()) {
            bus_options.data_timing = data;
        } else {
            bus_options.data_bitrate = options.data_bitrate;
        }
    }
    return bus_options;
}

void print_message(const std::string& prefix, const gsusb::CanMessage& message) {
    std::ostringstream oss;
    oss << prefix << " ID=0x" << std::hex << std::uppercase << message.arbitration_id << std::dec
        << " DL=" << message.data.size()
        << " FD=" << (message.is_fd ? "Y" : "N")
        << " BRS=" << (message.bitrate_switch ? "Y" : "N")
        << " CH=" << message.channel << " DATA=";
    for (std::size_t index = 0; index < message.data.size(); ++index) {
        if (index != 0) {
            oss << ' ';
        }
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(message.data[index]);
    }
    std::cout << oss.str() << "\n";
}

int run_multi_summary_demo(const std::string& run_title, const std::string& summary_title, const std::vector<int>& channels, const CommonOptions& options, WorkerStyle style, const BusFactory& bus_factory, const MessageFactory& message_factory) {
    (void)style;
    std::vector<std::unique_ptr<gsusb::GsUsbBus>> buses;
    std::vector<ChannelStats> stats(static_cast<std::size_t>(*std::max_element(channels.begin(), channels.end()) + 1));
    const auto rx_timeout = std::chrono::milliseconds(static_cast<int>(std::lround(options.rx_timeout_s * 1000.0)));
    const auto period = std::chrono::duration<double>(options.period_s);
    std::vector<std::uint64_t> counters(stats.size(), 0);

    for (int channel : channels) {
        std::cout << "Opening Channel " << channel << "...\n";
        buses.push_back(bus_factory(channel));
    }

    std::cout << run_title << "\n";
    const auto run_start = std::chrono::steady_clock::now();
    auto next_tick = run_start;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count() < options.duration_s) {
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const int channel = channels[index];
            buses[index]->send(message_factory(channel, counters[static_cast<std::size_t>(channel)]++));
            stats[static_cast<std::size_t>(channel)].tx_count.fetch_add(1);
        }

        next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        while (std::chrono::steady_clock::now() < next_tick) {
            bool received_any = false;
            for (std::size_t index = 0; index < channels.size(); ++index) {
                const int channel = channels[index];
                auto message = buses[index]->recv(std::chrono::milliseconds(1));
                if (message.has_value()) {
                    stats[static_cast<std::size_t>(channel)].rx_count.fetch_add(1);
                    received_any = true;
                }
            }
            if (!received_any) {
                Sleep(1);
            }
        }
    }

    const auto drain_deadline = std::chrono::steady_clock::now() + rx_timeout;
    while (std::chrono::steady_clock::now() < drain_deadline) {
        bool received_any = false;
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const int channel = channels[index];
            auto message = buses[index]->recv(std::chrono::milliseconds(1));
            if (message.has_value()) {
                stats[static_cast<std::size_t>(channel)].rx_count.fetch_add(1);
                received_any = true;
            }
        }
        if (!received_any) {
            Sleep(1);
        }
    }

    const double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count();

    print_stats_summary(summary_title, channels, stats, elapsed_s);
    for (auto& bus : buses) {
        bus->shutdown();
    }
    return 0;
}

}  // namespace demo