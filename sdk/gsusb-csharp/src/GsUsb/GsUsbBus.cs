using System.Collections.Concurrent;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace GsUsb;

public sealed class GsUsbBus : IDisposable
{
    internal const byte ReqHostFormat = 0;
    internal const byte ReqBittiming = 1;
    internal const byte ReqMode = 2;
    internal const byte ReqBtConst = 4;
    internal const byte ReqDeviceConfig = 5;
    internal const byte ReqDataBittiming = 10;
    internal const byte ReqBtConstExt = 11;
    internal const byte ReqSetTermination = 12;
    internal const byte ReqGetTermination = 13;

    internal const uint GsCanModeReset = 0;
    internal const uint GsCanModeStart = 1;
    internal const uint GsCanModeListenOnly = 1u << 0;
    internal const uint GsCanModeHwTimestamp = 1u << 4;
    internal const uint GsCanModeFd = 1u << 8;

    internal const uint GsCanFeatureHwTimestamp = 1u << 4;
    internal const uint GsCanFeatureFd = 1u << 8;
    internal const uint GsCanFeatureTermination = 1u << 11;

    internal const byte GsCanFlagFd = 1 << 1;
    internal const byte GsCanFlagBrs = 1 << 2;
    internal const byte GsCanFlagEsi = 1 << 3;

    internal const uint CanEffFlag = 0x80000000u;
    internal const uint CanRtrFlag = 0x40000000u;
    internal const uint CanErrFlag = 0x20000000u;
    internal const uint CanEffMask = 0x1FFFFFFFu;
    internal const uint CanSffMask = 0x000007FFu;

    internal const uint ControlTimeoutMs = 500;
    internal const uint BulkTimeoutMs = 5;

    private readonly BusOptions _options;
    private readonly SharedUsbSession _session;
    private readonly ConcurrentQueue<CanMessage> _queue = new();
    private readonly SemaphoreSlim _queueSignal = new(0);
    private int _shutdownCalled;
    private volatile bool _stopped;

    public GsUsbBus(BusOptions? options = null)
    {
        _options = options ?? new BusOptions();
        _session = SharedUsbSession.Acquire(_options);
        ChannelInfo = _session.Summary.Label + "/gsusb_channel=" + _options.GsUsbChannel.ToString(CultureInfo.InvariantCulture);
        _session.AddBus(this, _options.GsUsbChannel);
        Capabilities = _session.Channels[_options.GsUsbChannel];

        try
        {
            var nominal = _options.NominalTiming;
            if (nominal is null)
            {
                if (_options.Bitrate is null)
                {
                    throw new InvalidOperationException("bitrate or nominal_timing is required");
                }

                nominal = GsUsbUtilities.CalculateTiming(Capabilities.NominalLimits, _options.Bitrate.Value);
            }

            var data = _options.DataTiming;
            if (_options.Fd)
            {
                if (data is null)
                {
                    if (_options.DataBitrate is null)
                    {
                        throw new InvalidOperationException("data_bitrate or data_timing is required when fd=true");
                    }

                    if (Capabilities.DataLimits is null)
                    {
                        throw new InvalidOperationException("channel does not support CAN FD");
                    }

                    data = GsUsbUtilities.CalculateTiming(Capabilities.DataLimits.Value, _options.DataBitrate.Value);
                }
            }

            _session.ConfigureChannel(_options.GsUsbChannel, nominal.Value, data, _options.Fd, _options.ListenOnly, _options.TerminationEnabled, start: true);
        }
        catch
        {
            Shutdown();
            throw;
        }
    }

    public string ChannelInfo { get; }
    public DeviceSummary DeviceSummary => _session.Summary;
    public ChannelCapabilities Capabilities { get; }

    public void Send(CanMessage message)
    {
        ObjectDisposedException.ThrowIf(_shutdownCalled != 0, this);
        _session.SendFrame(_options.GsUsbChannel, message);
    }

    public CanMessage? Receive(TimeSpan timeout)
    {
        if (_queueSignal.Wait(timeout) && _queue.TryDequeue(out var message))
        {
            return message;
        }

        return null;
    }

    internal void OnMessageReceived(CanMessage message)
    {
        if (_stopped)
        {
            return;
        }

        _queue.Enqueue(message);
        _queueSignal.Release();
    }

    public void Shutdown()
    {
        if (Interlocked.Exchange(ref _shutdownCalled, 1) != 0)
        {
            return;
        }

        _stopped = true;
        _queueSignal.Release();

        try
        {
            _session.CloseChannel(_options.GsUsbChannel);
        }
        catch
        {
        }

        SharedUsbSession.Release(_session, this, _options.GsUsbChannel);
    }

    public void Dispose()
    {
        Shutdown();
        _queueSignal.Dispose();
    }

    internal sealed class SharedUsbSession
    {
        private static readonly object RegistryLock = new();
        private static readonly Dictionary<string, WeakReference<SharedUsbSession>> Registry = [];

        private readonly object _writeLock = new();
        private readonly object _busLock = new();
        private readonly Dictionary<int, List<GsUsbBus>> _buses = [];
        private readonly Thread _readerThread;

        private int _refs;
        private volatile bool _running = true;

        private SharedUsbSession(BusOptions options)
        {
            Selector = string.IsNullOrWhiteSpace(options.Selector) ? "auto" : options.Selector;
            Context = InitializeContext();

            var wantedInterface = options.InterfaceNumber;
            var wantedSerial = options.SerialNumber;
            var (wantedBus, wantedAddress) = ParseSelector(Selector, ref wantedInterface, ref wantedSerial);

            var listCount = LibUsbNative.libusb_get_device_list(Context, out var listPtr);
            if (listCount < 0)
            {
                LibUsbNative.libusb_exit(Context);
                throw new IOException(FormatLibUsbError("libusb_get_device_list", (int)listCount));
            }

            try
            {
                for (nint index = 0; index < listCount; index++)
                {
                    var devicePtr = Marshal.ReadIntPtr(listPtr, checked((int)(index * IntPtr.Size)));
                    if (devicePtr == IntPtr.Zero)
                    {
                        continue;
                    }

                    if (LibUsbNative.libusb_get_device_descriptor(devicePtr, out var descriptor) != LibUsbNative.Success)
                    {
                        continue;
                    }

                    if (descriptor.IdVendor != options.VendorId || descriptor.IdProduct != options.ProductId)
                    {
                        continue;
                    }

                    BulkInterfaceInfo bulkInfo;
                    DeviceSummary summary;
                    try
                    {
                        bulkInfo = FindBulkInterface(devicePtr, wantedInterface);
                        summary = BuildSummary(devicePtr, descriptor, bulkInfo.InterfaceNumber);
                    }
                    catch
                    {
                        continue;
                    }

                    if (wantedBus is not null && summary.Bus != wantedBus.Value) continue;
                    if (wantedAddress is not null && summary.Address != wantedAddress.Value) continue;
                    if (!string.IsNullOrEmpty(wantedSerial) && !string.Equals(summary.SerialNumber, wantedSerial, StringComparison.Ordinal)) continue;

                    var openRc = LibUsbNative.libusb_open(devicePtr, out var handle);
                    if (openRc != LibUsbNative.Success || handle == IntPtr.Zero)
                    {
                        throw new IOException(FormatLibUsbError("libusb_open", openRc));
                    }

                    Handle = handle;
                    Summary = summary;
                    InterfaceNumber = bulkInfo.InterfaceNumber;
                    InEndpoint = bulkInfo.InEndpoint;
                    OutEndpoint = bulkInfo.OutEndpoint;
                    break;
                }
            }
            finally
            {
                LibUsbNative.libusb_free_device_list(listPtr, 1);
            }

            if (Handle == IntPtr.Zero)
            {
                LibUsbNative.libusb_exit(Context);
                throw new IOException("no GSUSB device found");
            }

            _ = LibUsbNative.libusb_set_configuration(Handle, 1);
            var claimRc = LibUsbNative.libusb_claim_interface(Handle, InterfaceNumber);
            if (claimRc != LibUsbNative.Success)
            {
                Cleanup();
                throw new IOException(FormatLibUsbError("libusb_claim_interface", claimRc));
            }

            SendHostFormat(Handle, InterfaceNumber);
            var deviceInfo = ReadDeviceInfo(Handle, InterfaceNumber);
            SwVersion = deviceInfo.SwVersion;
            HwVersion = deviceInfo.HwVersion;
            Channels = deviceInfo.Channels;
            RxFrameSize = Channels.Count == 0 ? 80 : Channels.Max(CalcRxSize);
            TxSizes = Channels.ToDictionary(static channel => channel.Index, CalcTxSize);

            _readerThread = new Thread(ReaderLoop)
            {
                IsBackground = true,
                Name = "GsUsbReader",
            };
            _readerThread.Start();
        }

        internal string Selector { get; }
        internal DeviceSummary Summary { get; } = new();
        internal IntPtr Context { get; }
        internal IntPtr Handle { get; private set; }
        internal int InterfaceNumber { get; }
        internal byte InEndpoint { get; }
        internal byte OutEndpoint { get; }
        internal uint SwVersion { get; }
        internal uint HwVersion { get; }
        internal IReadOnlyList<ChannelCapabilities> Channels { get; } = [];
        internal int RxFrameSize { get; }
        internal IReadOnlyDictionary<int, int> TxSizes { get; } = new Dictionary<int, int>();

        internal static SharedUsbSession Acquire(BusOptions options)
        {
            var key = BuildSessionKey(options);
            lock (RegistryLock)
            {
                if (Registry.TryGetValue(key, out var weakReference) && weakReference.TryGetTarget(out var existing))
                {
                    return existing;
                }

                var session = new SharedUsbSession(options);
                Registry[key] = new WeakReference<SharedUsbSession>(session);
                return session;
            }
        }

        internal static void Release(SharedUsbSession session, GsUsbBus bus, int logicalChannel)
        {
            var last = false;
            lock (RegistryLock)
            {
                last = session.RemoveBus(bus, logicalChannel);
                if (last)
                {
                    foreach (var key in Registry.Where(static item => !item.Value.TryGetTarget(out _)).Select(static item => item.Key).ToArray())
                    {
                        Registry.Remove(key);
                    }

                    foreach (var key in Registry.Where(item => item.Value.TryGetTarget(out var target) && ReferenceEquals(target, session)).Select(static item => item.Key).ToArray())
                    {
                        Registry.Remove(key);
                    }
                }
            }

            if (last)
            {
                session.Close();
            }
        }

        internal static IReadOnlyList<(IntPtr Device, DeviceSummary Summary)> EnumerateMatchingDevices(ushort vendorId, ushort productId)
        {
            var context = InitializeContext();
            try
            {
                var listCount = LibUsbNative.libusb_get_device_list(context, out var listPtr);
                if (listCount < 0)
                {
                    throw new IOException(FormatLibUsbError("libusb_get_device_list", (int)listCount));
                }

                try
                {
                    var results = new List<(IntPtr Device, DeviceSummary Summary)>();
                    for (nint index = 0; index < listCount; index++)
                    {
                        var devicePtr = Marshal.ReadIntPtr(listPtr, checked((int)(index * IntPtr.Size)));
                        if (devicePtr == IntPtr.Zero)
                        {
                            continue;
                        }

                        if (LibUsbNative.libusb_get_device_descriptor(devicePtr, out var descriptor) != LibUsbNative.Success)
                        {
                            continue;
                        }

                        if (descriptor.IdVendor != vendorId || descriptor.IdProduct != productId)
                        {
                            continue;
                        }

                        try
                        {
                            var bulk = FindBulkInterface(devicePtr, null);
                            results.Add((devicePtr, BuildSummary(devicePtr, descriptor, bulk.InterfaceNumber)));
                        }
                        catch
                        {
                        }
                    }

                    return results.OrderBy(static item => item.Summary.Bus).ThenBy(static item => item.Summary.Address).ThenBy(static item => item.Summary.InterfaceNumber).ToArray();
                }
                finally
                {
                    LibUsbNative.libusb_free_device_list(listPtr, 1);
                }
            }
            finally
            {
                LibUsbNative.libusb_exit(context);
            }
        }

        internal void AddBus(GsUsbBus bus, int logicalChannel)
        {
            lock (_busLock)
            {
                if (!_buses.TryGetValue(logicalChannel, out var listeners))
                {
                    listeners = [];
                    _buses[logicalChannel] = listeners;
                }

                listeners.Add(bus);
                _refs++;
            }
        }

        internal bool RemoveBus(GsUsbBus bus, int logicalChannel)
        {
            lock (_busLock)
            {
                if (_buses.TryGetValue(logicalChannel, out var listeners))
                {
                    listeners.Remove(bus);
                    if (listeners.Count == 0)
                    {
                        _buses.Remove(logicalChannel);
                    }
                }

                _refs = Math.Max(_refs - 1, 0);
                return _refs == 0;
            }
        }

        internal void Close()
        {
            _running = false;
            if (_readerThread.IsAlive)
            {
                _readerThread.Join(1000);
            }

            Cleanup();
        }

        internal void ConfigureChannel(int channelIndex, BusTiming nominal, BusTiming? data, bool fdEnabled, bool listenOnly, bool terminationEnabled, bool start)
        {
            CloseChannel(channelIndex);
            WriteBittiming(channelIndex, ReqBittiming, nominal);

            var cap = Channels[channelIndex];
            if (fdEnabled)
            {
                if (!cap.FdSupported || cap.DataLimits is null)
                {
                    throw new InvalidOperationException("channel does not support CAN FD");
                }

                if (data is null)
                {
                    throw new InvalidOperationException("FD enabled but data timing missing");
                }

                WriteBittiming(channelIndex, ReqDataBittiming, data.Value);
            }

            if (cap.TerminationSupported)
            {
                var state = BitConverter.GetBytes(terminationEnabled ? 120u : 0u);
                WriteControl(false, ReqSetTermination, channelIndex, state);
            }

            if (start)
            {
                uint flags = 0;
                if (listenOnly) flags |= GsCanModeListenOnly;
                if (fdEnabled) flags |= GsCanModeFd;
                if (cap.HardwareTimestamp) flags |= GsCanModeHwTimestamp;
                var payload = new byte[8];
                BitConverter.GetBytes(GsCanModeStart).CopyTo(payload, 0);
                BitConverter.GetBytes(flags).CopyTo(payload, 4);
                WriteControl(false, ReqMode, channelIndex, payload);
            }
        }

        internal void CloseChannel(int channelIndex)
        {
            var payload = new byte[8];
            BitConverter.GetBytes(GsCanModeReset).CopyTo(payload, 0);
            WriteControl(false, ReqMode, channelIndex, payload);
        }

        internal void SendFrame(int channelIndex, CanMessage message)
        {
            if (!TxSizes.TryGetValue(channelIndex, out var txSize))
            {
                throw new InvalidOperationException("invalid channel index");
            }

            var payload = new byte[txSize];
            BitConverter.GetBytes(0xFFFFFFFFu).CopyTo(payload, 0);

            var rawCanId = message.ArbitrationId;
            if (message.IsExtendedId) rawCanId |= CanEffFlag;
            if (message.IsRemoteFrame) rawCanId |= CanRtrFlag;
            BitConverter.GetBytes(rawCanId).CopyTo(payload, 4);

            var payloadLength = message.Data.Length;
            byte dlcCode;
            int copyLength;
            if (message.IsFd)
            {
                dlcCode = GsUsbUtilities.LengthToDlc(payloadLength);
                copyLength = GsUsbUtilities.DlcToLength(dlcCode);
            }
            else
            {
                copyLength = Math.Min(payloadLength, 8);
                dlcCode = (byte)copyLength;
            }

            payload[8] = dlcCode;
            payload[9] = (byte)channelIndex;
            byte flags = 0;
            if (message.IsFd) flags |= GsCanFlagFd;
            if (message.BitrateSwitch) flags |= GsCanFlagBrs;
            if (message.ErrorStateIndicator) flags |= GsCanFlagEsi;
            payload[10] = flags;

            if (!message.IsRemoteFrame && copyLength > 0)
            {
                Array.Copy(message.Data, 0, payload, 12, Math.Min(copyLength, payloadLength));
            }

            lock (_writeLock)
            {
                var rc = LibUsbNative.libusb_bulk_transfer(Handle, OutEndpoint, payload, payload.Length, out var transferred, ControlTimeoutMs);
                if (rc != LibUsbNative.Success || transferred <= 0)
                {
                    throw new IOException(FormatLibUsbError("libusb_bulk_transfer(write)", rc));
                }
            }
        }

        private void ReaderLoop()
        {
            while (_running)
            {
                var buffer = new byte[RxFrameSize];
                var rc = LibUsbNative.libusb_bulk_transfer(Handle, InEndpoint, buffer, buffer.Length, out var transferred, BulkTimeoutMs);
                if (rc < 0)
                {
                    if (rc == LibUsbNative.ErrorTimeout)
                    {
                        continue;
                    }

                    Thread.Sleep(50);
                    continue;
                }

                if (transferred <= 0)
                {
                    continue;
                }

                try
                {
                    var (message, logicalChannel) = ParseFrame(buffer, transferred);
                    List<GsUsbBus> listeners;
                    lock (_busLock)
                    {
                        listeners = _buses.TryGetValue(logicalChannel, out var current)
                            ? [.. current]
                            : [];
                    }

                    foreach (var listener in listeners)
                    {
                        listener.OnMessageReceived(message);
                    }
                }
                catch
                {
                }
            }
        }

        private static (CanMessage Message, int LogicalChannel) ParseFrame(byte[] raw, int size)
        {
            if (size < 20)
            {
                throw new InvalidOperationException("short frame");
            }

            var echoId = BitConverter.ToUInt32(raw, 0);
            var rawCanId = BitConverter.ToUInt32(raw, 4);
            var dlcCode = raw[8];
            var channelIndex = raw[9];
            var flags = raw[10];
            var isFd = (flags & GsCanFlagFd) != 0;
            var payloadLength = isFd ? GsUsbUtilities.DlcToLength(dlcCode) : Math.Min(dlcCode, (byte)8);
            var available = Math.Max(0, Math.Min(payloadLength, size - 12));
            var data = new byte[available];
            Array.Copy(raw, 12, data, 0, available);

            var message = new CanMessage
            {
                TimestampSeconds = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() / 1000.0,
                ArbitrationId = (rawCanId & CanEffFlag) != 0 ? rawCanId & CanEffMask : rawCanId & CanSffMask,
                IsExtendedId = (rawCanId & CanEffFlag) != 0,
                IsRemoteFrame = (rawCanId & CanRtrFlag) != 0,
                IsErrorFrame = (rawCanId & CanErrFlag) != 0,
                IsFd = isFd,
                BitrateSwitch = (flags & GsCanFlagBrs) != 0,
                ErrorStateIndicator = (flags & GsCanFlagEsi) != 0,
                IsRx = echoId == 0xFFFFFFFFu,
                Channel = channelIndex,
                Data = data,
            };

            return (message, channelIndex);
        }

        private void WriteBittiming(int channelIndex, byte request, BusTiming timing)
        {
            var payload = new byte[20];
            BitConverter.GetBytes(timing.PropSeg).CopyTo(payload, 0);
            BitConverter.GetBytes(timing.PhaseSeg1).CopyTo(payload, 4);
            BitConverter.GetBytes(timing.PhaseSeg2).CopyTo(payload, 8);
            BitConverter.GetBytes(timing.Sjw).CopyTo(payload, 12);
            BitConverter.GetBytes(timing.Brp).CopyTo(payload, 16);
            WriteControl(false, request, channelIndex, payload);
        }

        private byte[] WriteControl(bool directionIn, byte request, int value, byte[] payload)
        {
            lock (_writeLock)
            {
                var buffer = payload;
                var rc = LibUsbNative.libusb_control_transfer(
                    Handle,
                    LibUsbNative.BuildRequestType(directionIn),
                    request,
                    (ushort)value,
                    (ushort)InterfaceNumber,
                    buffer,
                    (ushort)buffer.Length,
                    ControlTimeoutMs);

                if (rc < 0)
                {
                    throw new IOException(FormatLibUsbError("libusb_control_transfer", rc));
                }

                return directionIn ? buffer[..rc] : [];
            }
        }

        private void Cleanup()
        {
            if (Handle != IntPtr.Zero)
            {
                try
                {
                    LibUsbNative.libusb_release_interface(Handle, InterfaceNumber);
                }
                catch
                {
                }

                LibUsbNative.libusb_close(Handle);
                Handle = IntPtr.Zero;
            }

            if (Context != IntPtr.Zero)
            {
                LibUsbNative.libusb_exit(Context);
            }
        }

        private static IntPtr InitializeContext()
        {
            var rc = LibUsbNative.libusb_init(out var context);
            if (rc != LibUsbNative.Success)
            {
                throw new IOException(FormatLibUsbError("libusb_init", rc));
            }

            return context;
        }

        private static string BuildSessionKey(BusOptions options)
        {
            return string.Join(':', options.VendorId, options.ProductId, options.Selector ?? "auto", options.InterfaceNumber?.ToString(CultureInfo.InvariantCulture) ?? string.Empty, options.SerialNumber ?? string.Empty);
        }

        private static string FormatLibUsbError(string what, int code) => $"{what} failed with libusb error {code}";

        private static (int? WantedBus, int? WantedAddress) ParseSelector(string selector, ref int? interfaceNumber, ref string? serialNumber)
        {
            if (string.IsNullOrWhiteSpace(selector) || string.Equals(selector, "auto", StringComparison.OrdinalIgnoreCase))
            {
                return (null, null);
            }

            var parts = selector.Split(':');
            if ((parts.Length == 2 || parts.Length == 3) && parts.All(static part => int.TryParse(part, NumberStyles.None, CultureInfo.InvariantCulture, out _)))
            {
                if (parts.Length == 3)
                {
                    interfaceNumber = int.Parse(parts[2], CultureInfo.InvariantCulture);
                }

                return (int.Parse(parts[0], CultureInfo.InvariantCulture), int.Parse(parts[1], CultureInfo.InvariantCulture));
            }

            serialNumber = selector;
            return (null, null);
        }

        private static string? ReadStringDescriptor(IntPtr device, byte descriptorIndex)
        {
            if (descriptorIndex == 0)
            {
                return null;
            }

            if (LibUsbNative.libusb_open(device, out var handle) != LibUsbNative.Success || handle == IntPtr.Zero)
            {
                return null;
            }

            try
            {
                var buffer = new byte[256];
                var length = LibUsbNative.libusb_get_string_descriptor_ascii(handle, descriptorIndex, buffer, buffer.Length);
                return length > 0 ? Encoding.UTF8.GetString(buffer, 0, length) : null;
            }
            finally
            {
                LibUsbNative.libusb_close(handle);
            }
        }

        private static BulkInterfaceInfo FindBulkInterface(IntPtr device, int? interfaceHint)
        {
            var rc = LibUsbNative.libusb_get_active_config_descriptor(device, out var configPtr);
            if (rc != LibUsbNative.Success || configPtr == IntPtr.Zero)
            {
                rc = LibUsbNative.libusb_get_config_descriptor(device, 0, out configPtr);
            }

            if (rc != LibUsbNative.Success || configPtr == IntPtr.Zero)
            {
                throw new IOException(FormatLibUsbError("libusb_get_config_descriptor", rc));
            }

            try
            {
                var config = Marshal.PtrToStructure<LibUsbNative.ConfigDescriptor>(configPtr);
                var interfaceSize = Marshal.SizeOf<LibUsbNative.Interface>();
                var altSize = Marshal.SizeOf<LibUsbNative.InterfaceDescriptor>();
                var endpointSize = Marshal.SizeOf<LibUsbNative.EndpointDescriptor>();

                for (var interfaceIndex = 0; interfaceIndex < config.BNumInterfaces; interfaceIndex++)
                {
                    var interfacePtr = IntPtr.Add(config.Interface, interfaceIndex * interfaceSize);
                    var iface = Marshal.PtrToStructure<LibUsbNative.Interface>(interfacePtr);
                    for (var altIndex = 0; altIndex < iface.NumAltsetting; altIndex++)
                    {
                        var altPtr = IntPtr.Add(iface.Altsetting, altIndex * altSize);
                        var alt = Marshal.PtrToStructure<LibUsbNative.InterfaceDescriptor>(altPtr);
                        if (interfaceHint is not null && alt.BInterfaceNumber != interfaceHint.Value)
                        {
                            continue;
                        }

                        byte? inEp = null;
                        byte? outEp = null;
                        for (var endpointIndex = 0; endpointIndex < alt.BNumEndpoints; endpointIndex++)
                        {
                            var endpointPtr = IntPtr.Add(alt.Endpoint, endpointIndex * endpointSize);
                            var endpoint = Marshal.PtrToStructure<LibUsbNative.EndpointDescriptor>(endpointPtr);
                            if (LibUsbNative.EndpointType(endpoint.BmAttributes) != LibUsbNative.TransferTypeBulk)
                            {
                                continue;
                            }

                            if (LibUsbNative.EndpointDirection(endpoint.BEndpointAddress) == LibUsbNative.EndpointIn)
                            {
                                inEp = endpoint.BEndpointAddress;
                            }
                            else
                            {
                                outEp = endpoint.BEndpointAddress;
                            }
                        }

                        if (inEp is not null && outEp is not null)
                        {
                            return new BulkInterfaceInfo(alt.BInterfaceNumber, inEp.Value, outEp.Value);
                        }
                    }
                }
            }
            finally
            {
                LibUsbNative.libusb_free_config_descriptor(configPtr);
            }

            throw new InvalidOperationException("bulk endpoints not found");
        }

        private static DeviceSummary BuildSummary(IntPtr device, LibUsbNative.DeviceDescriptor descriptor, int interfaceNumber)
        {
            var bus = LibUsbNative.libusb_get_bus_number(device);
            var address = LibUsbNative.libusb_get_device_address(device);
            var serial = ReadStringDescriptor(device, descriptor.ISerialNumber);
            var manufacturer = ReadStringDescriptor(device, descriptor.IManufacturer);
            var product = ReadStringDescriptor(device, descriptor.IProduct);

            return new DeviceSummary
            {
                Bus = bus,
                Address = address,
                VendorId = descriptor.IdVendor,
                ProductId = descriptor.IdProduct,
                InterfaceNumber = interfaceNumber,
                SerialNumber = serial,
                Manufacturer = manufacturer,
                Product = product,
                Key = $"{bus:000}:{address:000}:{descriptor.IdVendor:X4}:{descriptor.IdProduct:X4}:{interfaceNumber}",
                Label = $"USB {bus:000}:{address:000} [{descriptor.IdVendor:X4}:{descriptor.IdProduct:X4}] {(string.IsNullOrWhiteSpace(product) ? "GSUSB" : product)}",
            };
        }

        private static DeviceInfo ReadDeviceInfo(IntPtr handle, int interfaceNumber)
        {
            var config = new byte[12];
            var rc = LibUsbNative.libusb_control_transfer(handle, LibUsbNative.BuildRequestType(true), ReqDeviceConfig, 1, (ushort)interfaceNumber, config, (ushort)config.Length, ControlTimeoutMs);
            if (rc < 12)
            {
                throw new IOException("failed to read device config");
            }

            var channelCount = config[3] + 1;
            var swVersion = BitConverter.ToUInt32(config, 4);
            var hwVersion = BitConverter.ToUInt32(config, 8);
            var channels = Enumerable.Range(0, channelCount).Select(index => ReadChannelCapabilities(handle, interfaceNumber, index)).ToArray();
            return new DeviceInfo(swVersion, hwVersion, channels);
        }

        private static ChannelCapabilities ReadChannelCapabilities(IntPtr handle, int interfaceNumber, int channelIndex)
        {
            var extData = new byte[72];
            var rc = LibUsbNative.libusb_control_transfer(handle, LibUsbNative.BuildRequestType(true), ReqBtConstExt, (ushort)channelIndex, (ushort)interfaceNumber, extData, (ushort)extData.Length, ControlTimeoutMs);
            if (rc < 0)
            {
                extData = [];
            }
            else if (rc < extData.Length)
            {
                extData = extData[..rc];
            }

            uint featureFlags;
            TimingLimits nominalLimits;
            TimingLimits? dataLimits = null;

            if (extData.Length >= 72)
            {
                var words = GsUsbUtilities.BytesToUInt32List(extData[..72]);
                featureFlags = words[0];
                nominalLimits = new TimingLimits((int)words[1], (int)words[2], (int)words[3], (int)words[4], (int)words[5], (int)words[6], (int)words[7], (int)words[8], (int)words[9]);
                dataLimits = new TimingLimits((int)words[1], (int)words[10], (int)words[11], (int)words[12], (int)words[13], (int)words[14], (int)words[15], (int)words[16], (int)words[17]);
            }
            else
            {
                var stdData = new byte[40];
                rc = LibUsbNative.libusb_control_transfer(handle, LibUsbNative.BuildRequestType(true), ReqBtConst, (ushort)channelIndex, (ushort)interfaceNumber, stdData, (ushort)stdData.Length, ControlTimeoutMs);
                if (rc < 40)
                {
                    throw new IOException("failed to read timing constants");
                }

                var words = GsUsbUtilities.BytesToUInt32List(stdData[..40]);
                featureFlags = words[0];
                nominalLimits = new TimingLimits((int)words[1], (int)words[2], (int)words[3], (int)words[4], (int)words[5], (int)words[6], (int)words[7], (int)words[8], (int)words[9]);
            }

            var terminationEnabled = false;
            if ((featureFlags & GsCanFeatureTermination) != 0)
            {
                var termData = new byte[4];
                rc = LibUsbNative.libusb_control_transfer(handle, LibUsbNative.BuildRequestType(true), ReqGetTermination, (ushort)channelIndex, (ushort)interfaceNumber, termData, (ushort)termData.Length, ControlTimeoutMs);
                if (rc >= 4)
                {
                    terminationEnabled = BitConverter.ToUInt32(termData, 0) == 120u;
                }
            }

            return new ChannelCapabilities
            {
                Index = channelIndex,
                FeatureFlags = featureFlags,
                NominalLimits = nominalLimits,
                DataLimits = dataLimits,
                FdSupported = (featureFlags & GsCanFeatureFd) != 0,
                HardwareTimestamp = (featureFlags & GsCanFeatureHwTimestamp) != 0,
                TerminationSupported = (featureFlags & GsCanFeatureTermination) != 0,
                TerminationEnabled = terminationEnabled,
            };
        }

        private static void SendHostFormat(IntPtr handle, int interfaceNumber)
        {
            var payload = BitConverter.GetBytes(0x0000BEEFu);
            try
            {
                _ = LibUsbNative.libusb_control_transfer(handle, LibUsbNative.BuildRequestType(false), ReqHostFormat, 1, (ushort)interfaceNumber, payload, (ushort)payload.Length, ControlTimeoutMs);
            }
            catch
            {
            }
        }

        private static int CalcRxSize(ChannelCapabilities capabilities)
        {
            if (capabilities.FdSupported)
            {
                return capabilities.HardwareTimestamp ? 80 : 76;
            }

            return capabilities.HardwareTimestamp ? 24 : 20;
        }

        private static int CalcTxSize(ChannelCapabilities capabilities) => capabilities.FdSupported ? 76 : 20;

        private sealed record DeviceInfo(uint SwVersion, uint HwVersion, IReadOnlyList<ChannelCapabilities> Channels);
        private sealed record BulkInterfaceInfo(int InterfaceNumber, byte InEndpoint, byte OutEndpoint);
    }
}