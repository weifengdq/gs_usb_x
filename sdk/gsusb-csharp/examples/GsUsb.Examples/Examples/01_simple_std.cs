using GsUsb;

namespace GsUsb.Examples;

internal static class Example01SimpleStd
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "Single-channel classic CAN example");
        Console.WriteLine($"Opening {options.Selector} tx={options.TxChannel} rx={options.RxChannel} @ {options.Bitrate}bps");
        using var txBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.TxChannel, fd: false));
        using var rxBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.RxChannel, fd: false));

        var message = new CanMessage
        {
            ArbitrationId = 0x123,
            Data = [0x11, 0x22, 0x33, 0x44],
        };

        DemoCommon.PrintMessage("Sending", message);
        txBus.Send(message);
        var received = rxBus.Receive(TimeSpan.FromSeconds(2));
        if (received is null)
        {
            Console.Error.WriteLine("No frame received");
            return 1;
        }

        DemoCommon.PrintMessage("Received", received);
        return 0;
    }
}