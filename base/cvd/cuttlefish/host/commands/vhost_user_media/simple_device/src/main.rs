// Copyright 2025, The Android Open Source Project
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

//! simple_device

use std::path::PathBuf;
use std::process::exit;
use std::sync::{Arc, RwLock};
use std::thread::{JoinHandle, spawn};

use clap::Parser;
use log::error;
use thiserror::Error;
use vhost_user_backend::VhostUserDaemon;
use vhu_media::VhuMediaBackend;
use virtio_media::devices::simple_device;
use virtio_media::protocol::VirtioMediaDeviceConfig;
use vm_memory::{GuestMemoryAtomic, GuestMemoryMmap};

#[derive(Debug, Error)]
pub(crate) enum Error {
    #[error("Could not create daemon: {0}")]
    CouldNotCreateDaemon(vhost_user_backend::Error),
    #[error("Fatal error: {0}")]
    ServeFailed(vhost_user_backend::Error),
}

type Result<T> = std::result::Result<T, Error>;

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct CmdLineArgs {
    /// Location of vhost-user Unix domain socket.
    #[clap(short, long, value_name = "SOCKET")]
    socket_path: PathBuf,
}

#[derive(PartialEq, Debug)]
struct Config {
    socket_path: PathBuf,
}

impl TryFrom<CmdLineArgs> for Config {
    type Error = Error;

    fn try_from(args: CmdLineArgs) -> Result<Self> {
        Ok(Config {
            socket_path: args.socket_path,
        })
    }
}

fn start_backend(args: CmdLineArgs) -> Result<()> {
    let config = Config::try_from(args)?;
    let socket_path = config.socket_path.clone();
    let handle: JoinHandle<Result<()>> = spawn(move || {
        loop {
            let mut card = [0u8; 32];
            let card_name = "simple_device";
            card[0..card_name.len()].copy_from_slice(card_name.as_bytes());
            use virtio_media::v4l2r::ioctl::Capabilities;
            let config = VirtioMediaDeviceConfig {
                // device_caps: (Capabilities::VIDEO_CAPTURE_MPLANE | Capabilities::STREAMING).bits(),
                device_caps: (Capabilities::VIDEO_CAPTURE | Capabilities::STREAMING).bits(),
                // VFL_TYPE_VIDEO
                device_type: 0,
                card,
            };
            let backend = Arc::new(RwLock::new(VhuMediaBackend::new(
                config,
                |event_queue, host_mapper| {
                    simple_device::SimpleCaptureDevice::new(event_queue, host_mapper)
                },
            )));
            let mut daemon = VhostUserDaemon::new(
                String::from("vhost-user-media-backend"),
                backend,
                GuestMemoryAtomic::new(GuestMemoryMmap::new()),
            )
            .map_err(Error::CouldNotCreateDaemon)?;
            daemon.serve(&socket_path).map_err(Error::ServeFailed)?;
        }
    });

    handle.join().unwrap()
}

fn main() {
    env_logger::init();

    if let Err(e) = start_backend(CmdLineArgs::parse()) {
        error!("{e}");
        exit(1);
    }
}
