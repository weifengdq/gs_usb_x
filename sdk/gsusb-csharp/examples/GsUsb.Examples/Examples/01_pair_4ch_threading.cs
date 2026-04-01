using GsUsb;

namespace GsUsb.Examples;

internal static class Example01Pair4ChThreading
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "4-channel pair traffic example: can0-can1 / can2-can3, all 4 channels TX+RX");
        var useFd = !options.Classic;
        var channels = new[] { 0, 1, 2, 3 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running 4-channel threading demo for {options.DurationSeconds:F6}s...",
            "4-channel pair summary",
            channels,
            options,
            WorkerStyle.Threading,
            channelIndex => new GsUsbBus(DemoCommon.MakeBusOptions(options, channelIndex, useFd)),
            (channelIndex, counter) => new CanMessage
            {
                ArbitrationId = 0x500u + (uint)channelIndex,
                IsFd = useFd,
                BitrateSwitch = useFd,
                Data = DemoCommon.MakePayload([(byte)channelIndex, (byte)(counter & 0xFF), (byte)((counter >> 8) & 0xFF), 0xA5, 0x5A, 0x11, 0x22, 0x33], useFd ? 12 : 8, 0x44),
            });
    }
}