/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
//! This module provides a Vsock Server helper.

use log::info;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::thread;
use vsock::VsockListener;

/// Vsock server helper.
pub struct VsockServer {
    is_server_running: Arc<AtomicBool>,
    thread_handle: Option<thread::JoinHandle<()>>,
    connection_thread_sender: Option<mpsc::Sender<bool>>,
}

impl VsockServer {
    pub fn new() -> Self {
        Self {
            is_server_running: Arc::new(AtomicBool::new(false)),
            thread_handle: None,
            connection_thread_sender: None,
        }
    }

    /// Spawns a thread to start accepting connections from clients.
    pub fn start(&mut self, port: u32, cid: u32) {
        self.stop();
        info!("Starting vsocks server for Lights service on port {} cid {}", port, cid);

        self.is_server_running.store(true, Ordering::SeqCst);

        let is_running = self.is_server_running.clone();
        let (sender, receiver) = mpsc::channel::<bool>();
        let server = VsockListener::bind_with_cid_port(cid, port).expect("bind failed");

        self.thread_handle = Some(thread::spawn(move || {
            while is_running.load(Ordering::SeqCst) {
                let (mut _connection, _addr) = server.accept().expect("accept failed");
                info!("Lights service vsock server connection established.");

                // Block until we receive a message to send on the socket.
                let _ = receiver.recv().expect("Unable to read from channel");

                // TODO: finish client-server protocol implementation
            }
        }));

        self.connection_thread_sender = Some(sender);
    }

    /// Stops the server thread.
    pub fn stop(&mut self) {
        info!("Stopping vsocks server for Lights service");

        self.is_server_running.store(false, Ordering::SeqCst);

        // Send a message on the channel to unblock the recv() call.
        if self.connection_thread_sender.is_some() {
            self.connection_thread_sender
                .as_ref()
                .unwrap()
                .send(false)
                .expect("Unable to send on channel");
        }

        if self.thread_handle.is_some() {
            let handle = self.thread_handle.take().expect("Thread handle should be valid");
            if handle.is_finished() {
                handle.join().expect("Could not join thread");
            }
        }
    }
}

impl Default for VsockServer {
    fn default() -> Self {
        Self::new()
    }
}
