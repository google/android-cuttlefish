// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Common utilities for Cuttlefish host secure_env Rust TAs.

use log::{error, trace};
use std::fs::File;
use std::io::{Read, Write};
use std::os::fd::AsFd;
use std::os::unix::net::UnixStream;

/// Logger module that forwards logs to the Android C++ backend.
pub mod logger;
// See `SnapshotSocketMessage` in suspend_resume_handler.h for docs.
const SNAPSHOT_SOCKET_MESSAGE_SUSPEND: u8 = 1;
const SNAPSHOT_SOCKET_MESSAGE_SUSPEND_ACK: u8 = 2;
const SNAPSHOT_SOCKET_MESSAGE_RESUME: u8 = 3;

/// Runs the main loop for a Cuttlefish TA, handling requests and snapshot signals.
pub fn run_ta_loop<F>(
    mut infile: File,
    mut outfile: File,
    mut snapshot_socket: UnixStream,
    max_size: usize,
    mut process: F,
) where
    F: FnMut(&[u8]) -> Vec<u8>,
{
    let mut buf = vec![0u8; max_size];
    loop {
        // Wait for data from either `infile` or `snapshot_socket`. If both have data, we prioritize
        // processing only `infile` until it is empty so that there is no pending state when we
        // suspend the loop.
        let mut fd_set = nix::sys::select::FdSet::new();
        fd_set.insert(infile.as_fd());
        fd_set.insert(snapshot_socket.as_fd());
        if let Err(e) = nix::sys::select::select(
            None,
            /*readfds=*/ Some(&mut fd_set),
            None,
            None,
            /*timeout=*/ None,
        ) {
            error!("FATAL: Failed to select on input FDs: {:?}", e);
            return;
        }

        if fd_set.contains(infile.as_fd()) {
            // Read a request message from the pipe, as a 4-byte BE length followed by the message.
            let mut req_len_data = [0u8; 4];
            if let Err(e) = infile.read_exact(&mut req_len_data) {
                error!("FATAL: Failed to read request length from connection: {:?}", e);
                return;
            }
            let req_len = u32::from_be_bytes(req_len_data) as usize;
            if req_len > max_size {
                error!("FATAL: Request too long ({})", req_len);
                return;
            }
            let req_data = &mut buf[..req_len];
            if let Err(e) = infile.read_exact(req_data) {
                error!(
                    "FATAL: Failed to read request data of length {} from connection: {:?}",
                    req_len, e
                );
                return;
            }

            // Pass to the TA to process.
            trace!("-> TA: received data: (len={})", req_data.len());
            let rsp = process(req_data);
            trace!("<- TA: send data: (len={})", rsp.len());

            // Send the response message down the pipe, as a 4-byte BE length followed by the message.
            let rsp_len: u32 = match rsp.len().try_into() {
                Ok(l) => l,
                Err(_e) => {
                    error!("FATAL: Response too long (len={})", rsp.len());
                    return;
                }
            };
            let rsp_len_data = rsp_len.to_be_bytes();
            if let Err(e) = outfile.write_all(&rsp_len_data[..]) {
                error!("FATAL: Failed to write response length to connection: {:?}", e);
                return;
            }
            if let Err(e) = outfile.write_all(&rsp) {
                error!(
                    "FATAL: Failed to write response data of length {} to connection: {:?}",
                    rsp_len, e
                );
                return;
            }
            let _ = outfile.flush();

            continue;
        }

        if fd_set.contains(snapshot_socket.as_fd()) {
            // Read suspend request.
            let mut suspend_request = 0u8;
            if let Err(e) = snapshot_socket.read_exact(std::slice::from_mut(&mut suspend_request)) {
                error!("FATAL: Failed to read suspend request: {:?}", e);
                return;
            }
            if suspend_request != SNAPSHOT_SOCKET_MESSAGE_SUSPEND {
                error!(
                    "FATAL: Unexpected value from snapshot socket: got {}, expected {}",
                    suspend_request, SNAPSHOT_SOCKET_MESSAGE_SUSPEND
                );
                return;
            }
            // Write ACK.
            if let Err(e) = snapshot_socket.write_all(&[SNAPSHOT_SOCKET_MESSAGE_SUSPEND_ACK]) {
                error!("FATAL: Failed to write suspend ACK request: {:?}", e);
                return;
            }
            // Block until we get a resume request.
            let mut resume_request = 0u8;
            if let Err(e) = snapshot_socket.read_exact(std::slice::from_mut(&mut resume_request)) {
                error!("FATAL: Failed to read resume request: {:?}", e);
                return;
            }
            if resume_request != SNAPSHOT_SOCKET_MESSAGE_RESUME {
                error!(
                    "FATAL: Unexpected value from snapshot socket: got {}, expected {}",
                    resume_request, SNAPSHOT_SOCKET_MESSAGE_RESUME
                );
                return;
            }
        }
    }
}
