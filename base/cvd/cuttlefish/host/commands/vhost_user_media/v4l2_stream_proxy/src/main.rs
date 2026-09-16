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

use std::path::PathBuf;
use std::sync::{Arc, RwLock};

use clap::Parser;
use vhost_user_backend::VhostUserDaemon;
use vhu_media::VhuMediaBackend;
use vhu_media::cli::Error;
use virtio_media::protocol::VirtioMediaDeviceConfig;
use virtio_media::v4l2r::ioctl::Capabilities;
use vm_memory::{GuestMemoryAtomic, GuestMemoryMmap};

mod device;
mod worker;

type Result<T> = std::result::Result<T, Error>;

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct CmdLineArgs {
    /// Location of vhost-user Unix domain socket.
    #[clap(short, long, value_name = "SOCKET")]
    socket_path: PathBuf,
    /// Log verbosity, one of Off, Error, Warning, Info, Debug, Trace.
    #[clap(short, long, default_value_t = log::LevelFilter::Debug)]
    verbosity: log::LevelFilter,
    /// Path to the host named pipe (FIFO).
    #[clap(long = "input_path", value_name = "INPUT_PATH")]
    input_path: PathBuf,
    /// Width of the video stream in pixels.
    #[clap(long = "input_width", value_name = "INPUT_WIDTH")]
    input_width: u32,
    /// Height of the video stream in pixels.
    #[clap(long = "input_height", value_name = "INPUT_HEIGHT")]
    input_height: u32,
    /// Frames per second (e.g. 30 or 30000/1001).
    #[clap(long = "input_fps", value_name = "INPUT_FPS")]
    input_fps: String,
}

fn parse_fps_to_interval(fps_str: &str) -> Option<(u32, u32)> {
    if let Ok(fps) = fps_str.parse::<u32>() {
        return Some((1, fps));
    }
    let parts: Vec<&str> = fps_str.split('/').collect();
    if parts.len() == 2 {
        if let (Ok(num), Ok(den)) = (parts[0].parse::<u32>(), parts[1].parse::<u32>()) {
            return Some((den, num));
        }
    }
    None
}

#[derive(Clone, Debug)]
pub struct Config {
    pub socket_path: PathBuf,
    pub input_path: PathBuf,
    pub input_width: u32,
    pub input_height: u32,
    pub fps_interval: (u32, u32),
    pub format: device::Format,
}

impl TryFrom<CmdLineArgs> for Config {
    type Error = Error;

    fn try_from(args: CmdLineArgs) -> Result<Self> {
        let fps_interval = parse_fps_to_interval(&args.input_fps).ok_or_else(|| {
            Error::InvalidArgument(format!("Invalid FPS format: {}", args.input_fps))
        })?;
        Ok(Config {
            socket_path: args.socket_path,
            input_path: args.input_path,
            input_width: args.input_width,
            input_height: args.input_height,
            fps_interval,
            format: device::Format::Yuv420M,
        })
    }
}

fn init_logging(verbosity: log::LevelFilter) -> Result<()> {
    let mut builder = env_logger::Builder::new();
    builder.filter_level(verbosity);
    builder.format_timestamp_secs();
    builder.init();
    Ok(())
}

const VFL_TYPE_VIDEO: u32 = 0;

fn start_backend(config: Config) -> Result<()> {
    let socket_path = config.socket_path.clone();
    let mut card = [0u8; 32];
    let card_name = "v4l2_stream_proxy";
    card[0..card_name.len()].copy_from_slice(card_name.as_bytes());

    loop {
        let caps = Capabilities::VIDEO_CAPTURE_MPLANE | Capabilities::STREAMING;
        let device_config = VirtioMediaDeviceConfig {
            device_caps: caps.bits(),
            device_type: VFL_TYPE_VIDEO,
            card,
        };
        let backend_config = config.clone();
        let backend = Arc::new(RwLock::new(VhuMediaBackend::new(
            device_config,
            move |event_queue, host_mapper| {
                crate::device::V4l2Stream::new(event_queue, host_mapper, backend_config.clone())
            },
        )));
        let mut daemon = VhostUserDaemon::new(
            String::from("vhost-user-media-backend"),
            backend,
            GuestMemoryAtomic::new(GuestMemoryMmap::new()),
        )
        .map_err(Error::CouldNotCreateDaemon)?;
        log::info!("vhost-user-media-backend daemon started");
        daemon.serve(&socket_path).map_err(Error::ServeFailed)?;
        log::info!("vhost-user-media-backend daemon closed gracefully");
    }
}

fn main() -> Result<()> {
    let args = CmdLineArgs::parse();

    init_logging(args.verbosity)?;

    start_backend(Config::try_from(args)?)
}
