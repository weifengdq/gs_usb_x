namespace GsUsb.Examples;

internal static class Program
{
    private static readonly IReadOnlyDictionary<string, Func<string[], int>> Examples = new Dictionary<string, Func<string[], int>>(StringComparer.OrdinalIgnoreCase)
    {
        ["01_simple_std"] = Example01SimpleStd.Run,
        ["02_periodic"] = Example02Periodic.Run,
        ["03_simple_fd"] = Example03SimpleFd.Run,
        ["04_custom_timing"] = Example04CustomTiming.Run,
        ["04_multi_std_asyncio"] = Example04MultiStdAsyncio.Run,
        ["01_pair_4ch_threading"] = Example01Pair4ChThreading.Run,
        ["02_pair_8ch_threading"] = Example02Pair8ChThreading.Run,
        ["05_pair_4ch_threading"] = Example05Pair4ChThreading.Run,
        ["06_pair_8ch_threading"] = Example06Pair8ChThreading.Run,
        ["07_pair_4ch_asyncio"] = Example07Pair4ChAsyncio.Run,
        ["08_pair_8ch_asyncio"] = Example08Pair8ChAsyncio.Run,
        ["09_multi_custom_timing_threading"] = Example09MultiCustomTimingThreading.Run,
        ["10_multi_custom_timing_asyncio"] = Example10MultiCustomTimingAsyncio.Run,
        ["11_long_run_stress"] = Example11LongRunStress.Run,
    };

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0 || args[0] is "--help" or "-h")
            {
                PrintUsage();
                return 0;
            }

            var exampleName = args[0];
            if (!Examples.TryGetValue(exampleName, out var runner))
            {
                Console.Error.WriteLine($"Unknown example: {exampleName}");
                PrintUsage();
                return 1;
            }

            return runner(args[1..]);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }

    private static void PrintUsage()
    {
        Console.WriteLine("GsUsb.Examples <example-name> [options]");
        Console.WriteLine();
        Console.WriteLine("Examples:");
        foreach (var key in Examples.Keys.OrderBy(static value => value, StringComparer.OrdinalIgnoreCase))
        {
            Console.WriteLine($"  {key}");
        }
    }
}