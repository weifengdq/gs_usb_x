#include "demo_common.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <iostream>
#include <windows.h>

int main(int argc, char** argv) {
    try {
        const auto options = demo::parse_common_options(argc, argv, "Single-channel periodic send example");
        auto tx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.tx_channel, false));
        auto rx_bus = std::make_unique<gsusb::GsUsbBus>(demo::make_bus_options(options, options.rx_channel, false));

        const int count = std::max(1, static_cast<int>(std::lround(options.duration_s / std::max(options.period_s, 0.001))));
        std::uint64_t tx_count = 0;
        std::uint64_t rx_count = 0;
        for (int index = 0; index < count; ++index) {
            gsusb::CanMessage message;
            message.arbitration_id = 0x100;
            message.data = {static_cast<std::uint8_t>(index & 0xFF), 0x01, 0x02, 0x03};
            tx_bus->send(message);
            ++tx_count;
            if (auto received = rx_bus->recv(std::chrono::milliseconds(static_cast<int>(options.rx_timeout_s * 1000.0))); received.has_value()) {
                ++rx_count;
                demo::print_message("Received", received.value());
            }
            Sleep(static_cast<DWORD>(std::max(0.0, options.period_s * 1000.0)));
        }

        std::cout << "Periodic summary TX=" << tx_count << " RX=" << rx_count << "\n";
        tx_bus->shutdown();
        rx_bus->shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}