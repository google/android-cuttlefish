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
//! This implements the run-test-inference tool for Cuttlefish.
use anyhow::{Context, Result};
use clap::Parser;
use commands::{Command, InferenceOptions, NPU_SOCKET_NAME};
use serde::{Deserialize, Serialize};
use std::os::android::net::SocketAddrExt;
use std::os::unix::net::UnixStream;
mod commands;

#[derive(Parser, Debug)]
struct Options {
    /// Job priority for the inference
    #[arg(short, long, default_value_t = 500)]
    job_priority: i32,
    /// Original UID for the inference
    #[arg(short, long, default_value_t = -1)]
    original_uid: i32,
}

fn main() -> Result<()> {
    let options = Options::parse();
    let addr = std::os::unix::net::SocketAddr::from_abstract_name(NPU_SOCKET_NAME)
        .context("Failed to create socket address")?;
    let sock = UnixStream::connect_addr(&addr).context("Failed to connect to socket")?;
    let mut de = serde_json::Deserializer::from_reader(&sock);
    let mut se = serde_json::Serializer::new(&sock);
    let cmd = Command::RunTestInference(InferenceOptions {
        priority: options.job_priority,
        original_uid: options.original_uid,
        num_requests: 200,
    });
    cmd.serialize(&mut se).context("Failed to send command")?;
    match Command::deserialize(&mut de).context("Failed to receive command")? {
        Command::Error => {
            anyhow::bail!("Received error from server");
        }
        Command::Success => { /* continue */ }
        unknown => {
            anyhow::bail!("Unexpected reply: {unknown:?}");
        }
    }

    Ok(())
}
