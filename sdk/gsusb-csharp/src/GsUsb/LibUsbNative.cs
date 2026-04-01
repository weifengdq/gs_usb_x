using System.Runtime.InteropServices;

namespace GsUsb;

internal static class LibUsbNative
{
    internal const int Success = 0;
    internal const int ErrorTimeout = -7;

    internal const byte EndpointIn = 0x80;
    internal const byte EndpointOut = 0x00;
    internal const byte TransferTypeMask = 0x03;
    internal const byte TransferTypeBulk = 0x02;
    internal const byte RequestTypeVendor = 0x40;
    internal const byte RecipientInterface = 0x01;

    [StructLayout(LayoutKind.Sequential)]
    internal struct DeviceDescriptor
    {
        public byte BLength;
        public byte BDescriptorType;
        public ushort BcdUsb;
        public byte BDeviceClass;
        public byte BDeviceSubClass;
        public byte BDeviceProtocol;
        public byte BMaxPacketSize0;
        public ushort IdVendor;
        public ushort IdProduct;
        public ushort BcdDevice;
        public byte IManufacturer;
        public byte IProduct;
        public byte ISerialNumber;
        public byte BNumConfigurations;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct EndpointDescriptor
    {
        public byte BLength;
        public byte BDescriptorType;
        public byte BEndpointAddress;
        public byte BmAttributes;
        public ushort WMaxPacketSize;
        public byte BInterval;
        public byte BRefresh;
        public byte BSynchAddress;
        public IntPtr Extra;
        public int ExtraLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct InterfaceDescriptor
    {
        public byte BLength;
        public byte BDescriptorType;
        public byte BInterfaceNumber;
        public byte BAlternateSetting;
        public byte BNumEndpoints;
        public byte BInterfaceClass;
        public byte BInterfaceSubClass;
        public byte BInterfaceProtocol;
        public byte IInterface;
        public IntPtr Endpoint;
        public IntPtr Extra;
        public int ExtraLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Interface
    {
        public IntPtr Altsetting;
        public int NumAltsetting;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ConfigDescriptor
    {
        public byte BLength;
        public byte BDescriptorType;
        public ushort WTotalLength;
        public byte BNumInterfaces;
        public byte BConfigurationValue;
        public byte IConfiguration;
        public byte BmAttributes;
        public byte MaxPower;
        public IntPtr Interface;
        public IntPtr Extra;
        public int ExtraLength;
    }

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_init(out IntPtr context);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void libusb_exit(IntPtr context);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint libusb_get_device_list(IntPtr context, out IntPtr list);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void libusb_free_device_list(IntPtr list, int unrefDevices);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte libusb_get_bus_number(IntPtr device);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte libusb_get_device_address(IntPtr device);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_get_device_descriptor(IntPtr device, out DeviceDescriptor descriptor);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_open(IntPtr device, out IntPtr handle);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void libusb_close(IntPtr handle);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_get_string_descriptor_ascii(IntPtr handle, byte descriptorIndex, [Out] byte[] data, int length);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_get_active_config_descriptor(IntPtr device, out IntPtr config);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_get_config_descriptor(IntPtr device, byte configIndex, out IntPtr config);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void libusb_free_config_descriptor(IntPtr config);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_set_configuration(IntPtr handle, int configuration);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_claim_interface(IntPtr handle, int interfaceNumber);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_release_interface(IntPtr handle, int interfaceNumber);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_control_transfer(
        IntPtr handle,
        byte requestType,
        byte request,
        ushort value,
        ushort index,
        [In, Out] byte[] data,
        ushort length,
        uint timeoutMs);

    [DllImport("libusb-1.0.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int libusb_bulk_transfer(
        IntPtr handle,
        byte endpoint,
        [In, Out] byte[] data,
        int length,
        out int transferred,
        uint timeoutMs);

    internal static byte EndpointDirection(byte address) => (byte)(address & EndpointIn);
    internal static byte EndpointType(byte attributes) => (byte)(attributes & TransferTypeMask);
    internal static byte BuildRequestType(bool directionIn) => (byte)((directionIn ? EndpointIn : EndpointOut) | RequestTypeVendor | RecipientInterface);
}