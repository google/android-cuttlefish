// Copyright 2026, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::collections::VecDeque;
use std::io::BufWriter;
use std::io::Result as IoResult;
use std::io::Seek;
use std::io::SeekFrom;
use std::io::Write;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;
use std::str::FromStr;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use vmm_sys_util::eventfd::{EventFd, EFD_NONBLOCK};
use vmm_sys_util::poll::{PollContext, PollToken};
use vmm_sys_util::timerfd::TimerFd;

use v4l2r::PixelFormat;
use v4l2r::QueueType;
use v4l2r::bindings;
use v4l2r::bindings::v4l2_fmtdesc;
use v4l2r::bindings::v4l2_format;
use v4l2r::bindings::v4l2_requestbuffers;
use v4l2r::ioctl::BufferCapabilities;
use v4l2r::ioctl::BufferField;
use v4l2r::ioctl::BufferFlags;
use v4l2r::ioctl::CtrlId;
use v4l2r::ioctl::CtrlWhich;
use v4l2r::ioctl::EventType as V4l2EventType;
use v4l2r::ioctl::QueryCtrlFlags;
use v4l2r::ioctl::SubscribeEventFlags;
use v4l2r::ioctl::V4l2Buffer;
use v4l2r::ioctl::V4l2PlanesWithBackingMut;
use v4l2r::memory::MemoryType;
use virtio_media::VirtioMediaDevice;
use virtio_media::VirtioMediaDeviceSession;
use virtio_media::VirtioMediaEventQueue;
use virtio_media::VirtioMediaHostMemoryMapper;
use virtio_media::io::ReadFromDescriptorChain;
use virtio_media::io::WriteToDescriptorChain;
use virtio_media::ioctl::IoctlResult;
use virtio_media::ioctl::VirtioMediaIoctlHandler;
use virtio_media::ioctl::virtio_media_dispatch_ioctl;
use virtio_media::memfd::MemFdBuffer;
use virtio_media::mmap::MmapMappingManager;
use virtio_media::protocol::DequeueBufferEvent;
use virtio_media::protocol::SessionEvent;
use virtio_media::protocol::SgEntry;
use virtio_media::protocol::V4l2Event;
use virtio_media::protocol::V4l2Ioctl;
use virtio_media::protocol::VIRTIO_MEDIA_MMAP_FLAG_RW;

/// Rust equivalent of the V4L2_CTRL_ID2WHICH C preprocessor macro.
/// Extracts the control class ID from a control ID by masking out the lower 16 bits
/// and any reserved top bits (uses mask 0x0fff0000).
/// See: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/control.html
const fn v4l2_ctrl_id2which(id: u32) -> u32 {
    id & 0x0fff0000
}

/// https://developer.android.com/reference/android/hardware/camera2/CameraMetadata#LENS_FACING_FRONT
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LensFacing {
    Front = 0,
    Back = 1,
    External = 2,
}

impl FromStr for LensFacing {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "FRONT" => Ok(LensFacing::Front),
            "BACK" => Ok(LensFacing::Back),
            "EXTERNAL" => Ok(LensFacing::External),
            _ => Err(format!("Invalid lens facing: {}. Expected FRONT, BACK, or EXTERNAL", s)),
        }
    }
}

/// Encapsulates the camera gain value.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Gain(i32);

impl Gain {
    pub const MIN: i32 = 100;
    pub const MAX: i32 = 1600;
    pub const DEFAULT: i32 = 100;

    pub fn new(value: i32) -> Result<Self, libc::c_int> {
        if value < Self::MIN || value > Self::MAX {
            Err(libc::ERANGE)
        } else {
            Ok(Gain(value))
        }
    }

    pub fn value(&self) -> i32 {
        self.0
    }
}

impl Default for Gain {
    fn default() -> Self {
        Gain(Self::DEFAULT)
    }
}

/// State of all camera controls.
#[derive(Debug, Clone, PartialEq, Eq)]
struct CameraControls {
    lens_facing: LensFacing,
    gain: Gain,
}

impl CameraControls {
    fn new(lens_facing: LensFacing) -> Self {
        Self {
            lens_facing,
            gain: Gain::default(),
        }
    }
}

/// Current status of a buffer.
#[derive(Debug, PartialEq, Eq)]
enum BufferState {
    /// Buffer has just been created (or streamed off) and not been used yet.
    New,
    /// Buffer has been QBUF'd by the driver but not yet processed.
    Incoming,
    /// Buffer has been processed and is ready for dequeue.
    Outgoing {
        /// Sequence of the generated frame.
        sequence: u32,
    },
}

/// Information about a single plane of a multi-planar buffer.
struct Plane {
    /// Backing memory file descriptor.
    fd: MemFdBuffer,
    /// Offset that can be used to map the plane's memory.
    offset: u32,
}

/// Information about a single buffer.
struct Buffer {
    /// Current state of the buffer.
    state: BufferState,
    /// V4L2 representation of this buffer to be sent to the guest when requested.
    v4l2_buffer: V4l2Buffer,
    /// Backing storage and offsets for the planes of the buffer.
    planes: [Plane; 3],
}

impl Buffer {
    fn new(v4l2_buffer: V4l2Buffer, planes: [Plane; 3]) -> Self {
        Self {
            state: BufferState::New,
            v4l2_buffer,
            planes,
        }
    }

    /// Update the state of the buffer as well as its V4L2 representation.
    fn set_state(&mut self, state: BufferState, width: u32, height: u32) {
        let mut flags = self.v4l2_buffer.flags();
        match state {
            BufferState::New => {
                let planes = self.v4l2_buffer.planes_with_backing_iter_mut();
                if let V4l2PlanesWithBackingMut::Mmap(mut planes) = planes {
                    *planes.next().unwrap().bytesused = 0;
                    *planes.next().unwrap().bytesused = 0;
                    *planes.next().unwrap().bytesused = 0;
                }
                flags &= !BufferFlags::QUEUED;
            }
            BufferState::Incoming => {
                let planes = self.v4l2_buffer.planes_with_backing_iter_mut();
                if let V4l2PlanesWithBackingMut::Mmap(mut planes) = planes {
                    *planes.next().unwrap().bytesused = 0;
                    *planes.next().unwrap().bytesused = 0;
                    *planes.next().unwrap().bytesused = 0;
                }
                flags |= BufferFlags::QUEUED;
            }
            BufferState::Outgoing { sequence } => {
                {
                    let planes = self.v4l2_buffer.planes_with_backing_iter_mut();
                    if let V4l2PlanesWithBackingMut::Mmap(mut planes) = planes {
                        *planes.next().unwrap().bytesused = width * height;
                        *planes.next().unwrap().bytesused = width * height / 4;
                        *planes.next().unwrap().bytesused = width * height / 4;
                    }
                }
                self.v4l2_buffer.set_sequence(sequence);
                let mut ts = libc::timespec {
                    tv_sec: 0,
                    tv_nsec: 0,
                };
                // SAFETY: clock_gettime is a standard POSIX libc call with a valid pointer.
                unsafe {
                    libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts);
                }
                self.v4l2_buffer.set_timestamp(bindings::timeval {
                    tv_sec: ts.tv_sec as bindings::__time_t,
                    tv_usec: (ts.tv_nsec / 1000) as bindings::__time_t,
                });
                flags &= !BufferFlags::QUEUED;
            }
        }
        self.v4l2_buffer.set_flags(flags);
        self.state = state;
    }
}

/// Shared session state between ioctl handlers and the background frame worker.
pub struct SessionSharedState {
    /// Current iteration of the pattern generation cycle.
    iteration: u64,
    /// Buffers currently allocated for this session.
    buffers: Vec<Buffer>,
    /// Queue of buffers awaiting processing.
    queued_buffers: VecDeque<usize>,
    /// Is the session currently streaming?
    streaming: bool,
    /// Is the session stopped?
    stopped: bool,
    /// Monotonic counter incremented on streamoff/reset to invalidate in-flight frames.
    stream_count: u64,
    /// Camera controls (gain, lens facing).
    controls: CameraControls,
    /// Width of the video.
    width: u32,
    /// Height of the video.
    height: u32,
}

/// Session data of [`EmulatedCamera`].
pub struct EmulatedCameraSession {
    /// Id of the session.
    id: u32,
    /// Shared state between ioctl handlers and frame worker.
    state: Arc<Mutex<SessionSharedState>>,
    /// EventFd to wake up the background worker thread on state changes.
    wakeup_evt: Arc<EventFd>,
    /// Background frame worker thread handle.
    worker_handle: Option<std::thread::JoinHandle<()>>,
}

impl VirtioMediaDeviceSession for EmulatedCameraSession {
    fn poll_fd(&self) -> Option<BorrowedFd<'_>> {
        None
    }
}

impl EmulatedCameraSession {
    fn write_pattern<WY: std::io::Write, WU: std::io::Write, WV: std::io::Write>(
        iteration: u64,
        controls: &CameraControls,
        width: u32,
        height: u32,
        mut sink_y: WY,
        mut sink_u: WU,
        mut sink_v: WV,
    ) -> IoctlResult<()> {
        let mut writer_y = BufWriter::new(&mut sink_y);
        let mut writer_u = BufWriter::new(&mut sink_u);
        let mut writer_v = BufWriter::new(&mut sink_v);
        // The base Y (luma) value changes over iterations to create a moving pattern.
        let base_y = (iteration % 256) as u8;
        // Apply gain to the luma channel.
        // Gain::MIN (100) represents 1.0x gain. Higher values scale the brightness.
        // We clamp the result to 255.0 to avoid overflow.
        let y = ((base_y as f32) * (controls.gain.value() as f32 / Gain::MIN as f32)).min(255.0) as u8;
        let u = ((iteration + 64) % 256) as u8;
        let v = ((iteration + 128) % 256) as u8;
        for _ in 0..(width * height) {
            writer_y.write_all(&[y]).map_err(|_| libc::EIO)?;
        }
        for _ in 0..(width * height / 4) {
            writer_u.write_all(&[u]).map_err(|_| libc::EIO)?;
        }
        for _ in 0..(width * height / 4) {
            writer_v.write_all(&[v]).map_err(|_| libc::EIO)?;
        }
        Ok(())
    }
}

const FRAME_INTERVAL: Duration = Duration::from_nanos(1_000_000_000 / FRAME_RATE as u64);

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum WorkerToken {
    Wakeup,
    Timer,
}

impl PollToken for WorkerToken {
    fn as_raw_token(&self) -> u64 {
        match self {
            WorkerToken::Wakeup => 0,
            WorkerToken::Timer => 1,
        }
    }

    fn from_raw_token(data: u64) -> Self {
        match data {
            0 => WorkerToken::Wakeup,
            1 => WorkerToken::Timer,
            _ => unreachable!(),
        }
    }
}

/// Write basic pattern into the queued buffers
fn spawn_frame_worker<Q: VirtioMediaEventQueue + Send + 'static>(
    session_id: u32,
    shared: Arc<Mutex<SessionSharedState>>,
    wakeup_evt: Arc<EventFd>,
    evt_queue: Arc<Mutex<Q>>,
) -> std::thread::JoinHandle<()> {
    std::thread::spawn(move || {
        let mut timer_fd = match TimerFd::new() {
            Ok(t) => t,
            Err(e) => {
                log::error!("Failed to create TimerFd for worker thread: {}", e);
                return;
            }
        };
        let poll_ctx: PollContext<WorkerToken> = match PollContext::new() {
            Ok(p) => p,
            Err(e) => {
                log::error!("Failed to create PollContext for worker thread: {}", e);
                return;
            }
        };
        if let Err(e) = poll_ctx.add(&*wakeup_evt, WorkerToken::Wakeup) {
            log::error!("Failed to add wakeup eventfd to PollContext: {}", e);
            return;
        }
        if let Err(e) = poll_ctx.add(&timer_fd, WorkerToken::Timer) {
            log::error!("Failed to add timerfd to PollContext: {}", e);
            return;
        }
        let mut timer_armed = false;

        loop {
            let ready_events = match poll_ctx.wait() {
                Ok(ev) => ev,
                Err(e) => {
                    log::error!("PollContext wait failed in worker thread: {}", e);
                    break;
                }
            };

            let mut timer_triggered = false;
            for event in ready_events.iter_readable() {
                match event.token() {
                    WorkerToken::Wakeup => {
                        let _ = wakeup_evt.read();
                    }
                    WorkerToken::Timer => {
                        let _ = timer_fd.wait();
                        timer_triggered = true;
                    }
                }
            }

            let (buf_id, stream_count, iteration, controls, width, height, file_y, file_u, file_v) = {
                let mut guard = shared.lock().unwrap();
                if guard.stopped {
                    break;
                }

                let should_arm = guard.streaming && !guard.queued_buffers.is_empty();
                if should_arm && !timer_armed {
                    // Set the initial duration to 1 ns so the timer tick expires immediately
                    // to process and deliver the first frame right after STREAMON.
                    if let Err(e) = timer_fd.reset(Duration::from_nanos(1), Some(FRAME_INTERVAL)) {
                        log::error!("Failed to arm TimerFd: {}", e);
                        break;
                    }
                    timer_armed = true;
                } else if !should_arm && timer_armed {
                    if let Err(e) = timer_fd.clear() {
                        log::error!("Failed to disarm TimerFd: {}", e);
                        break;
                    }
                    timer_armed = false;
                }

                if !timer_triggered || !guard.streaming {
                    continue;
                }

                let buf_id = match guard.queued_buffers.pop_front() {
                    Some(id) => id,
                    None => continue,
                };
                let buffer = match guard.buffers.get_mut(buf_id) {
                    Some(b) => b,
                    None => continue,
                };

                for plane in &mut buffer.planes {
                    let _ = plane.fd.as_file().seek(SeekFrom::Start(0));
                }

                let file_y = buffer.planes[0].fd.as_file().try_clone().ok();
                let file_u = buffer.planes[1].fd.as_file().try_clone().ok();
                let file_v = buffer.planes[2].fd.as_file().try_clone().ok();

                (
                    buf_id,
                    guard.stream_count,
                    guard.iteration,
                    guard.controls.clone(),
                    guard.width,
                    guard.height,
                    file_y,
                    file_u,
                    file_v,
                )
            };

            // Release the lock during pattern rendering:
            if let (Some(fy), Some(fu), Some(fv)) = (file_y, file_u, file_v) {
                let _ = EmulatedCameraSession::write_pattern(
                    iteration,
                    &controls,
                    width,
                    height,
                    fy,
                    fu,
                    fv,
                );
            }

            // Re-acquire lock to verify stream_count and dispatch DequeueBuffer:
            let mut guard = shared.lock().unwrap();
            if guard.stream_count == stream_count {
                if let Some(buffer) = guard.buffers.get_mut(buf_id) {
                    // Resolution reconfiguration requires STREAMOFF (which increments stream_count),
                    // so `width`/`height` are guaranteed to match `guard.width`/`guard.height`.
                    buffer.set_state(
                        BufferState::Outgoing {
                            sequence: iteration as u32,
                        },
                        width,
                        height,
                    );
                    let v4l2_buf_clone = buffer.v4l2_buffer.clone();
                    guard.iteration += 1;

                    evt_queue.lock().unwrap().send_event(V4l2Event::DequeueBuffer(
                        DequeueBufferEvent::new(session_id, v4l2_buf_clone),
                    ));
                }
            }
        }
    })
}

/// Emulated camera used for testing Android camera stack.
///
/// This implementation looks forward to have feature parity with existing Android Guest Emulated
/// camera https://cs.android.com/android/platform/superproject/main/+/main:hardware/google/camera/devices/EmulatedCamera/
pub struct EmulatedCamera<Q: VirtioMediaEventQueue, HM: VirtioMediaHostMemoryMapper> {
    /// Queue used to send events to the guest.
    evt_queue: Arc<Mutex<Q>>,
    /// Host MMAP mapping manager.
    mmap_manager: MmapMappingManager<HM>,
    /// ID of the session with allocated buffers, if any.
    ///
    /// v4l2-compliance checks that only a single session can have allocated buffers at a given
    /// time, since that's how actual hardware works - no two sessions can access a camera at the
    /// same time. It will fails if we allow simultaneous sessions to be active, so we need this
    /// artificial limitation to make it pass fully.
    active_session: Option<u32>,
    /// Camera controls.
    controls: CameraControls,
    /// Width of the video.
    width: u32,
    /// Height of the video.
    height: u32,
}

impl<Q, HM> EmulatedCamera<Q, HM>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    HM: VirtioMediaHostMemoryMapper,
{
    pub fn new(evt_queue: Q, mapper: HM, lens_facing: LensFacing) -> Self {
        Self {
            evt_queue: Arc::new(Mutex::new(evt_queue)),
            mmap_manager: MmapMappingManager::from(mapper),
            active_session: None,
            controls: CameraControls::new(lens_facing),
            width: WIDTH,
            height: HEIGHT,
        }
    }

    fn lens_facing_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        let name_str = "LENS_FACING";
        let mut name = [0u8; 32];
        name[0..name_str.len()].copy_from_slice(name_str.as_bytes());
        bindings::v4l2_query_ext_ctrl {
            id: CID_LENS_FACING,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER,
            name: name.map(|b| b as i8),
            minimum: LensFacing::Front as i64,
            maximum: LensFacing::External as i64,
            step: 1,
            default_value: self.controls.lens_facing as i64,
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY,
            elems: 1,
            elem_size: std::mem::size_of::<u32>() as u32,
            ..Default::default()
        }
    }

    fn gain_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        let name_str = "Gain";
        let mut name = [0u8; 32];
        name[0..name_str.len()].copy_from_slice(name_str.as_bytes());
        bindings::v4l2_query_ext_ctrl {
            id: bindings::V4L2_CID_GAIN,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER,
            name: name.map(|b| b as i8),
            minimum: Gain::MIN as i64,
            maximum: Gain::MAX as i64,
            step: 1,
            default_value: Gain::DEFAULT as i64,
            flags: 0,
            elems: 1,
            elem_size: std::mem::size_of::<i32>() as u32,
            ..Default::default()
        }
    }

    fn user_class_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        let name_str = "User Controls";
        let mut name = [0u8; 32];
        name[0..name_str.len()].copy_from_slice(name_str.as_bytes());
        bindings::v4l2_query_ext_ctrl {
            id: bindings::V4L2_CID_USER_CLASS,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_CTRL_CLASS,
            name: name.map(|b| b as i8),
            // V4L2 standard requires control class headers to be marked as both RO and WO.
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY | bindings::V4L2_CTRL_FLAG_WRITE_ONLY,
            ..Default::default()
        }
    }

    fn camera_class_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        let name_str = "Camera Controls";
        let mut name = [0u8; 32];
        name[0..name_str.len()].copy_from_slice(name_str.as_bytes());
        bindings::v4l2_query_ext_ctrl {
            id: bindings::V4L2_CID_CAMERA_CLASS,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_CTRL_CLASS,
            name: name.map(|b| b as i8),
            // V4L2 standard requires control class headers to be marked as both RO and WO.
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY | bindings::V4L2_CTRL_FLAG_WRITE_ONLY,
            ..Default::default()
        }
    }
}

impl<Q, HM, Reader, Writer> VirtioMediaDevice<Reader, Writer> for EmulatedCamera<Q, HM>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    HM: VirtioMediaHostMemoryMapper,
    Reader: ReadFromDescriptorChain,
    Writer: WriteToDescriptorChain,
{
    type Session = EmulatedCameraSession;

    fn new_session(&mut self, session_id: u32) -> std::result::Result<Self::Session, i32> {
        let shared_state = Arc::new(Mutex::new(SessionSharedState {
            iteration: 0,
            buffers: Default::default(),
            queued_buffers: Default::default(),
            streaming: false,
            stopped: false,
            stream_count: 0,
            controls: self.controls.clone(),
            width: self.width,
            height: self.height,
        }));
        let wakeup_evt = Arc::new(EventFd::new(EFD_NONBLOCK).map_err(|e| {
            log::error!("Failed to create EventFd for session {}: {}", session_id, e);
            libc::EMFILE
        })?);
        let worker_handle = spawn_frame_worker(
            session_id,
            Arc::clone(&shared_state),
            Arc::clone(&wakeup_evt),
            Arc::clone(&self.evt_queue),
        );
        Ok(EmulatedCameraSession {
            id: session_id,
            state: shared_state,
            wakeup_evt,
            worker_handle: Some(worker_handle),
        })
    }

    fn close_session(&mut self, mut session: Self::Session) {
        // Nothing to cleanup when `close_session` is called for sessions without
        // allocated buffers, hence the early return.
        if self.active_session == Some(session.id) {
            self.active_session = None;
        }

        let buffers = {
            let mut state = session.state.lock().unwrap();
            state.stopped = true;
            state.streaming = false;
            state.stream_count += 1;
            let _ = session.wakeup_evt.write(1);
            std::mem::take(&mut state.buffers)
        };
        if let Some(handle) = session.worker_handle.take() {
            let _ = handle.join();
        }

        for buffer in &buffers {
            for plane in &buffer.planes {
                self.mmap_manager.unregister_buffer(plane.offset);
            }
        }
    }

    fn do_ioctl(
        &mut self,
        session: &mut Self::Session,
        ioctl: V4l2Ioctl,
        reader: &mut Reader,
        writer: &mut Writer,
    ) -> IoResult<()> {
        virtio_media_dispatch_ioctl(self, session, ioctl, reader, writer)
    }

    fn do_mmap(
        &mut self,
        session: &mut Self::Session,
        flags: u32,
        offset: u32,
    ) -> std::result::Result<(u64, u64), i32> {
        let mut state = session.state.lock().unwrap();
        let buffer = state
            .buffers
            .iter_mut()
            .find(|b| b.planes.iter().any(|p| p.offset == offset))
            .ok_or(libc::EINVAL)?;
        let plane = buffer
            .planes
            .iter()
            .find(|p| p.offset == offset)
            .ok_or(libc::EINVAL)?;
        let rw = (flags & VIRTIO_MEDIA_MMAP_FLAG_RW) != 0;
        let fd = plane.fd.as_file().as_fd();
        let (guest_addr, size) = self
            .mmap_manager
            .create_mapping(offset, fd, rw)
            .map_err(|_| libc::EINVAL)?;
        Ok((guest_addr, size))
    }

    fn do_munmap(&mut self, guest_addr: u64) -> std::result::Result<(), i32> {
        self.mmap_manager
            .remove_mapping(guest_addr)
            .map(|_| ())
            .map_err(|_| libc::EINVAL)
    }
}

// Use an offset for virtio-media custom camera class control id values.
const CID_OFFSET: u32 = bindings::V4L2_CID_CAMERA_CLASS_BASE + 0x100;
const CID_LENS_FACING: u32 = CID_OFFSET + 1;

const PIXELFORMAT: u32 = PixelFormat::from_fourcc(b"YM12").to_u32();
const WIDTH: u32 = 640;
const HEIGHT: u32 = 480;
const FRAME_RATE: u32 = 30;

const SUPPORTED_SIZES: [(u32, u32); 2] = [
    (640, 480),
    (1280, 720),
];

const INPUTS: [bindings::v4l2_input; 1] = [bindings::v4l2_input {
    index: 0,
    name: *b"Default\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    type_: bindings::V4L2_INPUT_TYPE_CAMERA,
    ..unsafe { std::mem::zeroed() }
}];

fn default_fmtdesc(queue: QueueType) -> v4l2_fmtdesc {
    v4l2_fmtdesc {
        index: 0,
        type_: queue as u32,
        pixelformat: PIXELFORMAT,
        ..Default::default()
    }
}

fn session_fmt(queue: QueueType, width: u32, height: u32) -> v4l2_format {
    let pix_mp = bindings::v4l2_pix_format_mplane {
        width,
        height,
        pixelformat: PIXELFORMAT,
        field: bindings::v4l2_field_V4L2_FIELD_NONE,
        colorspace: bindings::v4l2_colorspace_V4L2_COLORSPACE_SRGB,
        num_planes: 3,
        plane_fmt: [
            bindings::v4l2_plane_pix_format {
                // Size of Y plane
                sizeimage: width * height,
                // Bytes per line for Y plane
                bytesperline: width,
                ..Default::default()
            },
            bindings::v4l2_plane_pix_format {
                // Size of U plane (chroma subsampled by 2 in both directions)
                sizeimage: width * height / 4,
                // Bytes per line for U plane
                bytesperline: width / 2,
                ..Default::default()
            },
            bindings::v4l2_plane_pix_format {
                // Size of V plane
                sizeimage: width * height / 4,
                // Bytes per line for V plane
                bytesperline: width / 2,
                ..Default::default()
            },
            Default::default(),
            Default::default(),
            Default::default(),
            Default::default(),
            Default::default(),
        ],
        ..Default::default()
    };

    v4l2_format {
        type_: queue as u32,
        fmt: bindings::v4l2_format__bindgen_ty_1 { pix_mp },
    }
}

/// Implementations of the ioctls required by a v4l2 CAPTURE device.
impl<Q, HM> VirtioMediaIoctlHandler for EmulatedCamera<Q, HM>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    HM: VirtioMediaHostMemoryMapper,
{
    type Session = EmulatedCameraSession;

    fn enum_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2_fmtdesc> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        if index > 0 {
            return Err(libc::EINVAL);
        }

        Ok(default_fmtdesc(queue))
    }

    fn g_fmt(&mut self, _session: &Self::Session, queue: QueueType) -> IoctlResult<v4l2_format> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        log::info!("g_fmt: returning {}x{}", self.width, self.height);
        Ok(session_fmt(queue, self.width, self.height))
    }

    fn s_fmt(
        &mut self,
        session: &mut Self::Session,
        queue: QueueType,
        format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        
        let pix_mp = unsafe { format.fmt.pix_mp };
        let req_width = pix_mp.width;
        let req_height = pix_mp.height;
        log::info!("s_fmt: requested {}x{}", req_width, req_height);
        
        if SUPPORTED_SIZES.contains(&(req_width, req_height)) {
            self.width = req_width;
            self.height = req_height;
            let mut state = session.state.lock().unwrap();
            state.width = req_width;
            state.height = req_height;
            log::info!("s_fmt: set resolution to {}x{}", req_width, req_height);
        } else {
            log::info!("s_fmt: requested resolution {}x{} not supported, keeping {}x{}", req_width, req_height, self.width, self.height);
        }
        
        Ok(session_fmt(queue, self.width, self.height))
    }

    fn try_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        
        let pix_mp = unsafe { format.fmt.pix_mp };
        let req_width = pix_mp.width;
        let req_height = pix_mp.height;
        log::info!("try_fmt: requested {}x{}", req_width, req_height);
        
        if SUPPORTED_SIZES.contains(&(req_width, req_height)) {
            Ok(session_fmt(queue, req_width, req_height))
        } else {
            Ok(session_fmt(queue, self.width, self.height))
        }
    }

    fn g_parm(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
    ) -> IoctlResult<bindings::v4l2_streamparm> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }

        let mut parm = bindings::v4l2_streamparm {
            type_: queue as u32,
            ..Default::default()
        };

        // SAFETY: The `parm` union is used for the capture type.
        let capture = unsafe { &mut parm.parm.capture };
        capture.capability = bindings::V4L2_CAP_TIMEPERFRAME;
        capture.timeperframe = bindings::v4l2_fract {
            numerator: 1,
            denominator: FRAME_RATE,
        };

        Ok(parm)
    }

    fn s_parm(
        &mut self,
        _session: &mut Self::Session,
        mut parm: bindings::v4l2_streamparm,
    ) -> IoctlResult<bindings::v4l2_streamparm> {
        if parm.type_ != QueueType::VideoCaptureMplane as u32 {
            return Err(libc::EINVAL);
        }

        // We just return the fixed values, ignoring what the user set.
        // SAFETY: The `parm` union is used for the capture type.
        let capture = unsafe { &mut parm.parm.capture };
        capture.capability = bindings::V4L2_CAP_TIMEPERFRAME;
        capture.timeperframe = bindings::v4l2_fract {
            numerator: 1,
            denominator: FRAME_RATE,
        };

        Ok(parm)
    }

    fn reqbufs(
        &mut self,
        session: &mut Self::Session,
        queue: QueueType,
        memory: MemoryType,
        count: u32,
    ) -> IoctlResult<v4l2_requestbuffers> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        if memory != MemoryType::Mmap {
            return Err(libc::EINVAL);
        }
        let mut state = session.state.lock().unwrap();
        if state.streaming {
            return Err(libc::EBUSY);
        }
        // Buffers cannot be requested on a session if there is already another session with
        // allocated buffers.
        match self.active_session {
            Some(id) if id != session.id => return Err(libc::EBUSY),
            _ => (),
        }

        // Reqbufs(0) is an implicit streamoff.
        if count == 0 {
            self.active_session = None;
            state.streaming = false;
            state.stream_count += 1;
            state.queued_buffers.clear();
            let _ = session.wakeup_evt.write(1);
        } else {
            state.queued_buffers.clear();
            for buffer in state.buffers.iter_mut() {
                buffer.set_state(BufferState::New, self.width, self.height);
            }
            self.active_session = Some(session.id);
        }

        let count = std::cmp::min(count, 32);

        for buffer in &state.buffers {
            for plane in &buffer.planes {
                self.mmap_manager.unregister_buffer(plane.offset);
            }
        }

        let size_y = (self.width * self.height) as u64;
        let size_u = (self.width * self.height / 4) as u64;
        let size_v = (self.width * self.height / 4) as u64;

        state.width = self.width;
        state.height = self.height;

        state.buffers = (0..count)
            .map(|i| -> std::result::Result<Buffer, i32> {
                let fd_y = MemFdBuffer::new(size_y).map_err(|e| {
                    log::error!("failed to allocate MMAP buffer Y: {:#}", e);
                    libc::ENOMEM
                })?;
                let fd_u = MemFdBuffer::new(size_u).map_err(|e| {
                    log::error!("failed to allocate MMAP buffer U: {:#}", e);
                    libc::ENOMEM
                })?;
                let fd_v = MemFdBuffer::new(size_v).map_err(|e| {
                    log::error!("failed to allocate MMAP buffer V: {:#}", e);
                    libc::ENOMEM
                })?;
                let offset_y = self
                    .mmap_manager
                    .register_buffer(None, size_y as u32)
                    .map_err(|_| libc::EINVAL)?;
                let offset_u = self
                    .mmap_manager
                    .register_buffer(None, size_u as u32)
                    .map_err(|_| libc::EINVAL)?;
                let offset_v = self
                    .mmap_manager
                    .register_buffer(None, size_v as u32)
                    .map_err(|_| libc::EINVAL)?;

                let planes_structs = [
                    Plane {
                        fd: fd_y,
                        offset: offset_y,
                    },
                    Plane {
                        fd: fd_u,
                        offset: offset_u,
                    },
                    Plane {
                        fd: fd_v,
                        offset: offset_v,
                    },
                ];

                let mut v4l2_buffer = V4l2Buffer::new(queue, i, MemoryType::Mmap);
                // TODO(b/520129053): The v4l2r crate's `set_num_planes()` doesn't allow sizing-up.
                // So we directly set the plane count to 3 on the raw buffer so that the plane
                // iterator yields all 3 planes (Y, U, V).
                unsafe {
                    (*v4l2_buffer.as_mut_ptr()).length = 3;
                }
                if let V4l2PlanesWithBackingMut::Mmap(mut planes) =
                    v4l2_buffer.planes_with_backing_iter_mut()
                {
                    let mut plane = planes.next().unwrap();
                    plane.set_mem_offset(offset_y);
                    *plane.length = size_y as u32;

                    let mut plane = planes.next().unwrap();
                    plane.set_mem_offset(offset_u);
                    *plane.length = size_u as u32;

                    let mut plane = planes.next().unwrap();
                    plane.set_mem_offset(offset_v);
                    *plane.length = size_v as u32;
                } else {
                    // SAFETY: we have just set the buffer type to MMAP. Reaching this point means a bug in
                    // the code.
                    panic!()
                }
                v4l2_buffer.set_field(BufferField::None);
                v4l2_buffer.set_flags(BufferFlags::TIMESTAMP_MONOTONIC);

                Ok(Buffer::new(v4l2_buffer, planes_structs))
            })
            .collect::<std::result::Result<_, _>>()?;

        Ok(v4l2_requestbuffers {
            count,
            type_: queue as u32,
            memory: memory as u32,
            capabilities: (BufferCapabilities::SUPPORTS_MMAP
                | BufferCapabilities::SUPPORTS_ORPHANED_BUFS)
                .bits(),
            // This device does not support V4L2_BUF_CAP_SUPPORTS_MMAP_CACHE_HINTS,
            // so the flags field in v4l2_requestbuffers must be 0.
            flags: 0,
            ..Default::default()
        })
    }

    fn querybuf(
        &mut self,
        session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2r::ioctl::V4l2Buffer> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        let state = session.state.lock().unwrap();
        let buffer = state.buffers.get(index as usize).ok_or(libc::EINVAL)?;

        Ok(buffer.v4l2_buffer.clone())
    }

    fn qbuf(
        &mut self,
        session: &mut Self::Session,
        buffer: v4l2r::ioctl::V4l2Buffer,
        _guest_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<v4l2r::ioctl::V4l2Buffer> {
        let mut state = session.state.lock().unwrap();
        let host_buffer = state
            .buffers
            .get_mut(buffer.index() as usize)
            .ok_or(libc::EINVAL)?;
        // Attempt to queue already queued buffer.
        if matches!(host_buffer.state, BufferState::Incoming) {
            return Err(libc::EINVAL);
        }

        host_buffer.set_state(BufferState::Incoming, self.width, self.height);
        let buffer = host_buffer.v4l2_buffer.clone();
        state.queued_buffers.push_back(buffer.index() as usize);
        let _ = session.wakeup_evt.write(1);

        Ok(buffer)
    }

    fn streamon(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        let mut state = session.state.lock().unwrap();
        if state.buffers.is_empty() {
            return Err(libc::EINVAL);
        }
        state.streaming = true;
        let _ = session.wakeup_evt.write(1);

        Ok(())
    }

    fn streamoff(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        let mut state = session.state.lock().unwrap();
        state.streaming = false;
        state.stream_count += 1;
        state.queued_buffers.clear();
        for buffer in state.buffers.iter_mut() {
            buffer.set_state(BufferState::New, self.width, self.height);
        }
        let _ = session.wakeup_evt.write(1);

        Ok(())
    }

    fn g_input(&mut self, _session: &Self::Session) -> IoctlResult<i32> {
        Ok(0)
    }

    fn s_input(&mut self, _session: &mut Self::Session, input: i32) -> IoctlResult<i32> {
        if input != 0 { Err(libc::EINVAL) } else { Ok(0) }
    }

    fn enuminput(
        &mut self,
        _session: &Self::Session,
        index: u32,
    ) -> IoctlResult<bindings::v4l2_input> {
        INPUTS.get(index as usize).map(|&x| x).ok_or(libc::EINVAL)
    }

    fn enum_framesizes(
        &mut self,
        _session: &Self::Session,
        index: u32,
        pixel_format: u32,
    ) -> IoctlResult<bindings::v4l2_frmsizeenum> {
        log::info!("enum_framesizes: index {}, format {}", index, pixel_format);
        if pixel_format != PIXELFORMAT {
            log::info!("enum_framesizes: format {} not supported", pixel_format);
            return Err(libc::EINVAL);
        }
        
        let &(width, height) = SUPPORTED_SIZES.get(index as usize).ok_or_else(|| {
            log::info!("enum_framesizes: index {} out of bounds", index);
            libc::EINVAL
        })?;

        log::info!("enum_framesizes: returning {}x{}", width, height);
        Ok(bindings::v4l2_frmsizeenum {
            index,
            pixel_format,
            type_: bindings::v4l2_frmsizetypes_V4L2_FRMSIZE_TYPE_DISCRETE,
            __bindgen_anon_1: bindings::v4l2_frmsizeenum__bindgen_ty_1 {
                discrete: bindings::v4l2_frmsize_discrete {
                    width,
                    height,
                },
            },
            ..Default::default()
        })
    }

    fn enum_frameintervals(
        &mut self,
        _session: &Self::Session,
        index: u32,
        pixel_format: u32,
        width: u32,
        height: u32,
    ) -> IoctlResult<bindings::v4l2_frmivalenum> {
        log::info!("enum_frameintervals: index {}, format {}, {}x{}", index, pixel_format, width, height);
        if pixel_format != PIXELFORMAT {
            log::info!("enum_frameintervals: format {} not supported", pixel_format);
            return Err(libc::EINVAL);
        }
        if !SUPPORTED_SIZES.contains(&(width, height)) {
            log::info!("enum_frameintervals: size {}x{} not supported", width, height);
            return Err(libc::EINVAL);
        }
        if index > 0 {
            log::info!("enum_frameintervals: index {} > 0 not supported", index);
            return Err(libc::EINVAL);
        }

        Ok(bindings::v4l2_frmivalenum {
            index,
            pixel_format,
            width,
            height,
            type_: bindings::v4l2_frmivaltypes_V4L2_FRMIVAL_TYPE_DISCRETE,
            __bindgen_anon_1: bindings::v4l2_frmivalenum__bindgen_ty_1 {
                discrete: bindings::v4l2_fract {
                    numerator: 1,
                    denominator: FRAME_RATE,
                },
            },
            ..Default::default()
        })
    }

    /// https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html#control-flags
    fn query_ext_ctrl(
        &mut self,
        _session: &Self::Session,
        id: CtrlId,
        flags: QueryCtrlFlags,
    ) -> IoctlResult<bindings::v4l2_query_ext_ctrl> {
        let id: u32 = unsafe { std::mem::transmute(id) };
        if flags.contains(QueryCtrlFlags::NEXT) {
            if id < bindings::V4L2_CID_USER_CLASS {
                return Ok(self.user_class_query_ext_ctrl());
            } else if id < bindings::V4L2_CID_GAIN {
                return Ok(self.gain_query_ext_ctrl());
            } else if id < bindings::V4L2_CID_CAMERA_CLASS {
                return Ok(self.camera_class_query_ext_ctrl());
            } else if id < CID_LENS_FACING {
                return Ok(self.lens_facing_query_ext_ctrl());
            }
        } else {
            if id == bindings::V4L2_CID_USER_CLASS {
                return Ok(self.user_class_query_ext_ctrl());
            } else if id == bindings::V4L2_CID_GAIN {
                return Ok(self.gain_query_ext_ctrl());
            } else if id == bindings::V4L2_CID_CAMERA_CLASS {
                return Ok(self.camera_class_query_ext_ctrl());
            } else if id == CID_LENS_FACING {
                return Ok(self.lens_facing_query_ext_ctrl());
            }
        }
        return Err(libc::EINVAL);
    }

    fn g_ext_ctrls(
        &mut self,
        _session: &Self::Session,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        // Validate control class. Also handles class support queries when count == 0.
        match which {
            CtrlWhich::Current | CtrlWhich::Default => {}
            CtrlWhich::Class(class) => {
                if class != bindings::V4L2_CTRL_CLASS_USER && class != bindings::V4L2_CTRL_CLASS_CAMERA {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
            _ => {
                ctrls.error_idx = ctrls.count;
                return Err(libc::EINVAL);
            }
        }

        // Ensure all requested controls belong to the selected class.
        if let CtrlWhich::Class(class_id) = which {
            for (_idx, ctrl) in ctrl_array.iter().enumerate() {
                if v4l2_ctrl_id2which(ctrl.id) != class_id {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
        }

        // Process controls. Class controls are write-only headers and must fail on read.
        for (_idx, ctrl) in ctrl_array.iter_mut().enumerate() {
            match ctrl.id {
                bindings::V4L2_CID_USER_CLASS | bindings::V4L2_CID_CAMERA_CLASS => {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EACCES);
                }
                bindings::V4L2_CID_GAIN => {
                    ctrl.__bindgen_anon_1.value = match which {
                        CtrlWhich::Default => Gain::DEFAULT,
                        _ => self.controls.gain.value(),
                    };
                }
                CID_LENS_FACING => {
                    ctrl.__bindgen_anon_1.value = self.controls.lens_facing as i32;
                }
                _ => {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
        }
        ctrls.error_idx = ctrls.count;
        Ok(())
    }

    fn try_ext_ctrls(
        &mut self,
        _session: &Self::Session,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        // Validate control class. Setting defaults is not allowed for TRY/SET.
        match which {
            CtrlWhich::Current => {}
            CtrlWhich::Class(class) => {
                if class != bindings::V4L2_CTRL_CLASS_USER && class != bindings::V4L2_CTRL_CLASS_CAMERA {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
            _ => {
                ctrls.error_idx = ctrls.count;
                return Err(libc::EINVAL);
            }
        }

        // Ensure all requested controls belong to the selected class.
        if let CtrlWhich::Class(class_id) = which {
            for (idx, ctrl) in ctrl_array.iter().enumerate() {
                if v4l2_ctrl_id2which(ctrl.id) != class_id {
                    ctrls.error_idx = idx as u32;
                    return Err(libc::EINVAL);
                }
            }
        }

        // Validate control values. Class controls are read-only headers and must fail on write/try.
        for (idx, ctrl) in ctrl_array.iter_mut().enumerate() {
            match ctrl.id {
                bindings::V4L2_CID_USER_CLASS | bindings::V4L2_CID_CAMERA_CLASS => {
                    ctrls.error_idx = idx as u32;
                    return Err(libc::EACCES);
                }
                bindings::V4L2_CID_GAIN => {
                    let value = unsafe { ctrl.__bindgen_anon_1.value };
                    if let Err(err) = Gain::new(value) {
                        ctrls.error_idx = idx as u32;
                        return Err(err);
                    }
                }
                CID_LENS_FACING => {
                    ctrls.error_idx = idx as u32;
                    return Err(libc::EACCES);
                }
                _ => {
                    ctrls.error_idx = idx as u32;
                    return Err(libc::EINVAL);
                }
            }
        }
        ctrls.error_idx = ctrls.count;
        Ok(())
    }

    fn s_ext_ctrls(
        &mut self,
        session: &mut Self::Session,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        // Validate control class. Setting defaults is not allowed for TRY/SET.
        match which {
            CtrlWhich::Current => {}
            CtrlWhich::Class(class) => {
                if class != bindings::V4L2_CTRL_CLASS_USER && class != bindings::V4L2_CTRL_CLASS_CAMERA {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
            _ => {
                ctrls.error_idx = ctrls.count;
                return Err(libc::EINVAL);
            }
        }

        // Ensure all requested controls belong to the selected class.
        if let CtrlWhich::Class(class_id) = which {
            for (_idx, ctrl) in ctrl_array.iter().enumerate() {
                if v4l2_ctrl_id2which(ctrl.id) != class_id {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
        }

        // Apply control values. Class controls are read-only headers and must fail on write/try.
        for (_idx, ctrl) in ctrl_array.iter_mut().enumerate() {
            match ctrl.id {
                bindings::V4L2_CID_USER_CLASS | bindings::V4L2_CID_CAMERA_CLASS => {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EACCES);
                }
                bindings::V4L2_CID_GAIN => {
                    let value = unsafe { ctrl.__bindgen_anon_1.value };
                    match Gain::new(value) {
                        Ok(gain) => {
                            if self.controls.gain != gain {
                                self.controls.gain = gain;
                                {
                                    let mut state = session.state.lock().unwrap();
                                    state.controls.gain = gain;
                                }
                                let ctrl_event = bindings::v4l2_event {
                                    type_: bindings::V4L2_EVENT_CTRL,
                                    id: bindings::V4L2_CID_GAIN,
                                    ..Default::default()
                                };
                                self.evt_queue.lock().unwrap().send_event(V4l2Event::Event(SessionEvent::new(
                                    session.id, ctrl_event,
                                )));
                            }
                        }
                        Err(err) => {
                            ctrls.error_idx = ctrls.count;
                            return Err(err);
                        }
                    }
                }
                CID_LENS_FACING => {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EACCES);
                }
                _ => {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
        }
        ctrls.error_idx = ctrls.count;
        Ok(())
    }

    fn subscribe_event(
        &mut self,
        session: &mut Self::Session,
        event: V4l2EventType,
        flags: SubscribeEventFlags,
    ) -> IoctlResult<()> {
        if !flags.contains(SubscribeEventFlags::SEND_INITIAL) {
            return Err(libc::EINVAL);
        }
        match event {
            V4l2EventType::Ctrl(id) => match id {
                CID_LENS_FACING | bindings::V4L2_CID_GAIN => {
                    let ctrl_event = bindings::v4l2_event {
                        type_: bindings::V4L2_EVENT_CTRL,
                        id,
                        ..Default::default()
                    };
                    self.evt_queue
                        .lock()
                        .unwrap()
                        .send_event(V4l2Event::Event(SessionEvent::new(session.id, ctrl_event)));
                    Ok(())
                }
                bindings::V4L2_CID_USER_CLASS | bindings::V4L2_CID_CAMERA_CLASS => {
                    // Subscription succeeds, but we do not send any initial event.
                    Ok(())
                }
                _ => Err(libc::EINVAL),
            },
            _ => Err(libc::EINVAL),
        }
    }

    fn unsubscribe_event(
        &mut self,
        _session: &mut Self::Session,
        event: bindings::v4l2_event_subscription,
    ) -> IoctlResult<()> {
        return if event.type_ == bindings::V4L2_EVENT_CTRL
            && (event.id == CID_LENS_FACING
                || event.id == bindings::V4L2_CID_GAIN
                || event.id == bindings::V4L2_CID_USER_CLASS
                || event.id == bindings::V4L2_CID_CAMERA_CLASS)
        {
            Ok(())
        } else {
            Err(libc::EINVAL)
        };
    }
}
