import can

from common import get_parser
from gsusb import GsUsbBus


def main() -> None:
    parser = get_parser("Single-channel classic CAN example")
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
        msg = can.Message(arbitration_id=0x123, data=[0x11, 0x22, 0x33, 0x44], is_extended_id=False)
        print(f"Sending: {msg}")
        bus.send(msg)
        print("Listening for messages (Ctrl+C to exit)...")
        while True:
            rx = bus.recv(1.0)
            if rx is not None:
                print(f"Received: {rx}")
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
