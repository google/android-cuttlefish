//
// Copyright (C) 2022 The Android Open Source Project
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

//! Software-only trait implementations using fake keys.

use kmr_common::{
    crypto,
    crypto::{Hkdf, Rng},
    Error,
};
use kmr_crypto_boring::{hmac::BoringHmac, rng::BoringRng};
use kmr_ta::device::RetrieveKeyMaterial;

/// Root key retrieval using hard-coded fake keys.
pub struct Keys;

impl RetrieveKeyMaterial for Keys {
    fn root_kek(&self, _context: &[u8]) -> Result<crypto::RawKeyMaterial, Error> {
        // Matches `MASTER_KEY` in system/keymaster/key_blob_utils/software_keyblobs.cpp
        Ok(crypto::RawKeyMaterial([0; 16].to_vec()))
    }
    fn kak(&self) -> Result<crypto::aes::Key, Error> {
        // Matches `kFakeKeyAgreementKey` in
        // system/keymaster/km_openssl/soft_keymaster_enforcement.cpp.
        Ok(crypto::aes::Key::Aes256([0; 32]))
    }
    fn unique_id_hbk(&self, _ckdf: &dyn crypto::Ckdf) -> Result<crypto::hmac::Key, Error> {
        // Matches value used in system/keymaster/contexts/pure_soft_keymaster_context.cpp.
        crypto::hmac::Key::new_from(b"MustBeRandomBits")
    }
}

pub struct Derive {
    hbk: Vec<u8>,
}

impl Default for Derive {
    fn default() -> Self {
        // Use random data as an emulation of a hardware-backed key.
        let mut hbk = vec![0; 32];
        let mut rng = BoringRng::default();
        rng.fill_bytes(&mut hbk);
        Self { hbk }
    }
}

impl crate::rpc::DeriveBytes for Derive {
    fn derive_bytes(&self, context: &[u8], output_len: usize) -> Result<Vec<u8>, Error> {
        BoringHmac.hkdf(&[], &self.hbk, context, output_len)
    }
}

/// RPC artifact retrieval using software fake key.
pub type RpcArtifacts = crate::rpc::Artifacts<Derive>;
