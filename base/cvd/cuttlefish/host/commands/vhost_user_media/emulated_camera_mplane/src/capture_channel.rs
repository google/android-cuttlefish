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

use anyhow::{Context, Result as AnyhowResult};
use std::collections::VecDeque;
use std::fs::File;
use std::io::{Seek, Write};
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::time::Duration;

use crate::device::{CameraControls, TestPattern};

/// Triple of planar files (Y, U, V) representing the capture target.
pub struct CapturePlanes {
    pub y: File,
    pub u: File,
    pub v: File,
}

impl CapturePlanes {
    /// Rewinds all plane file offsets back to 0.
    ///
    /// Buffers are reused across capture requests. Rewinding ensures each write
    /// starts at the beginning of the plane rather than appending past prior frames.
    pub fn rewind(&mut self) -> std::io::Result<()> {
        self.y.rewind()?;
        self.u.rewind()?;
        self.v.rewind()?;
        Ok(())
    }
}

/// A capture request submitted to the channel for processing.
pub struct CaptureRequest {
    pub buffer_id: usize,
    pub planes: CapturePlanes,
}

/// Event emitted by the capture channel when a request has been completed.
#[derive(Debug, Clone, Copy)]
pub struct CaptureCompletedEvent {
    pub buffer_id: usize,
    pub sequence: u32,
}

/// Control commands sent to the capture channel worker thread.
pub enum ChannelCmd {
    /// Start capturing at the given resolution.
    StartCapture { width: u32, height: u32 },
    /// Stop capturing and flush pending requests.
    StopCapture,
    /// Enqueue a capture request into the channel's request queue.
    Queue(CaptureRequest),
    /// Update channel controls (e.g. pattern, gain).
    SetControls(CameraControls),
    /// Terminate capture worker thread.
    Shutdown,
}

/// Generate capture data.
pub struct CaptureProcessor {
    session_id: u32,
    controls: CameraControls,
    width: u32,
    height: u32,
    streaming: bool,
    iteration: u64,
    pending_requests: VecDeque<CaptureRequest>,
    completion_tx: Sender<CaptureCompletedEvent>,
}

impl CaptureProcessor {
    pub fn new(
        session_id: u32,
        initial_controls: CameraControls,
        completion_tx: Sender<CaptureCompletedEvent>,
    ) -> Self {
        Self {
            session_id,
            controls: initial_controls,
            width: 640,
            height: 480,
            streaming: false,
            iteration: 0,
            pending_requests: VecDeque::new(),
            completion_tx,
        }
    }

    /// Runs the processor loop waiting on channel commands with a 30 FPS timeout.
    pub fn run(&mut self, cmd_rx: Receiver<ChannelCmd>) -> AnyhowResult<()> {
        const FRAME_INTERVAL: Duration = Duration::from_nanos(1_000_000_000 / 30);

        loop {
            let timeout = if self.streaming {
                FRAME_INTERVAL
            } else {
                Duration::MAX
            };

            match cmd_rx.recv_timeout(timeout) {
                Ok(ChannelCmd::Shutdown) => {
                    log::info!("Channel {}: shutdown received", self.session_id);
                    break;
                }
                Ok(cmd) => self.handle_cmd(cmd),
                Err(RecvTimeoutError::Timeout) => {
                    self.produce_capture()?;
                }
                Err(RecvTimeoutError::Disconnected) => {
                    log::info!(
                        "Channel {}: command sender disconnected, shutting down",
                        self.session_id
                    );
                    break;
                }
            }
        }
        Ok(())
    }

    fn handle_cmd(&mut self, cmd: ChannelCmd) {
        match cmd {
            ChannelCmd::StartCapture { width: w, height: h } => {
                self.width = w;
                self.height = h;
                self.streaming = true;
                self.iteration = 0;
            }
            ChannelCmd::StopCapture => {
                self.streaming = false;
                self.pending_requests.clear();
            }
            ChannelCmd::Queue(req) => {
                self.pending_requests.push_back(req);
            }
            ChannelCmd::SetControls(c) => {
                self.controls = c;
            }
            ChannelCmd::Shutdown => unreachable!(),
        }
    }

    fn produce_capture(&mut self) -> AnyhowResult<()> {
        if self.streaming && !self.pending_requests.is_empty() {
            let mut req = self.pending_requests.pop_front().unwrap();
            if let Err(e) = req.planes.rewind() {
                log::warn!(
                    "Channel {}: failed to rewind capture planes: {:#}",
                    self.session_id,
                    e
                );
            }

            if let Err(e) = Self::write_pattern(
                self.iteration,
                self.controls.test_pattern,
                &self.controls,
                self.width,
                self.height,
                &mut req.planes.y,
                &mut req.planes.u,
                &mut req.planes.v,
            ) {
                log::warn!(
                    "Channel {}: write_pattern failed: {:#}",
                    self.session_id,
                    e
                );
            }
            let seq = self.iteration as u32;
            self.iteration += 1;
            let _ = self.completion_tx.send(CaptureCompletedEvent {
                buffer_id: req.buffer_id,
                sequence: seq,
            });
        }
        Ok(())
    }

    fn write_pattern(
        iteration: u64,
        test_pattern: TestPattern,
        controls: &CameraControls,
        width: u32,
        height: u32,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> AnyhowResult<()> {
        test_pattern
            .generator()
            .write(iteration, controls, width, height, sink_y, sink_u, sink_v)
            .map_err(|e| anyhow::anyhow!("Pattern generator write error: errno {}", e))
    }
}

/// Handle for interacting with a capture channel.
pub struct CaptureChannel {
    cmd_tx: Sender<ChannelCmd>,
}

impl CaptureChannel {
    pub fn new(
        session_id: u32,
        initial_controls: CameraControls,
        completion_tx: Sender<CaptureCompletedEvent>,
    ) -> AnyhowResult<Self> {
        log::info!("channel: new");
        let (cmd_tx, cmd_rx) = std::sync::mpsc::channel();

        let mut processor = CaptureProcessor::new(session_id, initial_controls, completion_tx);

        std::thread::Builder::new()
            .name(format!("channel-{}", session_id))
            .spawn(move || processor.run(cmd_rx))
            .context("Failed to spawn capture processor thread")?;

        Ok(Self { cmd_tx })
    }

    /// Starts capturing on this channel at the specified dimensions.
    pub fn start_capture(&self, width: u32, height: u32) -> AnyhowResult<()> {
        self.send_cmd(ChannelCmd::StartCapture { width, height })
    }

    /// Stops capturing on this channel and flushes queued requests.
    pub fn stop_capture(&self) -> AnyhowResult<()> {
        self.send_cmd(ChannelCmd::StopCapture)
    }

    /// Queues target destination planes into the capture channel.
    pub fn queue_buffer(&self, buffer_id: usize, planes: CapturePlanes) -> AnyhowResult<()> {
        self.send_cmd(ChannelCmd::Queue(CaptureRequest { buffer_id, planes }))
    }

    /// Updates channel controls (e.g. test pattern, gain).
    pub fn set_controls(&self, controls: CameraControls) -> AnyhowResult<()> {
        self.send_cmd(ChannelCmd::SetControls(controls))
    }

    /// Signals the processor thread to terminate.
    pub fn shutdown(&self) {
        let _ = self.send_cmd(ChannelCmd::Shutdown);
    }

    fn send_cmd(&self, cmd: ChannelCmd) -> AnyhowResult<()> {
        self.cmd_tx
            .send(cmd)
            .context("Failed to send command to capture channel")
    }
}
