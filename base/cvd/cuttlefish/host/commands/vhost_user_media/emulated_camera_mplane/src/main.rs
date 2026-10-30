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

//! Emulated multi-planar V4L2 camera for the cuttlefish virtio-media camera HAL.

use std::path::PathBuf;

use clap::Parser;
use emulated_camera::LensFacing;
use thiserror::Error;

/// Name reported to the guest through `VIDIOC_QUERYCAP`.
const CARD_NAME: &str = "emulated_camera";

#[derive(Debug, Error)]
enum Error {
    #[error("Invalid argument: {0}")]
    InvalidArgument(String),
    #[error(transparent)]
    Serve(#[from] emulated_camera::Error),
}

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
    /// Lens facing configuration: FRONT, BACK, or EXTERNAL.
    #[clap(long, value_name = "LENS_FACING", default_value = "EXTERNAL")]
    lens_facing: String,
}

#[derive(PartialEq, Debug)]
struct Config {
    socket_path: PathBuf,
    lens_facing: LensFacing,
}

impl TryFrom<CmdLineArgs> for Config {
    type Error = Error;

    fn try_from(args: CmdLineArgs) -> Result<Self> {
        let lens_facing = args
            .lens_facing
            .parse::<LensFacing>()
            .map_err(Error::InvalidArgument)?;
        Ok(Config {
            socket_path: args.socket_path,
            lens_facing,
        })
    }
}

fn init_logging(verbosity: log::LevelFilter) {
    env_logger::builder()
        .format_timestamp_secs()
        .filter_level(verbosity)
        .init();
}

fn main() -> Result<()> {
    let args = CmdLineArgs::parse();

    init_logging(args.verbosity);

    let config = Config::try_from(args)?;
    emulated_camera::serve(&config.socket_path, CARD_NAME, config.lens_facing)?;
    Ok(())
}
