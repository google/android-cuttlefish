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

use nix::sys::eventfd::{EfdFlags, EventFd};
use std::collections::VecDeque;
use std::io::Result as IoResult;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;
use std::sync::mpsc::channel;
use std::sync::{Arc, Mutex};
use std::time::Instant;

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
use v4l2r::ioctl::QueryCtrlFlags;
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
use virtio_media::protocol::SgEntry;
use virtio_media::protocol::V4l2Ioctl;
use virtio_media::protocol::VIRTIO_MEDIA_MMAP_FLAG_RW;

use crate::Config;
use crate::worker::{WorkerCmd, WorkerHandle, worker_thread_loop};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Format {
    Yuv420M,
}

impl Format {
    pub fn from_str(s: &str) -> Option<Self> {
        match s {
            "YUV420M" => Some(Self::Yuv420M),
            _ => None,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Yuv420M => "YUV420M",
        }
    }

    pub fn fourcc(&self) -> u32 {
        let fourcc_bytes = match self {
            Self::Yuv420M => b"YM12",
        };
        PixelFormat::from_fourcc(fourcc_bytes).to_u32()
    }

    pub fn bytesperline(&self, width: u32, plane_idx: usize) -> u32 {
        match self {
            Self::Yuv420M => {
                if plane_idx == 0 {
                    width
                } else {
                    width / 2
                }
            }
        }
    }

    pub fn plane_sizes(&self, width: u32, height: u32) -> Vec<usize> {
        let w = width as usize;
        let h = height as usize;
        match self {
            Self::Yuv420M => vec![w * h, w * h / 4, w * h / 4],
        }
    }

    pub fn frame_size(&self, width: u32, height: u32) -> usize {
        self.plane_sizes(width, height).iter().sum()
    }
}

/// Current status of a buffer.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum BufferState {
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
pub(crate) struct Plane {
    /// Backing memory file descriptor.
    pub(crate) fd: MemFdBuffer,
    /// Offset that can be used to map the plane's memory.
    offset: u32,
}

/// Information about a single buffer.
pub(crate) struct Buffer {
    /// Current state of the buffer.
    pub(crate) state: BufferState,
    /// V4L2 representation of this buffer to be sent to the guest when requested.
    pub(crate) v4l2_buffer: V4l2Buffer,
    /// Backing storage and offsets for the planes of the buffer.
    pub(crate) planes: Vec<Plane>,
}

impl Buffer {
    fn new(v4l2_buffer: V4l2Buffer, planes: Vec<Plane>) -> Self {
        Self {
            state: BufferState::New,
            v4l2_buffer,
            planes,
        }
    }

    fn unset_flag(flags: &mut BufferFlags, v: BufferFlags) {
        *flags &= !v;
    }

    /// Update the state of the buffer as well as its V4L2 representation.
    pub(crate) fn set_state(&mut self, state: BufferState) {
        let mut flags = self.v4l2_buffer.flags();
        match state {
            BufferState::New => {
                self.clear_bytesused();
                Self::unset_flag(&mut flags, BufferFlags::QUEUED);
            }
            BufferState::Incoming => {
                self.clear_bytesused();
                flags |= BufferFlags::QUEUED;
            }
            BufferState::Outgoing { sequence } => {
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
                Self::unset_flag(&mut flags, BufferFlags::QUEUED);

                // Set bytesused to plane length for all planes
                if let V4l2PlanesWithBackingMut::Mmap(planes) =
                    self.v4l2_buffer.planes_with_backing_iter_mut()
                {
                    for mut plane in planes {
                        let len = *plane.length;
                        *plane.bytesused = len;
                    }
                }
            }
        }
        self.v4l2_buffer.set_flags(flags);
        self.state = state;
    }

    fn clear_bytesused(&mut self) {
        if let V4l2PlanesWithBackingMut::Mmap(planes) =
            self.v4l2_buffer.planes_with_backing_iter_mut()
        {
            for mut plane in planes {
                *plane.bytesused = 0;
            }
        }
    }
}

/// Inner state of a session.
pub(crate) struct SessionState {
    /// Id of the session.
    pub(crate) id: u32,
    /// Buffers currently allocated for this session.
    pub(crate) buffers: Vec<Buffer>,
    /// Queue of buffers awaiting processing.
    pub(crate) queued_buffers: VecDeque<usize>,
    /// Current sequence number of the generated frames.
    pub(crate) sequence: u32,
    /// Time when the last frame was completed.
    pub(crate) last_frame_time: Instant,
}

/// Session data of [`V4l2Stream`].
pub struct V4l2StreamSession {
    /// Id of the session.
    id: u32,
    state: Arc<Mutex<SessionState>>,
    worker: Option<WorkerHandle>,
    /// Is the session currently streaming?
    streaming: bool,
}

impl VirtioMediaDeviceSession for V4l2StreamSession {
    fn poll_fd(&self) -> Option<BorrowedFd<'_>> {
        None
    }
}

/// V4l2 stream device used for testing Android camera stack.
///
/// This implementation looks forward to have feature parity with existing Android Guest Emulated
/// Camera HAL.
pub struct V4l2Stream<Q: VirtioMediaEventQueue, HM: VirtioMediaHostMemoryMapper> {
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
    config: Config,
}

impl<Q, HM> V4l2Stream<Q, HM>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    HM: VirtioMediaHostMemoryMapper,
{
    pub fn new(evt_queue: Q, mapper: HM, config: Config) -> Self {
        Self {
            evt_queue: Arc::new(Mutex::new(evt_queue)),
            mmap_manager: MmapMappingManager::from(mapper),
            active_session: None,
            config,
        }
    }

    fn queue_type(&self) -> QueueType {
        QueueType::VideoCaptureMplane
    }

    fn default_fmt(&self, queue: QueueType) -> v4l2_format {
        let plane_sizes = self
            .config
            .format
            .plane_sizes(self.config.input_width, self.config.input_height);
        let mut plane_fmt: [bindings::v4l2_plane_pix_format; 8] = Default::default();
        for (i, &size) in plane_sizes.iter().enumerate() {
            plane_fmt[i].sizeimage = size as u32;
            plane_fmt[i].bytesperline = self.config.format.bytesperline(self.config.input_width, i);
        }

        let pix_mp = bindings::v4l2_pix_format_mplane {
            width: self.config.input_width,
            height: self.config.input_height,
            pixelformat: self.config.format.fourcc(),
            field: bindings::v4l2_field_V4L2_FIELD_NONE,
            colorspace: bindings::v4l2_colorspace_V4L2_COLORSPACE_SRGB,
            num_planes: plane_sizes.len() as u8,
            plane_fmt,
            ..Default::default()
        };
        v4l2_format {
            type_: queue as u32,
            fmt: bindings::v4l2_format__bindgen_ty_1 { pix_mp },
        }
    }

    fn default_fmtdesc(&self, queue: QueueType) -> v4l2_fmtdesc {
        let mut fmtdesc: bindings::v4l2_fmtdesc = Default::default();
        fmtdesc.type_ = queue as u32;
        fmtdesc.pixelformat = self.config.format.fourcc();
        let desc = self.config.format.as_str().as_bytes();
        fmtdesc.description[0..desc.len()].copy_from_slice(desc);
        fmtdesc
    }
}

impl<Q, HM, Reader, Writer> VirtioMediaDevice<Reader, Writer> for V4l2Stream<Q, HM>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    HM: VirtioMediaHostMemoryMapper,
    Reader: ReadFromDescriptorChain,
    Writer: WriteToDescriptorChain,
{
    type Session = V4l2StreamSession;

    fn new_session(&mut self, session_id: u32) -> std::result::Result<Self::Session, i32> {
        Ok(V4l2StreamSession {
            id: session_id,
            state: Arc::new(Mutex::new(SessionState {
                id: session_id,
                buffers: Vec::new(),
                queued_buffers: VecDeque::new(),
                sequence: 0,
                last_frame_time: Instant::now(),
            })),
            worker: None,
            streaming: false,
        })
    }

    fn close_session(&mut self, mut session: Self::Session) {
        if session.streaming {
            let _ = self.streamoff(&mut session, self.queue_type());
        }
        if let Some(id) = self.active_session {
            if id == session.id {
                self.active_session = None;
            }
        }

        let state = session.state.lock().unwrap();
        if state.buffers.is_empty() {
            return;
        }

        for buffer in &state.buffers {
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
        // Find which plane in which buffer matches the offset
        let mut found_plane: Option<(&mut Plane, u32)> = None;
        for buffer in &mut state.buffers {
            if let Some(idx) = buffer.planes.iter().position(|p| p.offset == offset) {
                found_plane = Some((&mut buffer.planes[idx], offset));
                break;
            }
        }
        let (plane, offset) = found_plane.ok_or(libc::EINVAL)?;
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

const INPUTS: [bindings::v4l2_input; 1] = [bindings::v4l2_input {
    index: 0,
    name: *b"Default\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    type_: bindings::V4L2_INPUT_TYPE_CAMERA,
    ..unsafe { std::mem::zeroed() }
}];

impl<Q, HM> VirtioMediaIoctlHandler for V4l2Stream<Q, HM>
where
    Q: VirtioMediaEventQueue + Send + 'static,
    HM: VirtioMediaHostMemoryMapper,
{
    type Session = V4l2StreamSession;

    fn enum_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2_fmtdesc> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }
        if index > 0 {
            return Err(libc::EINVAL);
        }
        Ok(self.default_fmtdesc(queue))
    }

    fn g_fmt(&mut self, _session: &Self::Session, queue: QueueType) -> IoctlResult<v4l2_format> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }
        Ok(self.default_fmt(queue))
    }

    fn s_fmt(
        &mut self,
        session: &mut Self::Session,
        queue: QueueType,
        format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        self.try_fmt(session, queue, format)
    }

    fn try_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        _format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }
        Ok(self.default_fmt(queue))
    }

    fn g_parm(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
    ) -> IoctlResult<bindings::v4l2_streamparm> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }

        let mut parm = bindings::v4l2_streamparm {
            type_: queue as u32,
            ..Default::default()
        };

        let (numerator, denominator) = self.config.fps_interval;

        // SAFETY: The `parm` union is used for the capture type.
        let capture = unsafe { &mut parm.parm.capture };
        capture.capability = bindings::V4L2_CAP_TIMEPERFRAME;
        capture.timeperframe = bindings::v4l2_fract {
            numerator,
            denominator,
        };

        Ok(parm)
    }

    fn s_parm(
        &mut self,
        _session: &mut Self::Session,
        mut parm: bindings::v4l2_streamparm,
    ) -> IoctlResult<bindings::v4l2_streamparm> {
        if parm.type_ != self.queue_type() as u32 {
            return Err(libc::EINVAL);
        }

        let (numerator, denominator) = self.config.fps_interval;

        // We just return the fixed values, ignoring what the user set.
        // SAFETY: The `parm` union is used for the capture type.
        let capture = unsafe { &mut parm.parm.capture };
        capture.capability = bindings::V4L2_CAP_TIMEPERFRAME;
        capture.timeperframe = bindings::v4l2_fract {
            numerator,
            denominator,
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
        let expected_queue = self.queue_type();
        if queue != expected_queue {
            return Err(libc::EINVAL);
        }
        if memory != MemoryType::Mmap {
            return Err(libc::EINVAL);
        }
        if session.streaming {
            return Err(libc::EBUSY);
        }
        match self.active_session {
            Some(id) if id != session.id => return Err(libc::EBUSY),
            _ => (),
        }

        let mut state = session.state.lock().unwrap();

        if count == 0 {
            self.active_session = None;
            state.queued_buffers.clear();
            for buffer in state.buffers.iter_mut() {
                buffer.set_state(BufferState::New);
            }
        } else {
            state.queued_buffers.clear();
            for buffer in state.buffers.iter_mut() {
                buffer.set_state(BufferState::New);
            }
            self.active_session = Some(session.id);
        }

        let count = std::cmp::min(count, 32);

        for buffer in &state.buffers {
            for plane in &buffer.planes {
                self.mmap_manager.unregister_buffer(plane.offset);
            }
        }

        let plane_sizes = self
            .config
            .format
            .plane_sizes(self.config.input_width, self.config.input_height);
        let num_planes = plane_sizes.len();

        state.buffers = (0..count)
            .map(|i| -> Result<Buffer, i32> {
                let mut planes = Vec::new();

                for &size in &plane_sizes {
                    let fd = MemFdBuffer::new(size as u64).map_err(|e| {
                        log::error!("failed to allocate MMAP buffer: {:#}", e);
                        libc::ENOMEM
                    })?;
                    let offset = self
                        .mmap_manager
                        .register_buffer(None, size as u32)
                        .map_err(|_| libc::EINVAL)?;
                    planes.push(Plane { fd, offset });
                }

                let mut v4l2_buffer = V4l2Buffer::new(expected_queue, i, MemoryType::Mmap);

                if num_planes > 1 {
                    unsafe {
                        (*v4l2_buffer.as_mut_ptr()).length = num_planes as u32;
                    }
                    if let V4l2PlanesWithBackingMut::Mmap(planes_iter) =
                        v4l2_buffer.planes_with_backing_iter_mut()
                    {
                        for (j, mut plane) in planes_iter.enumerate() {
                            plane.set_mem_offset(planes[j].offset);
                            *plane.length = plane_sizes[j] as u32;
                        }
                    } else {
                        panic!()
                    }
                } else {
                    if let V4l2PlanesWithBackingMut::Mmap(mut planes_iter) =
                        v4l2_buffer.planes_with_backing_iter_mut()
                    {
                        let mut plane = planes_iter.next().unwrap();
                        plane.set_mem_offset(planes[0].offset);
                        *plane.length = plane_sizes[0] as u32;
                    } else {
                        panic!()
                    }
                }

                v4l2_buffer.set_field(BufferField::None);
                v4l2_buffer.set_flags(BufferFlags::TIMESTAMP_MONOTONIC);

                Ok(Buffer::new(v4l2_buffer, planes))
            })
            .collect::<Result<_, _>>()?;

        Ok(v4l2_requestbuffers {
            count,
            type_: queue as u32,
            memory: memory as u32,
            capabilities: (BufferCapabilities::SUPPORTS_MMAP
                | BufferCapabilities::SUPPORTS_ORPHANED_BUFS)
                .bits(),
            flags: 0,
            ..Default::default()
        })
    }

    fn querybuf(
        &mut self,
        session: &V4l2StreamSession,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<V4l2Buffer> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }
        let state = session.state.lock().unwrap();
        let buffer = state.buffers.get(index as usize).ok_or(libc::EINVAL)?;
        Ok(buffer.v4l2_buffer.clone())
    }

    fn qbuf(
        &mut self,
        session: &mut Self::Session,
        qbuf: V4l2Buffer,
        _sg_entries: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<V4l2Buffer> {
        let mut state = session.state.lock().unwrap();
        let buf_id = qbuf.index() as usize;

        let buf_v4l2 = {
            let buffer = state.buffers.get_mut(buf_id).ok_or(libc::EINVAL)?;
            if buffer.state == BufferState::Incoming {
                return Err(libc::EINVAL);
            }
            buffer.set_state(BufferState::Incoming);
            buffer.v4l2_buffer.clone()
        };

        state.queued_buffers.push_back(buf_id);

        if session.streaming {
            if let Some(ref worker) = session.worker {
                let _ = worker.tx.send(WorkerCmd::BufferQueued);
                let _ = worker.event_fd.write(1);
            }
        }

        Ok(buf_v4l2)
    }

    fn streamon(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }

        {
            let mut state = session.state.lock().unwrap();

            if state.buffers.is_empty() {
                return Err(libc::EINVAL);
            }
            state.sequence = 0;
        }

        if session.streaming {
            return Ok(());
        }
        session.streaming = true;

        let event_fd = Arc::new(
            EventFd::from_flags(EfdFlags::EFD_CLOEXEC | EfdFlags::EFD_NONBLOCK)
                .map_err(|_| libc::ENOMEM)?,
        );
        let event_fd_clone = Arc::clone(&event_fd);

        let (tx, rx) = channel();
        let state_clone = Arc::clone(&session.state);
        let evt_queue_clone = Arc::clone(&self.evt_queue);
        let config_clone = self.config.clone();

        let join_handle = std::thread::spawn(move || {
            worker_thread_loop(
                config_clone,
                evt_queue_clone,
                state_clone,
                rx,
                event_fd_clone,
            );
        });

        session.worker = Some(WorkerHandle {
            tx,
            event_fd,
            join_handle,
        });

        Ok(())
    }

    fn streamoff(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != self.queue_type() {
            return Err(libc::EINVAL);
        }
        if session.streaming {
            session.streaming = false;
            if let Some(worker) = session.worker.take() {
                let _ = worker.tx.send(WorkerCmd::Stop);
                let _ = worker.event_fd.write(1);
                let _ = worker.join_handle.join();
            }
        }

        let mut state = session.state.lock().unwrap();
        state.queued_buffers.clear();
        for buffer in state.buffers.iter_mut() {
            buffer.set_state(BufferState::New);
        }

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
        INPUTS.get(index as usize).copied().ok_or(libc::EINVAL)
    }

    fn enum_framesizes(
        &mut self,
        _session: &Self::Session,
        index: u32,
        pixel_format: u32,
    ) -> IoctlResult<bindings::v4l2_frmsizeenum> {
        if pixel_format != self.config.format.fourcc() {
            return Err(libc::EINVAL);
        }
        if index > 0 {
            return Err(libc::EINVAL);
        }

        Ok(bindings::v4l2_frmsizeenum {
            index,
            pixel_format,
            type_: bindings::v4l2_frmsizetypes_V4L2_FRMSIZE_TYPE_DISCRETE,
            __bindgen_anon_1: bindings::v4l2_frmsizeenum__bindgen_ty_1 {
                discrete: bindings::v4l2_frmsize_discrete {
                    width: self.config.input_width,
                    height: self.config.input_height,
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
        if pixel_format != self.config.format.fourcc() {
            return Err(libc::EINVAL);
        }
        if width != self.config.input_width || height != self.config.input_height {
            return Err(libc::EINVAL);
        }
        if index > 0 {
            return Err(libc::EINVAL);
        }

        let (numerator, denominator) = self.config.fps_interval;

        Ok(bindings::v4l2_frmivalenum {
            index,
            pixel_format,
            width,
            height,
            type_: bindings::v4l2_frmivaltypes_V4L2_FRMIVAL_TYPE_DISCRETE,
            __bindgen_anon_1: bindings::v4l2_frmivalenum__bindgen_ty_1 {
                discrete: bindings::v4l2_fract {
                    numerator,
                    denominator,
                },
            },
            ..Default::default()
        })
    }

    fn query_ext_ctrl(
        &mut self,
        _session: &Self::Session,
        id: CtrlId,
        flags: QueryCtrlFlags,
    ) -> IoctlResult<bindings::v4l2_query_ext_ctrl> {
        let requested_id: u32 = unsafe { std::mem::transmute(id) };

        if flags.contains(QueryCtrlFlags::NEXT) {
            if requested_id < CID_LENS_FACING {
                return Ok(self.lens_facing_query_ext_ctrl());
            }
        } else if requested_id == CID_LENS_FACING {
            return Ok(self.lens_facing_query_ext_ctrl());
        }

        Err(libc::EINVAL)
    }

    fn g_ctrl(&mut self, _session: &Self::Session, id: u32) -> IoctlResult<bindings::v4l2_control> {
        if id == CID_LENS_FACING {
            return Ok(bindings::v4l2_control {
                id,
                value: self.config.lens_facing as i32,
            });
        }
        Err(libc::EINVAL)
    }

    fn g_ext_ctrls(
        &mut self,
        _session: &Self::Session,
        _which: CtrlWhich,
        _ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut Vec<bindings::v4l2_ext_control>,
        _user_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<()> {
        for ctrl in ctrl_array.iter_mut() {
            if ctrl.id == CID_LENS_FACING {
                ctrl.__bindgen_anon_1.value64 = self.config.lens_facing as i64;
            } else {
                return Err(libc::EINVAL);
            }
        }
        Ok(())
    }
}

/// https://developer.android.com/reference/android/hardware/camera2/CameraMetadata#LENS_FACING_FRONT
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LensFacing {
    Front = 0,
    Back = 1,
    External = 2,
}

impl std::str::FromStr for LensFacing {
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

const CID_OFFSET: u32 = bindings::V4L2_CID_CAMERA_CLASS_BASE + 0x100;
const CID_LENS_FACING: u32 = CID_OFFSET + 1;

fn ctrl_name(name: &str) -> [i8; 32] {
    let mut array = [0i8; 32];
    let bytes = name.as_bytes();
    let len = std::cmp::min(bytes.len(), 31);
    for i in 0..len {
        array[i] = bytes[i] as i8;
    }
    array
}

impl<Q: VirtioMediaEventQueue, HM: VirtioMediaHostMemoryMapper> V4l2Stream<Q, HM> {
    fn lens_facing_query_ext_ctrl(&self) -> bindings::v4l2_query_ext_ctrl {
        bindings::v4l2_query_ext_ctrl {
            id: CID_LENS_FACING,
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER,
            name: ctrl_name("LENS_FACING"),
            minimum: 0,
            maximum: 2,
            step: 1,
            default_value: self.config.lens_facing as i64,
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY,
            elems: 1,
            elem_size: std::mem::size_of::<u32>() as u32,
            ..Default::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lens_facing_from_str() {
        assert_eq!("FRONT".parse::<LensFacing>().unwrap(), LensFacing::Front);
        assert_eq!("BACK".parse::<LensFacing>().unwrap(), LensFacing::Back);
        assert_eq!(
            "EXTERNAL".parse::<LensFacing>().unwrap(),
            LensFacing::External
        );
        assert!("INVALID".parse::<LensFacing>().is_err());
    }

    #[test]
    fn test_ctrl_name_is_nul_padded() {
        let name = ctrl_name("LENS_FACING");
        assert_eq!(&name[..11], b"LENS_FACING".map(|b| b as i8));
        assert!(name[11..].iter().all(|byte| *byte == 0));
    }

    #[test]
    fn test_ctrl_name_truncates_and_stays_nul_terminated() {
        let name = ctrl_name("This control name is definitely far too long to fit");
        assert_eq!(name[31], 0);
    }
}
