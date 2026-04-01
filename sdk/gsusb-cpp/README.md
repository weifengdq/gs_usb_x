# gsusb-cpp

C++ driver and example set for the N32_GS_USB firmware running the gs_usb protocol on USB VID:PID 1D50:606F.

This project mirrors the structure of the existing python-can-gsusb package:
- single-channel classic CAN example
- single-channel periodic send example
- single-channel CAN FD example
- single-channel custom timing example
- 4-channel and 8-channel pair traffic examples
- multi-channel custom timing examples
- long-run multi-channel stress example

The current implementation uses libusb loaded at runtime from libusb-1.0.dll and uses a polling receive path in the core driver so it can build and run in the current MinGW environment.

## Layout

- include/gsusb/gsusb.hpp: public API
- src/gsusb.cpp: gs_usb protocol implementation and shared USB session
- src/libusb_dyn.cpp: dynamic binding layer for libusb-1.0.dll
- examples/: example programs aligned with the Python package naming

## Build

Tested on Windows with:
- CMake
- MinGW g++
- libusb-1.0.dll from Python package libusb-package

Build commands:

```powershell
cmake -S . -B build
cmake --build build -j 4
```

One-shot helper script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1
```

Useful variants:

```powershell
# Reuse existing build artifacts and only rerun the full example matrix
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1 -SkipConfigure -SkipBuild

# Run only a subset of examples
powershell -ExecutionPolicy Bypass -File .\scripts\run_examples.ps1 -SkipConfigure -SkipBuild -Only 01_simple_std,03_simple_fd,11_long_run_stress
```

## Runtime DLLs

The executables need these DLLs in build/ or on PATH:
- libusb-1.0.dll
- libstdc++-6.dll
- libgcc_s_seh-1.dll
- libwinpthread-1.dll

Verified copy commands used during this session:

```powershell
Copy-Item -Force "D:/n32/.venv/Lib/site-packages/libusb_package/libusb-1.0.dll" "D:/n32/sd/n32_gs_usb_sd/gsusb-cpp/build/libusb-1.0.dll"
Copy-Item -Force "C:/z/app/mingw64/bin/libstdc++-6.dll" "D:/n32/sd/n32_gs_usb_sd/gsusb-cpp/build/"
Copy-Item -Force "C:/z/app/mingw64/bin/libgcc_s_seh-1.dll" "D:/n32/sd/n32_gs_usb_sd/gsusb-cpp/build/"
Copy-Item -Force "C:/z/app/mingw64/bin/libwinpthread-1.dll" "D:/n32/sd/n32_gs_usb_sd/gsusb-cpp/build/"
```

Note: automatic POST_BUILD DLL copy was intentionally not kept in CMake because the current environment has a cmd.exe invocation problem in nested CMake/Ninja post-build steps.

The helper script above performs the same DLL copy step automatically before running the examples.

## Example Mapping

| C++ example | Purpose |
| --- | --- |
| 01_simple_std.exe | single-channel classic CAN send/recv |
| 02_periodic.exe | classic CAN periodic send/recv |
| 03_simple_fd.exe | single-channel CAN FD send/recv |
| 04_custom_timing.exe | single-channel CAN FD with explicit timings |
| 04_multi_std_asyncio.exe | 4-channel classic CAN summary demo |
| 01_pair_4ch_threading.exe | 4-channel FD pair summary demo |
| 02_pair_8ch_threading.exe | 8-channel FD pair summary demo |
| 05_pair_4ch_threading.exe | 4-channel FD pair wrapper demo |
| 06_pair_8ch_threading.exe | 8-channel FD pair wrapper demo |
| 07_pair_4ch_asyncio.exe | 4-channel FD async-style summary demo |
| 08_pair_8ch_asyncio.exe | 8-channel FD async-style summary demo |
| 09_multi_custom_timing_threading.exe | 4-channel FD custom timing summary demo |
| 10_multi_custom_timing_asyncio.exe | 4-channel FD custom timing async-style summary demo |
| 11_long_run_stress.exe | 4-channel or 8-channel long stress tool |

## Verified Hardware Setup

Validated against the real N32_GS_USB board already prepared in this workspace:
- USB device enumerated as 1D50:606F
- physical links: can0-can1, can2-can3, can4-can5, can6-can7
- nominal bitrate: 1 Mbit/s
- CAN FD data bitrate: 5 Mbit/s

## Test Results

All example executables were built successfully and then run against the real connected hardware from build/.

The same sequence is now scripted in scripts/run_examples.ps1 so the full local validation flow can be rerun without manually re-entering DLL copy and test commands.

Important note for summary demos: RX is expected to be about 2x TX because the current gs_usb data path reports both local echo and the peer-side received traffic for each active channel during these short loopback pair tests.

| Example | Command shape used | Result | Key observation |
| --- | --- | --- | --- |
| 01_simple_std.exe | --classic --bitrate 1000000 --tx-channel 0 --rx-channel 1 | PASS | single classic frame sent and received |
| 02_periodic.exe | --classic --bitrate 1000000 --tx-channel 0 --rx-channel 1 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | TX=20 RX=20 |
| 03_simple_fd.exe | --bitrate 1000000 --data-bitrate 5000000 --tx-channel 0 --rx-channel 1 | PASS | 64-byte FD+BRS frame received correctly |
| 04_custom_timing.exe | --tx-channel 0 --rx-channel 1 | PASS | 12-byte FD custom timing frame received |
| 04_multi_std_asyncio.exe | --classic --bitrate 1000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 01_pair_4ch_threading.exe | --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 02_pair_8ch_threading.exe | --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 8ch SUM TX=160 RX=320 |
| 05_pair_4ch_threading.exe | --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 06_pair_8ch_threading.exe | --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 8ch SUM TX=160 RX=318 |
| 07_pair_4ch_asyncio.exe | --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 08_pair_8ch_asyncio.exe | --bitrate 1000000 --data-bitrate 5000000 --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 8ch SUM TX=160 RX=320 |
| 09_multi_custom_timing_threading.exe | --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 10_multi_custom_timing_asyncio.exe | --duration 1 --period 0.05 --rx-timeout 0.05 | PASS | 4ch SUM TX=80 RX=160 |
| 11_long_run_stress.exe | --bitrate 1000000 --data-bitrate 5000000 --channel-count 8 --duration 2 --period 0.05 --rx-timeout 0.05 --payload-len 16 | PASS | 8ch SUM TX=320 RX=640 |

## Measured Summaries

Captured totals from the real test run:

- 04_multi_std_asyncio.exe: SUM TX=80, RX=160, TX/s=74.3, RX/s=148.7
- 01_pair_4ch_threading.exe: SUM TX=80, RX=160, TX/s=75.4, RX/s=150.9
- 02_pair_8ch_threading.exe: SUM TX=160, RX=320, TX/s=147.0, RX/s=294.0
- 05_pair_4ch_threading.exe: SUM TX=80, RX=160, TX/s=76.0, RX/s=152.1
- 06_pair_8ch_threading.exe: SUM TX=160, RX=318, TX/s=150.4, RX/s=298.8
- 07_pair_4ch_asyncio.exe: SUM TX=80, RX=160, TX/s=75.1, RX/s=150.2
- 08_pair_8ch_asyncio.exe: SUM TX=160, RX=320, TX/s=144.9, RX/s=289.7
- 09_multi_custom_timing_threading.exe: SUM TX=80, RX=160, TX/s=74.9, RX/s=149.9
- 10_multi_custom_timing_asyncio.exe: SUM TX=80, RX=160, TX/s=75.5, RX/s=151.1
- 11_long_run_stress.exe: SUM TX=320, RX=640, TX/s=153.0, RX/s=305.9

## Minimal API Example

```cpp
#include <gsusb/gsusb.hpp>

int main() {
    gsusb::BusOptions options;
    options.gsusb_channel = 0;
    options.bitrate = 1000000;

    gsusb::GsUsbBus bus(options);

    gsusb::CanMessage msg;
    msg.arbitration_id = 0x123;
    msg.data = {0x11, 0x22, 0x33, 0x44};

    bus.send(msg);
    auto rx = bus.recv(std::chrono::milliseconds(100));
    bus.shutdown();
    return rx.has_value() ? 0 : 1;
}
```
