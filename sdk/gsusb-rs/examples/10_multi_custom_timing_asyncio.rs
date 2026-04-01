#[path = "common/mod.rs"]
mod common;

use common::{build_pair_topology_text, get_demo_custom_timings, make_bus_options, make_payload, parse_common_options, run_multi_summary_demo, WorkerStyle};
use gsusb_rs::{CanMessage, GsUsbBus, Result};
use std::env;

fn main() -> Result<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    let options = parse_common_options(&args, "4-channel custom timing async example")?;
    let (nominal, data) = get_demo_custom_timings();
    let channels = vec![0u8, 1, 2, 3];
    println!("Pair topology: {}", build_pair_topology_text(&channels));
    run_multi_summary_demo(
        &format!("Running 4-channel custom timing async demo for {:.6}s...", options.duration_s),
        "4-channel custom timing async summary",
        &channels,
        &options,
        WorkerStyle::Async,
        |channel| GsUsbBus::new(make_bus_options(&options, channel, true, Some(nominal), Some(data))),
        |channel, counter| CanMessage {
            arbitration_id: 0x800 + u32::from(channel),
            is_fd: true,
            bitrate_switch: true,
            data: make_payload(&[channel, (counter & 0xFF) as u8, ((counter >> 8) & 0xFF) as u8], 20, channel),
            ..CanMessage::default()
        },
    )?;
    Ok(())
}