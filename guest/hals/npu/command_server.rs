/*
 * Copyright (C) 2025 The Android Open Source Project
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
//! This implements the NPU Scheduling Service for Cuttlefish.

use std::os::android::net::SocketAddrExt;
use std::os::fd::AsFd;
use std::os::unix::net::{UnixListener, UnixStream};
use std::sync::Arc;
use std::thread;

use anyhow::{anyhow, Context, Result};
use log::{error, info};
use nix::sys::socket::{getsockopt, sockopt::PeerCredentials};
use nix::unistd::{Pid, Uid};
use serde::{Deserialize, Serialize};

use crate::commands::{Command, NPU_SOCKET_NAME};
use crate::scheduling::SchedulingService;

fn get_peer_cred<F: AsFd>(fd: &F) -> Result<(Uid, Pid)> {
    let creds = getsockopt(fd, PeerCredentials)?;
    let uid = Uid::from_raw(creds.uid());
    let pid = Pid::from_raw(creds.pid());
    Ok((uid, pid))
}

fn handle_client(stream: &UnixStream, service: Arc<SchedulingService>) -> Result<()> {
    let (uid, pid) = get_peer_cred(&stream).context("Failed to get peer credentials")?;
    info!("Client connected: uid = {uid}, pid = {pid}");
    let mut de = serde_json::Deserializer::from_reader(stream);
    let mut se = serde_json::Serializer::new(stream);
    loop {
        let command = match Command::deserialize(&mut de) {
            Ok(command) => command,
            Err(ref e) if e.is_eof() => return Ok(()),
            Err(e) => return Err(e.into()),
        };

        let result = match command {
            Command::RunTestInference(ref options) => {
                service.run_test_inference(uid.as_raw() as i32, pid.as_raw(), options)
            }
            _ => Err(anyhow!("Unexpected command: {command:?}")),
        };
        if let Err(e) = result {
            error!("Command failed: {e}");
            Command::Error.serialize(&mut se)?;
        } else {
            Command::Success.serialize(&mut se)?;
        }
    }
}

pub fn start(service: Arc<SchedulingService>) -> Result<()> {
    let addr = std::os::unix::net::SocketAddr::from_abstract_name(NPU_SOCKET_NAME)?;
    let listener = UnixListener::bind_addr(&addr)?;
    info!("Listening on {NPU_SOCKET_NAME}");

    thread::spawn(move || {
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    let thread_service = service.clone();
                    thread::spawn(move || {
                        if let Err(e) = handle_client(&stream, thread_service) {
                            error!("Error handling client: {e:?}");
                        }
                    });
                }
                Err(e) => {
                    error!("Error accepting connection: {e}");
                }
            }
        }
    });
    Ok(())
}
