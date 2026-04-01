#[path = "common/mod.rs"]
mod common;

use common::{make_bus_options, parse_common_options, print_message};
use gsusb_rs::{CanMessage, GsUsbBus, Result};
use std::env;
use std::time::Duration;

fn main() -> Result<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    let options = parse_common_options(&args, "Single-channel periodic send example")?;
    let tx_bus = GsUsbBus::new(make_bus_options(&options, options.tx_channel, false, None, None))?;
    let rx_bus = GsUsbBus::new(make_bus_options(&options, options.rx_channel, false, None, None))?;

    let count = ((options.duration_s / options.period_s.max(0.001)).round() as i32).max(1);
    let mut tx_count = 0u64;
    let mut rx_count = 0u64;
    for index in 0..count {
        let message = CanMessage {
            arbitration_id: 0x100,
            data: vec![index as u8, 0x01, 0x02, 0x03],
            ..CanMessage::default()
        };
        tx_bus.send(&message)?;
        tx_count += 1;
        if let Some(received) = rx_bus.recv(Duration::from_secs_f64(options.rx_timeout_s)) {
            rx_count += 1;
            print_message("Received", &received);
        }
        std::thread::sleep(Duration::from_secs_f64(options.period_s.max(0.0)));
    }

    println!("Periodic summary TX={tx_count} RX={rx_count}");
    tx_bus.shutdown();
    rx_bus.shutdown();
    Ok(())
}