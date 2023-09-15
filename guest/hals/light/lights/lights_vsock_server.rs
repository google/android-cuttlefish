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

use anyhow::Context;
use log::{error, info};
use serde_json::json;
use std::io::Write;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::thread;
use vsock::{VsockListener, VsockStream};

/// Vsock server helper.
pub struct VsockServer {
    is_server_running: Arc<AtomicBool>,
    thread_handle: Option<thread::JoinHandle<anyhow::Result<()>>>,
    connection_thread_sender: Option<mpsc::Sender<Vec<u8>>>,
}

impl VsockServer {
    pub fn new() -> Self {
        Self {
            is_server_running: Arc::new(AtomicBool::new(false)),
            thread_handle: None,
            connection_thread_sender: None,
        }
    }

    /// Send the buffer length and then the buffer over a socket.
    fn send_buffer_with_length(
        mut connection: &VsockStream,
        buffer: Vec<u8>,
    ) -> anyhow::Result<()> {
        let vec_size = buffer.len() as u32;

        connection
            .write_all(&vec_size.to_le_bytes())
            .with_context(|| "Failed to send buffer length over socket")?;
        connection
            .write_all(buffer.as_slice())
            .with_context(|| "Failed to send buffer over socket")?;

        Ok(())
    }

    /// Spawns a thread to start accepting connections from clients.
    pub fn start(&mut self, port: u32, cid: u32) {
        self.stop();
        info!("Starting vsocks server for Lights service on port {} cid {}", port, cid);

        self.is_server_running.store(true, Ordering::SeqCst);

        let is_running = self.is_server_running.clone();
        let (sender, receiver) = mpsc::channel::<Vec<u8>>();
        let server = VsockListener::bind_with_cid_port(cid, port).expect("bind failed");

        self.thread_handle = Some(thread::spawn(move || -> anyhow::Result<()> {
            while is_running.load(Ordering::SeqCst) {
                let (connection, _addr) = server.accept()?;
                info!("Lights service vsock server connection established.");

                // Connection established, send the start session message.
                // If this fails it's because the connection dropped so we need
                // to start accepting connections from clients again.
                let start_message = json!({
                    "event": "VIRTUAL_DEVICE_START_LIGHTS_SESSION",
                });
                let mut json_as_vec = serde_json::to_vec(&start_message)?;
                Self::send_buffer_with_length(&connection, json_as_vec)?;

                // Receive messages from the channel and send them while the connection is valid.
                while is_running.load(Ordering::SeqCst) {
                    // Block until we receive a new message to send on the socket.
                    json_as_vec = receiver.recv().with_context(|| "Unable to read from channel")?;

                    match Self::send_buffer_with_length(&connection, json_as_vec) {
                        Ok(()) => continue,
                        Err(e) => {
                            error!("Failed to send buffer over socket. Error: {}", e);
                            break;
                        }
                    }
                }
            }

            Ok(())
        }));

        self.connection_thread_sender = Some(sender);
    }

    /// Stops the server thread.
    pub fn stop(&mut self) {
        info!("Stopping vsocks server for Lights service");

        self.is_server_running.store(false, Ordering::SeqCst);

        // Send the stop message on the channel. This will also unblock the recv() call.
        if self.connection_thread_sender.is_some() {
            let stop_message = json!({
                "event": "VIRTUAL_DEVICE_STOP_LIGHTS_SESSION",
            });

            self.connection_thread_sender
                .as_ref()
                .unwrap()
                .send(serde_json::to_vec(&stop_message).unwrap())
                .expect("Unable to send on channel");
        }

        if self.thread_handle.is_some() {
            let handle = self.thread_handle.take().expect("Thread handle should be valid");
            if handle.is_finished() {
                let thread_context = handle.join().expect("Could not join thread");
                info!("Connection thread exited with {:?}", thread_context);
            }
        }
    }
}

impl Default for VsockServer {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for VsockServer {
    fn drop(&mut self) {
        self.stop();
    }
}
