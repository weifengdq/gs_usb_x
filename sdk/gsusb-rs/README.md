# gsusb-rs

Rust driver and example set for the N32_GS_USB firmware running the gs_usb protocol on USB VID:PID 1D50:606F.

This project is the Rust companion to the already verified Python package in [python-can-gsusb](../python-can-gsusb). The example names are aligned with the Python examples as closely as practical.

## Layout

- src/lib.rs: Rust gs_usb driver library
- examples/: Rust example executables matching the Python naming
- scripts/run_examples.ps1: one-shot build and batch test runner

## Build

```powershell
cargo build --examples
```

## Runtime Dependency

This project uses `rusb` with the `vendored` feature, so no separate manual `libusb-1.0.dll` copy step is required for the Rust examples in this workspace.

## Example Commands

Single example:

```powershell
cargo run --example 01_simple_std -- --classic --bitrate 1000000 --tx-channel 0 --rx-channel 1
```

Batch script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1
```

Useful variants:

```powershell
# Reuse existing build output
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1 -SkipBuild

# Run only selected examples
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1 -SkipBuild -Only 01_simple_std,03_simple_fd,11_long_run_stress
```

## Example Mapping

| Rust example | Python counterpart | Purpose |
| --- | --- | --- |
| 01_simple_std | 01_simple_std.py | single-channel classic CAN send/recv |
| 02_periodic | 02_periodic.py | classic CAN periodic send/recv |
| 03_simple_fd | 03_simple_fd.py | single-channel CAN FD send/recv |
| 04_custom_timing | 04_custom_timing.py | single-channel CAN FD with explicit timings |
| 04_multi_std_asyncio | 04_multi_std_asyncio.py | 4-channel summary demo |
| 01_pair_4ch_threading | 01_pair_4ch_threading.py | 4-channel pair summary demo |
| 02_pair_8ch_threading | 02_pair_8ch_threading.py | 8-channel pair summary demo |
| 05_pair_4ch_threading | 05_pair_4ch_threading.py | 4-channel pair wrapper demo |
| 06_pair_8ch_threading | 06_pair_8ch_threading.py | 8-channel pair wrapper demo |
| 07_pair_4ch_asyncio | 07_pair_4ch_asyncio.py | 4-channel async-style summary demo |
| 08_pair_8ch_asyncio | 08_pair_8ch_asyncio.py | 8-channel async-style summary demo |
| 09_multi_custom_timing_threading | 09_multi_custom_timing_threading.py | 4-channel custom timing summary demo |
| 10_multi_custom_timing_asyncio | 10_multi_custom_timing_asyncio.py | 4-channel custom timing async-style summary demo |
| 11_long_run_stress | 11_long_run_stress.py | multi-channel long-run stress demo |

## Verified Hardware Setup

Validated against the real N32_GS_USB board in this workspace:
- USB device enumerated as 1D50:606F
- physical links: can0-can1, can2-can3, can4-can5, can6-can7
- nominal bitrate: 1 Mbit/s
- CAN FD data bitrate: 5 Mbit/s

## Test Results

All examples below were executed on the real connected hardware through:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1 -SkipBuild
```

Important note for summary demos: RX is expected to be about 2x TX because the current receive path reports both local echo and peer-side traffic during these short pair tests.

Important note for performance: the original Rust implementation used one mutex-protected device handle for both the background `read_bulk` loop and all foreground send/control transfers, which heavily serialized multi-channel traffic. After changing the shared handle to a read/write lock and rerunning the full hardware matrix, the Rust results now closely match the C++ and C# versions in this workspace for the current 50 ms period test set.

| Example | Command shape used | Result | Key observation |
| --- | --- | --- | --- |
| 01_simple_std | 01_simple_std --classic --bitrate 1000000 --tx-channel 0 --rx-channel 1 | PASS | single classic frame sent and received |
| 02_periodic | 02_periodic --classic --bitrate 1000000 --tx-channel 0 --rx-channel 1 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | TX=20 RX=20 |
| 03_simple_fd | 03_simple_fd --bitrate 1000000 --data-bitrate 5000000 --tx-channel 0 --rx-channel 1 | PASS | 64-byte FD+BRS frame received correctly |
| 04_custom_timing | 04_custom_timing --tx-channel 0 --rx-channel 1 | PASS | 12-byte FD custom timing frame received |
| 04_multi_std_asyncio | 04_multi_std_asyncio --classic --bitrate 1000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 01_pair_4ch_threading | 01_pair_4ch_threading --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 02_pair_8ch_threading | 02_pair_8ch_threading --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 8ch SUM TX=160 RX=320 |
| 05_pair_4ch_threading | 05_pair_4ch_threading --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 06_pair_8ch_threading | 06_pair_8ch_threading --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 8ch SUM TX=160 RX=320 |
| 07_pair_4ch_asyncio | 07_pair_4ch_asyncio --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 08_pair_8ch_asyncio | 08_pair_8ch_asyncio --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 8ch SUM TX=160 RX=320 |
| 09_multi_custom_timing_threading | 09_multi_custom_timing_threading --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 10_multi_custom_timing_asyncio | 10_multi_custom_timing_asyncio --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 11_long_run_stress | 11_long_run_stress --bitrate 1000000 --data-bitrate 5000000 --channel-count 8 --duration 2 --period 0.05 --rx-timeout 0.05 --payload-len 16 | PASS | 8ch SUM TX=320 RX=640 |

## Measured Summaries

- 04_multi_std_asyncio: SUM TX=80, RX=160, TX/s=75.8, RX/s=151.7
- 01_pair_4ch_threading: SUM TX=80, RX=160, TX/s=75.7, RX/s=151.3
- 02_pair_8ch_threading: SUM TX=160, RX=320, TX/s=150.8, RX/s=301.5
- 05_pair_4ch_threading: SUM TX=80, RX=160, TX/s=76.0, RX/s=152.0
- 06_pair_8ch_threading: SUM TX=160, RX=320, TX/s=150.5, RX/s=301.0
- 07_pair_4ch_asyncio: SUM TX=80, RX=160, TX/s=75.8, RX/s=151.5
- 08_pair_8ch_asyncio: SUM TX=160, RX=320, TX/s=151.7, RX/s=303.4
- 09_multi_custom_timing_threading: SUM TX=80, RX=160, TX/s=75.9, RX/s=151.8
- 10_multi_custom_timing_asyncio: SUM TX=80, RX=160, TX/s=75.6, RX/s=151.3
- 11_long_run_stress: SUM TX=320, RX=640, TX/s=154.8, RX/s=309.5

The full console log from the most recent run is stored at:

```text
target/debug/examples/run_examples.log
```