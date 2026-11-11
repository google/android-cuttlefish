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
use std::sync::{Arc, Mutex};

use crate::capture_channel::CaptureChannel;
use crate::control::ControlSet;
use crate::format::Yuv420m;
use crate::memory::{BufferMemoryHandler, QbufRequest, QueuedBuffer};
use crate::pipeline::Pipeline;
use crate::sensor::Sensor;
use crate::pattern::FramePattern;
use crate::render_params::RenderParams;

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

static PIPELINE: Pipeline = Pipeline {
    sensor: Sensor {
        hour: Sensor::DEFAULT_HOUR,
        exposure_s: Sensor::DEFAULT_EXPOSURE_S,
    },
};

/// What the camera renders into capture buffers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameSource {
    /// Basic test patterns.
    TestPatterns,
    /// Emulated physical scene.
    EmulatedScene,
}

impl FrameSource {
    /// Frame generator for this source given the current render parameters.
    pub(crate) fn generator(self, params: &RenderParams) -> &'static dyn FramePattern {
        match self {
            FrameSource::TestPatterns => params.test_pattern.generator(),
            FrameSource::EmulatedScene => &PIPELINE,
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
/// `M` decides how buffers are backed, see [`BufferMemoryHandler`]. The V4L2 controls exposed
/// to the guest are whatever the backend registered in its [`ControlSet`].
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
    /// V4L2 controls exposed to the guest.
    controls: ControlSet,
    /// What is rendered into capture buffers.
    frame_source: FrameSource,
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
    pub fn new(
        evt_queue: Q,
        buffer_memory_handler: M,
        controls: ControlSet,
        frame_source: FrameSource,
    ) -> Self {
        Self {
            buffer_memory_handler,
            active_session: None,
            evt_queue: Arc::new(Mutex::new(evt_queue)),
            controls,
            frame_source,
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

    /// Broadcasts `event` to the guest on behalf of `session_id`.
    fn send_event(&self, session_id: u32, event: bindings::v4l2_event) {
        self.evt_queue
            .lock()
            .unwrap()
            .send_event(V4l2Event::Event(SessionEvent::new(session_id, event)));
    }

    /// Pushes the current control state to the frame generators of `session`.
    fn sync_render_params(&self, session: &EmulatedCameraSession<M::Allocation>) {
        let _ = session
            .channel
            .set_render_params(self.controls.render_params());
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

            let channel = CaptureChannel::new(
                session_id,
                self.controls.render_params(),
                self.frame_source,
                capture_completed_tx,
            )
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
        self.controls
            .query_ext_ctrl(requested_id, flags.contains(QueryCtrlFlags::NEXT))
    }

    fn querymenu(
        &mut self,
        _session: &Self::Session,
        id: u32,
        index: u32,
    ) -> IoctlResult<bindings::v4l2_querymenu> {
        self.controls.querymenu(id, index)
    }

    fn g_ctrl(&mut self, _session: &Self::Session, id: u32) -> IoctlResult<bindings::v4l2_control> {
        self.controls.g_ctrl(id)
    }

    fn s_ctrl(
        &mut self,
        session: &mut Self::Session,
        id: u32,
        value: i32,
    ) -> IoctlResult<bindings::v4l2_control> {
        let outcome = self.controls.s_ctrl(id, value)?;
        if let Some(event) = outcome.event {
            self.send_event(session.id, event);
        }
        self.sync_render_params(session);
        Ok(outcome.control)
    }

    fn g_ext_ctrls(
        &mut self,
        _session: &Self::Session,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        self.controls.g_ext_ctrls(which, ctrls, ctrl_array)
    }

    fn try_ext_ctrls(
        &mut self,
        _session: &Self::Session,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        self.controls.try_ext_ctrls(which, ctrls, ctrl_array)
    }

    fn s_ext_ctrls(
        &mut self,
        session: &mut Self::Session,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        let events = self.controls.s_ext_ctrls(which, ctrls, ctrl_array)?;
        for event in events {
            self.send_event(session.id, event);
        }
        self.sync_render_params(session);
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
            V4l2EventType::Ctrl(id) => {
                if let Some(initial) = self.controls.subscribe_ctrl_event(id)? {
                    self.send_event(session.id, initial);
                }
                Ok(())
            }
            _ => Err(libc::EINVAL),
        }
    }

    fn unsubscribe_event(
        &mut self,
        _session: &mut Self::Session,
        event: bindings::v4l2_event_subscription,
    ) -> IoctlResult<()> {
        if event.type_ == bindings::V4L2_EVENT_CTRL && self.controls.knows(event.id) {
            Ok(())
        } else {
            Err(libc::EINVAL)
        }
    }
}
