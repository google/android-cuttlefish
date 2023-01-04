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

//! Device trait implementations using the TPM.

use kmr_common::{
    crypto, crypto::SHA256_DIGEST_LEN, km_err, vec_try, vec_try_with_capacity, Error,
    FallibleAllocExt,
};
use kmr_ta::device::DeviceHmac;

pub const ROOT_KEK_MARKER: &[u8] = b"CF Root KEK";

/// Device HMAC implementation that uses the TPM.
#[derive(Clone)]
pub struct TpmHmac {
    /// Opaque pointer to a `TpmResourceManager`.
    trm: *mut libc::c_void,
}

impl TpmHmac {
    pub fn new(trm: *mut libc::c_void) -> Self {
        Self { trm }
    }
    fn tpm_hmac(&self, data: &[u8]) -> Result<Vec<u8>, Error> {
        let mut tag = vec_try![0; 32]?;

        // Safety: all slices are valid with correct lengths.
        let rc = unsafe {
            secure_env_tpm::tpm_hmac(
                self.trm,
                data.as_ptr(),
                data.len() as u32,
                tag.as_mut_ptr(),
                tag.len() as u32,
            )
        };
        if rc == 0 {
            Ok(tag)
        } else {
            Err(km_err!(UnknownError, "HMAC calculation failed"))
        }
    }

    fn hkdf_expand(&self, info: &[u8], out_len: usize) -> Result<Vec<u8>, Error> {
        // HKDF expand: feed the derivation info into HMAC (using the TPM key) repeatedly.
        let n = (out_len + SHA256_DIGEST_LEN - 1) / SHA256_DIGEST_LEN;
        if n > 256 {
            return Err(km_err!(UnknownError, "overflow in hkdf"));
        }
        let mut t = vec_try_with_capacity!(SHA256_DIGEST_LEN)?;
        let mut okm = vec_try_with_capacity!(n * SHA256_DIGEST_LEN)?;
        let n = n as u8;
        for idx in 0..n {
            let mut input = vec_try_with_capacity!(t.len() + info.len() + 1)?;
            input.extend_from_slice(&t);
            input.extend_from_slice(info);
            input.push(idx + 1);

            t = self.tpm_hmac(&input)?;
            okm.try_extend_from_slice(&t)?;
        }
        okm.truncate(out_len);
        Ok(okm)
    }
}

impl kmr_ta::device::DeviceHmac for TpmHmac {
    fn hmac(&self, _imp: &dyn crypto::Hmac, data: &[u8]) -> Result<Vec<u8>, Error> {
        self.tpm_hmac(data)
    }
}

impl crate::rpc::DeriveBytes for TpmHmac {
    fn derive_bytes(&self, context: &[u8], output_len: usize) -> Result<Vec<u8>, Error> {
        self.hkdf_expand(context, output_len)
    }
}

// TPM-backed implementation of key retrieval/management functionality.
pub struct Keys {
    tpm_hmac: TpmHmac,
}

impl Keys {
    pub fn new(trm: *mut libc::c_void) -> Self {
        Self { tpm_hmac: TpmHmac::new(trm) }
    }
}

impl kmr_ta::device::RetrieveKeyMaterial for Keys {
    fn root_kek(&self, _context: &[u8]) -> Result<crypto::RawKeyMaterial, Error> {
        Ok(crypto::RawKeyMaterial(ROOT_KEK_MARKER.to_vec()))
    }

    fn kak(&self) -> Result<crypto::aes::Key, Error> {
        // Generate a TPM-bound shared secret to use as the base of HMAC key negotiation.
        let k = self.tpm_hmac.tpm_hmac(b"TPM ISharedSecret")?;
        let k: [u8; 32] =
            k.try_into().map_err(|_e| km_err!(UnknownError, "unexpected HMAC size"))?;
        Ok(crypto::aes::Key::Aes256(k))
    }

    fn hmac_key_agreed(&self, _key: &crypto::hmac::Key) -> Option<Box<dyn DeviceHmac>> {
        // After `ISharedSecret` negotiation completes, the spec implies that the calculated HMAC
        // key should be used by subsequent device HMAC calculations.  However, this implementation
        // uses a TPM-HMAC key instead, so that HMAC calculations agree between KeyMint and
        // Gatekeeper / ConfirmationUI.
        // TODO(b/242838132): consider installing the calculated key into the TPM and using it
        // thereafter.
        Some(Box::new(self.tpm_hmac.clone()))
    }

    fn unique_id_hbk(&self, _ckdf: &dyn crypto::Ckdf) -> Result<crypto::hmac::Key, Error> {
        // Generate a TPM-bound HBK to use for unique ID generation.
        let mut k = self.tpm_hmac.tpm_hmac(b"TPM unique ID HBK")?;
        k.truncate(16);
        Ok(crypto::hmac::Key(k))
    }
}

pub struct KeyDerivation {
    tpm_hmac: TpmHmac,
}

impl KeyDerivation {
    pub fn new(trm: *mut libc::c_void) -> Self {
        Self { tpm_hmac: TpmHmac::new(trm) }
    }
}

impl kmr_common::crypto::Hkdf for KeyDerivation {
    fn hkdf(&self, salt: &[u8], ikm: &[u8], info: &[u8], out_len: usize) -> Result<Vec<u8>, Error> {
        if ikm != ROOT_KEK_MARKER {
            // This code expects that the value from `Keys::root_kek()` above will be passed
            // unmodified to this function in its (only) use as key derivation.  If this is not the
            // case, then the assumptions below around TPM use may no longer be correct.
            return Err(km_err!(UnknownError, "unexpected root kek in key derivation"));
        }
        if !salt.is_empty() {
            // Similarly, we ignore the salt on the assumption that it is empty. If this changes,
            // then assumptions about use of this trait implementation may be wrong.
            return Err(km_err!(UnknownError, "unexpected non-empty salt in key derivation"));
        }

        // HKDF normally performs an initial extract step to create a pseudo-random key (PRK) for
        // use in the HKDF expand processing.  This implementation uses a TPM HMAC key for HKDF
        // expand processing instead, and so the HKDF extract step is skipped.
        self.tpm_hmac.hkdf_expand(info, out_len)
    }
}

/// RPC artifact retrieval using key material derived from the TPM.
pub type RpcArtifacts = crate::rpc::Artifacts<TpmHmac>;
