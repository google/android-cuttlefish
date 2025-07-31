// Copyright (C) 2025 The Android Open Source Project
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

//! Factory Reset Protection storage for Cuttlefish

use kmr_common::{frp_err, Error};
use kmr_ta::device::{FrpDataStorage, FrpSecretStorage};
use std::fs::{self, OpenOptions};
use std::io::{Read, Write};
use std::path::Path;

/// Location of FRP secret file.
const FRP_SECRET_FILE: &str = "frp_secret";

pub struct HostFrpSecretStorage;

impl FrpSecretStorage for HostFrpSecretStorage {
    fn store_secret(&mut self, secret: [u8; 32]) -> Result<(), Error> {
        let path = FRP_SECRET_FILE;
        OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(path)
            .map_err(|e| frp_err!(Failed, "failed to open secret file: {:?}", e))?
            .write_all(&secret)
            .map_err(|e| frp_err!(Failed, "failed to write secret to file: {:?}", e))
    }

    fn retrieve_secret(&self) -> Result<[u8; 32], Error> {
        if !Path::new(FRP_SECRET_FILE).exists() {
            // Secret not written yet, return default.
            return Ok([0; 32]);
        }
        let mut buf = Vec::new();
        OpenOptions::new()
            .read(true)
            .open(FRP_SECRET_FILE)
            .map_err(|e| frp_err!(Failed, "failed to open secret file: {:?}", e))?
            .read_to_end(&mut buf)
            .map_err(|e| frp_err!(Failed, "failed to read secret from file: {:?}", e))?;
        buf.try_into().map_err(|e| frp_err!(Failed, "invalid secret length: {:?}", e))
    }
}

pub struct HostFrpDataStorage;

impl FrpDataStorage for HostFrpDataStorage {
    fn store_data(&mut self, key: &str, data: &[u8]) -> Result<(), Error> {
        let path = format!("frp_data_{}", key);
        OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(&path)
            .map_err(|e| frp_err!(Failed, "failed to open data file: {:?}", e))?
            .write_all(data)
            .map_err(|e| frp_err!(Failed, "failed to write data to file: {:?}", e))
    }

    fn retrieve_data(&self, key: &str) -> Result<Option<Vec<u8>>, Error> {
        let path = format!("frp_data_{}", key);
        if !Path::new(&path).exists() {
            return Ok(None);
        }
        let mut buf = Vec::new();
        OpenOptions::new()
            .read(true)
            .open(&path)
            .map_err(|e| frp_err!(Failed, "failed to open data file: {:?}", e))?
            .read_to_end(&mut buf)
            .map_err(|e| frp_err!(Failed, "failed to read data from file: {:?}", e))?;
        Ok(Some(buf))
    }

    fn delete_data(&mut self, key: &str) -> Result<(), Error> {
        let path = format!("frp_data_{}", key);
        if Path::new(&path).exists() {
            fs::remove_file(&path)
                .map_err(|e| frp_err!(Failed, "failed to delete data file: {:?}", e))?;
        }
        Ok(())
    }

    fn clear(&mut self) -> Result<(), Error> {
        let entries =
            fs::read_dir(".").map_err(|e| frp_err!(Failed, "failed to read directory: {:?}", e))?;
        for entry in entries {
            let entry = entry.map_err(|e| frp_err!(Failed, "failed to read entry: {:?}", e))?;
            let path = entry.path();
            if let Some(name) = path.file_name().and_then(|n| n.to_str()) {
                if name.starts_with("frp_data_") {
                    fs::remove_file(&path)
                        .map_err(|e| frp_err!(Failed, "failed to delete data file: {:?}", e))?;
                }
            }
        }
        Ok(())
    }
}
