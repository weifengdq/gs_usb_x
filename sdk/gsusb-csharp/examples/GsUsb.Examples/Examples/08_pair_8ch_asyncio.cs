using GsUsb;

namespace GsUsb.Examples;

internal static class Example08Pair8ChAsyncio
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "8-channel pair async example");
        var useFd = !options.Classic;
        var channels = new[] { 0, 1, 2, 3, 4, 5, 6, 7 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running 8-channel async demo for {options.DurationSeconds:F6}s...",
            "8-channel async summary",
            channels,
            options,
            WorkerStyle.Async,
            channelIndex => new GsUsbBus(DemoCommon.MakeBusOptions(options, channelIndex, useFd)),
            (channelIndex, counter) => new CanMessage
            {
                ArbitrationId = 0x600u + (uint)channelIndex,
                IsFd = useFd,
                BitrateSwitch = useFd,
                Data = DemoCommon.MakePayload([(byte)channelIndex, (byte)(counter & 0xFF), (byte)((counter >> 8) & 0xFF), 0xC0, 0xDE, 0x12, 0x34, 0x56], useFd ? 12 : 8, 0x78),
            });
    }
}