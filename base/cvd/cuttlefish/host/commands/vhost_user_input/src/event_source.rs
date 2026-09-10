use std::collections::HashMap;
use std::io::{stdin, stdout, ErrorKind, Stdin, Write};
use std::os::fd::{AsFd, AsRawFd, BorrowedFd};
use std::os::unix::net::{UnixListener, UnixStream};
use std::sync::{Arc, Mutex};

use anyhow::{bail, Context, Result};
use log::{error, warn};
use nix::errno::Errno;
use nix::poll::{poll, PollFd, PollFlags, PollTimeout};
use vmm_sys_util::epoll::{ControlOperation, Epoll, EpollEvent, EventSet};

use crate::buf_reader::EventReader;

/// Provides input events to the vhost-user backend. Implementations can get these events from an
/// event device (e.g /dev/input/event0), a pipe, a server, etc.
/// The fd returned by as_fd() can be watched with a poll-like function, it will become readable
/// when new events are available. No assumption should be made about the nature of this fd, in
/// particular it should not be assumed this is an eventfd and the caller should never attempt to
/// read from it directly.
pub trait EventSource: Send + Sync + AsFd + Sized {
    /// Gets all input events available in this source. Input events are always delivered in groups
    /// ending with the SYN_REPORT type event. The returned list may be empty if only a partial
    /// group was read.
    fn get_events(&mut self) -> Result<Vec<u8>>;
    /// Send status feedback such as keyboard LED states back to the source.
    fn send_status_feedback(&mut self, status_buffer: Vec<u8>);
    /// Creates a new independenlty owned handle to the same underlying event source.
    fn try_clone(&self) -> Result<Self>;
}

/// Reads events from stdin, writes status feedback to stdout. Typically these channels will be
/// pipes connected to other processes or just /dev/null.
pub struct StdioEventSource {
    reader: EventReader<Stdin>,
}

impl StdioEventSource {
    pub fn new() -> StdioEventSource {
        StdioEventSource {
            reader: EventReader::new(stdin()),
        }
    }
}

impl AsFd for StdioEventSource {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.reader.as_fd()
    }
}

impl EventSource for StdioEventSource {
    fn get_events(&mut self) -> Result<Vec<u8>> {
        // This implementation can't recover from an errored or closed pipe
        self.reader
            .read_events()
            .context("Event source pipe closed")
    }

    fn send_status_feedback(&mut self, status_buffer: Vec<u8>) {
        if let Err(e) = stdout().write_all(&status_buffer) {
            warn!("Failed to send status feedback: {:?}", e);
        }
    }

    fn try_clone(&self) -> Result<Self> {
        Ok(StdioEventSource::new())
    }
}

/// Accepts connections on a unix socket and reads events from each client.
/// Guarantees that events from different clients are always separated by a SYN_REPORT event, but
/// events from multiple clients can still end up mixed in unexpected ways (such as a touch swipe
/// gesture appearing to jump around the screen).
pub struct UnixSocketEventSource {
    // This will become readable when new events are available. It will also notify the background
    // thread that it's time to stop when it closes on drop.
    events_fd: UnixStream,
    events: Arc<Mutex<Vec<u8>>>,
    status: Arc<Mutex<Vec<u8>>>,
    capture_sink: Arc<Mutex<Option<UnixStream>>>,
}

impl UnixSocketEventSource {
    pub fn new(listener: UnixListener) -> Result<Self> {
        let (fg_events_fd, bg_events_fd) =
            UnixStream::pair().context("Failed to create notifications socket pair")?;
        fg_events_fd
            .set_nonblocking(true)
            .context("Unable to set non-blocking")?;
        bg_events_fd
            .set_nonblocking(true)
            .context("Unable to set non-blocking")?;
        let events = Arc::new(Mutex::new(Vec::new()));
        let events_clone = events.clone();
        let status = Arc::new(Mutex::new(Vec::new()));
        let status_clone = events.clone();
        let capture_sink = Arc::new(Mutex::new(None));
        let capture_sink_clone = capture_sink.clone();
        std::thread::spawn(move || {
            server_loop(
                listener,
                bg_events_fd,
                events_clone,
                status_clone,
                capture_sink_clone,
            )
        });
        Ok(Self {
            events_fd: fg_events_fd,
            events,
            status,
            capture_sink,
        })
    }

    pub fn with_capture_server(
        listener: UnixListener,
        capture_server: UnixListener,
    ) -> Result<Self> {
        let res = Self::new(listener)?;
        let sink_clone = res.capture_sink.clone();
        std::thread::spawn(move || capture_loop(capture_server, sink_clone));
        Ok(res)
    }
}

impl AsFd for UnixSocketEventSource {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.events_fd.as_fd()
    }
}

impl EventSource for UnixSocketEventSource {
    fn get_events(&mut self) -> Result<Vec<u8>> {
        match std::io::copy(&mut self.events_fd, &mut std::io::sink()) {
            Err(e) if e.kind() == ErrorKind::WouldBlock => {}
            Err(e) => {
                bail!("Failed to read from background thread's channel: {:?}", e);
            }
            Ok(_) => {
                bail!("Background thread's channel closed unexpectedly");
            }
        }
        Ok(std::mem::take(&mut *self.events.lock().unwrap()))
    }

    fn send_status_feedback(&mut self, mut status_buffer: Vec<u8>) {
        self.status.lock().unwrap().append(&mut status_buffer);
        let _ = self.events_fd.write_all(&[0u8; 1]);
    }

    fn try_clone(&self) -> Result<Self> {
        Ok(Self {
            events_fd: self.events_fd.try_clone()?,
            events: self.events.clone(),
            status: self.status.clone(),
            capture_sink: self.capture_sink.clone(),
        })
    }
}

fn server_loop(
    listener: UnixListener,
    mut events_fd: UnixStream,
    events: Arc<Mutex<Vec<u8>>>,
    status: Arc<Mutex<Vec<u8>>>,
    capture_sink: Arc<Mutex<Option<UnixStream>>>,
) {
    const SERVER_TOKEN: u64 = 0;
    const EVENT_TOKEN: u64 = 1;
    const FIRST_CLIENT_TOKEN: u64 = 2;
    let mut clients = HashMap::<u64, EventReader<UnixStream>>::new();

    let epoll = Epoll::new().expect("Failed to create epoll instance");
    epoll
        .ctl(
            ControlOperation::Add,
            listener.as_raw_fd(),
            EpollEvent::new(EventSet::IN, SERVER_TOKEN),
        )
        .expect("Failed to add listener to epoll");
    epoll
        .ctl(
            ControlOperation::Add,
            events_fd.as_raw_fd(),
            EpollEvent::new(EventSet::IN | EventSet::HANG_UP, EVENT_TOKEN),
        )
        .expect("Failed to add events fd to epoll");
    let mut ready_events = [EpollEvent::default(); 16];
    loop {
        let num_ready = epoll
            .wait(-1, &mut ready_events)
            .expect("Failed to wait for events");
        for event in &ready_events[..num_ready] {
            match event.data() {
                SERVER_TOKEN => {
                    let client = match listener.accept() {
                        Ok((c, _)) => c,
                        Err(e) => {
                            error!("Failed to accept on event source server: {:?}", e);
                            continue;
                        }
                    };
                    if let Err(e) = client.set_nonblocking(true) {
                        error!("Failed to set events connection non-blocking: {:?}", e);
                        continue;
                    }
                    // New client available
                    let token = (FIRST_CLIENT_TOKEN
                        ..FIRST_CLIENT_TOKEN + (clients.len() as u64) + 1)
                        .find(|t| !clients.contains_key(t))
                        // Safe because we check more tokens than the map contains, so at least one
                        // isn't there.
                        .unwrap();
                    epoll
                        .ctl(
                            ControlOperation::Add,
                            client.as_raw_fd(),
                            EpollEvent::new(EventSet::IN | EventSet::HANG_UP, token),
                        )
                        .expect("Failed to add client stream to epoll");
                    clients.insert(token, EventReader::new(client));
                }
                EVENT_TOKEN => {
                    if event.event_set().contains(EventSet::HANG_UP) {
                        // Exit on closed events fd.
                        return;
                    }
                    match std::io::copy(&mut events_fd, &mut std::io::sink()) {
                        Err(e) if e.kind() == ErrorKind::WouldBlock => {}
                        Err(e) => {
                            panic!("Failed to read from background thread's channel: {:?}", e);
                        }
                        Ok(_) => {
                            // the other end of events_fd must be closed if io::copy succeeded
                            return;
                        }
                    }
                    let status = std::mem::take(&mut *status.lock().unwrap());
                    for client in clients.values_mut() {
                        // The client's inner reader is a UnixStream, so it can also write
                        match client.inner().write_all(&status) {
                            Err(e) if e.kind() != ErrorKind::WouldBlock => {
                                // Ignore WouldBlock errors since those are likely due to the client
                                // ignoring status feedback.
                                error!("Failed to send status feedback to client: {:?}", e);
                            }
                            _ => {}
                        }
                    }
                }
                client_token => {
                    let client = clients
                        .get_mut(&client_token)
                        .expect("epoll token is not associated to any fd");
                    match client.read_events() {
                        Ok(mut v) => {
                            if let Some(ref mut s) = *capture_sink.lock().unwrap() {
                                if let Err(e) = s.write_all(&v) {
                                    error!("Failed to write events to capture client: {:?}", e);
                                }
                            }
                            events.lock().unwrap().append(&mut v);
                            events_fd
                                .write_all(&[0u8; 1])
                                .expect("Failed to notify backend or available events");
                        }
                        Err(e) => {
                            if e.kind() != ErrorKind::UnexpectedEof {
                                error!("Error reading events: {:?}", e);
                            }
                            epoll
                                .ctl(
                                    ControlOperation::Delete,
                                    client.inner().as_raw_fd(),
                                    EpollEvent::default(),
                                )
                                .expect("Failed to remove client from epoll");
                            clients.remove(&client_token);
                        }
                    }
                }
            }
        }
    }
}

fn capture_loop(server: UnixListener, sink: Arc<Mutex<Option<UnixStream>>>) {
    loop {
        match server.accept() {
            Err(e) => {
                error!("Failed to accept connection on capture server: {:?}", e);
            }
            Ok((client, _)) => {
                if let Err(e) = client.set_nonblocking(true) {
                    error!(
                        "Failed to set capture client connection non-blocking: {:?}",
                        e
                    );
                    continue;
                }
                let client_clone = match client.try_clone() {
                    Err(e) => {
                        error!("Failed to clone capture client connection: {:?}", e);
                        continue;
                    }
                    Ok(c) => c,
                };
                let _ = sink.lock().unwrap().insert(client_clone);
                // Don't include POLLIN here, the peer could have closed its write channel
                let mut poll_fds = [PollFd::new(client.as_fd(), PollFlags::POLLHUP)];
                match poll(&mut poll_fds, PollTimeout::NONE) {
                    Ok(_) => {
                        continue;
                    }
                    Err(e) if e == Errno::EINTR => {
                        continue;
                    }
                    Err(e) => {
                        error!("Failed to poll capture client connection: {:?}", e);
                    }
                }
                let _ = sink.lock().unwrap().take();
            }
        }
    }
}
