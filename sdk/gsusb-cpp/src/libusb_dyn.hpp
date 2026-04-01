#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gsusb {
namespace detail {

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

struct libusb_device_descriptor {
    std::uint8_t bLength;
    std::uint8_t bDescriptorType;
    std::uint16_t bcdUSB;
    std::uint8_t bDeviceClass;
    std::uint8_t bDeviceSubClass;
    std::uint8_t bDeviceProtocol;
    std::uint8_t bMaxPacketSize0;
    std::uint16_t idVendor;
    std::uint16_t idProduct;
    std::uint16_t bcdDevice;
    std::uint8_t iManufacturer;
    std::uint8_t iProduct;
    std::uint8_t iSerialNumber;
    std::uint8_t bNumConfigurations;
};

struct libusb_endpoint_descriptor {
    std::uint8_t bLength;
    std::uint8_t bDescriptorType;
    std::uint8_t bEndpointAddress;
    std::uint8_t bmAttributes;
    std::uint16_t wMaxPacketSize;
    std::uint8_t bInterval;
    std::uint8_t bRefresh;
    std::uint8_t bSynchAddress;
    const unsigned char* extra;
    int extra_length;
};

struct libusb_interface_descriptor {
    std::uint8_t bLength;
    std::uint8_t bDescriptorType;
    std::uint8_t bInterfaceNumber;
    std::uint8_t bAlternateSetting;
    std::uint8_t bNumEndpoints;
    std::uint8_t bInterfaceClass;
    std::uint8_t bInterfaceSubClass;
    std::uint8_t bInterfaceProtocol;
    std::uint8_t iInterface;
    const libusb_endpoint_descriptor* endpoint;
    const unsigned char* extra;
    int extra_length;
};

struct libusb_interface {
    const libusb_interface_descriptor* altsetting;
    int num_altsetting;
};

struct libusb_config_descriptor {
    std::uint8_t bLength;
    std::uint8_t bDescriptorType;
    std::uint16_t wTotalLength;
    std::uint8_t bNumInterfaces;
    std::uint8_t bConfigurationValue;
    std::uint8_t iConfiguration;
    std::uint8_t bmAttributes;
    std::uint8_t MaxPower;
    const libusb_interface* interface;
    const unsigned char* extra;
    int extra_length;
};

class LibUsbApi {
public:
    static LibUsbApi& instance();

    void ensure_loaded();
    [[nodiscard]] std::string loaded_path() const;

    int init(libusb_context** context) const;
    void exit(libusb_context* context) const;
    std::intptr_t get_device_list(libusb_context* context, libusb_device*** list) const;
    void free_device_list(libusb_device** list, int unref_devices) const;
    std::uint8_t get_bus_number(libusb_device* device) const;
    std::uint8_t get_device_address(libusb_device* device) const;
    int get_device_descriptor(libusb_device* device, libusb_device_descriptor* descriptor) const;
    int open(libusb_device* device, libusb_device_handle** handle) const;
    void close(libusb_device_handle* handle) const;
    int get_string_descriptor_ascii(libusb_device_handle* handle, std::uint8_t descriptor_index, unsigned char* data, int length) const;
    int get_active_config_descriptor(libusb_device* device, libusb_config_descriptor** config) const;
    int get_config_descriptor(libusb_device* device, std::uint8_t config_index, libusb_config_descriptor** config) const;
    void free_config_descriptor(libusb_config_descriptor* config) const;
    int set_configuration(libusb_device_handle* handle, int configuration) const;
    int claim_interface(libusb_device_handle* handle, int interface_number) const;
    int release_interface(libusb_device_handle* handle, int interface_number) const;
    int control_transfer(libusb_device_handle* handle, std::uint8_t request_type, std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data, std::uint16_t length, unsigned int timeout_ms) const;
    int bulk_transfer(libusb_device_handle* handle, unsigned char endpoint, unsigned char* data, int length, int* transferred, unsigned int timeout_ms) const;

private:
    LibUsbApi() = default;
    void* module_ = nullptr;
    std::string loaded_path_;

    template <typename Fn>
    void load_symbol(Fn& out_fn, const char* name);

    using init_fn = int (*)(libusb_context**);
    using exit_fn = void (*)(libusb_context*);
    using get_device_list_fn = std::intptr_t (*)(libusb_context*, libusb_device***);
    using free_device_list_fn = void (*)(libusb_device**, int);
    using get_bus_number_fn = std::uint8_t (*)(libusb_device*);
    using get_device_address_fn = std::uint8_t (*)(libusb_device*);
    using get_device_descriptor_fn = int (*)(libusb_device*, libusb_device_descriptor*);
    using open_fn = int (*)(libusb_device*, libusb_device_handle**);
    using close_fn = void (*)(libusb_device_handle*);
    using get_string_descriptor_ascii_fn = int (*)(libusb_device_handle*, std::uint8_t, unsigned char*, int);
    using get_active_config_descriptor_fn = int (*)(libusb_device*, libusb_config_descriptor**);
    using get_config_descriptor_fn = int (*)(libusb_device*, std::uint8_t, libusb_config_descriptor**);
    using free_config_descriptor_fn = void (*)(libusb_config_descriptor*);
    using set_configuration_fn = int (*)(libusb_device_handle*, int);
    using claim_interface_fn = int (*)(libusb_device_handle*, int);
    using release_interface_fn = int (*)(libusb_device_handle*, int);
    using control_transfer_fn = int (*)(libusb_device_handle*, std::uint8_t, std::uint8_t, std::uint16_t, std::uint16_t, unsigned char*, std::uint16_t, unsigned int);
    using bulk_transfer_fn = int (*)(libusb_device_handle*, unsigned char, unsigned char*, int, int*, unsigned int);

    init_fn init_ = nullptr;
    exit_fn exit_ = nullptr;
    get_device_list_fn get_device_list_ = nullptr;
    free_device_list_fn free_device_list_ = nullptr;
    get_bus_number_fn get_bus_number_ = nullptr;
    get_device_address_fn get_device_address_ = nullptr;
    get_device_descriptor_fn get_device_descriptor_ = nullptr;
    open_fn open_ = nullptr;
    close_fn close_ = nullptr;
    get_string_descriptor_ascii_fn get_string_descriptor_ascii_ = nullptr;
    get_active_config_descriptor_fn get_active_config_descriptor_ = nullptr;
    get_config_descriptor_fn get_config_descriptor_ = nullptr;
    free_config_descriptor_fn free_config_descriptor_ = nullptr;
    set_configuration_fn set_configuration_ = nullptr;
    claim_interface_fn claim_interface_ = nullptr;
    release_interface_fn release_interface_ = nullptr;
    control_transfer_fn control_transfer_ = nullptr;
    bulk_transfer_fn bulk_transfer_ = nullptr;
};

constexpr std::uint8_t LIBUSB_ENDPOINT_IN = 0x80;
constexpr std::uint8_t LIBUSB_ENDPOINT_OUT = 0x00;
constexpr std::uint8_t LIBUSB_TRANSFER_TYPE_MASK = 0x03;
constexpr std::uint8_t LIBUSB_TRANSFER_TYPE_BULK = 0x02;
constexpr std::uint8_t LIBUSB_REQUEST_TYPE_VENDOR = 0x40;
constexpr std::uint8_t LIBUSB_RECIPIENT_INTERFACE = 0x01;

inline std::uint8_t endpoint_direction(std::uint8_t address) {
    return static_cast<std::uint8_t>(address & LIBUSB_ENDPOINT_IN);
}

inline std::uint8_t endpoint_type(std::uint8_t attributes) {
    return static_cast<std::uint8_t>(attributes & LIBUSB_TRANSFER_TYPE_MASK);
}

inline std::uint8_t build_request_type(bool direction_in) {
    return static_cast<std::uint8_t>((direction_in ? LIBUSB_ENDPOINT_IN : LIBUSB_ENDPOINT_OUT) | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE);
}

}  // namespace detail
}  // namespace gsusb