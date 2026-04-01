using GsUsb;

namespace GsUsb.Examples;

internal static class Example11LongRunStress
{
    public static int Run(string[] args)
    {
        var options = CommonOptions.Parse(args, "Long-duration GSUSB multi-channel stress tool");
        var useFd = !options.Classic;
        var channels = options.ChannelCount == 4 ? new[] { 0, 1, 2, 3 } : new[] { 0, 1, 2, 3, 4, 5, 6, 7 };
        Console.WriteLine($"Pair topology: {DemoCommon.BuildPairTopologyText(channels)}");
        return DemoCommon.RunMultiSummaryDemo(
            $"Running long stress for {options.DurationSeconds:F6}s, payload_len={options.PayloadLength}, fd={(useFd ? "true" : "false")}",
            "Long-run stress summary",
            channels,
            options,
            WorkerStyle.Threading,
            channelIndex => new GsUsbBus(DemoCommon.MakeBusOptions(options, channelIndex, useFd)),
            (channelIndex, counter) => new CanMessage
            {
                ArbitrationId = 0x680u + (uint)channelIndex,
                IsFd = useFd,
                BitrateSwitch = useFd,
                Data = DemoCommon.MakePayload([(byte)channelIndex, (byte)(counter & 0xFF), (byte)((counter >> 8) & 0xFF), 0x47, 0x53, 0x55, 0x42], useFd ? options.PayloadLength : Math.Min(options.PayloadLength, 8), (byte)((channelIndex + (int)counter) & 0xFF)),
            });
    }
}