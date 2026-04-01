import argparse
import os
import sys
import threading
from dataclasses import dataclass, field
from typing import Dict, Iterable


sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src")))


@dataclass
class ChannelStats:
    tx_count: int = 0
    rx_count: int = 0
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)

    def incr_tx(self) -> None:
        with self._lock:
            self.tx_count += 1

    def incr_rx(self) -> None:
        with self._lock:
            self.rx_count += 1

    def snapshot(self) -> tuple[int, int]:
        with self._lock:
            return self.tx_count, self.rx_count


def get_parser(description: str) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--selector", default="auto", help="USB selector: auto, BUS:ADDR, BUS:ADDR:IF, or serial number")
    parser.add_argument("--bitrate", type=int, default=1_000_000, help="Nominal bitrate in bps (default: 1000000)")
    parser.add_argument("--data-bitrate", type=int, default=5_000_000, help="CAN FD data bitrate in bps (default: 5000000)")
    parser.add_argument("--period", type=float, default=1.0, help="Transmit period in seconds (default: 1.0)")
    parser.add_argument("--duration", type=float, default=3.0, help="Run duration in seconds before printing summary (default: 3.0)")
    parser.add_argument("--rx-timeout", type=float, default=0.2, help="Receive timeout in seconds (default: 0.2)")
    parser.add_argument("--classic", action="store_true", help="Use classic CAN instead of CAN FD")
    parser.add_argument("--no-termination", action="store_true", help="Disable termination control request")
    return parser


def build_channel_stats(channels: Iterable[int]) -> Dict[int, ChannelStats]:
    return {channel_index: ChannelStats() for channel_index in channels}


def print_stats_summary(title: str, channels: Iterable[int], stats: Dict[int, ChannelStats], duration_s: float | None = None) -> None:
    total_tx = 0
    total_rx = 0
    show_rate = duration_s is not None and duration_s > 0
    print(f"\n{title}")
    if show_rate:
        print("CH    TX    TX/s      RX    RX/s")
        print("---------------------------------")
    else:
        print("CH    TX      RX")
        print("-------------------")
    for channel_index in channels:
        tx_count, rx_count = stats[channel_index].snapshot()
        total_tx += tx_count
        total_rx += rx_count
        if show_rate:
            tx_pps = tx_count / duration_s
            rx_pps = rx_count / duration_s
            print(f"{channel_index:<2}  {tx_count:>6}  {tx_pps:>6.1f}  {rx_count:>6}  {rx_pps:>6.1f}")
        else:
            print(f"{channel_index:<2}  {tx_count:>6}  {rx_count:>6}")
    if show_rate:
        print("---------------------------------")
        print(f"SUM  {total_tx:>6}  {total_tx / duration_s:>6.1f}  {total_rx:>6}  {total_rx / duration_s:>6.1f}")
    else:
        print("-------------------")
        print(f"SUM  {total_tx:>6}  {total_rx:>6}")


def build_pair_topology_text(channels: Iterable[int]) -> str:
    channel_list = list(channels)
    pairs = []
    for index in range(0, len(channel_list), 2):
        if index + 1 >= len(channel_list):
            break
        pairs.append(f"can{channel_list[index]}-can{channel_list[index + 1]}")
    return " / ".join(pairs)


def get_demo_custom_timings():
    from gsusb import BusTiming

    nominal = BusTiming(brp=2, prop_seg=0, phase_seg1=31, phase_seg2=8, sjw=8)
    data = BusTiming(brp=1, prop_seg=0, phase_seg1=7, phase_seg2=2, sjw=2)
    return nominal, data
