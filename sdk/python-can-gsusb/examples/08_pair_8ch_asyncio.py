import asyncio
import time

import can

from common import build_channel_stats, get_parser, print_stats_summary
from gsusb import GsUsbBus


CHANNELS = list(range(8))
PAIR_TEXT = "can0-can1 / can2-can3 / can4-can5 / can6-can7"


async def periodic_sender(channel_index: int, bus: GsUsbBus, stats, period_s: float, use_fd: bool, stop_event: asyncio.Event) -> None:
    counter = 0
    loop = asyncio.get_running_loop()
    next_tick = loop.time()
    try:
        while not stop_event.is_set():
            payload = [channel_index, counter & 0xFF, (counter >> 8) & 0xFF, 0xC0, 0xDE, 0x12, 0x34, 0x56]
            if use_fd:
                payload += [0x78, 0x9A, 0xBC, 0xEF]
            msg = can.Message(
                arbitration_id=0x600 + channel_index,
                is_extended_id=False,
                is_fd=use_fd,
                bitrate_switch=use_fd,
                data=payload,
            )
            bus.send(msg)
            stats[channel_index].incr_tx()
            counter += 1

            next_tick += period_s
            delay_s = next_tick - loop.time()
            if delay_s <= 0:
                await asyncio.sleep(0)
                continue
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=delay_s)
            except asyncio.TimeoutError:
                pass
    except asyncio.CancelledError:
        pass


async def bus_handler(channel_index: int, bus: GsUsbBus, stats, period_s: float, use_fd: bool, rx_timeout: float, stop_event: asyncio.Event) -> None:
    reader = can.AsyncBufferedReader()
    notifier = can.Notifier(bus, [reader], loop=asyncio.get_running_loop())
    tx_task = asyncio.create_task(periodic_sender(channel_index, bus, stats, period_s, use_fd, stop_event))
    try:
        while not stop_event.is_set():
            try:
                msg = await asyncio.wait_for(reader.get_message(), timeout=rx_timeout)
            except asyncio.TimeoutError:
                continue
            if msg is not None:
                stats[channel_index].incr_rx()
    except asyncio.CancelledError:
        pass
    finally:
        tx_task.cancel()
        await asyncio.gather(tx_task, return_exceptions=True)
        notifier.stop()


async def main_async(args) -> None:
    use_fd = not args.classic
    run_duration_s = max(args.duration, 0.0)
    buses = []
    tasks = []
    stats = build_channel_stats(CHANNELS)
    stop_event = asyncio.Event()
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
            tasks.append(
                asyncio.create_task(
                    bus_handler(channel_index, bus, stats, args.period, use_fd, args.rx_timeout, stop_event)
                )
            )

        run_start_time = time.monotonic()
        print(f"Running 8-channel asyncio demo for {run_duration_s:.1f}s...")
        await asyncio.sleep(run_duration_s)
    finally:
        stop_event.set()
        elapsed_s = None if run_start_time is None else max(time.monotonic() - run_start_time, 1e-9)
        await asyncio.gather(*tasks, return_exceptions=True)
        print_stats_summary("8-channel asyncio summary", CHANNELS, stats, elapsed_s)
        for bus in buses:
            bus.shutdown()


def main() -> None:
    parser = get_parser("8-Channel Standard CAN Asyncio Example")
    args = parser.parse_args()
    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        print("\nStopping...")


if __name__ == "__main__":
    main()