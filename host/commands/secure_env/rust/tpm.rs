//! Key management interactions using the TPM.

use kmr_common::{crypto, km_err, vec_try, Error};
use kmr_ta::device::DeviceHmac;

// TPM-backed implementation of key retrieval/management functionality.
pub struct Keys {
    /// Opaque pointer to a `TpmResourceManager`.
    trm: *mut libc::c_void,
}

impl Keys {
    pub fn new(trm: *mut libc::c_void) -> Self {
        Self { trm }
    }
}

impl kmr_ta::device::RetrieveKeyMaterial for Keys {
    fn root_kek(&self, _context: &[u8]) -> Result<crypto::RawKeyMaterial, Error> {
        // TODO(b/242838132): add TPM-backed implementation
        Ok(crypto::RawKeyMaterial(b"0123456789012345".to_vec()))
    }

    fn kak(&self) -> Result<crypto::aes::Key, Error> {
        // Generate a TPM-bound shared secret to use as the base of HMAC key negotiation.
        let k = tpm_hmac(self.trm, b"TPM ISharedSecret")?;
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
        Some(Box::new(TpmHmac { trm: self.trm }))
    }

    fn unique_id_hbk(&self, _ckdf: &dyn crypto::Ckdf) -> Result<crypto::hmac::Key, Error> {
        // Generate a TPM-bound HBK to use for unique ID generation.
        let mut k = tpm_hmac(self.trm, b"TPM unique ID HBK")?;
        k.truncate(16);
        Ok(crypto::hmac::Key(k))
    }
}

pub struct TpmHmac {
    /// Opaque pointer to a `TpmResourceManager`.
    trm: *mut libc::c_void,
}

impl kmr_ta::device::DeviceHmac for TpmHmac {
    fn hmac(&self, _imp: &dyn crypto::Hmac, data: &[u8]) -> Result<Vec<u8>, Error> {
        tpm_hmac(self.trm, data)
    }
}

fn tpm_hmac(trm: *mut libc::c_void, data: &[u8]) -> Result<Vec<u8>, Error> {
    let mut tag = vec_try![0; 32]?;

    // Safety: all slices are valid with correct lengths.
    let rc = unsafe {
        secure_env_tpm::tpm_hmac(
            trm,
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
