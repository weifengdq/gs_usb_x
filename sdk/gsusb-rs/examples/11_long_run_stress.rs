#[path = "common/mod.rs"]
mod common;

use common::{build_pair_topology_text, make_bus_options, make_payload, parse_common_options, run_multi_summary_demo, WorkerStyle};
use gsusb_rs::{CanMessage, GsUsbBus, Result};
use std::env;

fn main() -> Result<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    let options = parse_common_options(&args, "Long-duration GSUSB multi-channel stress tool")?;
    let use_fd = !options.classic;
    let channels = if options.channel_count == 4 { vec![0u8, 1, 2, 3] } else { vec![0u8, 1, 2, 3, 4, 5, 6, 7] };
    println!("Pair topology: {}", build_pair_topology_text(&channels));
    run_multi_summary_demo(
        &format!("Running long stress for {:.6}s, payload_len={}, fd={}", options.duration_s, options.payload_len, if use_fd { "true" } else { "false" }),
        "Long-run stress summary",
        &channels,
        &options,
        WorkerStyle::Threading,
        |channel| GsUsbBus::new(make_bus_options(&options, channel, use_fd, None, None)),
        |channel, counter| CanMessage {
            arbitration_id: 0x680 + u32::from(channel),
            is_fd: use_fd,
            bitrate_switch: use_fd,
            data: make_payload(
                &[channel, (counter & 0xFF) as u8, ((counter >> 8) & 0xFF) as u8, 0x47, 0x53, 0x55, 0x42],
                if use_fd { options.payload_len } else { options.payload_len.min(8) },
                channel.wrapping_add(counter as u8),
            ),
            ..CanMessage::default()
        },
    )?;
    Ok(())
}