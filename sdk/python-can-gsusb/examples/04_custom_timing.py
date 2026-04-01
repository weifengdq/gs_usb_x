import can

from common import get_demo_custom_timings, get_parser
from gsusb import GsUsbBus


def main() -> None:
    parser = get_parser("Single-channel custom timing CAN FD example")
    parser.add_argument("--gsusb-channel", type=int, default=0, help="Logical gs_usb channel (default: 0)")
    args = parser.parse_args()

    nominal, data = get_demo_custom_timings()

    print(f"Opening {args.selector} Channel {args.gsusb_channel}...")
    print("Applying custom timing: nominal=(2,0,31,8,8), data=(1,0,7,2,2)")
    bus = GsUsbBus(
        channel=args.selector,
        gsusb_channel=args.gsusb_channel,
        fd=True,
        nominal_timing=nominal,
        data_timing=data,
        termination_enabled=not args.no_termination,
    )
    try:
        msg = can.Message(
            arbitration_id=0x7F0,
            is_extended_id=False,
            is_fd=True,
            bitrate_switch=True,
            data=[0x11] + [0x00] * 11,
        )
        print("Sending custom timing frame...")
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