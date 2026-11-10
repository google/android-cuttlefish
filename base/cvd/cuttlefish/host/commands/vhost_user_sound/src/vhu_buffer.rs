use std::io::{Error as IoError, Read, Result as IoResult, Write};

use anyhow::{ensure, Context, Result};
use vhost_user_backend::VringRwLock;
use vhost_user_backend::VringT;
use virtio_queue::DescriptorChain;
use vm_memory::{GuestMemoryLoadGuard, GuestRegionCollection, GuestRegionMmap};
use zerocopy::byteorder::little_endian::U32 as Le32;
use zerocopy::{FromBytes, IntoBytes};

use crate::virtio_snd::{VirtioSndPcmStatus, VirtioSndPcmXfer, VirtioSndStatus};

const STATUS_MSG_SIZE: usize = std::mem::size_of::<VirtioSndPcmStatus>();
const XFER_HDR_SIZE: usize = std::mem::size_of::<VirtioSndPcmXfer>();

pub type VhuDescriptorChain =
    DescriptorChain<GuestMemoryLoadGuard<GuestRegionCollection<GuestRegionMmap>>>;

/// A wrapper around a descriptor chain containing sound data. There are specific implementations
/// for the two PCM stream directions.
pub trait VhuBuffer {
    /// Length of the pcm data buffer. Typically the total buffer size of the descriptor chain
    /// minus the header and status messages.
    fn len(&self) -> usize;

    /// Write the status message to the end of the writable section of the descriptor chain.
    /// Consumes the buffer since no more IO should be performed after finalizing.
    fn finalize(self, status: VirtioSndStatus, latency_bytes: u32) -> Result<CompletedBuffer>;

    /// Convenience method to finalize the buffer with status OK.
    fn finalize_ok(self, latency_bytes: u32) -> Result<CompletedBuffer>
    where
        Self: Sized,
    {
        self.finalize(VirtioSndStatus::Ok, latency_bytes)
    }
}

/// A wrapper around an output stream buffer.
pub struct PlaybackBuffer {
    desc_chain: VhuDescriptorChain,
    vring: VringRwLock,
    read_offset: usize,
    buffer_len: usize,
}

impl PlaybackBuffer {
    /// Create a new TX buffer from a descriptor chain. Reads the xfer header and returns the
    /// stream id alongside the buffer.
    pub fn new(desc_chain: VhuDescriptorChain, vring: VringRwLock) -> Result<(u32, Self)> {
        let writable_bytes = desc_chain.writer_at(0)?.available_bytes();
        ensure!(
            writable_bytes >= STATUS_MSG_SIZE,
            "TX buffer must have enough space to write the status message"
        );
        let mut reader = desc_chain.reader_at(0)?;
        let xfer_hdr = VirtioSndPcmXfer::read_from_io(&mut reader)
            .context("Failed to read xfer header from queue")?;
        let buffer_len = reader.available_bytes();
        Ok((
            xfer_hdr.stream_id.get(),
            Self {
                desc_chain,
                vring,
                read_offset: 0,
                buffer_len,
            },
        ))
    }
}

impl VhuBuffer for PlaybackBuffer {
    fn finalize(self, status: VirtioSndStatus, latency_bytes: u32) -> Result<CompletedBuffer> {
        self.desc_chain.write_status_at(0, status, latency_bytes)?;
        Ok(CompletedBuffer {
            head_index: self.desc_chain.head_index(),
            write_len: STATUS_MSG_SIZE,
            vring: self.vring,
        })
    }

    fn len(&self) -> usize {
        self.buffer_len
    }
}

impl Read for PlaybackBuffer {
    fn read(&mut self, buf: &mut [u8]) -> IoResult<usize> {
        // Don't allow reading beyond the sound buffer
        let read_limit = std::cmp::min(buf.len(), self.buffer_len - self.read_offset) as u64;
        let read_bytes = self
            .desc_chain
            .reader_at(XFER_HDR_SIZE + self.read_offset)
            .map_err(IoError::other)?
            .take(read_limit)
            .read(buf)?;
        self.read_offset += read_bytes;
        Ok(read_bytes)
    }
}

/// A wrapper around an input stream buffer.
pub struct CaptureBuffer {
    desc_chain: VhuDescriptorChain,
    vring: VringRwLock,
    write_offset: usize,
    buffer_len: usize,
}

impl CaptureBuffer {
    /// Create a new RX buffer from a descriptor chain. Reads the xfer header and returns the
    /// stream id alongside the buffer.
    pub fn new(desc_chain: VhuDescriptorChain, vring: VringRwLock) -> Result<(u32, Self)> {
        let writable_bytes = desc_chain
            .clone()
            .writer(desc_chain.memory())
            .context("Failed to create writer from descriptor")?
            .available_bytes();
        let buffer_len = writable_bytes
            .checked_sub(STATUS_MSG_SIZE)
            .context("RX buffer must have enough space to write the status message")?;
        let xfer_hdr = VirtioSndPcmXfer::read_from_io(&mut desc_chain.reader_at(0)?)
            .context("Failed to read xfer header from queue")?;
        Ok((
            xfer_hdr.stream_id.get(),
            Self {
                desc_chain,
                vring,
                write_offset: 0,
                buffer_len,
            },
        ))
    }
}

impl VhuBuffer for CaptureBuffer {
    fn finalize(self, status: VirtioSndStatus, latency_bytes: u32) -> Result<CompletedBuffer> {
        self.desc_chain
            .write_status_at(self.buffer_len, status, latency_bytes)?;
        Ok(CompletedBuffer {
            head_index: self.desc_chain.head_index(),
            write_len: self.buffer_len + STATUS_MSG_SIZE,
            vring: self.vring,
        })
    }

    fn len(&self) -> usize {
        self.buffer_len
    }
}

impl Write for CaptureBuffer {
    fn write(&mut self, buf: &[u8]) -> IoResult<usize> {
        // Don't allow writing beyond the sound buffer
        let write_size = std::cmp::min(self.buffer_len - self.write_offset, buf.len());
        let written = self
            .desc_chain
            .writer_at(self.write_offset)
            .map_err(IoError::other)?
            .write(&buf[..write_size])?;
        self.write_offset += written;
        Ok(written)
    }
    fn flush(&mut self) -> IoResult<()> {
        Ok(())
    }
}

/// Finalizes all buffers with Ok status and releases them. This preferable to finalizing and
/// releasing each buffer individually because it will notify the vring only once.
pub fn finalize_and_release_all<T: VhuBuffer, I: IntoIterator<Item = T>>(buffers: I) -> Result<()> {
    let mut completed_buffers: Vec<CompletedBuffer> = Vec::new();
    for buff in buffers {
        completed_buffers.push(buff.finalize_ok(0)?);
    }
    // All buffers are from the same vring because they all are of the same type T, so all vring
    // operations can be done on the first buffer's and a single notification is enough.
    let Some(mut vring_state) = completed_buffers.first().map(|b| b.vring.get_mut()) else {
        return Ok(());
    };
    for buffer in &completed_buffers {
        vring_state
            .add_used(buffer.head_index, buffer.write_len as u32)
            .context("Failed to add descriptor chain to used ring")?;
    }
    if vring_state.needs_notification().unwrap_or(true) {
        vring_state
            .signal_used_queue()
            .context("Failed to signal virtqueue")?;
    }
    Ok(())
}

/// Represents a completed buffer that can be returned to the virtual queue.
pub struct CompletedBuffer {
    head_index: u16,
    write_len: usize,
    vring: VringRwLock,
}

impl CompletedBuffer {
    /// Returns the buffer to the queue and signals the queue if needed. If multiple buffers from
    /// the same queue will be released together the finalize_and_release_all function should be
    /// used instead.
    pub fn release(self) -> Result<()> {
        let mut vring_state = self.vring.get_mut();
        vring_state
            .add_used(self.head_index, self.write_len as u32)
            .context("Failed to add descriptor chain to used ring")?;

        if vring_state.needs_notification().unwrap_or(true) {
            vring_state
                .signal_used_queue()
                .context("Failed to signal virtqueue")?;
        }
        Ok(())
    }
}

trait SndIoDescriptorChain {
    fn reader_at(&self, offset: usize) -> Result<virtio_queue::Reader<'_>>;
    fn writer_at(&self, offset: usize) -> Result<virtio_queue::Writer<'_>>;
    fn write_status_at(
        &self,
        offset: usize,
        status: VirtioSndStatus,
        latency_bytes: u32,
    ) -> Result<()>;
}

impl SndIoDescriptorChain for VhuDescriptorChain {
    fn reader_at(&self, offset: usize) -> Result<virtio_queue::Reader<'_>> {
        Ok(self.clone().reader(self.memory())?.split_at(offset)?)
    }

    fn writer_at(&self, offset: usize) -> Result<virtio_queue::Writer<'_>> {
        Ok(self.clone().writer(self.memory())?.split_at(offset)?)
    }

    fn write_status_at(
        &self,
        offset: usize,
        status: VirtioSndStatus,
        latency_bytes: u32,
    ) -> Result<()> {
        VirtioSndPcmStatus {
            status: Le32::new(status as u32),
            latency_bytes: Le32::new(latency_bytes),
        }
        .write_to_io(&mut self.writer_at(offset)?)
        .context("Failed to write pcm status")
    }
}
