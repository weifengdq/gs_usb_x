using GsUsb;

namespace GsUsb.Examples;

internal static class Example03SimpleFd
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "Single-channel CAN FD example");
        using var txBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.TxChannel, fd: true));
        using var rxBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.RxChannel, fd: true));

        var message = new CanMessage
        {
            ArbitrationId = 0x1FFFFFF0u,
            IsExtendedId = true,
            IsFd = true,
            BitrateSwitch = true,
            Data = DemoCommon.MakePayload([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88], 64, 0x00),
        };

        DemoCommon.PrintMessage("Sending", message);
        txBus.Send(message);
        var received = rxBus.Receive(TimeSpan.FromSeconds(2));
        if (received is null)
        {
            Console.Error.WriteLine("No FD frame received");
            return 1;
        }

        DemoCommon.PrintMessage("Received", received);
        return 0;
    }
}