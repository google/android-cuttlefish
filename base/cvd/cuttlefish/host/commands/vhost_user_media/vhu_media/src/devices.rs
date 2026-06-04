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
use std::io::Seek;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;
use v4l2r::QueueType;
use v4l2r::bindings;
use v4l2r::bindings::v4l2_fmtdesc;
use v4l2r::bindings::v4l2_format;
use v4l2r::bindings::v4l2_requestbuffers;
use v4l2r::ioctl::BufferCapabilities;
use v4l2r::ioctl::BufferField;
use v4l2r::ioctl::BufferFlags;
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
use virtio_media::protocol::SgEntry;
use virtio_media::protocol::V4l2Event;
use virtio_media::protocol::V4l2Ioctl;
use virtio_media::protocol::VIRTIO_MEDIA_MMAP_FLAG_RW;

pub trait CaptureDeviceFormat: Send + Sync + 'static {
    const QUEUE_TYPE: QueueType;
    const PIXELFORMAT: u32;
    const BUFFER_SIZE: u32;
    const FRAME_RATE: u32;
    const INPUTS: &'static [bindings::v4l2_input];
    const WIDTH: u32;
    const HEIGHT: u32;

    fn default_fmt(queue: QueueType) -> v4l2_format;
    fn write_pattern<W: std::io::Write>(iteration: u64, sink: W) -> IoctlResult<()>;

    fn enum_framesizes(index: u32, pixel_format: u32) -> IoctlResult<bindings::v4l2_frmsizeenum> {
        if pixel_format != Self::PIXELFORMAT {
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
                    width: Self::WIDTH,
                    height: Self::HEIGHT,
                },
            },
            ..Default::default()
        })
    }

    fn enum_frameintervals(
        index: u32,
        pixel_format: u32,
        width: u32,
        height: u32,
    ) -> IoctlResult<bindings::v4l2_frmivalenum> {
        if pixel_format != Self::PIXELFORMAT {
            return Err(libc::EINVAL);
        }
        if width != Self::WIDTH || height != Self::HEIGHT {
            return Err(libc::EINVAL);
        }
        if index > 0 {
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
                    denominator: Self::FRAME_RATE,
                },
            },
            ..Default::default()
        })
    }
}

/// Current status of a buffer.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum BufferState {
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

/// Information about a single buffer.
pub struct Buffer {
    /// Current state of the buffer.
    pub state: BufferState,
    /// V4L2 representation of this buffer to be sent to the guest when requested.
    pub v4l2_buffer: V4l2Buffer,
    /// Backing storage for the buffer.
    pub fd: MemFdBuffer,
    /// Offset that can be used to map the buffer.
    ///
    /// Cached from `v4l2_buffer` to avoid doing a match.
    pub offset: u32,
}

impl Buffer {
    pub fn new(v4l2_buffer: V4l2Buffer, fd: MemFdBuffer, offset: u32) -> Self {
        Self {
            state: BufferState::New,
            v4l2_buffer,
            fd,
            offset,
        }
    }

    /// Update the state of the buffer as well as its V4L2 representation.
    pub fn set_state(&mut self, state: BufferState, buffer_size: u32) {
        let mut flags = self.v4l2_buffer.flags();
        match state {
            BufferState::New => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = 0;
                flags &= !BufferFlags::QUEUED;
            }
            BufferState::Incoming => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = 0;
                flags |= BufferFlags::QUEUED;
            }
            BufferState::Outgoing { sequence } => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = buffer_size;
                self.v4l2_buffer.set_sequence(sequence);
                self.v4l2_buffer.set_timestamp(bindings::timeval {
                    tv_sec: (sequence + 1) as bindings::__time_t / 1000,
                    tv_usec: (sequence + 1) as bindings::__time_t % 1000,
                });
                flags &= !BufferFlags::QUEUED;
            }
        }
        self.v4l2_buffer.set_flags(flags);
        self.state = state;
    }
}

pub fn set_plane_offset_and_length(v4l2_buffer: &mut V4l2Buffer, offset: u32, buffer_size: u32) {
    if let V4l2PlanesWithBackingMut::Mmap(mut planes) = v4l2_buffer.planes_with_backing_iter_mut() {
        let mut plane = planes.next().unwrap();
        plane.set_mem_offset(offset);
        *plane.length = buffer_size;
    } else {
        // SAFETY: we have just set the buffer type to MMAP. Reaching this point means a bug in
        // the code.
        panic!()
    }
}

/// Session data of [`EmulatedCamera`].
pub struct EmulatedCameraSession<F: CaptureDeviceFormat> {
    /// Id of the session.
    id: u32,
    /// Current iteration of the pattern generation cycle.
    iteration: u64,
    /// Buffers currently allocated for this session.
    buffers: Vec<Buffer>,
    /// Queue of buffers awaiting processing.
    queued_buffers: VecDeque<usize>,
    /// Is the session currently streaming?
    streaming: bool,
    _phantom: std::marker::PhantomData<F>,
}

impl<F: CaptureDeviceFormat> VirtioMediaDeviceSession for EmulatedCameraSession<F> {
    fn poll_fd(&self) -> Option<BorrowedFd<'_>> {
        None
    }
}

impl<F: CaptureDeviceFormat> EmulatedCameraSession<F> {
    /// Write basic pattern into the queued buffers
    fn process_queued_buffers<Q: VirtioMediaEventQueue>(
        &mut self,
        evt_queue: &mut Q,
    ) -> IoctlResult<()> {
        while let Some(buf_id) = self.queued_buffers.pop_front() {
            let iteration = self.iteration;
            let buffer = self.buffers.get_mut(buf_id).ok_or(libc::EIO)?;
            buffer
                .fd
                .as_file()
                .seek(std::io::SeekFrom::Start(0))
                .map_err(|_| libc::EIO)?;

            F::write_pattern(iteration, buffer.fd.as_file())?;

            buffer.set_state(
                BufferState::Outgoing {
                    sequence: iteration as u32,
                },
                F::BUFFER_SIZE,
            );
            evt_queue.send_event(V4l2Event::DequeueBuffer(DequeueBufferEvent::new(
                self.id,
                buffer.v4l2_buffer.clone(),
            )));

            self.iteration += 1;
        }

        Ok(())
    }
}

/// Emulated camera used for testing Android camera stack.
pub struct EmulatedCamera<
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
    F: CaptureDeviceFormat,
> {
    /// Queue used to send events to the guest.
    evt_queue: Q,
    /// Host MMAP mapping manager.
    mmap_manager: MmapMappingManager<HM>,
    /// ID of the session with allocated buffers, if any.
    active_session: Option<u32>,
    _phantom: std::marker::PhantomData<F>,
}

impl<Q, HM, F> EmulatedCamera<Q, HM, F>
where
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
    F: CaptureDeviceFormat,
{
    pub fn new(evt_queue: Q, mapper: HM) -> Self {
        Self {
            evt_queue,
            mmap_manager: MmapMappingManager::from(mapper),
            active_session: None,
            _phantom: std::marker::PhantomData,
        }
    }
}

impl<Q, HM, F, Reader, Writer> VirtioMediaDevice<Reader, Writer> for EmulatedCamera<Q, HM, F>
where
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
    F: CaptureDeviceFormat,
    Reader: ReadFromDescriptorChain,
    Writer: WriteToDescriptorChain,
{
    type Session = EmulatedCameraSession<F>;

    fn new_session(&mut self, session_id: u32) -> std::result::Result<Self::Session, i32> {
        Ok(EmulatedCameraSession {
            id: session_id,
            iteration: 0,
            buffers: Default::default(),
            queued_buffers: Default::default(),
            streaming: false,
            _phantom: std::marker::PhantomData,
        })
    }

    fn close_session(&mut self, session: Self::Session) {
        if self.active_session != Some(session.id) {
            return;
        }

        self.active_session = None;

        for buffer in &session.buffers {
            self.mmap_manager.unregister_buffer(buffer.offset);
        }
    }

    fn do_ioctl(
        &mut self,
        session: &mut Self::Session,
        ioctl: V4l2Ioctl,
        reader: &mut Reader,
        writer: &mut Writer,
    ) -> std::io::Result<()> {
        virtio_media_dispatch_ioctl(self, session, ioctl, reader, writer)
    }

    fn do_mmap(
        &mut self,
        session: &mut Self::Session,
        flags: u32,
        offset: u32,
    ) -> std::result::Result<(u64, u64), i32> {
        let buffer = session
            .buffers
            .iter_mut()
            .find(|b| b.offset == offset)
            .ok_or(libc::EINVAL)?;
        let rw = (flags & VIRTIO_MEDIA_MMAP_FLAG_RW) != 0;
        let fd = buffer.fd.as_file().as_fd();
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

/// Implementations of the ioctls required by a v4l2 CAPTURE device.
impl<Q, HM, F> VirtioMediaIoctlHandler for EmulatedCamera<Q, HM, F>
where
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
    F: CaptureDeviceFormat,
{
    type Session = EmulatedCameraSession<F>;

    fn enum_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2_fmtdesc> {
        if queue != F::QUEUE_TYPE {
            return Err(libc::EINVAL);
        }
        if index > 0 {
            return Err(libc::EINVAL);
        }

        Ok(v4l2_fmtdesc {
            index: 0,
            type_: queue as u32,
            pixelformat: F::PIXELFORMAT,
            ..Default::default()
        })
    }

    fn g_fmt(&mut self, _session: &Self::Session, queue: QueueType) -> IoctlResult<v4l2_format> {
        if queue != F::QUEUE_TYPE {
            return Err(libc::EINVAL);
        }
        Ok(F::default_fmt(queue))
    }

    fn s_fmt(
        &mut self,
        _session: &mut Self::Session,
        queue: QueueType,
        _format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != F::QUEUE_TYPE {
            return Err(libc::EINVAL);
        }
        Ok(F::default_fmt(queue))
    }

    fn try_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        _format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != F::QUEUE_TYPE {
            return Err(libc::EINVAL);
        }
        Ok(F::default_fmt(queue))
    }

    fn g_parm(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
    ) -> IoctlResult<bindings::v4l2_streamparm> {
        if queue != F::QUEUE_TYPE {
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
            denominator: F::FRAME_RATE,
        };

        Ok(parm)
    }

    fn s_parm(
        &mut self,
        _session: &mut Self::Session,
        mut parm: bindings::v4l2_streamparm,
    ) -> IoctlResult<bindings::v4l2_streamparm> {
        if parm.type_ != F::QUEUE_TYPE as u32 {
            return Err(libc::EINVAL);
        }

        // We just return the fixed values, ignoring what the user set.
        // SAFETY: The `parm` union is used for the capture type.
        let capture = unsafe { &mut parm.parm.capture };
        capture.capability = bindings::V4L2_CAP_TIMEPERFRAME;
        capture.timeperframe = bindings::v4l2_fract {
            numerator: 1,
            denominator: F::FRAME_RATE,
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
        if queue != F::QUEUE_TYPE {
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

        if count == 0 {
            self.active_session = None;
            self.streamoff(session, queue)?;
        } else {
            // TODO factorize with streamoff.
            session.queued_buffers.clear();
            for buffer in session.buffers.iter_mut() {
                buffer.set_state(BufferState::New, F::BUFFER_SIZE);
            }
            self.active_session = Some(session.id);
        }

        let count = std::cmp::min(count, 32);

        for buffer in &session.buffers {
            self.mmap_manager.unregister_buffer(buffer.offset);
        }

        session.buffers = (0..count)
            .map(|i| {
                MemFdBuffer::new(F::BUFFER_SIZE as u64)
                    .map_err(|e| {
                        log::error!("failed to allocate MMAP buffers: {:#}", e);
                        libc::ENOMEM
                    })
                    .and_then(|fd| {
                        let offset = self
                            .mmap_manager
                            .register_buffer(None, F::BUFFER_SIZE)
                            .map_err(|_| libc::EINVAL)?;

                        let mut v4l2_buffer = V4l2Buffer::new(queue, i, MemoryType::Mmap);
                        set_plane_offset_and_length(&mut v4l2_buffer, offset, F::BUFFER_SIZE);
                        v4l2_buffer.set_field(BufferField::None);
                        v4l2_buffer.set_flags(BufferFlags::TIMESTAMP_MONOTONIC);

                        Ok(Buffer::new(v4l2_buffer, fd, offset))
                    })
            })
            .collect::<std::result::Result<_, _>>()?;

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
        session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2r::ioctl::V4l2Buffer> {
        if queue != F::QUEUE_TYPE {
            return Err(libc::EINVAL);
        }
        let buffer = session.buffers.get(index as usize).ok_or(libc::EINVAL)?;

        Ok(buffer.v4l2_buffer.clone())
    }

    fn qbuf(
        &mut self,
        session: &mut Self::Session,
        buffer: v4l2r::ioctl::V4l2Buffer,
        _guest_regions: Vec<Vec<SgEntry>>,
    ) -> IoctlResult<v4l2r::ioctl::V4l2Buffer> {
        let host_buffer = session
            .buffers
            .get_mut(buffer.index() as usize)
            .ok_or(libc::EINVAL)?;
        if matches!(host_buffer.state, BufferState::Incoming) {
            return Err(libc::EINVAL);
        }

        host_buffer.set_state(BufferState::Incoming, F::BUFFER_SIZE);
        session.queued_buffers.push_back(buffer.index() as usize);

        let buffer = host_buffer.v4l2_buffer.clone();

        if session.streaming {
            session.process_queued_buffers(&mut self.evt_queue)?;
        }

        Ok(buffer)
    }

    fn streamon(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != F::QUEUE_TYPE || session.buffers.is_empty() {
            return Err(libc::EINVAL);
        }
        session.streaming = true;

        session.process_queued_buffers(&mut self.evt_queue)?;

        Ok(())
    }

    fn streamoff(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != F::QUEUE_TYPE {
            return Err(libc::EINVAL);
        }
        session.streaming = false;
        session.queued_buffers.clear();
        for buffer in session.buffers.iter_mut() {
            buffer.set_state(BufferState::New, F::BUFFER_SIZE);
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
        F::INPUTS.get(index as usize).map(|&x| x).ok_or(libc::EINVAL)
    }

    fn enum_framesizes(
        &mut self,
        _session: &Self::Session,
        index: u32,
        pixel_format: u32,
    ) -> IoctlResult<bindings::v4l2_frmsizeenum> {
        F::enum_framesizes(index, pixel_format)
    }

    fn enum_frameintervals(
        &mut self,
        _session: &Self::Session,
        index: u32,
        pixel_format: u32,
        width: u32,
        height: u32,
    ) -> IoctlResult<bindings::v4l2_frmivalenum> {
        F::enum_frameintervals(index, pixel_format, width, height)
    }
}


