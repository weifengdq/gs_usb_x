#include "demo_common.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "Single-channel classic CAN example");
        std::cout << "Opening selector=" << options.selector << " tx=" << options.tx_channel << " rx=" << options.rx_channel << " @ " << options.bitrate << "bps\n";
        auto tx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.tx_channel, false));
        auto rx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.rx_channel, false));

        gsusb::CanMessage message;
        message.arbitration_id = 0x123;
        message.data = {0x11, 0x22, 0x33, 0x44};

        demo::print_message("Sending", message);
        tx_bus->send(message);
        auto received = rx_bus->recv(std::chrono::seconds(2));
        if (!received.has_value()) {
            std::cerr << "No frame received\n";
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