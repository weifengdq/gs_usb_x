from __future__ import annotations

import logging
import queue
import threading
import time
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

import can
import usb.core
import usb.util
from can import BusABC, Message
from can.util import dlc2len, len2dlc

try:
    import libusb_package
except Exception:  # pragma: no cover - optional import fallback
    libusb_package = None

try:
    import usb.backend.libusb1
except Exception:  # pragma: no cover - optional import fallback
    pass


LOGGER = logging.getLogger(__name__)

DEFAULT_VENDOR_ID = 0x1D50
DEFAULT_PRODUCT_ID = 0x606F

REQ_HOST_FORMAT = 0
REQ_BITTIMING = 1
REQ_MODE = 2
REQ_BT_CONST = 4
REQ_DEVICE_CONFIG = 5
REQ_DATA_BITTIMING = 10
REQ_BT_CONST_EXT = 11
REQ_SET_TERMINATION = 12
REQ_GET_TERMINATION = 13

GS_CAN_MODE_RESET = 0
GS_CAN_MODE_START = 1

GS_CAN_MODE_LISTEN_ONLY = 1 << 0
GS_CAN_MODE_HW_TIMESTAMP = 1 << 4
GS_CAN_MODE_FD = 1 << 8

GS_CAN_FEATURE_HW_TIMESTAMP = 1 << 4
GS_CAN_FEATURE_FD = 1 << 8
GS_CAN_FEATURE_TERMINATION = 1 << 11

GS_CAN_FLAG_OVERFLOW = 1 << 0
GS_CAN_FLAG_FD = 1 << 1
GS_CAN_FLAG_BRS = 1 << 2
GS_CAN_FLAG_ESI = 1 << 3

CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
CAN_EFF_MASK = 0x1FFFFFFF
CAN_SFF_MASK = 0x000007FF

CTRL_TIMEOUT_MS = 500
BULK_TIMEOUT_MS = 5

_SESSIONS: Dict[str, "SharedUsbSession"] = {}
_SESSIONS_LOCK = threading.Lock()


@dataclass(frozen=True)
class BusTiming:
    brp: int
    prop_seg: int
    phase_seg1: int
    phase_seg2: int
    sjw: int


@dataclass(frozen=True)
class TimingLimits:
    fclk_hz: int
    tseg1_min: int
    tseg1_max: int
    tseg2_min: int
    tseg2_max: int
    sjw_max: int
    brp_min: int
    brp_max: int
    brp_inc: int


@dataclass(frozen=True)
class DeviceSummary:
    key: str
    bus: int
    address: int
    vendor_id: int
    product_id: int
    interface_number: int
    serial_number: Optional[str]
    manufacturer: Optional[str]
    product: Optional[str]
    label: str


@dataclass(frozen=True)
class ChannelCapabilities:
    index: int
    feature_flags: int
    nominal_limits: TimingLimits
    data_limits: Optional[TimingLimits]
    fd_supported: bool
    hardware_timestamp: bool
    termination_supported: bool
    termination_enabled: bool


def _usb_backend():
    if libusb_package is not None and usb is not None:
        return usb.backend.libusb1.get_backend(find_library=lambda _name: libusb_package.get_library_path())
    return None


def _request_type(direction_in: bool) -> int:
    return usb.util.build_request_type(
        usb.util.CTRL_IN if direction_in else usb.util.CTRL_OUT,
        usb.util.CTRL_TYPE_VENDOR,
        usb.util.CTRL_RECIPIENT_INTERFACE,
    )


def _is_timeout(exc: Exception) -> bool:
    if isinstance(exc, usb.core.USBTimeoutError):
        return True
    if isinstance(exc, usb.core.USBError):
        text = str(exc).lower()
        return "timed out" in text or "timeout" in text
    return False


def _bytes_to_u32_list(data: bytes) -> List[int]:
    if len(data) % 4 != 0:
        raise ValueError("buffer length must be multiple of 4")
    return [int.from_bytes(data[index:index + 4], "little") for index in range(0, len(data), 4)]


def _calc_rx_size(cap: ChannelCapabilities) -> int:
    if cap.fd_supported:
        return 80 if cap.hardware_timestamp else 76
    return 24 if cap.hardware_timestamp else 20


def _calc_tx_size(cap: ChannelCapabilities) -> int:
    return 76 if cap.fd_supported else 20


def _enumerate_matching_devices(
    vendor_id: int = DEFAULT_VENDOR_ID,
    product_id: int = DEFAULT_PRODUCT_ID,
) -> List[Tuple[usb.core.Device, DeviceSummary]]:
    backend = _usb_backend()
    devices = usb.core.find(find_all=True, idVendor=vendor_id, idProduct=product_id, backend=backend)
    result: List[Tuple[usb.core.Device, DeviceSummary]] = []
    for device in devices or []:
        try:
            interface_number, _in_ep, _out_ep = _find_bulk_interface(device, None)
        except Exception:
            continue
        summary = _build_summary(device, interface_number)
        result.append((device, summary))
    result.sort(key=lambda item: (item[1].bus, item[1].address, item[1].interface_number))
    return result


def enumerate_devices(
    vendor_id: int = DEFAULT_VENDOR_ID,
    product_id: int = DEFAULT_PRODUCT_ID,
) -> List[DeviceSummary]:
    return [summary for _device, summary in _enumerate_matching_devices(vendor_id, product_id)]


def _parse_selector(selector: Optional[str]) -> Tuple[Optional[int], Optional[int], Optional[int], Optional[str]]:
    if selector is None or selector == "" or selector.lower() == "auto":
        return None, None, None, None

    parts = selector.split(":")
    if len(parts) in (2, 3) and all(part.isdigit() for part in parts):
        bus = int(parts[0])
        address = int(parts[1])
        interface_number = int(parts[2]) if len(parts) == 3 else None
        return bus, address, interface_number, None

    return None, None, None, selector


def _find_device_by_selector(
    selector: Optional[str],
    vendor_id: int,
    product_id: int,
    interface_number_hint: Optional[int],
    serial_number_hint: Optional[str],
) -> Tuple[usb.core.Device, DeviceSummary]:
    wanted_bus, wanted_address, wanted_interface, wanted_serial = _parse_selector(selector)
    if serial_number_hint:
        wanted_serial = serial_number_hint
    if interface_number_hint is not None:
        wanted_interface = interface_number_hint

    matches = _enumerate_matching_devices(vendor_id, product_id)
    if not matches:
        raise OSError("no GSUSB device found")

    for device, summary in matches:
        if wanted_bus is not None and summary.bus != wanted_bus:
            continue
        if wanted_address is not None and summary.address != wanted_address:
            continue
        if wanted_interface is not None and summary.interface_number != wanted_interface:
            continue
        if wanted_serial is not None and summary.serial_number != wanted_serial:
            continue
        return device, summary

    raise OSError(f"no device matches selector {selector!r}")


def _build_summary(device: usb.core.Device, interface_number: int) -> DeviceSummary:
    descriptor = device
    serial_number = None
    manufacturer = None
    product = None
    try:
        handle = device
        serial_number = usb.util.get_string(handle, handle.iSerialNumber) if handle.iSerialNumber else None
        manufacturer = usb.util.get_string(handle, handle.iManufacturer) if handle.iManufacturer else None
        product = usb.util.get_string(handle, handle.iProduct) if handle.iProduct else None
    except Exception:
        pass

    key = f"{device.bus:03}:{device.address:03}:{device.idVendor:04X}:{device.idProduct:04X}:{interface_number}"
    label = f"USB {device.bus:03}:{device.address:03} [{device.idVendor:04X}:{device.idProduct:04X}] {product or 'GSUSB'}"
    return DeviceSummary(
        key=key,
        bus=device.bus,
        address=device.address,
        vendor_id=device.idVendor,
        product_id=device.idProduct,
        interface_number=interface_number,
        serial_number=serial_number,
        manufacturer=manufacturer,
        product=product,
        label=label,
    )


def _find_bulk_interface(device: usb.core.Device, interface_number_hint: Optional[int]) -> Tuple[int, int, int]:
    try:
        config = device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
        config = device.get_active_configuration()

    for alt in config:
        if interface_number_hint is not None and alt.bInterfaceNumber != interface_number_hint:
            continue
        in_ep = None
        out_ep = None
        for endpoint in alt:
            if usb.util.endpoint_type(endpoint.bmAttributes) != usb.util.ENDPOINT_TYPE_BULK:
                continue
            if usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_IN:
                in_ep = endpoint.bEndpointAddress
            else:
                out_ep = endpoint.bEndpointAddress
        if in_ep is not None and out_ep is not None:
            return alt.bInterfaceNumber, in_ep, out_ep
    raise OSError("bulk endpoints not found")


def _read_channel_capabilities(device: usb.core.Device, interface_number: int, channel_index: int) -> ChannelCapabilities:
    request_type = _request_type(True)
    try:
        ext_data = bytes(
            device.ctrl_transfer(
                request_type,
                REQ_BT_CONST_EXT,
                channel_index,
                interface_number,
                72,
                timeout=CTRL_TIMEOUT_MS,
            )
        )
    except usb.core.USBError:
        ext_data = b""

    if len(ext_data) >= 72:
        words = _bytes_to_u32_list(ext_data[:72])
        feature_flags = words[0]
        nominal_limits = TimingLimits(words[1], words[2], words[3], words[4], words[5], words[6], words[7], words[8], words[9])
        data_limits = TimingLimits(words[1], words[10], words[11], words[12], words[13], words[14], words[15], words[16], words[17])
    else:
        std_data = bytes(
            device.ctrl_transfer(
                request_type,
                REQ_BT_CONST,
                channel_index,
                interface_number,
                40,
                timeout=CTRL_TIMEOUT_MS,
            )
        )
        if len(std_data) < 40:
            raise OSError(f"failed to read timing constants for channel {channel_index}")
        words = _bytes_to_u32_list(std_data[:40])
        feature_flags = words[0]
        nominal_limits = TimingLimits(words[1], words[2], words[3], words[4], words[5], words[6], words[7], words[8], words[9])
        data_limits = None

    termination_enabled = False
    if feature_flags & GS_CAN_FEATURE_TERMINATION:
        try:
            term_data = bytes(
                device.ctrl_transfer(
                    request_type,
                    REQ_GET_TERMINATION,
                    channel_index,
                    interface_number,
                    4,
                    timeout=CTRL_TIMEOUT_MS,
                )
            )
            termination_enabled = len(term_data) >= 4 and int.from_bytes(term_data[:4], "little") == 120
        except usb.core.USBError:
            termination_enabled = False

    return ChannelCapabilities(
        index=channel_index,
        feature_flags=feature_flags,
        nominal_limits=nominal_limits,
        data_limits=data_limits,
        fd_supported=bool(feature_flags & GS_CAN_FEATURE_FD),
        hardware_timestamp=bool(feature_flags & GS_CAN_FEATURE_HW_TIMESTAMP),
        termination_supported=bool(feature_flags & GS_CAN_FEATURE_TERMINATION),
        termination_enabled=termination_enabled,
    )


def _read_device_info(device: usb.core.Device, interface_number: int) -> Tuple[int, int, List[ChannelCapabilities]]:
    request_type = _request_type(True)
    config = bytes(
        device.ctrl_transfer(
            request_type,
            REQ_DEVICE_CONFIG,
            1,
            interface_number,
            12,
            timeout=CTRL_TIMEOUT_MS,
        )
    )
    if len(config) < 12:
        raise OSError("failed to read device config")
    channel_count = config[3] + 1
    sw_version = int.from_bytes(config[4:8], "little")
    hw_version = int.from_bytes(config[8:12], "little")
    channels = [_read_channel_capabilities(device, interface_number, index) for index in range(channel_count)]
    return sw_version, hw_version, channels


def _send_host_format(device: usb.core.Device, interface_number: int) -> None:
    payload = (0x0000BEEF).to_bytes(4, "little")
    try:
        device.ctrl_transfer(
            _request_type(False),
            REQ_HOST_FORMAT,
            1,
            interface_number,
            payload,
            timeout=CTRL_TIMEOUT_MS,
        )
    except usb.core.USBError:
        LOGGER.debug("host format request ignored", exc_info=True)


def _build_timing_from_dict(value: object) -> Optional[BusTiming]:
    if value is None:
        return None
    if isinstance(value, BusTiming):
        return value
    if isinstance(value, dict):
        return BusTiming(
            brp=int(value["brp"]),
            prop_seg=int(value.get("prop_seg", 0)),
            phase_seg1=int(value["phase_seg1"]),
            phase_seg2=int(value["phase_seg2"]),
            sjw=int(value["sjw"]),
        )
    raise TypeError("timing must be a BusTiming or dict")


def _calculate_timing(limits: TimingLimits, bitrate: int, sample_point: float) -> BusTiming:
    if bitrate <= 0:
        raise ValueError("bitrate must be > 0")

    best: Optional[Tuple[float, int, int, int, int]] = None
    step = max(limits.brp_inc, 1)
    for brp in range(limits.brp_min, limits.brp_max + 1, step):
        numerator = limits.fclk_hz
        denominator = brp * bitrate
        if denominator <= 0:
            continue
        if numerator % denominator != 0:
            continue
        total_tq = numerator // denominator
        if total_tq < 4:
            continue
        tseg_total = total_tq - 1
        tseg1_target = int(round((sample_point / 100.0) * total_tq - 1))
        for tseg1 in (tseg1_target, tseg1_target - 1, tseg1_target + 1):
            tseg2 = tseg_total - tseg1
            if tseg1 < limits.tseg1_min or tseg1 > limits.tseg1_max:
                continue
            if tseg2 < limits.tseg2_min or tseg2 > limits.tseg2_max:
                continue
            actual_sp = ((1 + tseg1) / total_tq) * 100.0
            sp_error = abs(actual_sp - sample_point)
            score = (sp_error, abs(brp), tseg2, tseg1, total_tq)
            if best is None or score < best:
                best = score
                best_timing = BusTiming(brp=brp, prop_seg=0, phase_seg1=tseg1, phase_seg2=tseg2, sjw=min(tseg2, limits.sjw_max))
    if best is None:
        raise ValueError(f"cannot derive timing for bitrate {bitrate} from fclk {limits.fclk_hz}")
    return best_timing


class SharedUsbSession:
    def __init__(self, selector: Optional[str], vendor_id: int, product_id: int, interface_number: Optional[int], serial_number: Optional[str]) -> None:
        self.device, self.summary = _find_device_by_selector(selector, vendor_id, product_id, interface_number, serial_number)
        self.interface_number, self.in_ep, self.out_ep = _find_bulk_interface(self.device, interface_number)
        self.key = self.summary.key
        self.write_lock = threading.Lock()
        self.bus_lock = threading.Lock()
        self.buses: Dict[int, List[GsUsbBus]] = {}
        self.refs = 0
        self.running = True

        try:
            self.device.set_configuration()
        except usb.core.USBError:
            pass
        try:
            if self.device.is_kernel_driver_active(self.interface_number):
                self.device.detach_kernel_driver(self.interface_number)
        except (NotImplementedError, usb.core.USBError):
            pass
        usb.util.claim_interface(self.device, self.interface_number)

        _send_host_format(self.device, self.interface_number)
        self.sw_version, self.hw_version, self.channels = _read_device_info(self.device, self.interface_number)
        self.rx_frame_size = max((_calc_rx_size(channel) for channel in self.channels), default=80)
        self.tx_sizes = {channel.index: _calc_tx_size(channel) for channel in self.channels}
        self.reader = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader.start()

    def add_bus(self, bus: "GsUsbBus", logical_channel: int) -> None:
        with self.bus_lock:
            self.buses.setdefault(logical_channel, []).append(bus)
            self.refs += 1

    def remove_bus(self, bus: "GsUsbBus", logical_channel: int) -> bool:
        with self.bus_lock:
            listeners = self.buses.get(logical_channel, [])
            if bus in listeners:
                listeners.remove(bus)
            if not listeners and logical_channel in self.buses:
                self.buses.pop(logical_channel, None)
            self.refs = max(self.refs - 1, 0)
            return self.refs == 0

    def close(self) -> None:
        self.running = False
        if self.reader.is_alive():
            self.reader.join(timeout=1.0)
        try:
            usb.util.release_interface(self.device, self.interface_number)
        except usb.core.USBError:
            pass
        usb.util.dispose_resources(self.device)

    def configure_channel(
        self,
        channel_index: int,
        nominal: BusTiming,
        data: Optional[BusTiming],
        *,
        fd_enabled: bool,
        listen_only: bool,
        termination_enabled: bool,
        start: bool,
    ) -> None:
        self.close_channel(channel_index)
        self._write_bittiming(channel_index, REQ_BITTIMING, nominal)

        cap = self.channels[channel_index]
        if fd_enabled:
            if not cap.fd_supported or cap.data_limits is None:
                raise ValueError(f"channel {channel_index} does not support CAN FD")
            if data is None:
                raise ValueError("FD enabled but data timing missing")
            self._write_bittiming(channel_index, REQ_DATA_BITTIMING, data)

        if cap.termination_supported:
            state = (120 if termination_enabled else 0).to_bytes(4, "little")
            self._write_control(False, REQ_SET_TERMINATION, channel_index, state)

        if start:
            flags = 0
            if listen_only:
                flags |= GS_CAN_MODE_LISTEN_ONLY
            if fd_enabled:
                flags |= GS_CAN_MODE_FD
            if cap.hardware_timestamp:
                flags |= GS_CAN_MODE_HW_TIMESTAMP
            payload = GS_CAN_MODE_START.to_bytes(4, "little") + flags.to_bytes(4, "little")
            self._write_control(False, REQ_MODE, channel_index, payload)

    def close_channel(self, channel_index: int) -> None:
        payload = GS_CAN_MODE_RESET.to_bytes(4, "little") + (0).to_bytes(4, "little")
        self._write_control(False, REQ_MODE, channel_index, payload)

    def send_frame(self, channel_index: int, msg: Message) -> None:
        tx_size = self.tx_sizes.get(channel_index)
        if tx_size is None:
            raise ValueError(f"invalid channel {channel_index}")

        payload = bytearray(tx_size)
        echo_id = 0xFFFFFFFF
        payload[0:4] = echo_id.to_bytes(4, "little")

        raw_can_id = int(msg.arbitration_id)
        if msg.is_extended_id:
            raw_can_id |= CAN_EFF_FLAG
        if msg.is_remote_frame:
            raw_can_id |= CAN_RTR_FLAG
        payload[4:8] = raw_can_id.to_bytes(4, "little")

        payload_len = len(msg.data)
        if msg.is_fd:
            dlc_code = len2dlc(payload_len)
            copy_len = dlc2len(dlc_code)
        else:
            copy_len = min(payload_len, 8)
            dlc_code = copy_len
        payload[8] = dlc_code
        payload[9] = channel_index

        flags = 0
        if msg.is_fd:
            flags |= GS_CAN_FLAG_FD
        if getattr(msg, "bitrate_switch", False):
            flags |= GS_CAN_FLAG_BRS
        if getattr(msg, "error_state_indicator", False):
            flags |= GS_CAN_FLAG_ESI
        payload[10] = flags

        if not msg.is_remote_frame:
            payload[12:12 + copy_len] = bytes(msg.data[:copy_len])

        with self.write_lock:
            written = self.device.write(self.out_ep, payload, timeout=CTRL_TIMEOUT_MS)
        if written <= 0:
            raise OSError("bulk write failed")

    def _write_bittiming(self, channel_index: int, request: int, timing: BusTiming) -> None:
        payload = bytearray(20)
        payload[0:4] = int(timing.prop_seg).to_bytes(4, "little")
        payload[4:8] = int(timing.phase_seg1).to_bytes(4, "little")
        payload[8:12] = int(timing.phase_seg2).to_bytes(4, "little")
        payload[12:16] = int(timing.sjw).to_bytes(4, "little")
        payload[16:20] = int(timing.brp).to_bytes(4, "little")
        self._write_control(False, request, channel_index, payload)

    def _write_control(self, direction_in: bool, request: int, value: int, payload: bytes | int) -> bytes:
        data = payload if isinstance(payload, (bytes, bytearray)) else bytes(payload)
        request_type = _request_type(direction_in)
        with self.write_lock:
            if direction_in:
                return bytes(
                    self.device.ctrl_transfer(
                        request_type,
                        request,
                        value,
                        self.interface_number,
                        len(data),
                        timeout=CTRL_TIMEOUT_MS,
                    )
                )
            self.device.ctrl_transfer(
                request_type,
                request,
                value,
                self.interface_number,
                data,
                timeout=CTRL_TIMEOUT_MS,
            )
        return b""

    def _reader_loop(self) -> None:
        while self.running:
            try:
                raw = bytes(self.device.read(self.in_ep, self.rx_frame_size, timeout=BULK_TIMEOUT_MS))
            except Exception as exc:
                if _is_timeout(exc):
                    continue
                LOGGER.error("USB read failed: %s", exc)
                time.sleep(0.05)
                continue

            if not raw:
                continue

            try:
                message, logical_channel = self._parse_frame(raw)
            except Exception as exc:
                LOGGER.warning("failed to parse frame: %s", exc)
                continue

            with self.bus_lock:
                listeners = list(self.buses.get(logical_channel, []))

            for bus in listeners:
                bus._on_message_received(message)

    def _parse_frame(self, raw: bytes) -> Tuple[Message, int]:
        if len(raw) < 20:
            raise ValueError(f"short frame {len(raw)}")
        echo_id = int.from_bytes(raw[0:4], "little")
        raw_can_id = int.from_bytes(raw[4:8], "little")
        dlc_code = raw[8]
        channel_index = raw[9]
        flags = raw[10]
        is_fd = bool(flags & GS_CAN_FLAG_FD)
        payload_len = dlc2len(dlc_code) if is_fd else min(dlc_code, 8)
        payload = bytes(raw[12:12 + payload_len])
        arbitration_id = (raw_can_id & CAN_EFF_MASK) if (raw_can_id & CAN_EFF_FLAG) else (raw_can_id & CAN_SFF_MASK)

        message = Message(
            timestamp=time.time(),
            arbitration_id=arbitration_id,
            is_extended_id=bool(raw_can_id & CAN_EFF_FLAG),
            is_remote_frame=bool(raw_can_id & CAN_RTR_FLAG),
            is_error_frame=bool(raw_can_id & CAN_ERR_FLAG),
            is_fd=is_fd,
            bitrate_switch=bool(flags & GS_CAN_FLAG_BRS),
            error_state_indicator=bool(flags & GS_CAN_FLAG_ESI),
            dlc=payload_len,
            data=payload,
            is_rx=(echo_id == 0xFFFFFFFF),
            channel=channel_index,
        )
        return message, channel_index


class GsUsbBus(BusABC):
    def __init__(
        self,
        channel: Optional[str] = "auto",
        *,
        gsusb_channel: int = 0,
        bitrate: Optional[int] = None,
        fd: bool = False,
        data_bitrate: Optional[int] = None,
        nominal_timing: Optional[object] = None,
        data_timing: Optional[object] = None,
        listen_only: bool = False,
        termination_enabled: bool = True,
        vendor_id: int = DEFAULT_VENDOR_ID,
        product_id: int = DEFAULT_PRODUCT_ID,
        interface_number: Optional[int] = None,
        serial_number: Optional[str] = None,
        **kwargs,
    ) -> None:
        self.selector = channel or "auto"
        self.gsusb_channel = int(gsusb_channel)
        self.queue: "queue.Queue[Message]" = queue.Queue()

        with _SESSIONS_LOCK:
            session = _SESSIONS.get(f"{vendor_id:04X}:{product_id:04X}:{self.selector}:{interface_number}:{serial_number}")
            if session is None:
                session = SharedUsbSession(self.selector, vendor_id, product_id, interface_number, serial_number)
                _SESSIONS[f"{vendor_id:04X}:{product_id:04X}:{self.selector}:{interface_number}:{serial_number}"] = session
            self._session_key = f"{vendor_id:04X}:{product_id:04X}:{self.selector}:{interface_number}:{serial_number}"
            self.session = session
            self.session.add_bus(self, self.gsusb_channel)

        super().__init__(channel=self.selector, bitrate=bitrate, fd=fd, **kwargs)

        try:
            capabilities = self.session.channels[self.gsusb_channel]
            nominal = _build_timing_from_dict(nominal_timing)
            if nominal is None:
                if bitrate is None:
                    raise ValueError("bitrate or nominal_timing is required")
                nominal = _calculate_timing(capabilities.nominal_limits, int(bitrate), 80.0)

            data = _build_timing_from_dict(data_timing)
            if fd:
                if data is None:
                    if data_bitrate is None:
                        raise ValueError("data_bitrate or data_timing is required when fd=True")
                    if capabilities.data_limits is None:
                        raise ValueError(f"channel {self.gsusb_channel} does not support CAN FD")
                    data = _calculate_timing(capabilities.data_limits, int(data_bitrate), 80.0)

            self.session.configure_channel(
                self.gsusb_channel,
                nominal,
                data,
                fd_enabled=fd,
                listen_only=listen_only,
                termination_enabled=termination_enabled,
                start=True,
            )
        except Exception:
            self.shutdown()
            raise

    @property
    def channel_info(self) -> str:
        return f"{self.selector}/gsusb_channel={self.gsusb_channel}"

    def send(self, msg: Message, timeout: Optional[float] = None) -> None:
        del timeout
        self.session.send_frame(self.gsusb_channel, msg)

    def _recv_internal(self, timeout: Optional[float]) -> Tuple[Optional[Message], bool]:
        try:
            message = self.queue.get(timeout=timeout)
            return message, False
        except queue.Empty:
            return None, False

    def _on_message_received(self, message: Message) -> None:
        self.queue.put(message)

    def shutdown(self) -> None:
        session = getattr(self, "session", None)
        if session is not None:
            try:
                session.close_channel(self.gsusb_channel)
            except Exception:
                pass
            with _SESSIONS_LOCK:
                if session.remove_bus(self, self.gsusb_channel):
                    session.close()
                    _SESSIONS.pop(self._session_key, None)
        super().shutdown()
