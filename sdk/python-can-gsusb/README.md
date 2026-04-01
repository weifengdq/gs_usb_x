# python-can-gsusb

This package provides a `python-can` interface for GSUSB multi-channel USB CAN FD firmware running on the HPM5xx or N32H7xx platform.

It follows the same multi-channel model as the Windows host tool `gs_usb_x` and exposes one USB device as up to 8 logical CAN channels.

## Features

- One USB device exposes up to 8 logical CAN channels.
- Multiple `python-can` bus objects share the same USB bulk session.
- Supports classic CAN and CAN FD.
- Supports standard gs_usb control requests for channel configuration.
- Supports termination and listen-only mode when the firmware reports those capabilities.

## Installation

```bash
pip install .
```

## Usage

```python
import can

bus0 = can.Bus(
    interface="gsusb",
    channel="auto",
    gsusb_channel=0,
    bitrate=1_000_000,
    fd=True,
    data_bitrate=5_000_000,
)

bus1 = can.Bus(
    interface="gsusb",
    channel="auto",
    gsusb_channel=1,
    bitrate=1_000_000,
    fd=True,
    data_bitrate=5_000_000,
)

msg = can.Message(
    arbitration_id=0x123,
    is_extended_id=False,
    is_fd=True,
    bitrate_switch=True,
    data=list(range(12)),
)
bus0.send(msg)

print(bus1.recv(1.0))
```

## Device Selector

The `channel` argument selects the USB device, not the CAN channel.

- `auto`: open the first matching `1D50:606F` device.
- `BBB:AAA`: match a USB bus/address pair, for example `001:004`.
- `BBB:AAA:IF`: match bus, address and interface number.
- Any other string is treated as a USB serial number.

The logical CAN channel is selected with `gsusb_channel=0..7`.

## Recommended Commands

The following commands are the exact commands used during the latest Windows hardware smoke tests in this workspace.

```powershell
python -m pip install -e .
python -m compileall src examples
python examples/04_multi_std_asyncio.py --duration 1 --period 0.05 --rx-timeout 0.1
python examples/01_pair_4ch_threading.py --duration 1 --period 0.05 --rx-timeout 0.1
python examples/02_pair_8ch_threading.py --duration 1 --period 0.05 --rx-timeout 0.1
python examples/08_pair_8ch_asyncio.py --duration 1 --period 0.05 --rx-timeout 0.1
python examples/11_long_run_stress.py --channel-count 8 --duration 2 --period 0.05 --rx-timeout 0.05
```

For direct `python-can` entry-point verification, the device was also tested with `can.Bus(interface="gsusb", ...)` on channels `0` and `1` for a real FD+BRS single-frame transfer.

## Examples

- `examples/01_simple_std.py`
  - Open one logical channel and send one classic CAN frame.
- `examples/02_periodic.py`
  - Open one logical channel and send frames periodically.
- `examples/03_simple_fd.py`
  - Open one logical channel and send one CAN FD+BRS frame.
- `examples/04_custom_timing.py`
  - Open one logical channel with explicit custom nominal/data timing.
- `examples/04_multi_std_asyncio.py`
  - Run 4 logical channels with asyncio and print per-channel TX/RX summary.
- `examples/07_pair_4ch_asyncio.py`
  - Wrapper entry for the 4-channel asyncio pair example.
- `examples/08_pair_8ch_asyncio.py`
  - `can0-can1`, `can2-can3`, `can4-can5`, `can6-can7` pair traffic with asyncio.
  - All 8 channels run TX/RX at the same time and print per-channel TX/RX summary.
- `examples/05_pair_4ch_threading.py`
  - `can0-can1` and `can2-can3` pair traffic.
  - All 4 channels run TX/RX at the same time and print per-channel TX/RX summary.
- `examples/06_pair_8ch_threading.py`
  - `can0-can1`, `can2-can3`, `can4-can5`, `can6-can7` pair traffic.
  - All 8 channels run TX/RX at the same time and print per-channel TX/RX summary.
- `examples/09_multi_custom_timing_threading.py`
  - Run 4 logical channels with explicit custom nominal/data timing using threading.
- `examples/10_multi_custom_timing_asyncio.py`
  - Run 4 logical channels with explicit custom nominal/data timing using asyncio.
- `examples/11_long_run_stress.py`
  - Long-duration multi-channel stress tool that prints per-channel TX/RX counts and pps totals.

## Verified On Hardware

This package has been smoke-tested against a real `GSUSB (HS)` device on Windows with the following conditions:

- Device: `1D50:606F`, interface `0`
- Physical wiring:
  - `can0-can1`
  - `can2-can3`
  - `can4-can5`
  - `can6-can7`
- Tested timing: nominal `1M`, data `5M`, CAN FD+BRS

Observed results during real runs:

- `can0 -> can1` single-frame receive path works.
- 4-channel simultaneous run (`0..3`) works.
- 8-channel simultaneous run (`0..7`) works.

Note: in simultaneous TX+RX tests each channel will usually observe both:

- its peer's traffic from the paired bus, and
- its own device-side echo/ack traffic

so RX counts can be roughly twice the TX count in simple loopback-style smoke tests.
