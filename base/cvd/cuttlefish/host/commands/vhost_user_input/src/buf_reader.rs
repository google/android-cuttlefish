use std::io::{Error, ErrorKind, Read, Result};
use std::os::fd::{AsFd, BorrowedFd};

use log::trace;

use crate::vio_input::{trim_to_event_size_multiple, VIRTIO_INPUT_EVENT_SIZE};

/// Object to read and temporarily store virtio input events.
/// An std::io::BufReader can't be used because it doesn't provide a way to read more bytes when
/// only a partial event has been read.
#[derive(Clone)]
pub struct EventReader<R: Read + AsFd + Send + Sync> {
    buf: [u8; 8192],
    size: usize,
    reader: R,
}

impl<R: Read + AsFd + Send + Sync> EventReader<R> {
    /// Create a new EventReader.
    pub fn new(reader: R) -> Self {
        Self {
            buf: [0u8; 8192],
            size: 0,
            reader,
        }
    }

    /// Reads more bytes from the inner reader. Returns a list of events once a full event
    /// group has been read, otherwise an empty list. Returns error if the underlying reader
    /// produces an error or reaches end of file (ErrorKind::UnexpectedEof).
    pub fn read_events(&mut self) -> Result<Vec<u8>> {
        let read_len = self.read_ahead()?;
        if read_len == 0 {
            // EOF reached or error
            return Err(Error::new(
                ErrorKind::UnexpectedEof,
                "Reached end of event stream",
            ));
        }
        // These events were checked on previous calls.
        let skip_len = trim_to_event_size_multiple(self.buffer().len() - read_len);
        let mut len = trim_to_event_size_multiple(self.buffer().len());
        // Search from the last fully read event for SYN_REPORT. Reading from the end ensures
        // all fully read event groups are consumed. Stop searching at events checked on a previous
        // read.
        while len > skip_len {
            // i >= previous_size because both previous_size and len are multiples of event size
            let i = len - VIRTIO_INPUT_EVENT_SIZE;
            // SYN_REPORT events have the first 4 bytes set to 0
            if self.buffer()[i..i + 4] == [0u8; 4] {
                // Consume events from the range [0..len]
                return Ok(self.consume(len));
            }
            len -= VIRTIO_INPUT_EVENT_SIZE;
        }
        Ok(Vec::new())
    }

    pub fn inner(&mut self) -> &mut R {
        &mut self.reader
    }

    /// Reads available bytes from the inner reader. Returns number of bytes read during this
    /// call (which may be less than the resulting size of the buffer) or 0 on EOF.
    fn read_ahead(&mut self) -> Result<usize> {
        if self.size == self.buf.len() {
            // The buffer may be full if SYN_REPORT events are not sent.
            return Err(Error::new(ErrorKind::OutOfMemory, "Event buffer is full"));
        }
        let read = loop {
            match self.reader.read(&mut self.buf[self.size..]) {
                Err(e) if e.kind() == ErrorKind::Interrupted => continue,
                res => break res,
            }
        }?;
        trace!("Read {} bytes", read);
        self.size += read;
        Ok(read)
    }

    /// Returns a slice with the available bytes.
    fn buffer(&self) -> &[u8] {
        &self.buf[..self.size]
    }

    /// Remove consumed bytes from the buffer, making more space for future reads. Returns the
    /// consumed bytes in a vector.
    fn consume(&mut self, count: usize) -> Vec<u8> {
        let v = self.buf[..count].to_vec();
        self.buf.copy_within(count..self.size, 0);
        self.size -= count;
        v
    }
}

impl<R: Read + AsFd + Send + Sync> AsFd for EventReader<R> {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.reader.as_fd()
    }
}
