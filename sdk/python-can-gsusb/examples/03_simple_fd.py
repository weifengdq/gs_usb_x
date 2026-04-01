import can

from common import get_parser
from gsusb import GsUsbBus


def main() -> None:
    parser = get_parser("Single-channel CAN FD example")
    parser.add_argument("--gsusb-channel", type=int, default=0, help="Logical gs_usb channel (default: 0)")
    args = parser.parse_args()

    print(f"Opening {args.selector} Channel {args.gsusb_channel} FD")
    print(f"Arb: {args.bitrate}bps, Data: {args.data_bitrate}bps")
    bus = GsUsbBus(
        channel=args.selector,
        gsusb_channel=args.gsusb_channel,
        bitrate=args.bitrate,
        fd=True,
        data_bitrate=args.data_bitrate,
        termination_enabled=not args.no_termination,
    )
    try:
        msg = can.Message(
            arbitration_id=0x1FFFFFF0,
            is_extended_id=True,
            is_fd=True,
            bitrate_switch=True,
            data=[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88] + [0x00] * 56,
        )
        print("Sending FD frame (64 bytes)...")
        bus.send(msg)
        print("Listening...")
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
