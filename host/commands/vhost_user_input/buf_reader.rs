use std::io::Read;

use anyhow::{bail, Context, Result};
use log::trace;

/// Object to read and temporarily store virtio input events.
/// An std::io::BufReader can't be used because it doesn't provide a way to read more bytes when
/// only a partial event has been read.
#[derive(Clone)]
pub struct BufReader<R: Read + Sync + Send> {
    buf: [u8; 8192],
    size: usize,
    reader: R,
}

impl<R: Read + Sync + Send> BufReader<R> {
    /// Create a new BufReader.
    pub fn new(reader: R) -> BufReader<R> {
        BufReader { buf: [0u8; 8192], size: 0, reader }
    }

    /// Reads available bytes from the underlying reader.
    pub fn read_ahead(&mut self) -> Result<()> {
        if self.size == self.buf.len() {
            // The buffer may be full when the driver doesn't provide virtq buffers to receive the
            // events.
            bail!("Event buffer is full");
        }
        let read = self.reader.read(&mut self.buf[self.size..]).context("Failed to read events")?;
        trace!("Read {} bytes", read);
        if read == 0 {
            bail!("Event source closed");
        }
        self.size += read;
        Ok(())
    }

    /// Returns a slice with the available bytes.
    pub fn buffer(&self) -> &[u8] {
        &self.buf[..self.size]
    }

    /// Remove consumed bytes from the buffer, making more space for future reads.
    pub fn consume(&mut self, count: usize) {
        self.buf.copy_within(count..self.size, 0);
        self.size -= count;
    }
}
