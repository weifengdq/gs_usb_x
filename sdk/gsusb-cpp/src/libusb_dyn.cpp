#include "libusb_dyn.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace gsusb {
namespace detail {

namespace {

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.empty() ? nullptr : &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring executable_dir() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        return {};
    }
    std::wstring full_path(buffer.data(), length);
    const auto pos = full_path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return {};
    }
    return full_path.substr(0, pos);
}

}  // namespace

LibUsbApi& LibUsbApi::instance() {
    static LibUsbApi api;
    return api;
}

void LibUsbApi::ensure_loaded() {
    if (module_ != nullptr) {
        return;
    }

    std::vector<std::wstring> candidates;
    if (const char* env_path = std::getenv("GSUSB_LIBUSB_DLL")) {
        int wide_size = MultiByteToWideChar(CP_UTF8, 0, env_path, -1, nullptr, 0);
        std::wstring wide_path(static_cast<std::size_t>(std::max(wide_size, 1)), L'\0');
        if (wide_size > 1) {
            MultiByteToWideChar(CP_UTF8, 0, env_path, -1, wide_path.empty() ? nullptr : &wide_path[0], wide_size);
            if (!wide_path.empty() && wide_path.back() == L'\0') {
                wide_path.pop_back();
            }
        }
        candidates.push_back(wide_path);
    }
    const auto exe_dir = executable_dir();
    if (!exe_dir.empty()) {
        candidates.push_back(exe_dir + L"\\libusb-1.0.dll");
    }
    candidates.emplace_back(L"libusb-1.0.dll");
    candidates.emplace_back(L"D:/n32/.venv/Lib/site-packages/libusb_package/libusb-1.0.dll");
    candidates.emplace_back(L"d:/n32/.venv/Lib/site-packages/libusb_package/libusb-1.0.dll");

    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        HMODULE module = LoadLibraryW(candidate.c_str());
        if (module == nullptr) {
            continue;
        }
        module_ = module;
        loaded_path_ = utf8_from_wide(candidate);
        break;
    }

    if (module_ == nullptr) {
        throw std::runtime_error("failed to load libusb-1.0.dll; set GSUSB_LIBUSB_DLL or place libusb-1.0.dll next to the executable");
    }

    load_symbol(init_, "libusb_init");
    load_symbol(exit_, "libusb_exit");
    load_symbol(get_device_list_, "libusb_get_device_list");
    load_symbol(free_device_list_, "libusb_free_device_list");
    load_symbol(get_bus_number_, "libusb_get_bus_number");
    load_symbol(get_device_address_, "libusb_get_device_address");
    load_symbol(get_device_descriptor_, "libusb_get_device_descriptor");
    load_symbol(open_, "libusb_open");
    load_symbol(close_, "libusb_close");
    load_symbol(get_string_descriptor_ascii_, "libusb_get_string_descriptor_ascii");
    load_symbol(get_active_config_descriptor_, "libusb_get_active_config_descriptor");
    load_symbol(get_config_descriptor_, "libusb_get_config_descriptor");
    load_symbol(free_config_descriptor_, "libusb_free_config_descriptor");
    load_symbol(set_configuration_, "libusb_set_configuration");
    load_symbol(claim_interface_, "libusb_claim_interface");
    load_symbol(release_interface_, "libusb_release_interface");
    load_symbol(control_transfer_, "libusb_control_transfer");
    load_symbol(bulk_transfer_, "libusb_bulk_transfer");
}

std::string LibUsbApi::loaded_path() const {
    return loaded_path_;
}

int LibUsbApi::init(libusb_context** context) const {
    return init_(context);
}

void LibUsbApi::exit(libusb_context* context) const {
    exit_(context);
}

std::intptr_t LibUsbApi::get_device_list(libusb_context* context, libusb_device*** list) const {
    return get_device_list_(context, list);
}

void LibUsbApi::free_device_list(libusb_device** list, int unref_devices) const {
    free_device_list_(list, unref_devices);
}

std::uint8_t LibUsbApi::get_bus_number(libusb_device* device) const {
    return get_bus_number_(device);
}

std::uint8_t LibUsbApi::get_device_address(libusb_device* device) const {
    return get_device_address_(device);
}

int LibUsbApi::get_device_descriptor(libusb_device* device, libusb_device_descriptor* descriptor) const {
    return get_device_descriptor_(device, descriptor);
}

int LibUsbApi::open(libusb_device* device, libusb_device_handle** handle) const {
    return open_(device, handle);
}

void LibUsbApi::close(libusb_device_handle* handle) const {
    close_(handle);
}

int LibUsbApi::get_string_descriptor_ascii(libusb_device_handle* handle, std::uint8_t descriptor_index, unsigned char* data, int length) const {
    return get_string_descriptor_ascii_(handle, descriptor_index, data, length);
}

int LibUsbApi::get_active_config_descriptor(libusb_device* device, libusb_config_descriptor** config) const {
    return get_active_config_descriptor_(device, config);
}

int LibUsbApi::get_config_descriptor(libusb_device* device, std::uint8_t config_index, libusb_config_descriptor** config) const {
    return get_config_descriptor_(device, config_index, config);
}

void LibUsbApi::free_config_descriptor(libusb_config_descriptor* config) const {
    free_config_descriptor_(config);
}

int LibUsbApi::set_configuration(libusb_device_handle* handle, int configuration) const {
    return set_configuration_(handle, configuration);
}

int LibUsbApi::claim_interface(libusb_device_handle* handle, int interface_number) const {
    return claim_interface_(handle, interface_number);
}

int LibUsbApi::release_interface(libusb_device_handle* handle, int interface_number) const {
    return release_interface_(handle, interface_number);
}

int LibUsbApi::control_transfer(libusb_device_handle* handle, std::uint8_t request_type, std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data, std::uint16_t length, unsigned int timeout_ms) const {
    return control_transfer_(handle, request_type, request, value, index, data, length, timeout_ms);
}

int LibUsbApi::bulk_transfer(libusb_device_handle* handle, unsigned char endpoint, unsigned char* data, int length, int* transferred, unsigned int timeout_ms) const {
    return bulk_transfer_(handle, endpoint, data, length, transferred, timeout_ms);
}

template <typename Fn>
void LibUsbApi::load_symbol(Fn& out_fn, const char* name) {
    out_fn = reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(module_), name));
    if (out_fn == nullptr) {
        throw std::runtime_error(std::string("failed to load symbol from libusb: ") + name);
    }
}

}  // namespace detail
}  // namespace gsusb