#[path = "common/mod.rs"]
mod common;

use common::{make_bus_options, make_payload, parse_common_options, print_message};
use gsusb_rs::{CanMessage, GsUsbBus, Result};
use std::env;
use std::time::Duration;

fn main() -> Result<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    let options = parse_common_options(&args, "Single-channel CAN FD example")?;
    let tx_bus = GsUsbBus::new(make_bus_options(&options, options.tx_channel, true, None, None))?;
    let rx_bus = GsUsbBus::new(make_bus_options(&options, options.rx_channel, true, None, None))?;

    let message = CanMessage {
        arbitration_id: 0x1FFF_FFF0,
        is_extended_id: true,
        is_fd: true,
        bitrate_switch: true,
        data: make_payload(&[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88], 64, 0x00),
        ..CanMessage::default()
    };
    print_message("Sending", &message);
    tx_bus.send(&message)?;
    let received = rx_bus.recv(Duration::from_secs(2)).ok_or_else(|| gsusb_rs::GsUsbError("No FD frame received".to_string()))?;
    print_message("Received", &received);
    tx_bus.shutdown();
    rx_bus.shutdown();
    Ok(())
}