#include "demo_common.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "4-Channel Custom Timing Threading Example");
        const auto [nominal, data] = demo::get_demo_custom_timings();
        const std::vector<int> channels = {0, 1, 2, 3};
        std::cout << "Pair topology: " << demo::build_pair_topology_text(channels) << "\n";
        return demo::run_multi_summary_demo(
            "Running 4-channel custom timing threading demo for " + std::to_string(options.duration_s) + "s...",
            "4-channel custom timing threading summary",
            channels,
            options,
            demo::WorkerStyle::Threading,
            [&](int channel_index) {
                return std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, channel_index, true, nominal, data));
            },
            [&](int channel_index, std::uint64_t counter) {
                gsusb::CanMessage msg;
                msg.arbitration_id = 0x700u + static_cast<std::uint32_t>(channel_index);
                msg.is_fd = true;
                msg.bitrate_switch = true;
                msg.data = demo::make_payload({static_cast<std::uint8_t>(channel_index), static_cast<std::uint8_t>(counter & 0xFF), static_cast<std::uint8_t>((counter >> 8) & 0xFF)}, 20u, static_cast<std::uint8_t>(channel_index));
                return msg;
            }
        );
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}