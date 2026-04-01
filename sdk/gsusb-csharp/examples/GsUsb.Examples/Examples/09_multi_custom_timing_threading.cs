using GsUsb;

namespace GsUsb.Examples;

internal static class Example09MultiCustomTimingThreading
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "4-channel custom timing threading example");
        var (nominal, data) = DemoCommon.GetDemoCustomTimings();
        var channels = new[] { 0, 1, 2, 3 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running 4-channel custom timing threading demo for {options.DurationSeconds:F6}s...",
            "4-channel custom timing threading summary",
            channels,
            options,
            WorkerStyle.Threading,
            channelIndex => new GsUsbBus(DemoCommon.MakeBusOptions(options, channelIndex, true, nominal, data)),
            (channelIndex, counter) => new CanMessage
            {
                ArbitrationId = 0x700u + (uint)channelIndex,
                IsFd = true,
                BitrateSwitch = true,
                Data = DemoCommon.MakePayload([(byte)channelIndex, (byte)(counter & 0xFF), (byte)((counter >> 8) & 0xFF)], 20, (byte)channelIndex),
            });
    }
}