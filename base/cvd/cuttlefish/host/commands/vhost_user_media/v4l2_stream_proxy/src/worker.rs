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

use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::fd::AsFd;
use std::os::unix::fs::OpenOptionsExt;
use std::sync::mpsc::{Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Instant;

use nix::poll::{PollFd, PollFlags, PollTimeout, poll};
use nix::sys::eventfd::EventFd;
use virtio_media::VirtioMediaEventQueue;
use virtio_media::protocol::{DequeueBufferEvent, V4l2Event};

use crate::Config;
use crate::device::{BufferState, SessionState};

#[derive(Debug)]
pub(crate) enum WorkerCmd {
    Stop,
    BufferQueued,
}

pub(crate) struct WorkerHandle {
    pub(crate) tx: Sender<WorkerCmd>,
    pub(crate) event_fd: Arc<EventFd>,
    pub(crate) join_handle: JoinHandle<()>,
}

/// Explicit representation of the worker thread states.
enum WorkerState {
    /// FIFO is closed. Trying to open it.
    Unopened,
    /// FIFO is open, but waiting for buffers.
    Idle { fifo_file: File },
    /// FIFO is open and streaming into a buffer.
    Streaming {
        fifo_file: File,
        buffer_idx: usize,
        plane_idx: usize,
        plane_offset: usize,
    },
    /// Terminal state. Thread should exit.
    Stopped,
}

/// Helper function to drain control channel and check for Stop command.
fn should_stop(rx: &Receiver<WorkerCmd>, event_fd: &EventFd) -> bool {
    // Read eventfd to clear the notification
    if let Err(e) = event_fd.read() {
        if e != nix::Error::EWOULDBLOCK {
            log::error!("Failed to read eventfd: {:?}", e);
        }
    }
    // Drain channel
    let mut stop = false;
    while let Ok(cmd) = rx.try_recv() {
        if let WorkerCmd::Stop = cmd {
            stop = true;
        }
    }
    stop
}

fn handle_unopened(
    config: &Config,
    session_state: &Mutex<SessionState>,
    rx: &Receiver<WorkerCmd>,
    event_fd: &EventFd,
) -> WorkerState {
    match std::fs::OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NONBLOCK)
        .open(&config.input_path)
    {
        Ok(file) => {
            log::info!("FIFO opened");
            let mut s_state = session_state.lock().unwrap();
            if let Some(buf_idx) = s_state.queued_buffers.pop_front() {
                WorkerState::Streaming {
                    fifo_file: file,
                    buffer_idx: buf_idx,
                    plane_idx: 0,
                    plane_offset: 0,
                }
            } else {
                WorkerState::Idle { fifo_file: file }
            }
        }
        Err(e) => {
            log::error!("Failed to open FIFO: {:?}", e);
            if should_stop(rx, event_fd) {
                log::info!("Worker thread stopped during open retry");
                WorkerState::Stopped
            } else {
                WorkerState::Unopened
            }
        }
    }
}

fn handle_idle(
    fifo_file: File,
    session_state: &Mutex<SessionState>,
    rx: &Receiver<WorkerCmd>,
    event_fd: &EventFd,
) -> WorkerState {
    let mut poll_fds = [PollFd::new(event_fd.as_fd(), PollFlags::POLLIN)];
    match poll(&mut poll_fds, PollTimeout::NONE) {
        Ok(_) => {}
        Err(e) if e == nix::Error::EINTR => return WorkerState::Idle { fifo_file },
        Err(e) => {
            log::error!("Poll error in Idle: {:?}", e);
            return WorkerState::Idle { fifo_file };
        }
    }

    if poll_fds[0]
        .revents()
        .unwrap_or(PollFlags::empty())
        .contains(PollFlags::POLLIN)
    {
        if should_stop(rx, event_fd) {
            return WorkerState::Stopped;
        }
        let mut s_state = session_state.lock().unwrap();
        if let Some(buf_idx) = s_state.queued_buffers.pop_front() {
            WorkerState::Streaming {
                fifo_file,
                buffer_idx: buf_idx,
                plane_idx: 0,
                plane_offset: 0,
            }
        } else {
            WorkerState::Idle { fifo_file }
        }
    } else {
        WorkerState::Idle { fifo_file }
    }
}

fn handle_streaming<Q: VirtioMediaEventQueue>(
    mut fifo_file: File,
    buffer_idx: usize,
    mut plane_idx: usize,
    mut plane_offset: usize,
    session_state: &Mutex<SessionState>,
    rx: &Receiver<WorkerCmd>,
    event_fd: &EventFd,
    plane_sizes: &[usize],
    local_buf: &mut [u8],
    evt_queue: &Mutex<Q>,
) -> WorkerState {
    let mut poll_fds = [
        PollFd::new(event_fd.as_fd(), PollFlags::POLLIN),
        PollFd::new(fifo_file.as_fd(), PollFlags::POLLIN),
    ];
    match poll(&mut poll_fds, PollTimeout::NONE) {
        Ok(_) => {}
        Err(e) if e == nix::Error::EINTR => {
            return WorkerState::Streaming {
                fifo_file,
                buffer_idx,
                plane_idx,
                plane_offset,
            };
        }
        Err(e) => {
            log::error!("Poll error in Streaming: {:?}", e);
            return WorkerState::Streaming {
                fifo_file,
                buffer_idx,
                plane_idx,
                plane_offset,
            };
        }
    }

    if poll_fds[0]
        .revents()
        .unwrap_or(PollFlags::empty())
        .contains(PollFlags::POLLIN)
    {
        if should_stop(rx, event_fd) {
            return WorkerState::Stopped;
        }
    }

    // When the FIFO writer disconnects and the buffer is drained, Linux poll()
    // reports only POLLHUP (without POLLIN). Handling POLLHUP ensures we call
    // read() to consume remaining bytes and receive Ok(0) (EOF) to cleanly
    // transition to WorkerState::Unopened rather than busy-looping on poll().
    if poll_fds[1]
        .revents()
        .unwrap_or(PollFlags::empty())
        .intersects(PollFlags::POLLIN | PollFlags::POLLHUP)
    {
        let plane_size = plane_sizes[plane_idx];
        let remaining = plane_size - plane_offset;
        let read_chunk = std::cmp::min(local_buf.len(), remaining);

        match fifo_file.read(&mut local_buf[..read_chunk]) {
            Ok(0) => {
                log::info!("FIFO EOF, writer disconnected. Re-opening...");
                WorkerState::Unopened
            }
            Ok(bytes_read) => {
                let mut s_state = session_state.lock().unwrap();
                let session_id = s_state.id;
                let buffer = &mut s_state.buffers[buffer_idx];
                let plane = &mut buffer.planes[plane_idx];

                if let Err(e) = plane
                    .fd
                    .as_file()
                    .seek(SeekFrom::Start(plane_offset as u64))
                {
                    log::error!("Seek error: {:?}", e);
                    return WorkerState::Stopped;
                }

                if let Err(e) = plane.fd.as_file().write_all(&local_buf[..bytes_read]) {
                    log::error!("Write error: {:?}", e);
                    return WorkerState::Stopped;
                }

                plane_offset += bytes_read;
                if plane_offset == plane_size {
                    plane_idx += 1;
                    plane_offset = 0;

                    if plane_idx == plane_sizes.len() {
                        let sequence = s_state.sequence;
                        s_state.sequence += 1;
                        let now = Instant::now();
                        let delta = now.duration_since(s_state.last_frame_time);
                        s_state.last_frame_time = now;

                        log::info!(
                            "Frame completed: session {}, seq {}, buf_idx {}, delta {:?}",
                            session_id,
                            sequence,
                            buffer_idx,
                            delta
                        );

                        s_state.buffers[buffer_idx].set_state(BufferState::Outgoing { sequence });
                        let v4l2_buf = s_state.buffers[buffer_idx].v4l2_buffer.clone();

                        evt_queue
                            .lock()
                            .unwrap()
                            .send_event(V4l2Event::DequeueBuffer(DequeueBufferEvent::new(
                                session_id, v4l2_buf,
                            )));

                        if let Some(next_buf_idx) = s_state.queued_buffers.pop_front() {
                            WorkerState::Streaming {
                                fifo_file,
                                buffer_idx: next_buf_idx,
                                plane_idx: 0,
                                plane_offset: 0,
                            }
                        } else {
                            WorkerState::Idle { fifo_file }
                        }
                    } else {
                        WorkerState::Streaming {
                            fifo_file,
                            buffer_idx,
                            plane_idx,
                            plane_offset,
                        }
                    }
                } else {
                    WorkerState::Streaming {
                        fifo_file,
                        buffer_idx,
                        plane_idx,
                        plane_offset,
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => WorkerState::Streaming {
                fifo_file,
                buffer_idx,
                plane_idx,
                plane_offset,
            },
            Err(e) => {
                log::error!("Read error: {:?}", e);
                WorkerState::Unopened
            }
        }
    } else {
        WorkerState::Streaming {
            fifo_file,
            buffer_idx,
            plane_idx,
            plane_offset,
        }
    }
}

pub(crate) fn worker_thread_loop<Q: VirtioMediaEventQueue + Send + 'static>(
    config: Config,
    evt_queue: Arc<Mutex<Q>>,
    session_state: Arc<Mutex<SessionState>>,
    rx: Receiver<WorkerCmd>,
    event_fd: Arc<EventFd>,
) {
    log::info!("Worker thread started for FIFO: {:?}", config.input_path);

    let plane_sizes = config
        .format
        .plane_sizes(config.input_width, config.input_height);
    let mut local_buf = [0u8; 4096];

    let mut state = WorkerState::Unopened;

    while !matches!(state, WorkerState::Stopped) {
        state = match state {
            WorkerState::Unopened => handle_unopened(&config, &session_state, &rx, &event_fd),
            WorkerState::Idle { fifo_file } => {
                handle_idle(fifo_file, &session_state, &rx, &event_fd)
            }
            WorkerState::Streaming {
                fifo_file,
                buffer_idx,
                plane_idx,
                plane_offset,
            } => handle_streaming(
                fifo_file,
                buffer_idx,
                plane_idx,
                plane_offset,
                &session_state,
                &rx,
                &event_fd,
                &plane_sizes,
                &mut local_buf,
                &*evt_queue,
            ),
            WorkerState::Stopped => WorkerState::Stopped,
        };
    }

    log::info!("Worker thread stopped");
}
