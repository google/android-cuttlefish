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
use std::io::Write;
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant};

use crate::device::FrameSource;
use crate::format::Yuv420m;
use crate::render_params::RenderParams;

/// Where a capture request is rendered.
pub trait CaptureSink: Send + Sync {
    /// Returns the planes as sequential sinks, each positioned at its start.
    fn planes(&mut self) -> std::io::Result<[&mut dyn Write; Yuv420m::NUM_PLANES]>;
}

/// A capture request submitted to the channel for processing.
pub struct CaptureRequest {
    pub buffer_id: usize,
    pub sink: Box<dyn CaptureSink>,
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
    /// Update the render parameters (e.g. pattern, gain).
    SetRenderParams(RenderParams),
    /// Terminate capture worker thread.
    Shutdown,
}

/// Paces periodic capture events with zero cumulative drift.
///
/// Each tick deadline is calculated from the initial start time:
///
///     target_time(k) = stream_start + (k * interval)
///     wait_duration  = max(0, target_time(k) - now)
///
/// For example, at 30 FPS (`interval ≈ 33.333 ms`):
/// - Tick 1 target: `stream_start + 1 * 33.333 ms` = 33.333 ms
/// - Tick 2 target: `stream_start + 2 * 33.333 ms` = 66.666 ms
/// - Tick 3 target: `stream_start + 3 * 33.333 ms` = 99.999 ms
///
/// Processing time between ticks is absorbed into `wait_duration`, maintaining
/// a constant average interval over time. For example, if processing tick 1 takes 2.0 ms
/// (`now = 35.333 ms`), the wait for tick 2 is:
///
///     wait_duration = 66.666 ms - 35.333 ms = 31.333 ms
///
/// If execution stalls beyond an entire interval:
///
///     expected_ticks_count = (now - stream_start) / interval
///     dropped_ticks        = expected_ticks_count - last_ticks_count
///
/// For example, if a 100 ms stall occurs after tick 1 (`now = 134.8 ms`):
/// - `expected_ticks_count = 134.8 ms / 33.333 ms = 4`
/// - `dropped_ticks = 4 - 1 = 3` (ticks 2, 3, and 4 missed during the stall)
/// - Next scheduled deadline is tick 5: `stream_start + 5 * 33.333 ms` = 166.665 ms
///
/// Missed ticks are skipped so the schedule stays locked to elapsed time without burst-firing.
#[derive(Debug)]
pub struct CaptureClock {
    interval: Duration,
    stream_start: Instant,
    next_tick: Instant,
    ticks_count: u64,
}

impl CaptureClock {
    /// Creates a new capture clock configured with the specified tick interval.
    pub fn new(interval: Duration) -> Self {
        let now = Instant::now();
        Self {
            interval,
            stream_start: now,
            next_tick: now,
            ticks_count: 0,
        }
    }

    /// Creates a new capture clock pacing at the specified rate in ticks per second.
    pub fn for_fps(fps: u32) -> Self {
        Self::new(Duration::from_nanos(1_000_000_000 / fps as u64))
    }

    /// Starts the timeline from the current instant.
    pub fn start(&mut self) {
        let now = Instant::now();
        self.stream_start = now;
        self.ticks_count = 1;
        self.next_tick = now + self.interval;
    }

    /// Returns the exact duration remaining until the next scheduled tick.
    ///
    /// If the scheduled tick deadline has already passed, returns `Duration::ZERO`.
    pub fn time_until_next_tick(&self) -> Duration {
        let now = Instant::now();
        if now >= self.next_tick {
            Duration::ZERO
        } else {
            self.next_tick - now
        }
    }

    /// Advances the clock to the next periodic tick deadline, detecting and compensating for overruns.
    ///
    /// Returns the number of dropped ticks if execution was delayed longer than an entire interval
    pub fn advance(&mut self) -> u64 {
        let now = Instant::now();
        let target = self.stream_start
            + Duration::from_nanos(
                self.ticks_count
                    .saturating_mul(self.interval.as_nanos() as u64),
            );

        let mut dropped = 0;
        if now > target + self.interval {
            let elapsed = now.duration_since(self.stream_start);
            let expected_ticks_count = (elapsed.as_nanos() / self.interval.as_nanos()) as u64;
            assert!(
                expected_ticks_count >= self.ticks_count,
                "expected_ticks_count ({expected_ticks_count}) cannot be less than ticks_count ({})",
                self.ticks_count
            );
            dropped = expected_ticks_count - self.ticks_count;
            self.ticks_count = expected_ticks_count;
        }

        self.ticks_count += 1;
        self.next_tick = self.stream_start
            + Duration::from_nanos(
                self.ticks_count
                    .saturating_mul(self.interval.as_nanos() as u64),
            );

        dropped
    }
}

/// Generate capture data.
pub struct CaptureProcessor {
    session_id: u32,
    params: RenderParams,
    frame_source: FrameSource,
    width: u32,
    height: u32,
    streaming: bool,
    iteration: u64,
    pending_requests: VecDeque<CaptureRequest>,
    completion_tx: Sender<CaptureCompletedEvent>,
    clock: CaptureClock,
}

impl CaptureProcessor {
    pub fn new(
        session_id: u32,
        initial_params: RenderParams,
        frame_source: FrameSource,
        completion_tx: Sender<CaptureCompletedEvent>,
    ) -> Self {
        Self {
            session_id,
            params: initial_params,
            frame_source,
            width: 640,
            height: 480,
            streaming: false,
            iteration: 0,
            pending_requests: VecDeque::new(),
            completion_tx,
            clock: CaptureClock::for_fps(30),
        }
    }

    /// Runs the processor loop waiting on channel commands with periodic drift-free cadence.
    pub fn run(&mut self, cmd_rx: Receiver<ChannelCmd>) -> AnyhowResult<()> {
        loop {
            if self.streaming {
                let delay = self.clock.time_until_next_tick();
                match cmd_rx.recv_timeout(delay) {
                    Ok(ChannelCmd::Shutdown) => {
                        log::info!("Channel {}: shutdown received", self.session_id);
                        break;
                    }
                    Ok(cmd) => self.handle_cmd(cmd),
                    Err(RecvTimeoutError::Timeout) => self.produce_capture()?,
                    Err(RecvTimeoutError::Disconnected) => {
                        log::info!(
                            "Channel {}: command sender disconnected, shutting down",
                            self.session_id
                        );
                        break;
                    }
                }
            } else {
                match cmd_rx.recv() {
                    Ok(ChannelCmd::Shutdown) => {
                        log::info!("Channel {}: shutdown received", self.session_id);
                        break;
                    }
                    Ok(cmd) => self.handle_cmd(cmd),
                    Err(_) => {
                        log::info!(
                            "Channel {}: command sender disconnected, shutting down",
                            self.session_id
                        );
                        break;
                    }
                }
            }
        }
        Ok(())
    }

    fn handle_cmd(&mut self, cmd: ChannelCmd) {
        match cmd {
            ChannelCmd::StartCapture {
                width: w,
                height: h,
            } => {
                self.width = w;
                self.height = h;
                self.streaming = true;
                self.iteration = 0;
                self.clock.start();
            }
            ChannelCmd::StopCapture => {
                self.streaming = false;
                self.pending_requests.clear();
            }
            ChannelCmd::Queue(req) => {
                self.pending_requests.push_back(req);
            }
            ChannelCmd::SetRenderParams(params) => {
                self.params = params;
            }
            ChannelCmd::Shutdown => unreachable!(),
        }
    }

    fn produce_capture(&mut self) -> AnyhowResult<()> {
        let seq = self.iteration as u32;
        self.iteration += 1;

        if let Some(mut req) = self.pending_requests.pop_front() {
            match req.sink.planes() {
                Ok([sink_y, sink_u, sink_v]) => {
                    if let Err(e) = Self::write_pattern(
                        seq as u64,
                        self.frame_source,
                        &self.params,
                        self.width,
                        self.height,
                        sink_y,
                        sink_u,
                        sink_v,
                    ) {
                        log::warn!("Channel {}: write_pattern failed: {:#}", self.session_id, e);
                    }
                }
                Err(e) => log::warn!(
                    "Channel {}: failed to access capture planes: {:#}",
                    self.session_id,
                    e
                ),
            }
            let _ = self.completion_tx.send(CaptureCompletedEvent {
                buffer_id: req.buffer_id,
                sequence: seq,
            });
        } else {
            log::trace!(
                "Channel {}: capture tick underrun (sequence {}, no request queued)",
                self.session_id,
                seq
            );
        }

        let dropped = self.clock.advance();
        if dropped > 0 {
            log::debug!(
                "Channel {}: timing overrun, skipping {} ticks",
                self.session_id,
                dropped
            );
            self.iteration += dropped;
        }
        Ok(())
    }

    fn write_pattern(
        iteration: u64,
        frame_source: FrameSource,
        params: &RenderParams,
        width: u32,
        height: u32,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> AnyhowResult<()> {
        frame_source
            .generator(params)
            .write(iteration, params, width, height, sink_y, sink_u, sink_v)
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
        initial_params: RenderParams,
        frame_source: FrameSource,
        completion_tx: Sender<CaptureCompletedEvent>,
    ) -> AnyhowResult<Self> {
        log::info!("channel: new");
        let (cmd_tx, cmd_rx) = std::sync::mpsc::channel();

        let mut processor =
            CaptureProcessor::new(session_id, initial_params, frame_source, completion_tx);

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

    /// Queues a capture request for `buffer_id` into the capture channel.
    pub fn queue_buffer(&self, buffer_id: usize, sink: Box<dyn CaptureSink>) -> AnyhowResult<()> {
        self.send_cmd(ChannelCmd::Queue(CaptureRequest { buffer_id, sink }))
    }

    /// Updates the render parameters (e.g. test pattern, gain).
    pub fn set_render_params(&self, params: RenderParams) -> AnyhowResult<()> {
        self.send_cmd(ChannelCmd::SetRenderParams(params))
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
