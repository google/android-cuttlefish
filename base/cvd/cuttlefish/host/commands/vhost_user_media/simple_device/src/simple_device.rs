//! Simple example virtio-media CAPTURE device with no dependency.
//!
//! This module illustrates how to write a device for virtio-media. It exposes a capture device
//! that generates a RGB pattern on the buffers queued by the guest.

use std::collections::VecDeque;
use std::io::BufWriter;
use std::io::Result as IoResult;
use std::io::Seek;
use std::io::SeekFrom;
use std::io::Write;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;

use v4l2r::PixelFormat;
use v4l2r::QueueType;
use v4l2r::bindings;
use v4l2r::bindings::v4l2_fmtdesc;
use v4l2r::bindings::v4l2_format;
use v4l2r::bindings::v4l2_pix_format;
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

/// Information about a single buffer.
struct Buffer {
    /// Current state of the buffer.
    state: BufferState,
    /// V4L2 representation of this buffer to be sent to the guest when requested.
    v4l2_buffer: V4l2Buffer,
    /// Backing storage for the buffer.
    fd: MemFdBuffer,
    /// Offset that can be used to map the buffer.
    ///
    /// Cached from `v4l2_buffer` to avoid doing a match.
    offset: u32,
}

impl Buffer {
    /// Update the state of the buffer as well as its V4L2 representation.
    fn set_state(&mut self, state: BufferState) {
        let mut flags = self.v4l2_buffer.flags();
        match state {
            BufferState::New => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = 0;
                flags -= BufferFlags::QUEUED;
            }
            BufferState::Incoming => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = 0;
                flags |= BufferFlags::QUEUED;
            }
            BufferState::Outgoing { sequence } => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = BUFFER_SIZE;
                self.v4l2_buffer.set_sequence(sequence);
                self.v4l2_buffer.set_timestamp(bindings::timeval {
                    tv_sec: (sequence + 1) as bindings::__time_t / 1000,
                    tv_usec: (sequence + 1) as bindings::__time_t % 1000,
                });
                flags -= BufferFlags::QUEUED;
            }
        }

        self.v4l2_buffer.set_flags(flags);
        self.state = state;
    }
}

/// Session data of [`SimpleCaptureDevice`].
pub struct SimpleCaptureDeviceSession {
    /// Id of the session.
    id: u32,
    /// Current iteration of the pattern generation cycle.
    iteration: u64,
    /// Buffers currently allocated for this session.
    buffers: Vec<Buffer>,
    /// FIFO of queued buffers awaiting processing.
    queued_buffers: VecDeque<usize>,
    /// Is the session currently streaming?
    streaming: bool,
}

impl VirtioMediaDeviceSession for SimpleCaptureDeviceSession {
    fn poll_fd(&self) -> Option<BorrowedFd<'_>> {
        None
    }
}

impl SimpleCaptureDeviceSession {
    /// Generate the data pattern on all queued buffers and send the corresponding
    /// [`DequeueBufferEvent`] to the driver.
    fn process_queued_buffers<Q: VirtioMediaEventQueue>(
        &mut self,
        evt_queue: &mut Q,
    ) -> IoctlResult<()> {
        while let Some(buf_id) = self.queued_buffers.pop_front() {
            let buffer = self.buffers.get_mut(buf_id).ok_or(libc::EIO)?;
            let sequence = self.iteration as u32;

            buffer
                .fd
                .as_file()
                .seek(SeekFrom::Start(0))
                .map_err(|_| libc::EIO)?;
            let mut writer = BufWriter::new(buffer.fd.as_file());
            let color = [
                0xffu8 * (sequence as u8 % 2),
                0x55u8 * (sequence as u8 % 3),
                0x10u8 * (sequence as u8 % 16),
            ];
            for _ in 0..(WIDTH * HEIGHT) {
                let _ = writer.write(&color).map_err(|_| libc::EIO)?;
            }
            drop(writer);

            *buffer.v4l2_buffer.get_first_plane_mut().bytesused = BUFFER_SIZE;
            buffer.set_state(BufferState::Outgoing { sequence });
            // TODO: should we set the DONE flag here?
            self.iteration += 1;

            let v4l2_buffer = buffer.v4l2_buffer.clone();

            evt_queue.send_event(V4l2Event::DequeueBuffer(DequeueBufferEvent::new(
                self.id,
                v4l2_buffer,
            )));
        }

        Ok(())
    }
}

/// A simplistic video capture device, used to demonstrate how device code can be written, or for
/// testing VMMs and guests without dedicated hardware support.
///
/// This device supports a single pixel format (`RGB3`) and a single resolution, and generates
/// frames of varying uniform color. The only buffer type supported is `MMAP`
pub struct SimpleCaptureDevice<Q: VirtioMediaEventQueue, HM: VirtioMediaHostMemoryMapper> {
    /// Queue used to send events to the guest.
    evt_queue: Q,
    /// Host MMAP mapping manager.
    mmap_manager: MmapMappingManager<HM>,
    /// ID of the session with allocated buffers, if any.
    ///
    /// v4l2-compliance checks that only a single session can have allocated buffers at a given
    /// time, since that's how actual hardware works - no two sessions can access a camera at the
    /// same time. It will fails if we allow simultaneous sessions to be active, so we need this
    /// artificial limitation to make it pass fully.
    active_session: Option<u32>,
}

impl<Q, HM> SimpleCaptureDevice<Q, HM>
where
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
{
    pub fn new(evt_queue: Q, mapper: HM) -> Self {
        Self {
            evt_queue,
            mmap_manager: MmapMappingManager::from(mapper),
            active_session: None,
        }
    }
}

impl<Q, HM, Reader, Writer> VirtioMediaDevice<Reader, Writer> for SimpleCaptureDevice<Q, HM>
where
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
    Reader: ReadFromDescriptorChain,
    Writer: WriteToDescriptorChain,
{
    type Session = SimpleCaptureDeviceSession;

    fn new_session(&mut self, session_id: u32) -> Result<Self::Session, i32> {
        Ok(SimpleCaptureDeviceSession {
            id: session_id,
            iteration: 0,
            buffers: Default::default(),
            queued_buffers: Default::default(),
            streaming: false,
        })
    }

    fn close_session(&mut self, session: Self::Session) {
        if self.active_session == Some(session.id) {
            self.active_session = None;
        }

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
    ) -> IoResult<()> {
        virtio_media_dispatch_ioctl(self, session, ioctl, reader, writer)
    }

    fn do_mmap(
        &mut self,
        session: &mut Self::Session,
        flags: u32,
        offset: u32,
    ) -> Result<(u64, u64), i32> {
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

        // TODO: would be nice to enable this, but how do we find the buffer again during munmap?
        //
        // Maybe keep a guest_addr -> session map in the device...
        // buffer.v4l2_buffer.set_flags(buffer.v4l2_buffer.flags() | BufferFlags::MAPPED);

        Ok((guest_addr, size))
    }

    fn do_munmap(&mut self, guest_addr: u64) -> Result<(), i32> {
        self.mmap_manager
            .remove_mapping(guest_addr)
            .map(|_| ())
            .map_err(|_| libc::EINVAL)
    }
}

const PIXELFORMAT: u32 = PixelFormat::from_fourcc(b"RGB3").to_u32();
const WIDTH: u32 = 640;
const HEIGHT: u32 = 480;
const BYTES_PER_LINE: u32 = WIDTH * 3;
const BUFFER_SIZE: u32 = BYTES_PER_LINE * HEIGHT;

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

fn default_fmt(queue: QueueType) -> v4l2_format {
    let pix = v4l2_pix_format {
        width: WIDTH,
        height: HEIGHT,
        pixelformat: PIXELFORMAT,
        field: bindings::v4l2_field_V4L2_FIELD_NONE,
        bytesperline: BYTES_PER_LINE,
        sizeimage: BUFFER_SIZE,
        colorspace: bindings::v4l2_colorspace_V4L2_COLORSPACE_SRGB,
        ..Default::default()
    };

    v4l2_format {
        type_: queue as u32,
        fmt: bindings::v4l2_format__bindgen_ty_1 { pix },
    }
}

/// Implementations of the ioctls required by a CAPTURE device.
impl<Q, HM> VirtioMediaIoctlHandler for SimpleCaptureDevice<Q, HM>
where
    Q: VirtioMediaEventQueue,
    HM: VirtioMediaHostMemoryMapper,
{
    type Session = SimpleCaptureDeviceSession;

    fn enum_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2_fmtdesc> {
        if queue != QueueType::VideoCapture {
            return Err(libc::EINVAL);
        }
        if index > 0 {
            return Err(libc::EINVAL);
        }

        Ok(default_fmtdesc(queue))
    }

    fn g_fmt(&mut self, _session: &Self::Session, queue: QueueType) -> IoctlResult<v4l2_format> {
        if queue != QueueType::VideoCapture {
            return Err(libc::EINVAL);
        }

        Ok(default_fmt(queue))
    }

    fn s_fmt(
        &mut self,
        _session: &mut Self::Session,
        queue: QueueType,
        _format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != QueueType::VideoCapture {
            return Err(libc::EINVAL);
        }

        Ok(default_fmt(queue))
    }

    fn try_fmt(
        &mut self,
        _session: &Self::Session,
        queue: QueueType,
        _format: v4l2_format,
    ) -> IoctlResult<v4l2_format> {
        if queue != QueueType::VideoCapture {
            return Err(libc::EINVAL);
        }

        Ok(default_fmt(queue))
    }

    fn reqbufs(
        &mut self,
        session: &mut Self::Session,
        queue: QueueType,
        memory: MemoryType,
        count: u32,
    ) -> IoctlResult<v4l2_requestbuffers> {
        if queue != QueueType::VideoCapture {
            return Err(libc::EINVAL);
        }
        if memory != MemoryType::Mmap {
            return Err(libc::EINVAL);
        }
        if session.streaming {
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
            self.streamoff(session, queue)?;
        } else {
            // TODO factorize with streamoff.
            session.queued_buffers.clear();
            for buffer in session.buffers.iter_mut() {
                buffer.set_state(BufferState::New);
            }
            self.active_session = Some(session.id);
        }

        let count = std::cmp::min(count, 32);

        for buffer in &session.buffers {
            self.mmap_manager.unregister_buffer(buffer.offset);
        }

        session.buffers = (0..count)
            .map(|i| {
                MemFdBuffer::new(BUFFER_SIZE as u64)
                    .map_err(|e| {
                        log::error!("failed to allocate MMAP buffers: {:#}", e);
                        libc::ENOMEM
                    })
                    .and_then(|fd| {
                        let offset = self
                            .mmap_manager
                            .register_buffer(None, BUFFER_SIZE)
                            .map_err(|_| libc::EINVAL)?;

                        let mut v4l2_buffer =
                            V4l2Buffer::new(QueueType::VideoCapture, i, MemoryType::Mmap);
                        if let V4l2PlanesWithBackingMut::Mmap(mut planes) =
                            v4l2_buffer.planes_with_backing_iter_mut()
                        {
                            // SAFETY: every buffer has at least one plane.
                            let mut plane = planes.next().unwrap();
                            plane.set_mem_offset(offset);
                            *plane.length = BUFFER_SIZE;
                        } else {
                            // SAFETY: we have just set the buffer type to MMAP. Reaching this point means a bug in
                            // the code.
                            panic!()
                        }
                        v4l2_buffer.set_field(BufferField::None);
                        v4l2_buffer.set_flags(BufferFlags::TIMESTAMP_MONOTONIC);

                        Ok(Buffer {
                            state: BufferState::New,
                            v4l2_buffer,
                            fd,
                            offset,
                        })
                    })
            })
            .collect::<Result<_, _>>()?;

        Ok(v4l2_requestbuffers {
            count,
            type_: queue as u32,
            memory: memory as u32,
            capabilities: (BufferCapabilities::SUPPORTS_MMAP
                | BufferCapabilities::SUPPORTS_ORPHANED_BUFS)
                .bits(),
            ..Default::default()
        })
    }

    fn querybuf(
        &mut self,
        session: &Self::Session,
        queue: QueueType,
        index: u32,
    ) -> IoctlResult<v4l2r::ioctl::V4l2Buffer> {
        if queue != QueueType::VideoCapture {
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
        // Attempt to queue already queued buffer.
        if matches!(host_buffer.state, BufferState::Incoming) {
            return Err(libc::EINVAL);
        }

        host_buffer.set_state(BufferState::Incoming);
        session.queued_buffers.push_back(buffer.index() as usize);

        let buffer = host_buffer.v4l2_buffer.clone();

        if session.streaming {
            session.process_queued_buffers(&mut self.evt_queue)?;
        }

        Ok(buffer)
    }

    fn streamon(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != QueueType::VideoCapture || session.buffers.is_empty() {
            return Err(libc::EINVAL);
        }
        session.streaming = true;

        session.process_queued_buffers(&mut self.evt_queue)?;

        Ok(())
    }

    fn streamoff(&mut self, session: &mut Self::Session, queue: QueueType) -> IoctlResult<()> {
        if queue != QueueType::VideoCapture {
            return Err(libc::EINVAL);
        }
        session.streaming = false;
        session.queued_buffers.clear();
        for buffer in session.buffers.iter_mut() {
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
        match INPUTS.get(index as usize) {
            Some(&input) => Ok(input),
            None => Err(libc::EINVAL),
        }
    }
}
