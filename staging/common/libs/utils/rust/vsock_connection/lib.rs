// Copyright 2022, The Android Open Source Project
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

//! Rust wrapper for VSockServerConnection.

use cxx::UniquePtr;

/// This module exposes the VsockServerConnection C++ class to Rust.
#[allow(unsafe_op_in_unsafe_fn)]
#[cxx::bridge(namespace = "cuttlefish")]
mod ffi {
    unsafe extern "C++" {
        include!("wrapper.h");

        type VsockServerConnection;
        fn create_vsock_server_connection() -> UniquePtr<VsockServerConnection>;
        fn IsConnected(self: Pin<&mut VsockServerConnection>) -> bool;
        fn Connect(self: Pin<&mut VsockServerConnection>, port: u32, cid: u32) -> bool;
        fn ServerShutdown(self: Pin<&mut VsockServerConnection>);
    }
}

/// Rust wrapper for a VsockServerConnection.
pub struct VsockServerConnection {
    instance: UniquePtr<ffi::VsockServerConnection>,
}

impl VsockServerConnection {
    /// Creates a VsockServerConnection.
    pub fn new() -> Self {
        Self { instance: ffi::create_vsock_server_connection() }
    }

    /// Returns if the vsock server has an active connection or not.
    pub fn is_connected(&mut self) -> bool {
        self.instance.pin_mut().IsConnected()
    }

    /// Start connection using the provided port and cid.
    pub fn connect(&mut self, port: u32, cid: u32) -> bool {
        self.instance.pin_mut().Connect(port, cid)
    }

    /// Shutdown connection.
    pub fn server_shutdown(&mut self) {
        self.instance.pin_mut().ServerShutdown();
    }
}

impl Default for VsockServerConnection {
    fn default() -> Self {
        Self::new()
    }
}

// SAFETY: VsockServerConnection is a pointer to an `ffi::VsockServerConnection`, which in turn only
// contains a file descriptor and so is safe to pass between threads.
unsafe impl Send for VsockServerConnection {}
