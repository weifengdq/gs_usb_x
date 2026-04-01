use once_cell::sync::Lazy;
use rusb::{Context, Device, DeviceDescriptor, DeviceHandle, Direction, Recipient, RequestType, UsbContext};
use std::collections::{HashMap, VecDeque};
use std::error::Error as StdError;
use std::fmt::{Display, Formatter};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, RwLock, Weak};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

pub const DEFAULT_VENDOR_ID: u16 = 0x1D50;
pub const DEFAULT_PRODUCT_ID: u16 = 0x606F;

const REQ_HOST_FORMAT: u8 = 0;
const REQ_BITTIMING: u8 = 1;
const REQ_MODE: u8 = 2;
const REQ_BT_CONST: u8 = 4;
const REQ_DEVICE_CONFIG: u8 = 5;
const REQ_DATA_BITTIMING: u8 = 10;
const REQ_BT_CONST_EXT: u8 = 11;
const REQ_SET_TERMINATION: u8 = 12;
const REQ_GET_TERMINATION: u8 = 13;

const GS_CAN_MODE_RESET: u32 = 0;
const GS_CAN_MODE_START: u32 = 1;
const GS_CAN_MODE_LISTEN_ONLY: u32 = 1u32 << 0;
const GS_CAN_MODE_HW_TIMESTAMP: u32 = 1u32 << 4;
const GS_CAN_MODE_FD: u32 = 1u32 << 8;

const GS_CAN_FEATURE_HW_TIMESTAMP: u32 = 1u32 << 4;
const GS_CAN_FEATURE_FD: u32 = 1u32 << 8;
const GS_CAN_FEATURE_TERMINATION: u32 = 1u32 << 11;

const GS_CAN_FLAG_FD: u8 = 1u8 << 1;
const GS_CAN_FLAG_BRS: u8 = 1u8 << 2;
const GS_CAN_FLAG_ESI: u8 = 1u8 << 3;

const CAN_EFF_FLAG: u32 = 0x8000_0000;
const CAN_RTR_FLAG: u32 = 0x4000_0000;
const CAN_ERR_FLAG: u32 = 0x2000_0000;
const CAN_EFF_MASK: u32 = 0x1FFF_FFFF;
const CAN_SFF_MASK: u32 = 0x0000_07FF;

const CTRL_TIMEOUT: Duration = Duration::from_millis(500);
const BULK_TIMEOUT: Duration = Duration::from_millis(5);

static SESSION_REGISTRY: Lazy<Mutex<HashMap<String, Weak<SharedUsbSession>>>> =
    Lazy::new(|| Mutex::new(HashMap::new()));
static NEXT_BUS_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Clone)]
pub struct GsUsbError(pub String);

impl Display for GsUsbError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl StdError for GsUsbError {}

impl From<rusb::Error> for GsUsbError {
    fn from(value: rusb::Error) -> Self {
        Self(value.to_string())
    }
}

impl From<std::num::ParseIntError> for GsUsbError {
    fn from(value: std::num::ParseIntError) -> Self {
        Self(value.to_string())
    }
}

pub type Result<T> = std::result::Result<T, GsUsbError>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BusTiming {
    pub brp: i32,
    pub prop_seg: i32,
    pub phase_seg1: i32,
    pub phase_seg2: i32,
    pub sjw: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TimingLimits {
    pub fclk_hz: i32,
    pub tseg1_min: i32,
    pub tseg1_max: i32,
    pub tseg2_min: i32,
    pub tseg2_max: i32,
    pub sjw_max: i32,
    pub brp_min: i32,
    pub brp_max: i32,
    pub brp_inc: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeviceSummary {
    pub key: String,
    pub bus: u8,
    pub address: u8,
    pub vendor_id: u16,
    pub product_id: u16,
    pub interface_number: u8,
    pub serial_number: Option<String>,
    pub manufacturer: Option<String>,
    pub product: Option<String>,
    pub label: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChannelCapabilities {
    pub index: u8,
    pub feature_flags: u32,
    pub nominal_limits: TimingLimits,
    pub data_limits: Option<TimingLimits>,
    pub fd_supported: bool,
    pub hardware_timestamp: bool,
    pub termination_supported: bool,
    pub termination_enabled: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct CanMessage {
    pub arbitration_id: u32,
    pub is_extended_id: bool,
    pub is_remote_frame: bool,
    pub is_error_frame: bool,
    pub is_fd: bool,
    pub bitrate_switch: bool,
    pub error_state_indicator: bool,
    pub is_rx: bool,
    pub channel: i32,
    pub timestamp_s: f64,
    pub data: Vec<u8>,
}

impl Default for CanMessage {
    fn default() -> Self {
        Self {
            arbitration_id: 0,
            is_extended_id: false,
            is_remote_frame: false,
            is_error_frame: false,
            is_fd: false,
            bitrate_switch: false,
            error_state_indicator: false,
            is_rx: true,
            channel: -1,
            timestamp_s: 0.0,
            data: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct BusOptions {
    pub selector: String,
    pub gsusb_channel: u8,
    pub bitrate: Option<i32>,
    pub fd: bool,
    pub data_bitrate: Option<i32>,
    pub nominal_timing: Option<BusTiming>,
    pub data_timing: Option<BusTiming>,
    pub listen_only: bool,
    pub termination_enabled: bool,
    pub vendor_id: u16,
    pub product_id: u16,
    pub interface_number: Option<u8>,
    pub serial_number: Option<String>,
}

impl BusOptions {
    #[must_use]
    pub fn new() -> Self {
        Self {
            selector: "auto".to_string(),
            termination_enabled: true,
            vendor_id: DEFAULT_VENDOR_ID,
            product_id: DEFAULT_PRODUCT_ID,
            ..Self::default()
        }
    }
}

#[derive(Clone)]
struct BusInner {
    id: u64,
    logical_channel: u8,
    session: Arc<SharedUsbSession>,
    channel_info: String,
    capabilities: ChannelCapabilities,
    queue: Arc<(Mutex<VecDeque<CanMessage>>, Condvar)>,
    stopped: Arc<AtomicBool>,
    shutdown_called: Arc<AtomicBool>,
}

impl BusInner {
    fn on_message_received(&self, message: CanMessage) {
        if self.stopped.load(Ordering::SeqCst) {
            return;
        }
        let (lock, cv) = &*self.queue;
        let mut queue = lock.lock().expect("queue lock poisoned");
        queue.push_back(message);
        cv.notify_one();
    }

    fn recv(&self, timeout: Duration) -> Option<CanMessage> {
        let deadline = Instant::now() + timeout;
        let (lock, cv) = &*self.queue;
        let mut queue = lock.lock().expect("queue lock poisoned");
        loop {
            if let Some(message) = queue.pop_front() {
                return Some(message);
            }
            if self.stopped.load(Ordering::SeqCst) {
                return None;
            }

            let now = Instant::now();
            if now >= deadline {
                return None;
            }
            let remaining = deadline.saturating_duration_since(now);
            let (next_queue, wait_result) = cv.wait_timeout(queue, remaining).expect("condvar wait failed");
            queue = next_queue;
            if wait_result.timed_out() && queue.is_empty() {
                return None;
            }
        }
    }

    fn shutdown(&self) {
        if self.shutdown_called.swap(true, Ordering::SeqCst) {
            return;
        }

        self.stopped.store(true, Ordering::SeqCst);
        {
            let (_, cv) = &*self.queue;
            cv.notify_all();
        }
        let _ = self.session.close_channel(self.logical_channel);
        SharedUsbSession::release(self.session.clone(), self.logical_channel, self.id);
    }
}

struct SharedUsbSession {
    _context: Context,
    handle: Arc<RwLock<DeviceHandle<Context>>>,
    summary: DeviceSummary,
    interface_number: u8,
    in_ep: u8,
    out_ep: u8,
    channels: Vec<ChannelCapabilities>,
    tx_sizes: HashMap<u8, usize>,
    rx_frame_size: usize,
    buses: Mutex<HashMap<u8, Vec<(u64, Weak<BusInner>)>>>,
    refs: Mutex<usize>,
    running: AtomicBool,
    reader_thread: Mutex<Option<JoinHandle<()>>>,
}

impl SharedUsbSession {
    fn acquire(options: &BusOptions) -> Result<Arc<Self>> {
        let key = build_session_key(options);
        if let Some(existing) = SESSION_REGISTRY
            .lock()
            .expect("session registry lock poisoned")
            .get(&key)
            .and_then(Weak::upgrade)
        {
            return Ok(existing);
        }

        let created = Self::create(options)?;
        SESSION_REGISTRY
            .lock()
            .expect("session registry lock poisoned")
            .insert(key, Arc::downgrade(&created));
        Ok(created)
    }

    fn release(session: Arc<Self>, logical_channel: u8, bus_id: u64) {
        let last = session.remove_bus(logical_channel, bus_id);
        if last {
            session.close();
            let mut registry = SESSION_REGISTRY.lock().expect("session registry lock poisoned");
            registry.retain(|_, weak| weak.upgrade().is_some_and(|strong| !Arc::ptr_eq(&strong, &session)));
        }
    }

    fn create(options: &BusOptions) -> Result<Arc<Self>> {
        let context = Context::new()?;
        let selector = if options.selector.trim().is_empty() {
            "auto".to_string()
        } else {
            options.selector.clone()
        };
        let mut wanted_interface = options.interface_number;
        let mut wanted_serial = options.serial_number.clone();
        let (wanted_bus, wanted_address) = parse_selector_bus_address(&selector, &mut wanted_interface, &mut wanted_serial)?;

        let devices = context.devices()?;
        let mut found: Option<(Device<Context>, DeviceSummary, BulkInterfaceInfo)> = None;
        for device in devices.iter() {
            let descriptor = match device.device_descriptor() {
                Ok(value) => value,
                Err(_) => continue,
            };
            if descriptor.vendor_id() != options.vendor_id || descriptor.product_id() != options.product_id {
                continue;
            }

            let bulk = match find_bulk_interface(&device, wanted_interface) {
                Ok(value) => value,
                Err(_) => continue,
            };
            let summary = build_summary(&device, &descriptor, bulk.interface_number);
            if wanted_bus.is_some_and(|value| summary.bus != value) {
                continue;
            }
            if wanted_address.is_some_and(|value| summary.address != value) {
                continue;
            }
            if wanted_serial.as_ref().is_some_and(|value| summary.serial_number.as_deref() != Some(value.as_str())) {
                continue;
            }
            found = Some((device, summary, bulk));
            break;
        }

        let (device, summary, bulk) = found.ok_or_else(|| GsUsbError("no GSUSB device found".to_string()))?;
        let mut handle = device.open()?;
        let _ = handle.set_active_configuration(1);
        handle.claim_interface(bulk.interface_number)?;
        send_host_format(&mut handle, bulk.interface_number);
        let device_info = read_device_info(&mut handle, bulk.interface_number)?;
        let rx_frame_size = device_info
            .channels
            .iter()
            .map(calc_rx_size)
            .max()
            .unwrap_or(80);
        let tx_sizes = device_info
            .channels
            .iter()
            .map(|channel| (channel.index, calc_tx_size(channel)))
            .collect::<HashMap<_, _>>();

        let session = Arc::new(Self {
            _context: context,
            handle: Arc::new(RwLock::new(handle)),
            summary,
            interface_number: bulk.interface_number,
            in_ep: bulk.in_ep,
            out_ep: bulk.out_ep,
            channels: device_info.channels,
            tx_sizes,
            rx_frame_size,
            buses: Mutex::new(HashMap::new()),
            refs: Mutex::new(0),
            running: AtomicBool::new(true),
            reader_thread: Mutex::new(None),
        });

        let weak = Arc::downgrade(&session);
        let reader = thread::spawn(move || {
            if let Some(strong) = weak.upgrade() {
                strong.reader_loop();
            }
        });
        *session.reader_thread.lock().expect("reader thread lock poisoned") = Some(reader);
        Ok(session)
    }

    fn add_bus(&self, logical_channel: u8, bus_id: u64, bus: &Arc<BusInner>) {
        let mut buses = self.buses.lock().expect("buses lock poisoned");
        buses.entry(logical_channel).or_default().push((bus_id, Arc::downgrade(bus)));
        *self.refs.lock().expect("refs lock poisoned") += 1;
    }

    fn remove_bus(&self, logical_channel: u8, bus_id: u64) -> bool {
        let mut buses = self.buses.lock().expect("buses lock poisoned");
        if let Some(entries) = buses.get_mut(&logical_channel) {
            entries.retain(|(id, weak)| *id != bus_id && weak.upgrade().is_some());
            if entries.is_empty() {
                buses.remove(&logical_channel);
            }
        }
        let mut refs = self.refs.lock().expect("refs lock poisoned");
        *refs = refs.saturating_sub(1);
        *refs == 0
    }

    fn close(&self) {
        if !self.running.swap(false, Ordering::SeqCst) {
            return;
        }
        if let Some(reader) = self.reader_thread.lock().expect("reader thread lock poisoned").take() {
            let _ = reader.join();
        }
        if let Ok(handle) = self.handle.write() {
            let _ = handle.release_interface(self.interface_number);
        }
    }

    fn configure_channel(
        &self,
        channel_index: u8,
        nominal: BusTiming,
        data: Option<BusTiming>,
        fd_enabled: bool,
        listen_only: bool,
        termination_enabled: bool,
        start: bool,
    ) -> Result<()> {
        self.close_channel(channel_index)?;
        self.write_bittiming(channel_index, REQ_BITTIMING, nominal)?;
        let cap = self.channels[channel_index as usize].clone();
        if fd_enabled {
            if !cap.fd_supported || cap.data_limits.is_none() {
                return Err(GsUsbError("channel does not support CAN FD".to_string()));
            }
            let data = data.ok_or_else(|| GsUsbError("FD enabled but data timing missing".to_string()))?;
            self.write_bittiming(channel_index, REQ_DATA_BITTIMING, data)?;
        }

        if cap.termination_supported {
            let payload = if termination_enabled { 120u32 } else { 0u32 }.to_le_bytes();
            self.write_control(false, REQ_SET_TERMINATION, channel_index, &payload)?;
        }

        if start {
            let mut flags = 0u32;
            if listen_only {
                flags |= GS_CAN_MODE_LISTEN_ONLY;
            }
            if fd_enabled {
                flags |= GS_CAN_MODE_FD;
            }
            if cap.hardware_timestamp {
                flags |= GS_CAN_MODE_HW_TIMESTAMP;
            }
            let mut payload = [0u8; 8];
            payload[..4].copy_from_slice(&GS_CAN_MODE_START.to_le_bytes());
            payload[4..].copy_from_slice(&flags.to_le_bytes());
            self.write_control(false, REQ_MODE, channel_index, &payload)?;
        }
        Ok(())
    }

    fn close_channel(&self, channel_index: u8) -> Result<()> {
        let mut payload = [0u8; 8];
        payload[..4].copy_from_slice(&GS_CAN_MODE_RESET.to_le_bytes());
        self.write_control(false, REQ_MODE, channel_index, &payload)?;
        Ok(())
    }

    fn send_frame(&self, channel_index: u8, message: &CanMessage) -> Result<()> {
        let tx_size = *self
            .tx_sizes
            .get(&channel_index)
            .ok_or_else(|| GsUsbError("invalid channel index".to_string()))?;
        let mut payload = vec![0u8; tx_size];
        payload[..4].copy_from_slice(&0xFFFF_FFFFu32.to_le_bytes());

        let mut raw_can_id = message.arbitration_id;
        if message.is_extended_id {
            raw_can_id |= CAN_EFF_FLAG;
        }
        if message.is_remote_frame {
            raw_can_id |= CAN_RTR_FLAG;
        }
        payload[4..8].copy_from_slice(&raw_can_id.to_le_bytes());

        let payload_len = message.data.len();
        let (dlc_code, copy_len) = if message.is_fd {
            let dlc = len_to_dlc(payload_len as i32);
            (dlc, dlc_to_len(dlc) as usize)
        } else {
            let copy = payload_len.min(8);
            (copy as u8, copy)
        };
        payload[8] = dlc_code;
        payload[9] = channel_index;
        let mut flags = 0u8;
        if message.is_fd {
            flags |= GS_CAN_FLAG_FD;
        }
        if message.bitrate_switch {
            flags |= GS_CAN_FLAG_BRS;
        }
        if message.error_state_indicator {
            flags |= GS_CAN_FLAG_ESI;
        }
        payload[10] = flags;
        if !message.is_remote_frame && copy_len > 0 {
            let len = copy_len.min(message.data.len());
            payload[12..12 + len].copy_from_slice(&message.data[..len]);
        }

        let handle = self.handle.read().expect("handle lock poisoned");
        handle.write_bulk(self.out_ep, &payload, CTRL_TIMEOUT)?;
        Ok(())
    }

    fn write_bittiming(&self, channel_index: u8, request: u8, timing: BusTiming) -> Result<()> {
        let mut payload = [0u8; 20];
        payload[..4].copy_from_slice(&(timing.prop_seg as u32).to_le_bytes());
        payload[4..8].copy_from_slice(&(timing.phase_seg1 as u32).to_le_bytes());
        payload[8..12].copy_from_slice(&(timing.phase_seg2 as u32).to_le_bytes());
        payload[12..16].copy_from_slice(&(timing.sjw as u32).to_le_bytes());
        payload[16..20].copy_from_slice(&(timing.brp as u32).to_le_bytes());
        self.write_control(false, request, channel_index, &payload)?;
        Ok(())
    }

    fn write_control(&self, direction_in: bool, request: u8, value: u8, payload: &[u8]) -> Result<Vec<u8>> {
        let handle = self.handle.write().expect("handle lock poisoned");
        let request_type = rusb::request_type(
            if direction_in { Direction::In } else { Direction::Out },
            RequestType::Vendor,
            Recipient::Interface,
        );
        if direction_in {
            let mut buffer = vec![0u8; payload.len()];
            let read = handle.read_control(
                request_type,
                request,
                u16::from(value),
                u16::from(self.interface_number),
                &mut buffer,
                CTRL_TIMEOUT,
            )?;
            buffer.truncate(read);
            return Ok(buffer);
        }

        handle.write_control(
            request_type,
            request,
            u16::from(value),
            u16::from(self.interface_number),
            payload,
            CTRL_TIMEOUT,
        )?;
        Ok(Vec::new())
    }

    fn reader_loop(&self) {
        while self.running.load(Ordering::SeqCst) {
            let mut buffer = vec![0u8; self.rx_frame_size];
            let read_result = {
                let handle = self.handle.read().expect("handle lock poisoned");
                handle.read_bulk(self.in_ep, &mut buffer, BULK_TIMEOUT)
            };

            let transferred = match read_result {
                Ok(value) => value,
                Err(rusb::Error::Timeout) => continue,
                Err(_) => {
                    thread::sleep(Duration::from_millis(50));
                    continue;
                }
            };
            if transferred == 0 {
                continue;
            }

            let (message, logical_channel) = match parse_frame(&buffer[..transferred]) {
                Ok(value) => value,
                Err(_) => continue,
            };
            let listeners = {
                let buses = self.buses.lock().expect("buses lock poisoned");
                buses.get(&logical_channel).cloned().unwrap_or_default()
            };
            for (_, weak) in listeners {
                if let Some(listener) = weak.upgrade() {
                    listener.on_message_received(message.clone());
                }
            }
        }
    }
}

pub struct GsUsbBus {
    inner: Arc<BusInner>,
}

impl GsUsbBus {
    pub fn new(options: BusOptions) -> Result<Self> {
        let session = SharedUsbSession::acquire(&options)?;
        let capabilities = session
            .channels
            .get(options.gsusb_channel as usize)
            .cloned()
            .ok_or_else(|| GsUsbError("invalid gsusb_channel".to_string()))?;
        let bus_id = NEXT_BUS_ID.fetch_add(1, Ordering::SeqCst);
        let inner = Arc::new(BusInner {
            id: bus_id,
            logical_channel: options.gsusb_channel,
            session: session.clone(),
            channel_info: format!("{}/gsusb_channel={}", session.summary.label, options.gsusb_channel),
            capabilities: capabilities.clone(),
            queue: Arc::new((Mutex::new(VecDeque::new()), Condvar::new())),
            stopped: Arc::new(AtomicBool::new(false)),
            shutdown_called: Arc::new(AtomicBool::new(false)),
        });
        session.add_bus(options.gsusb_channel, bus_id, &inner);

        let nominal = if let Some(value) = options.nominal_timing {
            value
        } else {
            let bitrate = options
                .bitrate
                .ok_or_else(|| GsUsbError("bitrate or nominal_timing is required".to_string()))?;
            calculate_timing(capabilities.nominal_limits, bitrate, 80.0)?
        };

        let data = if options.fd {
            if let Some(value) = options.data_timing {
                Some(value)
            } else {
                let bitrate = options
                    .data_bitrate
                    .ok_or_else(|| GsUsbError("data_bitrate or data_timing is required when fd=true".to_string()))?;
                Some(calculate_timing(
                    capabilities
                        .data_limits
                        .ok_or_else(|| GsUsbError("channel does not support CAN FD".to_string()))?,
                    bitrate,
                    80.0,
                )?)
            }
        } else {
            None
        };

        if let Err(error) = session.configure_channel(
            options.gsusb_channel,
            nominal,
            data,
            options.fd,
            options.listen_only,
            options.termination_enabled,
            true,
        ) {
            inner.shutdown();
            return Err(error);
        }

        Ok(Self { inner })
    }

    pub fn send(&self, message: &CanMessage) -> Result<()> {
        self.inner.session.send_frame(self.inner.logical_channel, message)
    }

    #[must_use]
    pub fn recv(&self, timeout: Duration) -> Option<CanMessage> {
        self.inner.recv(timeout)
    }

    pub fn shutdown(&self) {
        self.inner.shutdown();
    }

    #[must_use]
    pub fn channel_info(&self) -> &str {
        &self.inner.channel_info
    }

    #[must_use]
    pub fn device_summary(&self) -> &DeviceSummary {
        &self.inner.session.summary
    }

    #[must_use]
    pub fn capabilities(&self) -> &ChannelCapabilities {
        &self.inner.capabilities
    }
}

impl Drop for GsUsbBus {
    fn drop(&mut self) {
        self.shutdown();
    }
}

pub fn enumerate_devices(vendor_id: u16, product_id: u16) -> Result<Vec<DeviceSummary>> {
    let context = Context::new()?;
    let devices = context.devices()?;
    let mut result = Vec::new();
    for device in devices.iter() {
        let descriptor = match device.device_descriptor() {
            Ok(value) => value,
            Err(_) => continue,
        };
        if descriptor.vendor_id() != vendor_id || descriptor.product_id() != product_id {
            continue;
        }
        let bulk = match find_bulk_interface(&device, None) {
            Ok(value) => value,
            Err(_) => continue,
        };
        result.push(build_summary(&device, &descriptor, bulk.interface_number));
    }
    result.sort_by_key(|item| (item.bus, item.address, item.interface_number));
    Ok(result)
}

pub fn calculate_timing(limits: TimingLimits, bitrate: i32, sample_point_percent: f64) -> Result<BusTiming> {
    if bitrate <= 0 {
        return Err(GsUsbError("bitrate must be > 0".to_string()));
    }
    let mut best: Option<(f64, i32, i32, i32, i32)> = None;
    let mut best_timing = None;
    let step = limits.brp_inc.max(1);
    let mut brp = limits.brp_min;
    while brp <= limits.brp_max {
        let denominator = i64::from(brp) * i64::from(bitrate);
        if denominator > 0 && i64::from(limits.fclk_hz) % denominator == 0 {
            let total_tq = (i64::from(limits.fclk_hz) / denominator) as i32;
            if total_tq >= 4 {
                let tseg_total = total_tq - 1;
                let tseg1_target = ((sample_point_percent / 100.0) * f64::from(total_tq) - 1.0).round() as i32;
                for tseg1 in [tseg1_target, tseg1_target - 1, tseg1_target + 1] {
                    let tseg2 = tseg_total - tseg1;
                    if tseg1 < limits.tseg1_min
                        || tseg1 > limits.tseg1_max
                        || tseg2 < limits.tseg2_min
                        || tseg2 > limits.tseg2_max
                    {
                        continue;
                    }
                    let actual_sp = ((1.0 + f64::from(tseg1)) / f64::from(total_tq)) * 100.0;
                    let score = (
                        (actual_sp - sample_point_percent).abs(),
                        brp.abs(),
                        tseg2,
                        tseg1,
                        total_tq,
                    );
                    if best.is_none_or(|current| score < current) {
                        best = Some(score);
                        best_timing = Some(BusTiming {
                            brp,
                            prop_seg: 0,
                            phase_seg1: tseg1,
                            phase_seg2: tseg2,
                            sjw: tseg2.min(limits.sjw_max),
                        });
                    }
                }
            }
        }
        brp += step;
    }
    best_timing.ok_or_else(|| GsUsbError("cannot derive timing from channel limits".to_string()))
}

#[derive(Clone, Copy)]
struct BulkInterfaceInfo {
    interface_number: u8,
    in_ep: u8,
    out_ep: u8,
}

fn parse_selector_bus_address(
    selector: &str,
    interface_number: &mut Option<u8>,
    serial_number: &mut Option<String>,
) -> Result<(Option<u8>, Option<u8>)> {
    if selector.is_empty() || selector.eq_ignore_ascii_case("auto") {
        return Ok((None, None));
    }
    let parts = selector.split(':').collect::<Vec<_>>();
    if (parts.len() == 2 || parts.len() == 3) && parts.iter().all(|part| part.chars().all(|value| value.is_ascii_digit())) {
        let bus = parts[0].parse::<u8>()?;
        let address = parts[1].parse::<u8>()?;
        if parts.len() == 3 {
            *interface_number = Some(parts[2].parse::<u8>()?);
        }
        return Ok((Some(bus), Some(address)));
    }
    *serial_number = Some(selector.to_string());
    Ok((None, None))
}

fn build_session_key(options: &BusOptions) -> String {
    format!(
        "{}:{}:{}:{}:{}",
        options.vendor_id,
        options.product_id,
        if options.selector.is_empty() { "auto" } else { &options.selector },
        options.interface_number.map_or_else(String::new, |value| value.to_string()),
        options.serial_number.clone().unwrap_or_default()
    )
}

fn build_summary(device: &Device<Context>, descriptor: &DeviceDescriptor, interface_number: u8) -> DeviceSummary {
    let bus = device.bus_number();
    let address = device.address();
    let (serial_number, manufacturer, product) = if let Ok(handle) = device.open() {
        (
            handle.read_serial_number_string_ascii(descriptor).ok(),
            handle.read_manufacturer_string_ascii(descriptor).ok(),
            handle.read_product_string_ascii(descriptor).ok(),
        )
    } else {
        (None, None, None)
    };
    let label = format!(
        "USB {bus:03}:{address:03} [{:04X}:{:04X}] {}",
        descriptor.vendor_id(),
        descriptor.product_id(),
        product.clone().unwrap_or_else(|| "GSUSB".to_string())
    );
    let key = format!(
        "{bus:03}:{address:03}:{:04X}:{:04X}:{interface_number}",
        descriptor.vendor_id(),
        descriptor.product_id()
    );
    DeviceSummary {
        key,
        bus,
        address,
        vendor_id: descriptor.vendor_id(),
        product_id: descriptor.product_id(),
        interface_number,
        serial_number,
        manufacturer,
        product,
        label,
    }
}

fn find_bulk_interface(device: &Device<Context>, interface_hint: Option<u8>) -> Result<BulkInterfaceInfo> {
    let config = device
        .active_config_descriptor()
        .or_else(|_| device.config_descriptor(0))
        .map_err(GsUsbError::from)?;

    for interface in config.interfaces() {
        for descriptor in interface.descriptors() {
            if interface_hint.is_some_and(|value| descriptor.interface_number() != value) {
                continue;
            }
            let mut in_ep = None;
            let mut out_ep = None;
            for endpoint in descriptor.endpoint_descriptors() {
                if endpoint.transfer_type() != rusb::TransferType::Bulk {
                    continue;
                }
                match endpoint.direction() {
                    Direction::In => in_ep = Some(endpoint.address()),
                    Direction::Out => out_ep = Some(endpoint.address()),
                }
            }
            if let (Some(in_ep), Some(out_ep)) = (in_ep, out_ep) {
                return Ok(BulkInterfaceInfo {
                    interface_number: descriptor.interface_number(),
                    in_ep,
                    out_ep,
                });
            }
        }
    }
    Err(GsUsbError("bulk endpoints not found".to_string()))
}

fn send_host_format(handle: &mut DeviceHandle<Context>, interface_number: u8) {
    let payload = 0x0000_BEEFu32.to_le_bytes();
    let _ = handle.write_control(
        rusb::request_type(Direction::Out, RequestType::Vendor, Recipient::Interface),
        REQ_HOST_FORMAT,
        1,
        u16::from(interface_number),
        &payload,
        CTRL_TIMEOUT,
    );
}

fn read_device_info(handle: &mut DeviceHandle<Context>, interface_number: u8) -> Result<DeviceInfo> {
    let mut config = [0u8; 12];
    let read = handle.read_control(
        rusb::request_type(Direction::In, RequestType::Vendor, Recipient::Interface),
        REQ_DEVICE_CONFIG,
        1,
        u16::from(interface_number),
        &mut config,
        CTRL_TIMEOUT,
    )?;
    if read < 12 {
        return Err(GsUsbError("failed to read device config".to_string()));
    }
    let sw_version = u32::from_le_bytes(config[4..8].try_into().expect("sw version slice"));
    let hw_version = u32::from_le_bytes(config[8..12].try_into().expect("hw version slice"));
    let channel_count = usize::from(config[3]) + 1;
    let mut channels = Vec::with_capacity(channel_count);
    for channel_index in 0..channel_count {
        channels.push(read_channel_capabilities(handle, interface_number, channel_index as u8)?);
    }
    Ok(DeviceInfo {
        _sw_version: sw_version,
        _hw_version: hw_version,
        channels,
    })
}

fn read_channel_capabilities(
    handle: &mut DeviceHandle<Context>,
    interface_number: u8,
    channel_index: u8,
) -> Result<ChannelCapabilities> {
    let mut ext_data = [0u8; 72];
    let ext_read = handle.read_control(
        rusb::request_type(Direction::In, RequestType::Vendor, Recipient::Interface),
        REQ_BT_CONST_EXT,
        u16::from(channel_index),
        u16::from(interface_number),
        &mut ext_data,
        CTRL_TIMEOUT,
    );
    let (feature_flags, nominal_limits, data_limits) = match ext_read {
        Ok(read) if read >= 72 => {
            let words = bytes_to_u32_list(&ext_data[..72])?;
            (
                words[0],
                TimingLimits {
                    fclk_hz: words[1] as i32,
                    tseg1_min: words[2] as i32,
                    tseg1_max: words[3] as i32,
                    tseg2_min: words[4] as i32,
                    tseg2_max: words[5] as i32,
                    sjw_max: words[6] as i32,
                    brp_min: words[7] as i32,
                    brp_max: words[8] as i32,
                    brp_inc: words[9] as i32,
                },
                Some(TimingLimits {
                    fclk_hz: words[1] as i32,
                    tseg1_min: words[10] as i32,
                    tseg1_max: words[11] as i32,
                    tseg2_min: words[12] as i32,
                    tseg2_max: words[13] as i32,
                    sjw_max: words[14] as i32,
                    brp_min: words[15] as i32,
                    brp_max: words[16] as i32,
                    brp_inc: words[17] as i32,
                }),
            )
        }
        _ => {
            let mut std_data = [0u8; 40];
            let read = handle.read_control(
                rusb::request_type(Direction::In, RequestType::Vendor, Recipient::Interface),
                REQ_BT_CONST,
                u16::from(channel_index),
                u16::from(interface_number),
                &mut std_data,
                CTRL_TIMEOUT,
            )?;
            if read < 40 {
                return Err(GsUsbError("failed to read timing constants".to_string()));
            }
            let words = bytes_to_u32_list(&std_data[..40])?;
            (
                words[0],
                TimingLimits {
                    fclk_hz: words[1] as i32,
                    tseg1_min: words[2] as i32,
                    tseg1_max: words[3] as i32,
                    tseg2_min: words[4] as i32,
                    tseg2_max: words[5] as i32,
                    sjw_max: words[6] as i32,
                    brp_min: words[7] as i32,
                    brp_max: words[8] as i32,
                    brp_inc: words[9] as i32,
                },
                None,
            )
        }
    };

    let mut termination_enabled = false;
    if (feature_flags & GS_CAN_FEATURE_TERMINATION) != 0 {
        let mut term_data = [0u8; 4];
        if let Ok(read) = handle.read_control(
            rusb::request_type(Direction::In, RequestType::Vendor, Recipient::Interface),
            REQ_GET_TERMINATION,
            u16::from(channel_index),
            u16::from(interface_number),
            &mut term_data,
            CTRL_TIMEOUT,
        ) {
            if read >= 4 {
                termination_enabled = u32::from_le_bytes(term_data) == 120;
            }
        }
    }

    Ok(ChannelCapabilities {
        index: channel_index,
        feature_flags,
        nominal_limits,
        data_limits,
        fd_supported: (feature_flags & GS_CAN_FEATURE_FD) != 0,
        hardware_timestamp: (feature_flags & GS_CAN_FEATURE_HW_TIMESTAMP) != 0,
        termination_supported: (feature_flags & GS_CAN_FEATURE_TERMINATION) != 0,
        termination_enabled,
    })
}

#[derive(Debug)]
struct DeviceInfo {
    _sw_version: u32,
    _hw_version: u32,
    channels: Vec<ChannelCapabilities>,
}

fn parse_frame(raw: &[u8]) -> Result<(CanMessage, u8)> {
    if raw.len() < 20 {
        return Err(GsUsbError("short frame".to_string()));
    }
    let echo_id = u32::from_le_bytes(raw[..4].try_into().expect("echo id slice"));
    let raw_can_id = u32::from_le_bytes(raw[4..8].try_into().expect("can id slice"));
    let dlc_code = raw[8];
    let channel_index = raw[9];
    let flags = raw[10];
    let is_fd = (flags & GS_CAN_FLAG_FD) != 0;
    let payload_len = if is_fd { dlc_to_len(dlc_code) as usize } else { usize::from(dlc_code.min(8)) };
    let available = raw.len().saturating_sub(12).min(payload_len);
    Ok((
        CanMessage {
            arbitration_id: if (raw_can_id & CAN_EFF_FLAG) != 0 {
                raw_can_id & CAN_EFF_MASK
            } else {
                raw_can_id & CAN_SFF_MASK
            },
            is_extended_id: (raw_can_id & CAN_EFF_FLAG) != 0,
            is_remote_frame: (raw_can_id & CAN_RTR_FLAG) != 0,
            is_error_frame: (raw_can_id & CAN_ERR_FLAG) != 0,
            is_fd,
            bitrate_switch: (flags & GS_CAN_FLAG_BRS) != 0,
            error_state_indicator: (flags & GS_CAN_FLAG_ESI) != 0,
            is_rx: echo_id == 0xFFFF_FFFF,
            channel: i32::from(channel_index),
            timestamp_s: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map_or(0.0, |value| value.as_secs_f64()),
            data: raw[12..12 + available].to_vec(),
        },
        channel_index,
    ))
}

fn bytes_to_u32_list(data: &[u8]) -> Result<Vec<u32>> {
    if data.len() % 4 != 0 {
        return Err(GsUsbError("buffer length must be multiple of 4".to_string()));
    }
    Ok(data
        .chunks_exact(4)
        .map(|chunk| u32::from_le_bytes(chunk.try_into().expect("u32 chunk slice")))
        .collect())
}

fn calc_rx_size(cap: &ChannelCapabilities) -> usize {
    if cap.fd_supported {
        if cap.hardware_timestamp { 80 } else { 76 }
    } else if cap.hardware_timestamp {
        24
    } else {
        20
    }
}

fn calc_tx_size(cap: &ChannelCapabilities) -> usize {
    if cap.fd_supported { 76 } else { 20 }
}

fn dlc_to_len(dlc: u8) -> i32 {
    match dlc {
        0..=8 => i32::from(dlc),
        9 => 12,
        10 => 16,
        11 => 20,
        12 => 24,
        13 => 32,
        14 => 48,
        _ => 64,
    }
}

fn len_to_dlc(len: i32) -> u8 {
    match len {
        i32::MIN..=0 => 0,
        1..=8 => len as u8,
        9..=12 => 9,
        13..=16 => 10,
        17..=20 => 11,
        21..=24 => 12,
        25..=32 => 13,
        33..=48 => 14,
        _ => 15,
    }
}
