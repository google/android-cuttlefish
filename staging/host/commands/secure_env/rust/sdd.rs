// Copyright (C) 2023 The Android Open Source Project
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

//! Secure deletion data manager for Cuttlefish.
//! This implementetation is "secure" in the sense that the underlying storage can not be accessed
//! by Android. However, it is does not provide any protections agains the host, i.e. anyone with
//! access to the host can read and alter the contents of deletion data.

use kmr_common::{crypto, keyblob, km_err, Error};
use kmr_proto::storage;
use log::{error, info};
use protobuf::Message;
use std::fs;
use std::path;

const SECURE_DELETION_DATA_FILE: &str = "keymint_secure_deletion_data";

fn read_sdd_file() -> Result<storage::SecureDeletionData, Error> {
    let mut f = fs::File::open(SECURE_DELETION_DATA_FILE).map_err(|e| {
        km_err!(SecureHwCommunicationFailed, "failed to open secure deletion data file: {:?}", e)
    })?;
    storage::SecureDeletionData::parse_from_reader(&mut f).map_err(|e| {
        km_err!(SecureHwCommunicationFailed, "failed to parse secure deletion data: {:?}", e)
    })
}

fn write_sdd_file(data: &storage::SecureDeletionData) -> Result<(), Error> {
    let mut f = fs::File::create(SECURE_DELETION_DATA_FILE).map_err(|e| {
        km_err!(SecureHwCommunicationFailed, "failed to create secure deletion data file: {:?}", e)
    })?;
    data.write_to_writer(&mut f).map_err(|e| {
        km_err!(
            SecureHwCommunicationFailed,
            "failed to write to secure deletion data file: {:?}",
            e
        )
    })
}

pub struct HostSddManager {
    // Local cache of data stored on disk.
    data: storage::SecureDeletionData,
}

impl HostSddManager {
    fn init(&mut self, rng: &mut dyn crypto::Rng) -> Result<(), Error> {
        // Restore data from disk if it was previously saved.
        if path::Path::new(SECURE_DELETION_DATA_FILE).exists() {
            info!("Secure deletion data file found. Parsing.");
            self.data = read_sdd_file()?;
            return Ok(());
        }

        info!("No secure deletion data file found. Creating one.");

        // Initialize factory reset secret.
        self.data.factory_secret.resize(32, 0);
        rng.fill_bytes(&mut self.data.factory_secret[..]);

        // Create secure deletion data file.
        write_sdd_file(&self.data)
    }

    pub fn new(rng: &mut dyn crypto::Rng) -> Result<Self, Error> {
        let mut sdd_mgr = Self { data: storage::SecureDeletionData::default() };
        sdd_mgr.init(rng).map(|_| sdd_mgr)
    }
}

impl keyblob::SecureDeletionSecretManager for HostSddManager {
    fn get_or_create_factory_reset_secret(
        &mut self,
        rng: &mut dyn crypto::Rng,
    ) -> Result<keyblob::SecureDeletionData, Error> {
        if self.data.factory_secret.is_empty() {
            self.init(rng)?;
        }
        self.get_factory_reset_secret()
    }

    fn get_factory_reset_secret(&self) -> Result<keyblob::SecureDeletionData, Error> {
        if self.data.factory_secret.is_empty() {
            return Err(km_err!(UnknownError, "no factory secret available"));
        }
        Ok(keyblob::SecureDeletionData {
            factory_reset_secret: self.data.factory_secret.clone().try_into().unwrap(),
            secure_deletion_secret: [0; 16],
        })
    }

    fn new_secret(
        &mut self,
        rng: &mut dyn crypto::Rng,
        _purpose: keyblob::SlotPurpose,
    ) -> Result<(keyblob::SecureDeletionSlot, keyblob::SecureDeletionData), Error> {
        // Allocate new slot ID.
        let slot_id = self
            .data
            .last_free_slot
            .checked_add(1)
            .ok_or(km_err!(RollbackResistanceUnavailable, "ran out of slot IDs"))?;

        info!("Generating new secret with slot ID: {:?}", slot_id);

        assert!(
            !self.data.secure_deletion_secrets.contains_key(&slot_id),
            "Slot ID already in use: {:?}",
            slot_id
        );

        // Generate new sdd.
        let mut sdd = self.get_or_create_factory_reset_secret(rng)?;
        rng.fill_bytes(&mut sdd.secure_deletion_secret[..]);

        // Cache the secure deletion secret locally.
        self.data.secure_deletion_secrets.insert(slot_id, sdd.secure_deletion_secret.to_vec());
        self.data.last_free_slot = slot_id;

        // Save the secure deletion secret on disk.
        match write_sdd_file(&self.data) {
            Ok(_) => Ok((keyblob::SecureDeletionSlot(slot_id), sdd)),
            Err(e) => {
                // Restore cached state.
                self.data.secure_deletion_secrets.remove(&slot_id).unwrap();
                self.data.last_free_slot = slot_id - 1;
                Err(e)
            }
        }
    }

    fn get_secret(
        &self,
        slot: keyblob::SecureDeletionSlot,
    ) -> Result<keyblob::SecureDeletionData, Error> {
        let slot_id = slot.0;
        info!("Fetching secret with slot ID: {:?}", slot_id);

        let secret = self.data.secure_deletion_secrets.get(&slot_id).ok_or(km_err!(
            InvalidKeyBlob,
            "slot ID: {:?} not found.",
            slot_id
        ))?;
        Ok(keyblob::SecureDeletionData {
            factory_reset_secret: self.data.factory_secret.clone().try_into().unwrap(),
            secure_deletion_secret: secret.clone().try_into().unwrap(),
        })
    }

    fn delete_secret(&mut self, slot: keyblob::SecureDeletionSlot) -> Result<(), Error> {
        let slot_id = slot.0;
        info!("Deleting secret with slot ID: {:?}", slot_id);

        let secret = self
            .data
            .secure_deletion_secrets
            .remove(&slot_id)
            .ok_or(km_err!(InvalidKeyBlob, "slot ID not found."))?;

        // Save the secure deletion secret on disk.
        if let Err(e) = write_sdd_file(&self.data) {
            // Restore cached state.
            self.data.secure_deletion_secrets.insert(slot_id, secret).unwrap();
            return Err(e);
        }
        Ok(())
    }

    fn delete_all(&mut self) {
        info!("Deleting all secrets");
        self.data = storage::SecureDeletionData::default();
        if path::Path::new(SECURE_DELETION_DATA_FILE).exists() {
            // We want to guarantee that if this function returns, all secrets have been
            // successfully deleted. So, panic if we fail to delete the file.
            for _ in 0..5 {
                match fs::remove_file(SECURE_DELETION_DATA_FILE) {
                    Ok(_) => return,
                    Err(e) => error!("Couldn't delete file: {:?}", e),
                }
            }
            panic!("FATAL: Failed to delete secure deletion data file.");
        }
    }
}
