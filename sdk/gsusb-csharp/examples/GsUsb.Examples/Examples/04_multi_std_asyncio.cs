using GsUsb;

namespace GsUsb.Examples;

internal static class Example04MultiStdAsyncio
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "4-channel standard CAN async example");
        var useFd = !options.Classic;
        var channels = new[] { 0, 1, 2, 3 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running 4-channel async demo for {options.DurationSeconds:F6}s...",
            "4-channel async summary",
            channels,
            options,
            WorkerStyle.Async,
            channelIndex => new GsUsbBus(DemoCommon.MakeBusOptions(options, channelIndex, useFd)),
            (channelIndex, counter) => new CanMessage
            {
                ArbitrationId = 0x300u + (uint)channelIndex,
                IsFd = useFd,
                BitrateSwitch = useFd,
                Data = DemoCommon.MakePayload([(byte)channelIndex, (byte)(counter & 0xFF), (byte)((counter >> 8) & 0xFF), 0x31, 0x41, 0x59, 0x26, 0x53], useFd ? 12 : 8, 0x58),
            });
    }
}