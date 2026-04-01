import time

import can

from common import get_parser
from gsusb import GsUsbBus


def main() -> None:
    parser = get_parser("Single-channel periodic send example")
    parser.add_argument("--gsusb-channel", type=int, default=0, help="Logical gs_usb channel (default: 0)")
    args = parser.parse_args()

    print(f"Opening {args.selector} Channel {args.gsusb_channel} @ {args.bitrate}bps...")
    bus = GsUsbBus(
        channel=args.selector,
        gsusb_channel=args.gsusb_channel,
        bitrate=args.bitrate,
        fd=False,
        termination_enabled=not args.no_termination,
    )
    try:
        print(f"Starting periodic send every {args.period}s")
        counter = 0
        while True:
            msg = can.Message(
                arbitration_id=0x100,
                data=[counter & 0xFF, 0x01, 0x02, 0x03],
                is_extended_id=False,
            )
            bus.send(msg)
            print(f"Sent: {msg}")
            stop = time.time() + args.period
            while time.time() < stop:
                rx = bus.recv(0.1)
                if rx is not None:
                    print(f"Received: {rx}")
            counter += 1
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
