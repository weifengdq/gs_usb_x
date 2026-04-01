using GsUsb;

namespace GsUsb.Examples;

internal static class Example02Periodic
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "Single-channel periodic send example");
        using var txBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.TxChannel, fd: false));
        using var rxBus = new GsUsbBus(DemoCommon.MakeBusOptions(options, options.RxChannel, fd: false));

        var count = Math.Max(1, (int)Math.Round(options.DurationSeconds / Math.Max(options.PeriodSeconds, 0.001), MidpointRounding.AwayFromZero));
        ulong txCount = 0;
        ulong rxCount = 0;
        for (var index = 0; index < count; index++)
        {
            var message = new CanMessage
            {
                ArbitrationId = 0x100,
                Data = [(byte)(index & 0xFF), 0x01, 0x02, 0x03],
            };
            txBus.Send(message);
            txCount++;

            var received = rxBus.Receive(TimeSpan.FromSeconds(options.ReceiveTimeoutSeconds));
            if (received is not null)
            {
                rxCount++;
                DemoCommon.PrintMessage("Received", received);
            }

            Thread.Sleep(TimeSpan.FromSeconds(Math.Max(0.0, options.PeriodSeconds)));
        }

        Console.WriteLine($"Periodic summary TX={txCount} RX={rxCount}");
        return 0;
    }
}