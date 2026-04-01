using System.Globalization;
using GsUsb;

namespace GsUsb.Examples;

internal enum WorkerStyle
{
    Threading,
    Async,
}

internal sealed class CommonOptions
{
    public string Selector { get; set; } = "auto";
    public int Bitrate { get; set; } = 1_000_000;
    public int DataBitrate { get; set; } = 5_000_000;
    public double PeriodSeconds { get; set; } = 1.0;
    public double DurationSeconds { get; set; } = 3.0;
    public double ReceiveTimeoutSeconds { get; set; } = 0.2;
    public bool Classic { get; set; }
    public bool NoTermination { get; set; }
    public int TxChannel { get; set; }
    public int RxChannel { get; set; } = 1;
    public int ChannelCount { get; set; } = 8;
    public int PayloadLength { get; set; } = 12;

    public static CommonOptions Parse(string[] args, string description)
    {
        var options = new CommonOptions();
        for (var index = 0; index < args.Length; index++)
        {
            var arg = args[index];
            string NextValue(string name)
            {
                if (index + 1 >= args.Length)
                {
                    throw new InvalidOperationException($"missing value for {name}");
                }

                index++;
                return args[index];
            }

            switch (arg)
            {
                case "--help":
                case "-h":
                    PrintUsage(description);
                    Environment.Exit(0);
                    break;
                case "--selector":
                    options.Selector = NextValue("--selector");
                    break;
                case "--bitrate":
                    options.Bitrate = int.Parse(NextValue("--bitrate"), CultureInfo.InvariantCulture);
                    break;
                case "--data-bitrate":
                    options.DataBitrate = int.Parse(NextValue("--data-bitrate"), CultureInfo.InvariantCulture);
                    break;
                case "--period":
                    options.PeriodSeconds = double.Parse(NextValue("--period"), CultureInfo.InvariantCulture);
                    break;
                case "--duration":
                    options.DurationSeconds = double.Parse(NextValue("--duration"), CultureInfo.InvariantCulture);
                    break;
                case "--rx-timeout":
                    options.ReceiveTimeoutSeconds = double.Parse(NextValue("--rx-timeout"), CultureInfo.InvariantCulture);
                    break;
                case "--classic":
                    options.Classic = true;
                    break;
                case "--no-termination":
                    options.NoTermination = true;
                    break;
                case "--tx-channel":
                    options.TxChannel = int.Parse(NextValue("--tx-channel"), CultureInfo.InvariantCulture);
                    break;
                case "--rx-channel":
                    options.RxChannel = int.Parse(NextValue("--rx-channel"), CultureInfo.InvariantCulture);
                    break;
                case "--channel-count":
                    options.ChannelCount = int.Parse(NextValue("--channel-count"), CultureInfo.InvariantCulture);
                    break;
                case "--payload-len":
                    options.PayloadLength = int.Parse(NextValue("--payload-len"), CultureInfo.InvariantCulture);
                    break;
                default:
                    throw new InvalidOperationException($"unknown argument: {arg}");
            }
        }

        return options;
    }

    private static void PrintUsage(string description)
    {
        Console.WriteLine(description);
        Console.WriteLine("  --selector <auto|BUS:ADDR|BUS:ADDR:IF|SERIAL>");
        Console.WriteLine("  --bitrate <bps>");
        Console.WriteLine("  --data-bitrate <bps>");
        Console.WriteLine("  --period <seconds>");
        Console.WriteLine("  --duration <seconds>");
        Console.WriteLine("  --rx-timeout <seconds>");
        Console.WriteLine("  --classic");
        Console.WriteLine("  --no-termination");
        Console.WriteLine("  --tx-channel <index>");
        Console.WriteLine("  --rx-channel <index>");
        Console.WriteLine("  --channel-count <4|8>");
        Console.WriteLine("  --payload-len <bytes>");
    }
}

internal sealed class ChannelStats
{
    private long _txCount;
    private long _rxCount;

    public void IncrementTx() => Interlocked.Increment(ref _txCount);
    public void IncrementRx() => Interlocked.Increment(ref _rxCount);
    public long TxCount => Interlocked.Read(ref _txCount);
    public long RxCount => Interlocked.Read(ref _rxCount);
}

internal static class DemoCommon
{
    internal delegate GsUsbBus BusFactory(int channelIndex);
    internal delegate CanMessage MessageFactory(int channelIndex, ulong counter);

    public static (BusTiming Nominal, BusTiming Data) GetDemoCustomTimings()
    {
        return (new BusTiming(2, 0, 31, 8, 8), new BusTiming(1, 0, 7, 2, 2));
    }

    public static string BuildPairTopologyText(IReadOnlyList<int> channels)
    {
        var pairs = new List<string>();
        for (var index = 0; index + 1 < channels.Count; index += 2)
        {
            pairs.Add($"can{channels[index]}-can{channels[index + 1]}");
        }

        return string.Join(" / ", pairs);
    }

    public static byte[] MakePayload(IEnumerable<byte> prefix, int totalLength, byte fillValue)
    {
        var payload = prefix.ToList();
        if (payload.Count < totalLength)
        {
            payload.AddRange(Enumerable.Repeat(fillValue, totalLength - payload.Count));
        }

        if (payload.Count > totalLength)
        {
            payload.RemoveRange(totalLength, payload.Count - totalLength);
        }

        return [.. payload];
    }

    public static BusOptions MakeBusOptions(CommonOptions options, int channelIndex, bool fd, BusTiming? nominal = null, BusTiming? data = null)
    {
        return new BusOptions
        {
            Selector = options.Selector,
            GsUsbChannel = channelIndex,
            Fd = fd,
            TerminationEnabled = !options.NoTermination,
            Bitrate = nominal is null ? options.Bitrate : null,
            DataBitrate = fd && data is null ? options.DataBitrate : null,
            NominalTiming = nominal,
            DataTiming = data,
        };
    }

    public static void PrintMessage(string prefix, CanMessage message)
    {
        Console.WriteLine($"{prefix} {message}");
    }

    public static void PrintStatsSummary(string title, IReadOnlyList<int> channels, IReadOnlyList<ChannelStats> stats, double durationSeconds)
    {
        var effectiveDuration = durationSeconds > 0 ? durationSeconds : 1.0;
        long totalTx = 0;
        long totalRx = 0;

        Console.WriteLine();
        Console.WriteLine(title);
        Console.WriteLine("CH    TX    TX/s      RX    RX/s");
        Console.WriteLine("---------------------------------");

        foreach (var channel in channels)
        {
            var tx = stats[channel].TxCount;
            var rx = stats[channel].RxCount;
            totalTx += tx;
            totalRx += rx;
            Console.WriteLine($"{channel,-2}  {tx,6}  {tx / effectiveDuration,6:F1}  {rx,6}  {rx / effectiveDuration,6:F1}");
        }

        Console.WriteLine("---------------------------------");
        Console.WriteLine($"SUM  {totalTx,6}  {totalTx / effectiveDuration,6:F1}  {totalRx,6}  {totalRx / effectiveDuration,6:F1}");
    }

    public static int RunMultiSummaryDemo(string runTitle, string summaryTitle, IReadOnlyList<int> channels, CommonOptions options, WorkerStyle style, BusFactory busFactory, MessageFactory messageFactory)
    {
        _ = style;
        var buses = new List<GsUsbBus>();
        var stats = Enumerable.Range(0, channels.Max() + 1).Select(static _ => new ChannelStats()).ToArray();
        var counters = new ulong[stats.Length];
        var period = TimeSpan.FromSeconds(options.PeriodSeconds);
        var receiveTimeout = TimeSpan.FromSeconds(options.ReceiveTimeoutSeconds);

        try
        {
            foreach (var channel in channels)
            {
                Console.WriteLine($"Opening Channel {channel}...");
                buses.Add(busFactory(channel));
            }

            Console.WriteLine(runTitle);
            var runStart = DateTime.UtcNow;
            var nextTick = DateTime.UtcNow;
            while ((DateTime.UtcNow - runStart).TotalSeconds < options.DurationSeconds)
            {
                for (var index = 0; index < channels.Count; index++)
                {
                    var channel = channels[index];
                    buses[index].Send(messageFactory(channel, counters[channel]++));
                    stats[channel].IncrementTx();
                }

                nextTick += period;
                while (DateTime.UtcNow < nextTick)
                {
                    var receivedAny = false;
                    for (var index = 0; index < channels.Count; index++)
                    {
                        var channel = channels[index];
                        var message = buses[index].Receive(TimeSpan.FromMilliseconds(1));
                        if (message is not null)
                        {
                            stats[channel].IncrementRx();
                            receivedAny = true;
                        }
                    }

                    if (!receivedAny)
                    {
                        Thread.Sleep(1);
                    }
                }
            }

            var drainDeadline = DateTime.UtcNow + receiveTimeout;
            while (DateTime.UtcNow < drainDeadline)
            {
                var receivedAny = false;
                for (var index = 0; index < channels.Count; index++)
                {
                    var channel = channels[index];
                    var message = buses[index].Receive(TimeSpan.FromMilliseconds(1));
                    if (message is not null)
                    {
                        stats[channel].IncrementRx();
                        receivedAny = true;
                    }
                }

                if (!receivedAny)
                {
                    Thread.Sleep(1);
                }
            }

            var elapsed = (DateTime.UtcNow - runStart).TotalSeconds;
            PrintStatsSummary(summaryTitle, channels, stats, elapsed);
            return 0;
        }
        finally
        {
            foreach (var bus in buses)
            {
                bus.Dispose();
            }
        }
    }
}