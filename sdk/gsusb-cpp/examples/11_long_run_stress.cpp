#include "demo_common.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "Long-duration GSUSB multi-channel stress tool");
        const bool use_fd = !options.classic;
        const std::vector<int> channels = options.channel_count == 4 ? std::vector<int>{0, 1, 2, 3} : std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7};
        std::cout << "Pair topology: " << demo::build_pair_topology_text(channels) << "\n";
        return demo::run_multi_summary_demo(
            "Running long stress for " + std::to_string(options.duration_s) + "s, payload_len=" + std::to_string(options.payload_len) + ", fd=" + std::string(use_fd ? "true" : "false"),
            "Long-run stress summary",
            channels,
            options,
            demo::WorkerStyle::Threading,
            [&](int channel_index) {
                return std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, channel_index, use_fd));
            },
            [&](int channel_index, std::uint64_t counter) {
                gsusb::CanMessage msg;
                msg.arbitration_id = 0x680u + static_cast<std::uint32_t>(channel_index);
                msg.is_fd = use_fd;
                msg.bitrate_switch = use_fd;
                msg.data = demo::make_payload({static_cast<std::uint8_t>(channel_index), static_cast<std::uint8_t>(counter & 0xFF), static_cast<std::uint8_t>((counter >> 8) & 0xFF), 0x47, 0x53, 0x55, 0x42}, static_cast<std::size_t>(use_fd ? options.payload_len : std::min(options.payload_len, 8)), static_cast<std::uint8_t>((channel_index + static_cast<int>(counter)) & 0xFF));
                return msg;
            }
        );
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}