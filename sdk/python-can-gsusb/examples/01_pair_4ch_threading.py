import threading
import time

import can

from common import build_channel_stats, get_parser, print_stats_summary
from gsusb import GsUsbBus


CHANNELS = [0, 1, 2, 3]
PAIR_TEXT = "can0-can1 / can2-can3"


def rx_worker(channel_index: int, bus: GsUsbBus, stop_event: threading.Event, rx_timeout: float, stats) -> None:
    while not stop_event.is_set():
        try:
            msg = bus.recv(rx_timeout)
            if msg is not None:
                stats[channel_index].incr_rx()
        except Exception:
            break


def tx_worker(channel_index: int, bus: GsUsbBus, stop_event: threading.Event, period_s: float, use_fd: bool, stats) -> None:
    counter = 0
    next_tick = time.monotonic()
    while not stop_event.is_set():
        try:
            payload = [channel_index, counter & 0xFF, (counter >> 8) & 0xFF, 0xA5, 0x5A, 0x11, 0x22, 0x33]
            if use_fd:
                payload += [0x44, 0x55, 0x66, 0x77]
            msg = can.Message(
                arbitration_id=0x500 + channel_index,
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
    parser = get_parser("4-channel pair traffic example: can0-can1 / can2-can3, all 4 channels TX+RX")
    args = parser.parse_args()
    use_fd = not args.classic
    run_duration_s = max(args.duration, 0.0)

    buses = []
    threads = []
    stop_event = threading.Event()
    stats = build_channel_stats(CHANNELS)
    run_start_time = None

    try:
        print(f"Pair topology: {PAIR_TEXT}")
        for channel_index in CHANNELS:
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
            thread_tx = threading.Thread(target=tx_worker, args=(channel_index, bus, stop_event, args.period, use_fd, stats), daemon=True)
            thread_rx.start()
            thread_tx.start()
            threads.extend([thread_rx, thread_tx])

        run_start_time = time.monotonic()
        print(f"Running 4-channel threading demo for {run_duration_s:.1f}s...")
        deadline = time.monotonic() + run_duration_s
        while not stop_event.is_set():
            remaining_s = deadline - time.monotonic()
            if remaining_s <= 0:
                break
            time.sleep(min(remaining_s, 0.2))
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        stop_event.set()
        for thread in threads:
            thread.join(timeout=1.0)
        elapsed_s = None if run_start_time is None else max(time.monotonic() - run_start_time, 1e-9)
        print_stats_summary("4-channel pair summary", CHANNELS, stats, elapsed_s)
        for bus in buses:
            bus.shutdown()


if __name__ == "__main__":
    main()
