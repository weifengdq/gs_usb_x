#include "demo_common.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "4-Channel Standard CAN Async Example");
        const bool use_fd = !options.classic;
        const std::vector<int> channels = {0, 1, 2, 3};
        std::cout << "Pair topology: " << demo::build_pair_topology_text(channels) << "\n";
        return demo::run_multi_summary_demo(
            "Running 4-channel async demo for " + std::to_string(options.duration_s) + "s...",
            "4-channel async summary",
            channels,
            options,
            demo::WorkerStyle::Async,
            [&](int channel_index) {
                return std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, channel_index, use_fd));
            },
            [&](int channel_index, std::uint64_t counter) {
                gsusb::CanMessage msg;
                msg.arbitration_id = 0x300u + static_cast<std::uint32_t>(channel_index);
                msg.is_fd = use_fd;
                msg.bitrate_switch = use_fd;
                msg.data = demo::make_payload({static_cast<std::uint8_t>(channel_index), static_cast<std::uint8_t>(counter & 0xFF), static_cast<std::uint8_t>((counter >> 8) & 0xFF), 0x31, 0x41, 0x59, 0x26, 0x53}, use_fd ? 12u : 8u, 0x58);
                return msg;
            }
        );
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}