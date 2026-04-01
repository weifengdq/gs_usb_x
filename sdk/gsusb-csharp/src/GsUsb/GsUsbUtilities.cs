using System.Buffers.Binary;

namespace GsUsb;

public static class GsUsbUtilities
{
    public static IReadOnlyList<DeviceSummary> EnumerateDevices(ushort vendorId = BusOptions.DefaultVendorId, ushort productId = BusOptions.DefaultProductId)
    {
        return GsUsbBus.SharedUsbSession.EnumerateMatchingDevices(vendorId, productId).Select(static item => item.Summary).ToArray();
    }

    public static BusTiming CalculateTiming(TimingLimits limits, int bitrate, double samplePointPercent = 80.0)
    {
        if (bitrate <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(bitrate), "bitrate must be > 0");
        }

        var found = false;
        var bestSpError = double.MaxValue;
        var bestBrp = int.MaxValue;
        var bestTseg2 = int.MaxValue;
        var bestTseg1 = int.MaxValue;
        var bestTotalTq = int.MaxValue;
        var bestTiming = default(BusTiming);

        var step = Math.Max(limits.BrpInc, 1);
        for (var brp = limits.BrpMin; brp <= limits.BrpMax; brp += step)
        {
            var denominator = (long)brp * bitrate;
            if (denominator <= 0 || limits.FclkHz % denominator != 0)
            {
                continue;
            }

            var totalTq = (int)(limits.FclkHz / denominator);
            if (totalTq < 4)
            {
                continue;
            }

            var tsegTotal = totalTq - 1;
            var tseg1Target = (int)Math.Round((samplePointPercent / 100.0) * totalTq - 1.0, MidpointRounding.AwayFromZero);
            foreach (var tseg1 in new[] { tseg1Target, tseg1Target - 1, tseg1Target + 1 })
            {
                var tseg2 = tsegTotal - tseg1;
                if (tseg1 < limits.Tseg1Min || tseg1 > limits.Tseg1Max || tseg2 < limits.Tseg2Min || tseg2 > limits.Tseg2Max)
                {
                    continue;
                }

                var actualSp = ((1.0 + tseg1) / totalTq) * 100.0;
                var spError = Math.Abs(actualSp - samplePointPercent);
                var isBetter = !found
                    || spError < bestSpError
                    || (Math.Abs(spError - bestSpError) < 1e-9 && brp < bestBrp)
                    || (Math.Abs(spError - bestSpError) < 1e-9 && brp == bestBrp && tseg2 < bestTseg2)
                    || (Math.Abs(spError - bestSpError) < 1e-9 && brp == bestBrp && tseg2 == bestTseg2 && tseg1 < bestTseg1)
                    || (Math.Abs(spError - bestSpError) < 1e-9 && brp == bestBrp && tseg2 == bestTseg2 && tseg1 == bestTseg1 && totalTq < bestTotalTq);

                if (!isBetter)
                {
                    continue;
                }

                found = true;
                bestSpError = spError;
                bestBrp = brp;
                bestTseg2 = tseg2;
                bestTseg1 = tseg1;
                bestTotalTq = totalTq;
                bestTiming = new BusTiming(brp, 0, tseg1, tseg2, Math.Min(tseg2, limits.SjwMax));
            }
        }

        if (!found)
        {
            throw new InvalidOperationException("cannot derive timing from channel limits");
        }

        return bestTiming;
    }

    internal static uint[] BytesToUInt32List(byte[] data)
    {
        if ((data.Length % 4) != 0)
        {
            throw new InvalidOperationException("buffer length must be multiple of 4");
        }

        var result = new uint[data.Length / 4];
        for (var index = 0; index < result.Length; index++)
        {
            result[index] = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(index * 4, 4));
        }

        return result;
    }

    internal static int DlcToLength(byte dlc) => dlc switch
    {
        <= 8 => dlc,
        9 => 12,
        10 => 16,
        11 => 20,
        12 => 24,
        13 => 32,
        14 => 48,
        _ => 64,
    };

    internal static byte LengthToDlc(int length) => length switch
    {
        <= 0 => 0,
        <= 8 => (byte)length,
        <= 12 => 9,
        <= 16 => 10,
        <= 20 => 11,
        <= 24 => 12,
        <= 32 => 13,
        <= 48 => 14,
        _ => 15,
    };
}