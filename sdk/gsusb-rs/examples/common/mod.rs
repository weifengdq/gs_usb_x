#![allow(dead_code)]

use gsusb_rs::{BusOptions, BusTiming, CanMessage, GsUsbBus, Result};
use std::time::{Duration, Instant};

#[derive(Clone, Copy)]
pub enum WorkerStyle {
    Threading,
    Async,
}

#[derive(Clone, Debug)]
pub struct CommonOptions {
    pub selector: String,
    pub bitrate: i32,
    pub data_bitrate: i32,
    pub period_s: f64,
    pub duration_s: f64,
    pub rx_timeout_s: f64,
    pub classic: bool,
    pub no_termination: bool,
    pub tx_channel: u8,
    pub rx_channel: u8,
    pub channel_count: usize,
    pub payload_len: usize,
}

impl Default for CommonOptions {
    fn default() -> Self {
        Self {
            selector: "auto".to_string(),
            bitrate: 1_000_000,
            data_bitrate: 5_000_000,
            period_s: 1.0,
            duration_s: 3.0,
            rx_timeout_s: 0.2,
            classic: false,
            no_termination: false,
            tx_channel: 0,
            rx_channel: 1,
            channel_count: 8,
            payload_len: 12,
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct ChannelStats {
    pub tx_count: u64,
    pub rx_count: u64,
}

pub fn parse_common_options(args: &[String], description: &str) -> Result<CommonOptions> {
    let mut options = CommonOptions::default();
    let mut index = 0usize;
    while index < args.len() {
        let arg = &args[index];
        let mut next_value = |name: &str| -> Result<String> {
            if index + 1 >= args.len() {
                return Err(gsusb_rs::GsUsbError(format!("missing value for {name}")));
            }
            index += 1;
            Ok(args[index].clone())
        };
        match arg.as_str() {
            "--help" | "-h" => {
                print_usage(description);
                std::process::exit(0);
            }
            "--selector" => options.selector = next_value("--selector")?,
            "--bitrate" => options.bitrate = next_value("--bitrate")?.parse()?,
            "--data-bitrate" => options.data_bitrate = next_value("--data-bitrate")?.parse()?,
            "--period" => options.period_s = next_value("--period")?.parse().map_err(|err: std::num::ParseFloatError| gsusb_rs::GsUsbError(err.to_string()))?,
            "--duration" => options.duration_s = next_value("--duration")?.parse().map_err(|err: std::num::ParseFloatError| gsusb_rs::GsUsbError(err.to_string()))?,
            "--rx-timeout" => options.rx_timeout_s = next_value("--rx-timeout")?.parse().map_err(|err: std::num::ParseFloatError| gsusb_rs::GsUsbError(err.to_string()))?,
            "--classic" => options.classic = true,
            "--no-termination" => options.no_termination = true,
            "--tx-channel" => options.tx_channel = next_value("--tx-channel")?.parse()?,
            "--rx-channel" => options.rx_channel = next_value("--rx-channel")?.parse()?,
            "--channel-count" => options.channel_count = next_value("--channel-count")?.parse()?,
            "--payload-len" => options.payload_len = next_value("--payload-len")?.parse()?,
            _ => return Err(gsusb_rs::GsUsbError(format!("unknown argument: {arg}"))),
        }
        index += 1;
    }
    Ok(options)
}

fn print_usage(description: &str) {
    println!("{description}");
    println!("  --selector <auto|BUS:ADDR|BUS:ADDR:IF|SERIAL>");
    println!("  --bitrate <bps>");
    println!("  --data-bitrate <bps>");
    println!("  --period <seconds>");
    println!("  --duration <seconds>");
    println!("  --rx-timeout <seconds>");
    println!("  --classic");
    println!("  --no-termination");
    println!("  --tx-channel <index>");
    println!("  --rx-channel <index>");
    println!("  --channel-count <4|8>");
    println!("  --payload-len <bytes>");
}

pub fn get_demo_custom_timings() -> (BusTiming, BusTiming) {
    (
        BusTiming { brp: 2, prop_seg: 0, phase_seg1: 31, phase_seg2: 8, sjw: 8 },
        BusTiming { brp: 1, prop_seg: 0, phase_seg1: 7, phase_seg2: 2, sjw: 2 },
    )
}

pub fn build_pair_topology_text(channels: &[u8]) -> String {
    channels
        .chunks(2)
        .filter(|pair| pair.len() == 2)
        .map(|pair| format!("can{}-can{}", pair[0], pair[1]))
        .collect::<Vec<_>>()
        .join(" / ")
}

pub fn make_payload(prefix: &[u8], total_length: usize, fill_value: u8) -> Vec<u8> {
    let mut payload = prefix.to_vec();
    if payload.len() < total_length {
        payload.resize(total_length, fill_value);
    }
    payload.truncate(total_length);
    payload
}

pub fn print_stats_summary(title: &str, channels: &[u8], stats: &[ChannelStats], duration_s: f64) {
    let effective_duration = if duration_s > 0.0 { duration_s } else { 1.0 };
    let mut total_tx = 0u64;
    let mut total_rx = 0u64;
    println!();
    println!("{title}");
    println!("CH    TX    TX/s      RX    RX/s");
    println!("---------------------------------");
    for &channel in channels {
        let stat = stats[channel as usize];
        total_tx += stat.tx_count;
        total_rx += stat.rx_count;
        println!(
            "{:<2}  {:>6}  {:>6.1}  {:>6}  {:>6.1}",
            channel,
            stat.tx_count,
            stat.tx_count as f64 / effective_duration,
            stat.rx_count,
            stat.rx_count as f64 / effective_duration
        );
    }
    println!("---------------------------------");
    println!(
        "SUM  {:>6}  {:>6.1}  {:>6}  {:>6.1}",
        total_tx,
        total_tx as f64 / effective_duration,
        total_rx,
        total_rx as f64 / effective_duration
    );
}

pub fn make_bus_options(options: &CommonOptions, channel_index: u8, fd: bool, nominal: Option<BusTiming>, data: Option<BusTiming>) -> BusOptions {
    let mut bus_options = BusOptions::new();
    bus_options.selector = options.selector.clone();
    bus_options.gsusb_channel = channel_index;
    bus_options.fd = fd;
    bus_options.termination_enabled = !options.no_termination;
    bus_options.nominal_timing = nominal;
    bus_options.data_timing = data;
    if nominal.is_none() {
        bus_options.bitrate = Some(options.bitrate);
    }
    if fd && data.is_none() {
        bus_options.data_bitrate = Some(options.data_bitrate);
    }
    bus_options
}

pub fn print_message(prefix: &str, message: &CanMessage) {
    let hex = message
        .data
        .iter()
        .map(|value| format!("{value:02X}"))
        .collect::<Vec<_>>()
        .join(" ");
    println!(
        "{prefix} ID=0x{:X} DL={} FD={} BRS={} CH={} DATA={}",
        message.arbitration_id,
        message.data.len(),
        if message.is_fd { "Y" } else { "N" },
        if message.bitrate_switch { "Y" } else { "N" },
        message.channel,
        hex
    );
}

pub fn run_multi_summary_demo<BF, MF>(
    run_title: &str,
    summary_title: &str,
    channels: &[u8],
    options: &CommonOptions,
    style: WorkerStyle,
    bus_factory: BF,
    message_factory: MF,
) -> Result<i32>
where
    BF: Fn(u8) -> Result<GsUsbBus>,
    MF: Fn(u8, u64) -> CanMessage,
{
    let _ = style;
    let mut buses = Vec::new();
    let mut stats = vec![ChannelStats::default(); usize::from(*channels.iter().max().unwrap_or(&0)) + 1];
    let mut counters = vec![0u64; stats.len()];
    let receive_timeout = Duration::from_secs_f64(options.rx_timeout_s);
    let period = Duration::from_secs_f64(options.period_s);

    for &channel in channels {
        println!("Opening Channel {channel}...");
        buses.push(bus_factory(channel)?);
    }

    println!("{run_title}");
    let run_start = Instant::now();
    let mut next_tick = Instant::now();
    while run_start.elapsed().as_secs_f64() < options.duration_s {
        for (index, &channel) in channels.iter().enumerate() {
            let message = message_factory(channel, counters[channel as usize]);
            buses[index].send(&message)?;
            stats[channel as usize].tx_count += 1;
            counters[channel as usize] += 1;
        }

        next_tick += period;
        while Instant::now() < next_tick {
            let mut received_any = false;
            for (index, &channel) in channels.iter().enumerate() {
                if buses[index].recv(Duration::from_millis(1)).is_some() {
                    stats[channel as usize].rx_count += 1;
                    received_any = true;
                }
            }
            if !received_any {
                std::thread::sleep(Duration::from_millis(1));
            }
        }
    }

    let drain_deadline = Instant::now() + receive_timeout;
    while Instant::now() < drain_deadline {
        let mut received_any = false;
        for (index, &channel) in channels.iter().enumerate() {
            if buses[index].recv(Duration::from_millis(1)).is_some() {
                stats[channel as usize].rx_count += 1;
                received_any = true;
            }
        }
        if !received_any {
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    let elapsed = run_start.elapsed().as_secs_f64();
    print_stats_summary(summary_title, channels, &stats, elapsed);
    for bus in &buses {
        bus.shutdown();
    }
    Ok(0)
}