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

use anyhow::{Context, Result as AnyhowResult};
use std::io::Result as IoResult;
use std::os::fd::BorrowedFd;
use std::str::FromStr;
use std::sync::{Arc, Mutex};

use crate::capture_channel::CaptureChannel;
use crate::format::Yuv420m;
use crate::memory::{BufferMemoryHandler, QbufRequest, QueuedBuffer};
use crate::pattern::FramePattern;
use crate::pattern::julia_set::JuliaSet;
use crate::pattern::pulse::Pulse;
use crate::pattern::smpte::SmpteBars;

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
use v4l2r::memory::MemoryType;
use virtio_media::VirtioMediaDevice;
use virtio_media::VirtioMediaDeviceSession;
use virtio_media::VirtioMediaEventQueue;
use virtio_media::io::ReadFromDescriptorChain;
use virtio_media::io::WriteToDescriptorChain;
use virtio_media::ioctl::IoctlResult;
use virtio_media::ioctl::VirtioMediaIoctlHandler;
use virtio_media::ioctl::virtio_media_dispatch_ioctl;
use virtio_media::protocol::DequeueBufferEvent;
use virtio_media::protocol::SessionEvent;
use virtio_media::protocol::SgEntry;
use virtio_media::protocol::V4l2Event;
use virtio_media::protocol::V4l2Ioctl;

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
            _ => Err(format!(
                "Invalid lens facing: {}. Expected FRONT, BACK, or EXTERNAL",
                s
            )),
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

/// Test pattern selectable through `V4L2_CID_TEST_PATTERN`.
///
/// The discriminants double as the menu indices reported by `VIDIOC_QUERYMENU`, so they
/// must stay contiguous and start at [`TestPattern::MIN`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TestPattern {
    Pulse = 0,
    SmpteBars = 1,
    JuliaSet = 2,
}

impl TestPattern {
    /// Lowest menu index, reported as `minimum` for `V4L2_CID_TEST_PATTERN`.
    const MIN: i32 = TestPattern::Pulse as i32;
    /// Highest menu index, reported as `maximum` for `V4L2_CID_TEST_PATTERN`.
    const MAX: i32 = TestPattern::JuliaSet as i32;
    /// Pattern selected until the guest asks for something else.
    const DEFAULT: TestPattern = TestPattern::Pulse;

    /// Human readable name reported by `VIDIOC_QUERYMENU`.
    fn name(self) -> &'static str {
        match self {
            TestPattern::Pulse => "Pulse",
            TestPattern::SmpteBars => "SMPTE + Bouncing Box",
            TestPattern::JuliaSet => "Animated Julia Set",
        }
    }

    /// Frame generator backing this pattern.
    pub(crate) fn generator(self) -> &'static dyn FramePattern {
        match self {
            TestPattern::Pulse => &Pulse,
            TestPattern::SmpteBars => &SmpteBars,
            TestPattern::JuliaSet => &JuliaSet,
        }
    }
}

impl TryFrom<i32> for TestPattern {
    /// Raw `errno` reported to the guest for an out-of-range menu index.
    type Error = i32;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(TestPattern::Pulse),
            1 => Ok(TestPattern::SmpteBars),
            2 => Ok(TestPattern::JuliaSet),
            _ => Err(libc::ERANGE),
        }
    }
}

/// State of all camera controls.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CameraControls {
    pub lens_facing: LensFacing,
    pub gain: Gain,
    pub test_pattern: TestPattern,
}

impl CameraControls {
    pub fn new(lens_facing: LensFacing) -> Self {
        Self {
            lens_facing,
            gain: Gain::default(),
            test_pattern: TestPattern::DEFAULT,
        }
    }
}

/// Formats an ASCII name for a `v4l2_query_ext_ctrl` or `v4l2_querymenu` response, NUL-padding
/// the remainder of the 32-byte array. If `name` is longer than 31 bytes it is truncated and
/// the last byte is still forced to NUL.
fn ctrl_name(name: &str) -> [u8; 32] {
    let mut out = [0u8; 32];
    let copy_len = std::cmp::min(name.len(), 31);
    out[..copy_len].copy_from_slice(&name.as_bytes()[..copy_len]);
    out
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

/// Builds the V4L2 representation of buffer `index` of `queue` before its [`BufferMemoryHandler`]
/// fills in the memory specific plane fields.
fn new_v4l2_buffer(
    queue: QueueType,
    index: u32,
    memory: MemoryType,
    plane_sizes: [u32; Yuv420m::NUM_PLANES],
) -> V4l2Buffer {
    let mut v4l2_buffer = V4l2Buffer::new(queue, index, memory);
    // TODO(b/520129053): The v4l2r crate's `set_num_planes()` doesn't allow sizing-up.
    // So we directly set the plane count on the raw buffer so that the plane
    // iterator yields all planes.
    unsafe {
        (*v4l2_buffer.as_mut_ptr()).length = Yuv420m::NUM_PLANES as u32;
    }
    for (plane, size) in v4l2_buffer.planes_iter_mut().zip(plane_sizes) {
        *plane.length = size;
    }
    v4l2_buffer.set_field(BufferField::None);
    v4l2_buffer.set_flags(BufferFlags::TIMESTAMP_MONOTONIC);
    v4l2_buffer
}

/// Information about a single buffer.
struct Buffer<A> {
    /// Current state of the buffer.
    state: BufferState,
    /// V4L2 representation of this buffer to be sent to the guest when requested.
    v4l2_buffer: V4l2Buffer,
    /// Memory allocated for the planes of the buffer, see [`BufferMemoryHandler::Allocation`].
    allocation: A,
}

impl<A> Buffer<A> {
    fn new(v4l2_buffer: V4l2Buffer, allocation: A) -> Self {
        Self {
            state: BufferState::New,
            v4l2_buffer,
            allocation,
        }
    }

    /// Sets `bytesused` to zero on every plane.
    fn set_bytes_used_to_zero(&mut self) {
        for plane in self.v4l2_buffer.planes_iter_mut() {
            *plane.bytesused = 0;
        }
    }

    /// Sets `bytesused` to `length` on every plane.
    fn set_bytes_used_to_length(&mut self) {
        for plane in self.v4l2_buffer.planes_iter_mut() {
            *plane.bytesused = *plane.length;
        }
    }

    /// Update the state of the buffer as well as its V4L2 representation.
    fn set_state(&mut self, state: BufferState) {
        let mut flags = self.v4l2_buffer.flags();
        match state {
            BufferState::New => {
                self.set_bytes_used_to_zero();
                flags &= !BufferFlags::QUEUED;
            }
            BufferState::Incoming => {
                self.set_bytes_used_to_zero();
                flags |= BufferFlags::QUEUED;
            }
            BufferState::Outgoing { sequence } => {
                self.set_bytes_used_to_length();
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

/// Session data of [`EmulatedCamera`].
pub struct EmulatedCameraSession<A> {
    /// Id of the session.
    id: u32,
    /// Buffers currently allocated for this session.
    buffers: Arc<Mutex<Vec<Buffer<A>>>>,
    /// Is the session currently streaming?
    streaming: Arc<Mutex<bool>>,
    /// Virtual capture channel driving capture and frame pacing for this session.
    channel: CaptureChannel,
    /// Handle to the capture completed listener thread.
    capture_completed_thread: Option<std::thread::JoinHandle<()>>,
}

impl<A> VirtioMediaDeviceSession for EmulatedCameraSession<A> {
    fn poll_fd(&self) -> Option<BorrowedFd<'_>> {
        None
    }
}

/// Emulated camera used for testing Android camera stack.
///
/// This implementation looks forward to have feature parity with existing Android Guest Emulated
/// camera https://cs.android.com/android/platform/superproject/main/+/main:hardware/google/camera/devices/EmulatedCamera/
///
/// `M` decides how buffers are backed, see [`BufferMemoryHandler`].
pub struct EmulatedCamera<Q: VirtioMediaEventQueue, M: BufferMemoryHandler> {
    /// Memory handler of the capture buffers.
    buffer_memory_handler: M,
    /// ID of the session with allocated buffers, if any.
    ///
    /// v4l2-compliance checks that only a single session can have allocated buffers at a given
    /// time, since that's how actual hardware works - no two sessions can access a camera at the
    /// same time. It will fails if we allow simultaneous sessions to be active, so we need this
    /// artificial limitation to make it pass fully.
    active_session: Option<u32>,
    /// Queue used to send events to the guest.
    evt_queue: Arc<Mutex<Q>>,
    /// Camera controls (gain, lens facing, test pattern).
    controls: CameraControls,
    /// Width of the video.
    width: u32,
    /// Height of the video.
    height: u32,
}

impl<Q, M> EmulatedCamera<Q, M>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    M: BufferMemoryHandler,
{
    pub fn new(evt_queue: Q, buffer_memory_handler: M, lens_facing: LensFacing) -> Self {
        Self {
            buffer_memory_handler,
            active_session: None,
            evt_queue: Arc::new(Mutex::new(evt_queue)),
            controls: CameraControls::new(lens_facing),
            width: WIDTH,
            height: HEIGHT,
        }
    }

    fn plane_sizes(&self) -> [u32; Yuv420m::NUM_PLANES] {
        Yuv420m::plane_sizes(self.width, self.height)
    }

    fn free_buffers(&mut self, buffers: &[Buffer<M::Allocation>]) {
        for buffer in buffers {
            self.buffer_memory_handler.free(&buffer.allocation);
        }
    }

    fn new_buffer(&mut self, queue: QueueType, index: u32) -> IoctlResult<Buffer<M::Allocation>> {
        let plane_sizes = self.plane_sizes();
        let mut v4l2_buffer = new_v4l2_buffer(queue, index, M::MEMORY_TYPE, plane_sizes);
        let allocation = self
            .buffer_memory_handler
            .alloc(&mut v4l2_buffer, plane_sizes)?;
        Ok(Buffer::new(v4l2_buffer, allocation))
    }

    fn lens_facing_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        let lens_facing = self.controls.lens_facing;
        bindings::v4l2_query_ext_ctrl {
            id: CID_LENS_FACING,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER,
            name: ctrl_name("LENS_FACING").map(|b| b as i8),
            minimum: LensFacing::Front as i64,
            maximum: LensFacing::External as i64,
            step: 1,
            default_value: lens_facing as i64,
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

    fn image_proc_class_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        bindings::v4l2_query_ext_ctrl {
            id: bindings::V4L2_CID_IMAGE_PROC_CLASS,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_CTRL_CLASS,
            name: ctrl_name("Image Processing Controls").map(|b| b as i8),
            minimum: 0,
            maximum: 0,
            step: 0,
            default_value: 0,
            // A control class holds no value of its own, so it can be neither read nor
            // written. This mirrors what `v4l2_ctrl_fill()` does in the kernel.
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY | bindings::V4L2_CTRL_FLAG_WRITE_ONLY,
            elems: 1,
            elem_size: std::mem::size_of::<u32>() as u32,
            ..Default::default()
        }
    }

    fn test_pattern_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        bindings::v4l2_query_ext_ctrl {
            id: bindings::V4L2_CID_TEST_PATTERN,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_MENU,
            name: ctrl_name("Test Pattern").map(|b| b as i8),
            minimum: TestPattern::MIN as i64,
            maximum: TestPattern::MAX as i64,
            step: 1,
            default_value: TestPattern::DEFAULT as i64,
            flags: 0,
            elems: 1,
            elem_size: std::mem::size_of::<u32>() as u32,
            ..Default::default()
        }
    }

    /// Builds the `V4L2_EVENT_CTRL` payload describing the current state of `id`.
    fn ctrl_event(controls: &CameraControls, id: u32) -> IoctlResult<bindings::v4l2_event> {
        let mut event = bindings::v4l2_event {
            type_: bindings::V4L2_EVENT_CTRL,
            id,
            ..Default::default()
        };
        match id {
            CID_LENS_FACING => {
                event.u.ctrl.type_ = bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER;
                event.u.ctrl.__bindgen_anon_1.value = controls.lens_facing as i32;
                event.u.ctrl.minimum = LensFacing::Front as i32;
                event.u.ctrl.maximum = LensFacing::External as i32;
                event.u.ctrl.step = 1;
                event.u.ctrl.default_value = LensFacing::Front as i32;
            }
            bindings::V4L2_CID_GAIN => {
                event.u.ctrl.type_ = bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER;
                event.u.ctrl.__bindgen_anon_1.value = controls.gain.value();
                event.u.ctrl.minimum = Gain::MIN;
                event.u.ctrl.maximum = Gain::MAX;
                event.u.ctrl.step = 1;
                event.u.ctrl.default_value = Gain::DEFAULT;
            }
            bindings::V4L2_CID_TEST_PATTERN => {
                event.u.ctrl.type_ = bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_MENU;
                event.u.ctrl.__bindgen_anon_1.value = controls.test_pattern as i32;
                event.u.ctrl.minimum = TestPattern::MIN;
                event.u.ctrl.maximum = TestPattern::MAX;
                event.u.ctrl.step = 1;
                event.u.ctrl.default_value = TestPattern::DEFAULT as i32;
            }
            _ => return Err(libc::EINVAL),
        }
        // Listeners gate on this mask, so an event without it is silently ignored.
        event.u.ctrl.changes = bindings::V4L2_EVENT_CTRL_CH_VALUE;
        Ok(event)
    }

    /// Applies `pattern`, signalling subscribers if the value actually changed.
    fn set_test_pattern(&mut self, session_id: u32, pattern: TestPattern) -> IoctlResult<()> {
        if self.controls.test_pattern == pattern {
            return Ok(());
        }
        self.controls.test_pattern = pattern;
        let event = Self::ctrl_event(&self.controls, bindings::V4L2_CID_TEST_PATTERN)?;
        self.evt_queue
            .lock()
            .unwrap()
            .send_event(V4l2Event::Event(SessionEvent::new(session_id, event)));
        Ok(())
    }
}

impl<Q, M, Reader, Writer> VirtioMediaDevice<Reader, Writer> for EmulatedCamera<Q, M>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    M: BufferMemoryHandler,
    Reader: ReadFromDescriptorChain,
    Writer: WriteToDescriptorChain,
{
    type Session = EmulatedCameraSession<M::Allocation>;

    fn new_session(&mut self, session_id: u32) -> std::result::Result<Self::Session, i32> {
        let session = (|| -> AnyhowResult<Self::Session> {
            let buffers = Arc::new(Mutex::new(Vec::<Buffer<M::Allocation>>::new()));
            let streaming = Arc::new(Mutex::new(false));
            let (capture_completed_tx, capture_completed_rx) = std::sync::mpsc::channel();

            let channel =
                CaptureChannel::new(session_id, self.controls.clone(), capture_completed_tx)
                    .context("Failed to initialize capture channel")?;

            let buffers_clone = Arc::clone(&buffers);
            let streaming_clone = Arc::clone(&streaming);
            let evt_queue_clone = Arc::clone(&self.evt_queue);

            let capture_completed_thread = std::thread::Builder::new()
                .name(format!("capture-completed-thread{}", session_id))
                .spawn(move || {
                    while let Ok(ev) = capture_completed_rx.recv() {
                        let is_streaming = *streaming_clone.lock().unwrap();
                        if !is_streaming {
                            continue;
                        }
                        let mut buffers = buffers_clone.lock().unwrap();
                        if let Some(buf) = buffers.get_mut(ev.buffer_id) {
                            buf.set_state(BufferState::Outgoing {
                                sequence: ev.sequence,
                            });
                            let v4l2_buf = buf.v4l2_buffer.clone();
                            evt_queue_clone
                                .lock()
                                .unwrap()
                                .send_event(V4l2Event::DequeueBuffer(DequeueBufferEvent::new(
                                    session_id, v4l2_buf,
                                )));
                        }
                    }
                })
                .context("Failed to spawn capture completed thread")?;

            Ok(EmulatedCameraSession {
                id: session_id,
                buffers,
                streaming,
                channel,
                capture_completed_thread: Some(capture_completed_thread),
            })
        })();

        session.map_err(|e| {
            log::error!("Failed to create session {}: {:#}", session_id, e);
            libc::EIO
        })
    }

    fn close_session(&mut self, mut session: Self::Session) {
        session.channel.shutdown();
        if let Some(handle) = session.capture_completed_thread.take() {
            let _ = handle.join();
        }

        // Nothing to cleanup when `close_session` is called for sessions without
        // allocated buffers, hence the early return.
        if self.active_session != Some(session.id) {
            return;
        }

        self.active_session = None;

        let buffers = session.buffers.lock().unwrap();
        self.free_buffers(&buffers);
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
        let buffers = session.buffers.lock().unwrap();
        self.buffer_memory_handler
            .mmap(buffers.iter().map(|b| &b.allocation), flags, offset)
    }

    fn do_munmap(&mut self, guest_addr: u64) -> std::result::Result<(), i32> {
        self.buffer_memory_handler.munmap(guest_addr)
    }
}

// Use an offset for virtio-media custom camera class control id values.
const CID_OFFSET: u32 = bindings::V4L2_CID_CAMERA_CLASS_BASE + 0x100;
const CID_LENS_FACING: u32 = CID_OFFSET + 1;

const WIDTH: u32 = 640;
const HEIGHT: u32 = 480;
const FRAME_RATE: u32 = 30;

const SUPPORTED_SIZES: [(u32, u32); 3] = [(320, 240), (640, 480), (1280, 720)];

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
        pixelformat: Yuv420m::PIXELFORMAT,
        ..Default::default()
    }
}

/// `V4L2_BUF_CAP_SUPPORTS_*` flag advertising `memory` in `VIDIOC_REQBUFS`.
fn memory_type_to_buffer_capability(memory: MemoryType) -> BufferCapabilities {
    match memory {
        MemoryType::Mmap => BufferCapabilities::SUPPORTS_MMAP,
        MemoryType::UserPtr => BufferCapabilities::SUPPORTS_USERPTR,
        MemoryType::DmaBuf => BufferCapabilities::SUPPORTS_DMABUF,
        MemoryType::Overlay => BufferCapabilities::empty(),
    }
}

fn session_fmt(queue: QueueType, width: u32, height: u32) -> v4l2_format {
    let pix_mp = bindings::v4l2_pix_format_mplane {
        width,
        height,
        pixelformat: Yuv420m::PIXELFORMAT,
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
impl<Q, M> VirtioMediaIoctlHandler for EmulatedCamera<Q, M>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    M: BufferMemoryHandler,
{
    type Session = EmulatedCameraSession<M::Allocation>;

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
        _session: &mut Self::Session,
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
            log::info!("s_fmt: set resolution to {}x{}", req_width, req_height);
        } else {
            log::info!(
                "s_fmt: requested resolution {}x{} not supported, keeping {}x{}",
                req_width,
                req_height,
                self.width,
                self.height
            );
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
        if memory != M::MEMORY_TYPE {
            return Err(libc::EINVAL);
        }
        if *session.streaming.lock().unwrap() {
            return Err(libc::EBUSY);
        }
        // Buffers cannot be requested on a session if there is already another session with
        // allocated buffers.
        match self.active_session {
            Some(id) if id != session.id => return Err(libc::EBUSY),
            _ => (),
        }

        let mut buffers = session.buffers.lock().unwrap();

        // Reqbufs(0) is an implicit streamoff.
        if count == 0 {
            self.active_session = None;
            *session.streaming.lock().unwrap() = false;
            let _ = session.channel.stop_capture();
        } else {
            for buffer in buffers.iter_mut() {
                buffer.set_state(BufferState::New);
            }
            self.active_session = Some(session.id);
        }

        let count = std::cmp::min(count, 32);

        self.free_buffers(&buffers);

        *buffers = (0..count)
            .map(|i| self.new_buffer(queue, i))
            .collect::<std::result::Result<_, _>>()?;

        Ok(v4l2_requestbuffers {
            count,
            type_: queue as u32,
            memory: memory as u32,
            capabilities: (memory_type_to_buffer_capability(M::MEMORY_TYPE)
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
        let buffers = session.buffers.lock().unwrap();
        let buffer = buffers.get(index as usize).ok_or(libc::EINVAL)?;

        Ok(buffer.v4l2_buffer.clone())
    }

    fn qbuf(
        &mut self,
        session: &mut Self::Session,
        buffer: v4l2r::ioctl::V4l2Buffer,
        guest_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<v4l2r::ioctl::V4l2Buffer> {
        let buffer_idx = buffer.index() as usize;
        if buffer.memory() != M::MEMORY_TYPE {
            return Err(libc::EINVAL);
        }

        let mut buffers = session.buffers.lock().unwrap();
        let host_buffer = buffers.get_mut(buffer_idx).ok_or(libc::EINVAL)?;
        // Attempt to queue already queued buffer.
        if matches!(host_buffer.state, BufferState::Incoming) {
            return Err(libc::EINVAL);
        }

        let plane_sizes = self.plane_sizes();
        let QueuedBuffer { buffer, sink } = self.buffer_memory_handler.queue(
            &host_buffer.allocation,
            host_buffer.v4l2_buffer.clone(),
            QbufRequest {
                buffer,
                guest_regions,
            },
            plane_sizes,
        )?;
        host_buffer.v4l2_buffer = buffer;

        host_buffer.set_state(BufferState::Incoming);
        let buffer = host_buffer.v4l2_buffer.clone();
        drop(buffers);

        session
            .channel
            .queue_buffer(buffer_idx, sink)
            .map_err(|_| libc::EIO)?;

        Ok(buffer)
    }

    fn streamon(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        let buffers = session.buffers.lock().unwrap();
        if buffers.is_empty() {
            return Err(libc::EINVAL);
        }
        drop(buffers);
        *session.streaming.lock().unwrap() = true;
        session
            .channel
            .start_capture(self.width, self.height)
            .map_err(|_| libc::EIO)?;

        Ok(())
    }

    fn streamoff(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != QueueType::VideoCaptureMplane {
            return Err(libc::EINVAL);
        }
        *session.streaming.lock().unwrap() = false;
        let mut buffers = session.buffers.lock().unwrap();
        for buffer in buffers.iter_mut() {
            buffer.set_state(BufferState::New);
        }
        drop(buffers);
        session.channel.stop_capture().map_err(|_| libc::EIO)?;

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
        if pixel_format != Yuv420m::PIXELFORMAT {
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
                discrete: bindings::v4l2_frmsize_discrete { width, height },
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
        log::info!(
            "enum_frameintervals: index {}, format {}, {}x{}",
            index,
            pixel_format,
            width,
            height
        );
        if pixel_format != Yuv420m::PIXELFORMAT {
            log::info!("enum_frameintervals: format {} not supported", pixel_format);
            return Err(libc::EINVAL);
        }
        if !SUPPORTED_SIZES.contains(&(width, height)) {
            log::info!(
                "enum_frameintervals: size {}x{} not supported",
                width,
                height
            );
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
        let requested_id: u32 = unsafe { std::mem::transmute(id) };

        if flags.contains(QueryCtrlFlags::NEXT) {
            if requested_id < bindings::V4L2_CID_USER_CLASS {
                return Ok(self.user_class_query_ext_ctrl());
            } else if requested_id < bindings::V4L2_CID_GAIN {
                return Ok(self.gain_query_ext_ctrl());
            } else if requested_id < bindings::V4L2_CID_CAMERA_CLASS {
                return Ok(self.camera_class_query_ext_ctrl());
            } else if requested_id < CID_LENS_FACING {
                return Ok(self.lens_facing_query_ext_ctrl());
            } else if requested_id < bindings::V4L2_CID_IMAGE_PROC_CLASS {
                return Ok(self.image_proc_class_query_ext_ctrl());
            } else if requested_id < bindings::V4L2_CID_TEST_PATTERN {
                return Ok(self.test_pattern_query_ext_ctrl());
            }
        } else {
            match requested_id {
                bindings::V4L2_CID_USER_CLASS => return Ok(self.user_class_query_ext_ctrl()),
                bindings::V4L2_CID_GAIN => return Ok(self.gain_query_ext_ctrl()),
                bindings::V4L2_CID_CAMERA_CLASS => return Ok(self.camera_class_query_ext_ctrl()),
                CID_LENS_FACING => return Ok(self.lens_facing_query_ext_ctrl()),
                bindings::V4L2_CID_IMAGE_PROC_CLASS => {
                    return Ok(self.image_proc_class_query_ext_ctrl());
                }
                bindings::V4L2_CID_TEST_PATTERN => {
                    return Ok(self.test_pattern_query_ext_ctrl());
                }
                _ => {}
            }
        }

        Err(libc::EINVAL)
    }

    fn querymenu(
        &mut self,
        _session: &Self::Session,
        id: u32,
        index: u32,
    ) -> IoctlResult<bindings::v4l2_querymenu> {
        if id != bindings::V4L2_CID_TEST_PATTERN {
            return Err(libc::EINVAL);
        }
        // Menu indices are the `TestPattern` discriminants, so the enum decides the range.
        let pattern = i32::try_from(index)
            .ok()
            .and_then(|index| TestPattern::try_from(index).ok())
            .ok_or(libc::EINVAL)?;

        Ok(bindings::v4l2_querymenu {
            id,
            index,
            __bindgen_anon_1: bindings::v4l2_querymenu__bindgen_ty_1 {
                name: ctrl_name(pattern.name()),
            },
            ..Default::default()
        })
    }

    fn g_ctrl(&mut self, _session: &Self::Session, id: u32) -> IoctlResult<bindings::v4l2_control> {
        let value = match id {
            CID_LENS_FACING => self.controls.lens_facing as i32,
            bindings::V4L2_CID_GAIN => self.controls.gain.value(),
            bindings::V4L2_CID_TEST_PATTERN => self.controls.test_pattern as i32,
            bindings::V4L2_CID_USER_CLASS
            | bindings::V4L2_CID_CAMERA_CLASS
            | bindings::V4L2_CID_IMAGE_PROC_CLASS => return Err(libc::EACCES),
            _ => return Err(libc::EINVAL),
        };
        Ok(bindings::v4l2_control { id, value })
    }

    fn s_ctrl(
        &mut self,
        session: &mut Self::Session,
        id: u32,
        value: i32,
    ) -> IoctlResult<bindings::v4l2_control> {
        match id {
            CID_LENS_FACING
            | bindings::V4L2_CID_USER_CLASS
            | bindings::V4L2_CID_CAMERA_CLASS
            | bindings::V4L2_CID_IMAGE_PROC_CLASS => Err(libc::EACCES),
            bindings::V4L2_CID_GAIN => {
                let gain = Gain::new(value)?;
                if self.controls.gain != gain {
                    self.controls.gain = gain;
                    let event = Self::ctrl_event(&self.controls, bindings::V4L2_CID_GAIN)?;
                    self.evt_queue
                        .lock()
                        .unwrap()
                        .send_event(V4l2Event::Event(SessionEvent::new(session.id, event)));
                    let _ = session.channel.set_controls(self.controls.clone());
                }
                Ok(bindings::v4l2_control {
                    id,
                    value: gain.value(),
                })
            }
            bindings::V4L2_CID_TEST_PATTERN => {
                let pattern = TestPattern::try_from(value)?;
                self.set_test_pattern(session.id, pattern)?;
                let _ = session.channel.set_controls(self.controls.clone());
                Ok(bindings::v4l2_control {
                    id,
                    value: pattern as i32,
                })
            }
            _ => Err(libc::EINVAL),
        }
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
                if class != bindings::V4L2_CTRL_CLASS_USER
                    && class != bindings::V4L2_CTRL_CLASS_CAMERA
                    && class != bindings::V4L2_CTRL_CLASS_IMAGE_PROC
                {
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
            for ctrl in ctrl_array.iter() {
                if v4l2_ctrl_id2which(ctrl.id) != class_id {
                    ctrls.error_idx = ctrls.count;
                    return Err(libc::EINVAL);
                }
            }
        }

        // Process controls. Class controls are write-only headers and must fail on read.
        for ctrl in ctrl_array.iter_mut() {
            match ctrl.id {
                bindings::V4L2_CID_USER_CLASS
                | bindings::V4L2_CID_CAMERA_CLASS
                | bindings::V4L2_CID_IMAGE_PROC_CLASS => {
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
                bindings::V4L2_CID_TEST_PATTERN => {
                    ctrl.__bindgen_anon_1.value = match which {
                        CtrlWhich::Default => TestPattern::DEFAULT as i32,
                        _ => self.controls.test_pattern as i32,
                    };
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
                if class != bindings::V4L2_CTRL_CLASS_USER
                    && class != bindings::V4L2_CTRL_CLASS_CAMERA
                    && class != bindings::V4L2_CTRL_CLASS_IMAGE_PROC
                {
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
                bindings::V4L2_CID_USER_CLASS
                | bindings::V4L2_CID_CAMERA_CLASS
                | bindings::V4L2_CID_IMAGE_PROC_CLASS
                | CID_LENS_FACING => {
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
                bindings::V4L2_CID_TEST_PATTERN => {
                    let value = unsafe { ctrl.__bindgen_anon_1.value };
                    if let Err(err) = TestPattern::try_from(value) {
                        ctrls.error_idx = idx as u32;
                        return Err(err);
                    }
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
        user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        // Validate control class, selection, and values via try_ext_ctrls.
        if let Err(err) = self.try_ext_ctrls(session, which, ctrls, ctrl_array, user_regions) {
            ctrls.error_idx = ctrls.count;
            return Err(err);
        }

        // Apply control values. Class controls are read-only headers and must fail on write/try.
        for ctrl in ctrl_array.iter_mut() {
            match ctrl.id {
                bindings::V4L2_CID_GAIN => {
                    let value = unsafe { ctrl.__bindgen_anon_1.value };
                    if let Ok(gain) = Gain::new(value) {
                        if self.controls.gain != gain {
                            self.controls.gain = gain;
                            if let Ok(event) =
                                Self::ctrl_event(&self.controls, bindings::V4L2_CID_GAIN)
                            {
                                self.evt_queue.lock().unwrap().send_event(V4l2Event::Event(
                                    SessionEvent::new(session.id, event),
                                ));
                            }
                        }
                    }
                }
                bindings::V4L2_CID_TEST_PATTERN => {
                    let value = unsafe { ctrl.__bindgen_anon_1.value };
                    if let Ok(pattern) = TestPattern::try_from(value) {
                        self.set_test_pattern(session.id, pattern)?;
                    }
                }
                _ => {}
            }
        }
        let _ = session.channel.set_controls(self.controls.clone());
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
                CID_LENS_FACING | bindings::V4L2_CID_GAIN | bindings::V4L2_CID_TEST_PATTERN => {
                    let ctrl_event = Self::ctrl_event(&self.controls, id)?;
                    self.evt_queue
                        .lock()
                        .unwrap()
                        .send_event(V4l2Event::Event(SessionEvent::new(session.id, ctrl_event)));
                    Ok(())
                }
                bindings::V4L2_CID_USER_CLASS
                | bindings::V4L2_CID_CAMERA_CLASS
                | bindings::V4L2_CID_IMAGE_PROC_CLASS => {
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
                || event.id == bindings::V4L2_CID_TEST_PATTERN
                || event.id == bindings::V4L2_CID_USER_CLASS
                || event.id == bindings::V4L2_CID_CAMERA_CLASS
                || event.id == bindings::V4L2_CID_IMAGE_PROC_CLASS)
        {
            Ok(())
        } else {
            Err(libc::EINVAL)
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every index between `MIN` and `MAX` is advertised as a menu entry, so each one has
    /// to map back to a pattern with a name and a generator.
    #[test]
    fn every_advertised_menu_index_maps_to_a_pattern() {
        for value in TestPattern::MIN..=TestPattern::MAX {
            let pattern = TestPattern::try_from(value).expect("advertised index must be valid");
            assert_eq!(pattern as i32, value);
            assert!(!pattern.name().is_empty());
        }
    }

    #[test]
    fn out_of_range_menu_index_is_rejected() {
        assert_eq!(
            TestPattern::try_from(TestPattern::MIN - 1),
            Err(libc::ERANGE)
        );
        assert_eq!(
            TestPattern::try_from(TestPattern::MAX + 1),
            Err(libc::ERANGE)
        );
    }

    #[test]
    fn ctrl_name_is_nul_padded() {
        let name = ctrl_name("Test Pattern");
        assert_eq!(&name[..12], b"Test Pattern");
        assert!(name[12..].iter().all(|byte| *byte == 0));
    }

    #[test]
    fn ctrl_name_truncates_and_stays_nul_terminated() {
        let name = ctrl_name("This control name is definitely far too long to fit");
        assert_eq!(name[31], 0);
    }
}
