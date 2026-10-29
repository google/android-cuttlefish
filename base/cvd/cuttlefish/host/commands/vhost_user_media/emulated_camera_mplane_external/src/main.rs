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

//! Emulated multi-planar V4L2 camera for the reference external camera HAL.
//!
//! This backend targets the AOSP external (USB) camera HAL and the V4L2 compliance suite.

use std::path::PathBuf;

use clap::Parser;
use emulated_camera::LensFacing;

/// Name reported to the guest through `VIDIOC_QUERYCAP`.
const CARD_NAME: &str = "emulated_camera";

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct CmdLineArgs {
    /// Location of vhost-user Unix domain socket.
    #[clap(short, long, value_name = "SOCKET")]
    socket_path: PathBuf,
    /// Log verbosity, one of Off, Error, Warning, Info, Debug, Trace.
    #[clap(short, long, default_value_t = log::LevelFilter::Debug)]
    verbosity: log::LevelFilter,
}

fn init_logging(verbosity: log::LevelFilter) {
    env_logger::builder()
        .format_timestamp_secs()
        .filter_level(verbosity)
        .init();
}

fn main() -> emulated_camera::Result<()> {
    let args = CmdLineArgs::parse();

    init_logging(args.verbosity);

    emulated_camera::serve(&args.socket_path, CARD_NAME, LensFacing::External)
}
