using System.Globalization;

namespace GsUsb;

public readonly record struct BusTiming(int Brp, int PropSeg, int PhaseSeg1, int PhaseSeg2, int Sjw);

public readonly record struct TimingLimits(
    int FclkHz,
    int Tseg1Min,
    int Tseg1Max,
    int Tseg2Min,
    int Tseg2Max,
    int SjwMax,
    int BrpMin,
    int BrpMax,
    int BrpInc);

public sealed class DeviceSummary
{
    public string Key { get; init; } = string.Empty;
    public int Bus { get; init; }
    public int Address { get; init; }
    public int VendorId { get; init; }
    public int ProductId { get; init; }
    public int InterfaceNumber { get; init; }
    public string? SerialNumber { get; init; }
    public string? Manufacturer { get; init; }
    public string? Product { get; init; }
    public string Label { get; init; } = string.Empty;
}

public sealed class ChannelCapabilities
{
    public int Index { get; init; }
    public uint FeatureFlags { get; init; }
    public TimingLimits NominalLimits { get; init; }
    public TimingLimits? DataLimits { get; init; }
    public bool FdSupported { get; init; }
    public bool HardwareTimestamp { get; init; }
    public bool TerminationSupported { get; init; }
    public bool TerminationEnabled { get; init; }
}

public sealed class CanMessage
{
    public uint ArbitrationId { get; set; }
    public bool IsExtendedId { get; set; }
    public bool IsRemoteFrame { get; set; }
    public bool IsErrorFrame { get; set; }
    public bool IsFd { get; set; }
    public bool BitrateSwitch { get; set; }
    public bool ErrorStateIndicator { get; set; }
    public bool IsRx { get; set; } = true;
    public int Channel { get; set; } = -1;
    public double TimestampSeconds { get; set; }
    public byte[] Data { get; set; } = [];

    public override string ToString()
    {
        var hex = string.Join(" ", Data.Select(static value => value.ToString("X2", CultureInfo.InvariantCulture)));
        return $"ID=0x{ArbitrationId:X} DL={Data.Length} FD={(IsFd ? "Y" : "N")} BRS={(BitrateSwitch ? "Y" : "N")} CH={Channel} DATA={hex}";
    }
}

public sealed class BusOptions
{
    public const ushort DefaultVendorId = 0x1D50;
    public const ushort DefaultProductId = 0x606F;

    public string Selector { get; init; } = "auto";
    public int GsUsbChannel { get; init; }
    public int? Bitrate { get; init; }
    public bool Fd { get; init; }
    public int? DataBitrate { get; init; }
    public BusTiming? NominalTiming { get; init; }
    public BusTiming? DataTiming { get; init; }
    public bool ListenOnly { get; init; }
    public bool TerminationEnabled { get; init; } = true;
    public ushort VendorId { get; init; } = DefaultVendorId;
    public ushort ProductId { get; init; } = DefaultProductId;
    public int? InterfaceNumber { get; init; }
    public string? SerialNumber { get; init; }
}