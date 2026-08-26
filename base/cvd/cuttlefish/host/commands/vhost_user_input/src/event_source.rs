use std::io::{Read, Write};
use std::os::fd::{AsFd, BorrowedFd};

use log::{error, warn};

use crate::buf_reader::BufReader;
use crate::vio_input::{trim_to_event_size_multiple, VIRTIO_INPUT_EVENT_SIZE};

/// Provides input events to the vhost-user backend. Implementations may can get these events from
/// an event device (e.g /dev/input/event0), a pipe, a server, etc.
/// The fd returned by as_fd() can be watched with a poll-like function, it will become readable
/// when new events are available. No assumption should be made about the nature of this fd, in
/// particular it should not be assumed this is an eventfd and the caller should never attempt to
/// read from it directly.
pub trait EventSource: Sync + Send + AsFd {
    /// Gets all input events available in this source. Input events are always delivered in groups
    /// ending with the SYN_REPORT type event. The returned list may be empty if only a partial
    /// groups was read.
    fn get_events(&mut self) -> Vec<u8>;
    /// Send status feedback such as keyboard LED states back to the source.
    fn send_status_feedback(&mut self, status_buffer: &[u8]);
}

/// Reads events from a pipe
pub struct PipeEventSource<R: Read + AsFd + Sync + Send, W: Write + Send + Sync> {
    reader: BufReader<R>,
    writer: W,
}

impl<R: Read + AsFd + Send + Sync, W: Write + Send + Sync> PipeEventSource<R, W> {
    pub fn new(reader: R, writer: W) -> PipeEventSource<R, W> {
        PipeEventSource::<R, W> {
            reader: BufReader::new(reader),
            writer,
        }
    }
}

impl<R: Read + AsFd + Send + Sync, W: Write + Send + Sync> AsFd for PipeEventSource<R, W> {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.reader.as_fd()
    }
}

impl<R: Read + AsFd + Send + Sync, W: Write + Send + Sync> EventSource for PipeEventSource<R, W> {
    fn get_events(&mut self) -> Vec<u8> {
        match read_events(&mut self.reader) {
            // This implementation can't recover from an errored or closed pipe
            ReadResult::SourceClosed => panic!("Event source pipe closed"),
            ReadResult::Events(v) => v,
        }
    }

    fn send_status_feedback(&mut self, status_buffer: &[u8]) {
        if let Err(e) = self.writer.write_all(status_buffer) {
            warn!("Failed to send status feedback: {:?}", e);
        }
    }
}

enum ReadResult {
    SourceClosed,
    Events(Vec<u8>),
}
fn read_events<R: Read + AsFd + Send + Sync>(reader: &mut BufReader<R>) -> ReadResult {
    let read_len = match reader.read_ahead() {
        Ok(len) => len,
        Err(e) => {
            error!("Error reading from event source: {}", e);
            0usize
        }
    };
    if read_len == 0 {
        // EOF reached or error
        return ReadResult::SourceClosed;
    }
    // These events were checked on previous calls.
    let skip_len = trim_to_event_size_multiple(reader.buffer().len() - read_len);
    let mut len = trim_to_event_size_multiple(reader.buffer().len());
    let mut v = Vec::<u8>::new();
    // Search from the last fully read event for SYN_REPORT. Reading from the end ensures
    // all fully read event groups are consumed. Stop searching at events checked on a previous
    // read.
    while len > skip_len {
        // i >= previous_size because both previous_size and len are multiples of event size
        let i = len - VIRTIO_INPUT_EVENT_SIZE;
        // SYN_REPORT events have the first 4 bytes set to 0
        if reader.buffer()[i..i + 4] == [0u8; 4] {
            // Consume events from the range [0..len]
            v = reader.consume(len);
            break;
        }
        len -= VIRTIO_INPUT_EVENT_SIZE;
    }
    ReadResult::Events(v)
}

