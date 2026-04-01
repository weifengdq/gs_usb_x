#include "demo_common.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "Single-channel CAN FD example");
        auto tx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.tx_channel, true));
        auto rx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.rx_channel, true));

        gsusb::CanMessage message;
        message.arbitration_id = 0x1FFFFFF0u;
        message.is_extended_id = true;
        message.is_fd = true;
        message.bitrate_switch = true;
        message.data = demo::make_payload({0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}, 64, 0x00);

        demo::print_message("Sending", message);
        tx_bus->send(message);
        auto received = rx_bus->recv(std::chrono::seconds(2));
        if (!received.has_value()) {
            std::cerr << "No FD frame received\n";
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