use std::io::{ErrorKind, Read};
use std::os::fd::{AsFd, BorrowedFd};

use anyhow::{bail, Context, Result};
use log::trace;

/// Object to read and temporarily store virtio input events.
/// An std::io::BufReader can't be used because it doesn't provide a way to read more bytes when
/// only a partial event has been read.
#[derive(Clone)]
pub struct BufReader<R: Read + AsFd + Sync + Send> {
    buf: [u8; 8192],
    size: usize,
    reader: R,
}

impl<R: Read + AsFd + Sync + Send> BufReader<R> {
    /// Create a new BufReader.
    pub fn new(reader: R) -> BufReader<R> {
        BufReader {
            buf: [0u8; 8192],
            size: 0,
            reader,
        }
    }

    /// Reads available bytes from the underlying reader. Returns number of bytes read during this
    /// call (which may be less than the resulting size of the buffer) or 0 on EOF.
    pub fn read_ahead(&mut self) -> Result<usize> {
        if self.size == self.buf.len() {
            // The buffer may be full if SYN_REPORT events are not sent.
            bail!("Event buffer is full");
        }
        let read = loop {
            match self.reader.read(&mut self.buf[self.size..]) {
                Err(e) if e.kind() == ErrorKind::Interrupted => continue,
                res => break res,
            }
        }
        .context("Failed to read events")?;
        trace!("Read {} bytes", read);
        self.size += read;
        Ok(read)
    }

    /// Returns a slice with the available bytes.
    pub fn buffer(&self) -> &[u8] {
        &self.buf[..self.size]
    }

    /// Remove consumed bytes from the buffer, making more space for future reads. Returns the
    /// consumed bytes in a vector.
    pub fn consume(&mut self, count: usize) -> Vec<u8> {
        let v = self.buf[..count].to_vec();
        self.buf.copy_within(count..self.size, 0);
        self.size -= count;
        v
    }
}

impl<R: Read + AsFd + Send + Sync> AsFd for BufReader<R> {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.reader.as_fd()
    }
}
