#[path = "common/mod.rs"]
mod common;

use common::{get_demo_custom_timings, make_bus_options, make_payload, parse_common_options, print_message};
use gsusb_rs::{CanMessage, GsUsbBus, Result};
use std::env;
use std::time::Duration;

fn main() -> Result<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    let options = parse_common_options(&args, "Single-channel custom timing CAN FD example")?;
    let (nominal, data) = get_demo_custom_timings();
    let tx_bus = GsUsbBus::new(make_bus_options(&options, options.tx_channel, true, Some(nominal), Some(data)))?;
    let rx_bus = GsUsbBus::new(make_bus_options(&options, options.rx_channel, true, Some(nominal), Some(data)))?;

    let message = CanMessage {
        arbitration_id: 0x7F0,
        is_fd: true,
        bitrate_switch: true,
        data: make_payload(&[0x11], 12, 0x00),
        ..CanMessage::default()
    };
    print_message("Sending", &message);
    tx_bus.send(&message)?;
    let received = rx_bus.recv(Duration::from_secs(2)).ok_or_else(|| gsusb_rs::GsUsbError("No custom timing frame received".to_string()))?;
    print_message("Received", &received);
    tx_bus.shutdown();
    rx_bus.shutdown();
    Ok(())
}