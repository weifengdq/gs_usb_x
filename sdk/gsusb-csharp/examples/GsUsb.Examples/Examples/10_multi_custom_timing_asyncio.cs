using GsUsb;

namespace GsUsb.Examples;

internal static class Example10MultiCustomTimingAsyncio
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "4-channel custom timing async example");
        var (nominal, data) = DemoCommon.GetDemoCustomTimings();
        var channels = new[] { 0, 1, 2, 3 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running 4-channel custom timing async demo for {options.DurationSeconds:F6}s...",
            "4-channel custom timing async summary",
            channels,
            options,
            WorkerStyle.Async,
            channelIndex => new GsUsbBus(DemoCommon.MakeBusOptions(options, channelIndex, true, nominal, data)),
            (channelIndex, counter) => new CanMessage
            {
                ArbitrationId = 0x800u + (uint)channelIndex,
                IsFd = true,
                BitrateSwitch = true,
                Data = DemoCommon.MakePayload([(byte)channelIndex, (byte)(counter & 0xFF), (byte)((counter >> 8) & 0xFF)], 20, (byte)channelIndex),
            });
    }
}