using GsUsb;

namespace GsUsb.Examples;

internal static class Example02Pair8ChThreading
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "8-channel pair traffic example: can0-can1 / can2-can3 / can4-can5 / can6-can7, all 8 channels TX+RX");
        var useFd = !options.Classic;
        var channels = new[] { 0, 1, 2, 3, 4, 5, 6, 7 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running 8-channel threading demo for {options.DurationSeconds:F6}s...",
            "8-channel pair summary",
            channels,
            options,
            WorkerStyle.Threading,
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