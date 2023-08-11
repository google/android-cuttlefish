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
use std::sync::{Arc, Mutex};
use std::thread;
use vsock_utils::VsockServerConnection;

/// Vsock server helper.
pub struct VsockServer {
    vsock_connection: Arc<Mutex<VsockServerConnection>>,
    is_server_running: Arc<AtomicBool>,
    thread_handle: Option<thread::JoinHandle<()>>,
}

impl VsockServer {
    pub fn new() -> Self {
        Self {
            vsock_connection: Arc::new(Mutex::new(VsockServerConnection::new())),
            is_server_running: Arc::new(AtomicBool::new(false)),
            thread_handle: None,
        }
    }

    /// Spawns a thread to start accepting connections from clients.
    pub fn start(&mut self, port: u32, cid: u32) {
        self.stop();
        info!("Starting vsocks server for Lights service on port {} cid {}", port, cid);

        self.is_server_running.store(true, Ordering::SeqCst);

        let vsock_connection = self.vsock_connection.clone();
        let is_running = self.is_server_running.clone();
        self.thread_handle = Some(thread::spawn(move || {
            while is_running.load(Ordering::SeqCst) {
                if vsock_connection.lock().unwrap().connect(port, cid) {
                    info!("Lights service Vsock server connection established.");
                    // TODO: finish this once we know the protocol
                }
            }
        }));
    }

    /// Stops the server thread.
    pub fn stop(&mut self) {
        info!("Stopping vsocks server for Lights service");

        self.vsock_connection.lock().unwrap().server_shutdown();
        self.is_server_running.store(false, Ordering::SeqCst);

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
