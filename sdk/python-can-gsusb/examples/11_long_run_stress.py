import threading
import time

import can

from common import build_channel_stats, build_pair_topology_text, get_parser, print_stats_summary
from gsusb import GsUsbBus


def rx_worker(channel_index: int, bus: GsUsbBus, stop_event: threading.Event, rx_timeout: float, stats) -> None:
    while not stop_event.is_set():
        try:
            msg = bus.recv(rx_timeout)
            if msg is not None:
                stats[channel_index].incr_rx()
        except Exception:
            break


def tx_worker(channel_index: int, bus: GsUsbBus, stop_event: threading.Event, period_s: float, use_fd: bool, stats, payload_len: int) -> None:
    counter = 0
    next_tick = time.monotonic()
    while not stop_event.is_set():
        try:
            base_payload = [channel_index, counter & 0xFF, (counter >> 8) & 0xFF, 0x47, 0x53, 0x55, 0x42]
            target_len = payload_len if use_fd else min(payload_len, 8)
            payload = (base_payload + [(channel_index + counter) & 0xFF] * max(target_len - len(base_payload), 0))[:target_len]
            msg = can.Message(
                arbitration_id=0x680 + channel_index,
                is_extended_id=False,
                is_fd=use_fd,
                bitrate_switch=use_fd,
                data=payload,
            )
            bus.send(msg)
            stats[channel_index].incr_tx()
            counter += 1
        except Exception:
            break
        next_tick += period_s
        while not stop_event.is_set():
            delay_s = next_tick - time.monotonic()
            if delay_s <= 0:
                break
            time.sleep(min(delay_s, 0.01))


def main() -> None:
    parser = get_parser("Long-duration GSUSB multi-channel stress tool")
    parser.add_argument("--channel-count", type=int, choices=[4, 8], default=8, help="Number of logical channels to open (default: 8)")
    parser.add_argument("--payload-len", type=int, default=12, help="Payload length in bytes (default: 12 for FD, capped to 8 for classic)")
    args = parser.parse_args()
    use_fd = not args.classic
    channels = list(range(args.channel_count))
    run_duration_s = max(args.duration, 0.0)

    buses = []
    threads = []
    stop_event = threading.Event()
    stats = build_channel_stats(channels)
    run_start_time = None

    try:
        print(f"Pair topology: {build_pair_topology_text(channels)}")
        print(f"Running long stress for {run_duration_s:.1f}s, payload_len={args.payload_len}, fd={use_fd}")
        for channel_index in channels:
            print(f"Opening Channel {channel_index}...")
            bus = GsUsbBus(
                channel=args.selector,
                gsusb_channel=channel_index,
                bitrate=args.bitrate,
                fd=use_fd,
                data_bitrate=args.data_bitrate,
                termination_enabled=not args.no_termination,
            )
            buses.append(bus)

            thread_rx = threading.Thread(target=rx_worker, args=(channel_index, bus, stop_event, args.rx_timeout, stats), daemon=True)
            thread_tx = threading.Thread(target=tx_worker, args=(channel_index, bus, stop_event, args.period, use_fd, stats, args.payload_len), daemon=True)
            thread_rx.start()
            thread_tx.start()
            threads.extend([thread_rx, thread_tx])

        run_start_time = time.monotonic()
        deadline = time.monotonic() + run_duration_s
        while not stop_event.is_set():
            remaining_s = deadline - time.monotonic()
            if remaining_s <= 0:
                break
            time.sleep(min(remaining_s, 0.5))
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        stop_event.set()
        for thread in threads:
            thread.join(timeout=1.0)
        elapsed_s = None if run_start_time is None else max(time.monotonic() - run_start_time, 1e-9)
        print_stats_summary("Long-run stress summary", channels, stats, elapsed_s)
        for bus in buses:
            bus.shutdown()


if __name__ == "__main__":
    main()