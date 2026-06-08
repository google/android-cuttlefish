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

use clap::Parser;
use log::error;
use std::path::PathBuf;
use thiserror::Error as ThisError;

#[derive(Debug, ThisError)]
pub enum Error {
    #[error("Could not create daemon: {0}")]
    CouldNotCreateDaemon(vhost_user_backend::Error),
    #[error("Fatal error: {0}")]
    ServeFailed(vhost_user_backend::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

#[derive(PartialEq, Debug)]
pub struct Config {
    pub socket_path: PathBuf,
}

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
pub struct CmdLineArgs {
    /// Location of vhost-user Unix domain socket.
    #[clap(short, long, value_name = "SOCKET")]
    pub socket_path: PathBuf,
    /// Log verbosity, one of Off, Error, Warning, Info, Debug, Trace.
    #[clap(short, long, default_value_t = log::LevelFilter::Debug)]
    pub verbosity: log::LevelFilter,
}

impl TryFrom<CmdLineArgs> for Config {
    type Error = Error;

    fn try_from(args: CmdLineArgs) -> Result<Self> {
        Ok(Config {
            socket_path: args.socket_path,
        })
    }
}

pub fn init_logging(verbosity: log::LevelFilter) -> Result<()> {
    env_logger::builder()
        .format_timestamp_secs()
        .filter_level(verbosity)
        .init();
    Ok(())
}
