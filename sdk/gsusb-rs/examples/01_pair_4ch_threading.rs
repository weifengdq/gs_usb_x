#[path = "common/mod.rs"]
mod common;

use common::{build_pair_topology_text, make_bus_options, make_payload, parse_common_options, run_multi_summary_demo, WorkerStyle};
use gsusb_rs::{CanMessage, GsUsbBus, Result};
use std::env;

fn main() -> Result<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    let options = parse_common_options(&args, "4-channel pair traffic example: can0-can1 / can2-can3, all 4 channels TX+RX")?;
    let use_fd = !options.classic;
    let channels = vec![0u8, 1, 2, 3];
    println!("Pair topology: {}", build_pair_topology_text(&channels));
    run_multi_summary_demo(
        &format!("Running 4-channel threading demo for {:.6}s...", options.duration_s),
        "4-channel pair summary",
        &channels,
        &options,
        WorkerStyle::Threading,
        |channel| GsUsbBus::new(make_bus_options(&options, channel, use_fd, None, None)),
        |channel, counter| CanMessage {
            arbitration_id: 0x500 + u32::from(channel),
            is_fd: use_fd,
            bitrate_switch: use_fd,
            data: make_payload(&[channel, (counter & 0xFF) as u8, ((counter >> 8) & 0xFF) as u8, 0xA5, 0x5A, 0x11, 0x22, 0x33], if use_fd { 12 } else { 8 }, 0x44),
            ..CanMessage::default()
        },
    )?;
    Ok(())
}