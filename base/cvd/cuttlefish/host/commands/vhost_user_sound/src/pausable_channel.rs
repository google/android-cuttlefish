use std::collections::VecDeque;
use std::sync::{Condvar, Mutex};
use std::time::{Duration, Instant};

/// A multiple-producer-single-consumer channel. Unlike std's and crossbeam's channels, this
/// implementation allows pausing and activating the channel multiple times.
/// A paused channel always returns immediately with None when the recv* functions are called,
/// regardless of whether it has elements or not. It is ok to send new elements to a paused
/// channel.
pub struct PausableChannel<T> {
    inner: Mutex<Inner<T>>,
    condvar: Condvar,
}

impl<T: Send + 'static> PausableChannel<T> {
    /// Create a new paused channel.
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(Inner {
                queue: VecDeque::new(),
                active: false,
            }),
            condvar: Condvar::new(),
        }
    }

    pub fn activate(&self) {
        // A paused channel shouldn't have anyone waiting on the conditional variable, so there is
        // no need to notify.
        self.inner.lock().unwrap().active = true;
    }

    pub fn pause(&self) {
        self.inner.lock().unwrap().active = false;
        // Threads may be waiting for elements or a deadline.
        self.condvar.notify_all();
    }

    /// Send a new element through the channel. Elements can be sent while paused.
    pub fn send(&self, t: T) {
        let mut guard = self.inner.lock().unwrap();
        let should_notify = guard.queue.is_empty() && guard.active;
        guard.queue.push_back(t);
        if should_notify {
            self.condvar.notify_all();
        }
    }

    /// Receive an element from the channel or wait until one is sent. Returns None immediately if
    /// the channel is paused either at the time of the call or while waiting for elements.
    pub fn recv(&self) -> Option<T> {
        let mut guard = self.inner.lock().unwrap();
        loop {
            if !guard.active {
                return None;
            }
            if let Some(b) = guard.queue.pop_front() {
                break Some(b);
            }
            guard = self.condvar.wait(guard).unwrap();
        }
    }

    /// Similar to recv(), but it will wait until a specific instant before returning. The instant
    /// is calculated by a caller-provided function which can use the element to be received in
    /// the calculation, this means this function may sleep twice: first to wait for an element
    /// and then to wait for the instant to pass.
    pub fn recv_after<F: FnOnce(&T) -> Instant>(&self, f: F) -> Option<T> {
        let mut guard = self.inner.lock().unwrap();
        let period_end = loop {
            if !guard.active {
                return None;
            }
            match guard.queue.front() {
                Some(b) => break f(b),
                None => {
                    guard = self.condvar.wait(guard).unwrap();
                }
            }
        };

        loop {
            // TODO(jemoreira): Use a true sleep-until api. This one can sleep too much if the
            // thread is preempted between calculating sleep_duration and calling wait_timeout.
            let sleep_duration = period_end.saturating_duration_since(Instant::now());
            if sleep_duration > Duration::ZERO {
                guard = self.condvar.wait_timeout(guard, sleep_duration).unwrap().0;
            } else {
                // The queue should not be empty since the first loop waited for a buffer to be
                // available.
                return guard.queue.pop_front();
            }
            if !guard.active {
                return None;
            }
        }
    }

    /// Removes all elements sent through the channel but not yet received by a peer.
    pub fn drain(&self) -> VecDeque<T> {
        std::mem::take(&mut self.inner.lock().unwrap().queue)
    }
}

struct Inner<T> {
    queue: VecDeque<T>,
    active: bool,
}
