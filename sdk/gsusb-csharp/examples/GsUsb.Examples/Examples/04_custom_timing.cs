using GsUsb;

namespace GsUsb.Examples;

internal static class Example04CustomTiming
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "Single-channel custom timing CAN FD example");
        var (nominal, data) = DemoCommon.GetDemoCustomTimings();
        using var txBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.TxChannel, fd: true, nominal, data));
        using var rxBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.RxChannel, fd: true, nominal, data));

        var message = new CanMessage
        {
            ArbitrationId = 0x7F0,
            IsFd = true,
            BitrateSwitch = true,
            Data = DemoCommon.MakePayload([0x11], 12, 0x00),
        };

        DemoCommon.PrintMessage("Sending", message);
        txBus.Send(message);
        var received = rxBus.Receive(TimeSpan.FromSeconds(2));
        if (received is null)
        {
            Console.Error.WriteLine("No custom timing frame received");
            return 1;
        }

        DemoCommon.PrintMessage("Received", received);
        return 0;
    }
}