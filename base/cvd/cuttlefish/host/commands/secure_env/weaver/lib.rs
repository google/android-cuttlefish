//
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

//! Weaver TA for Cuttlefish.

use hal_wire::AsCborValue;
use log::{error, info};
use secure_env_common::run_ta_loop;
use std::boxed::Box;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::{self, BufReader, BufWriter};
use std::os::fd::AsRawFd;
use subtle::ConstantTimeEq;
use weaver_ta::{traits, Error, Slot, WeaverTa};

const SLOT_COUNT: i32 = 64;
const KEY_SIZE: i32 = 16;
const VALUE_SIZE: i32 = 16;
const MAX_CHANNEL_MSG_SIZE: usize = 4096;

/// Storage backend for Weaver that saves to a JSON file.
struct JsonSlotStorage {
    path: String,
}

impl JsonSlotStorage {
    fn new(path: String) -> Self {
        Self { path }
    }

    fn load_all(&self) -> BTreeMap<i32, Vec<u8>> {
        match File::open(&self.path) {
            Ok(file) => {
                let reader = BufReader::new(file);
                serde_json::from_reader(reader).unwrap_or_default()
            }
            Err(_) => BTreeMap::new(),
        }
    }

    fn save_all(&self, slots: &BTreeMap<i32, Vec<u8>>) -> Result<(), io::Error> {
        let file = File::create(&self.path)?;
        let writer = BufWriter::new(file);
        serde_json::to_writer(writer, slots)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
    }
}

impl traits::SlotStorage for JsonSlotStorage {
    fn read_slot(&self, slot_id: i32) -> Result<Slot, Error> {
        let all_slots = self.load_all();
        if let Some(data) = all_slots.get(&slot_id) {
            hal_wire::AsCborValue::from_slice(data).map_err(Error::Cbor)
        } else {
            Ok(Slot::default())
        }
    }

    fn write_slot(&mut self, slot_id: i32, slot: &Slot) -> Result<(), Error> {
        let mut all_slots = self.load_all();
        let data = slot.clone().into_vec().map_err(Error::Cbor)?;
        all_slots.insert(slot_id, data);
        self.save_all(&all_slots).map_err(|e| {
            error!("Failed to save Weaver storage: {:?}", e);
            Error::FileNotFound // Map to a generic error
        })
    }
}

struct StdClock;
impl hal_ta::traits::MonotonicClock for StdClock {
    fn now(&self) -> hal_wire::types::MillisecondsSinceEpoch {
        let now =
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
        hal_wire::types::MillisecondsSinceEpoch(now.as_millis() as i64)
    }
}

struct Compare;
impl hal_ta::traits::ConstTimeEq for Compare {
    fn eq(&self, left: &[u8], right: &[u8]) -> bool {
        left.ct_eq(right).into()
    }
}

/// Main entry point for the Weaver TA.
pub fn ta_main(
    infile: File,
    outfile: File,
    storage_path: String,
    snapshot_socket: std::os::unix::net::UnixStream,
) {
    log::set_logger(&secure_env_common::logger::AndroidCppLogger).unwrap();
    log::set_max_level(log::LevelFilter::Debug); // Filtering happens elsewhere
    info!(
        "Weaver TA running with infile={}, outfile={}, storage={}",
        infile.as_raw_fd(),
        outfile.as_raw_fd(),
        storage_path
    );

    let imp = traits::Implementation {
        clock: Box::new(StdClock),
        compare: Box::new(Compare),
        storage: Box::new(JsonSlotStorage::new(storage_path)),
        warm_up: Box::new(traits::NoopWarmUp {}),
    };
    let mut ta = WeaverTa::new(imp, SLOT_COUNT, KEY_SIZE, VALUE_SIZE);
    run_ta_loop(infile, outfile, snapshot_socket, MAX_CHANNEL_MSG_SIZE, |req| ta.process(req));
}
