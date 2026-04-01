#include "demo_common.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "Single-channel custom timing CAN FD example");
        const auto [nominal, data] = demo::get_demo_custom_timings();
        auto tx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.tx_channel, true, nominal, data));
        auto rx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.rx_channel, true, nominal, data));

        gsusb::CanMessage message;
        message.arbitration_id = 0x7F0;
        message.is_fd = true;
        message.bitrate_switch = true;
        message.data = demo::make_payload({0x11}, 12, 0x00);

        demo::print_message("Sending", message);
        tx_bus->send(message);
        auto received = rx_bus->recv(std::chrono::seconds(2));
        if (!received.has_value()) {
            std::cerr << "No custom timing frame received\n";
            return 1;
        }
        demo::print_message("Received", received.value());
        tx_bus->shutdown();
        rx_bus->shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}