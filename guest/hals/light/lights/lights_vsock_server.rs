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
use nix::sys::socket::{connect, socket, AddressFamily, SockFlag, SockType};
use serde::Serialize;
use serde_json::json;
use std::io::Write;
use std::os::fd::AsRawFd;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::thread;
use vsock::{VsockAddr, VsockListener, VsockStream};

/// Serializable Light information.
#[derive(Serialize)]
pub struct SerializableLight {
    id: u32,
    color: u32,
    light_type: u8,
    // Should be expanded as needed by improvements to the client.
}

impl SerializableLight {
    pub fn new(id: u32, color: u32, light_type: u8) -> Self {
        Self { id, color, light_type }
    }
}

/// Vsock server helper.
pub struct VsockServer {
    is_server_running: Arc<AtomicBool>,
    thread_handle: Option<thread::JoinHandle<anyhow::Result<()>>>,
    connection_thread_sender: mpsc::Sender<Vec<u8>>,
    guest_port: u32,
}

impl VsockServer {
    pub fn new(port: u32) -> anyhow::Result<Self> {
        let (sender, receiver) = mpsc::channel::<Vec<u8>>();
        let server = VsockListener::bind_with_cid_port(vsock::VMADDR_CID_ANY, port)?;
        let running_atomic = Arc::new(AtomicBool::new(true));

        Ok(Self {
            thread_handle: Some({
                let is_running = running_atomic.clone();
                thread::spawn(move || -> anyhow::Result<()> {
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
                            json_as_vec =
                                receiver.recv().with_context(|| "Unable to read from channel")?;

                            if let Err(e) = Self::send_buffer_with_length(&connection, json_as_vec)
                            {
                                error!("Failed to send buffer over socket. Error: {}", e);
                                break;
                            }
                        }
                    }

                    Ok(())
                })
            }),
            is_server_running: running_atomic,
            connection_thread_sender: sender,
            guest_port: port,
        })
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

    pub fn send_lights_state(&self, lights: Vec<SerializableLight>) {
        let update_message = json!({
            "event": "VIRTUAL_DEVICE_LIGHTS_UPDATE",
            "lights": lights,
        });
        self.connection_thread_sender
            .send(serde_json::to_vec(&update_message).unwrap())
            .expect("Unable to send update on channel");
    }
}

impl Drop for VsockServer {
    fn drop(&mut self) {
        info!("Stopping vsocks server for Lights service");

        self.is_server_running.store(false, Ordering::SeqCst);

        // Send the stop message on the channel. This will also unblock the recv() call.
        let stop_message = json!({
            "event": "VIRTUAL_DEVICE_STOP_LIGHTS_SESSION",
        });
        self.connection_thread_sender
            .send(serde_json::to_vec(&stop_message).unwrap())
            .expect("Unable to send on channel");

        // Try to connect to the server socket locally to unblock the connection
        // thread just in case it was blocked on accept() instead.
        let fd = socket(
            AddressFamily::Vsock,
            SockType::Stream,
            SockFlag::SOCK_NONBLOCK | SockFlag::SOCK_CLOEXEC,
            None,
        )
        .unwrap();
        let addr = VsockAddr::new(vsock::VMADDR_CID_LOCAL, self.guest_port);
        connect(fd.as_raw_fd(), &addr).unwrap();

        // We made sure to unblock the connection thread, now join it.
        let thread_result = self.thread_handle.take().unwrap().join().unwrap();
        info!("Connection thread finished with: {:?}", thread_result);
    }
}
