//! KeyMint TA core for Cuttlefish.

extern crate alloc;

use kmr_common::{crypto, Error};
use kmr_crypto_boring::{
    aes::BoringAes, aes_cmac::BoringAesCmac, des::BoringDes, ec::BoringEc, eq::BoringEq,
    hmac::BoringHmac, rng::BoringRng, rsa::BoringRsa,
};
use kmr_ta::device::{
    BootloaderDone, Implementation, NoOpRetrieveRpcArtifacts, RetrieveKeyMaterial,
    TrustedPresenceUnsupported,
};
use kmr_ta::{HardwareInfo, KeyMintTa, RpcInfo, RpcInfoV3};
use kmr_wire::keymint::SecurityLevel;
use kmr_wire::rpc::MINIMUM_SUPPORTED_KEYS_IN_CSR;
use libc::c_int;
use log::{debug, error, info};
use std::io::{Read, Write};
use std::os::unix::io::FromRawFd;

pub mod attest;
mod clock;
mod tpm;

/// Main routine for the KeyMint TA. Only returns if there is a fatal error.
pub fn ta_main(fd_in: c_int, fd_out: c_int, security_level: SecurityLevel, trm: *mut libc::c_void) {
    let _ = env_logger::try_init();
    info!(
        "KeyMint TA running with fd_id={}, fd_out={}, security_level={:?}",
        fd_in, fd_out, security_level,
    );

    // Safety: the following calls rely on this code being the sole owner of the file descriptors.
    let mut infile = unsafe { std::fs::File::from_raw_fd(fd_in) };
    let mut outfile = unsafe { std::fs::File::from_raw_fd(fd_out) };

    let hw_info = HardwareInfo {
        version_number: 1,
        security_level,
        impl_name: "Rust reference implementation for Cuttlefish",
        author_name: "Google",
        unique_id: "Cuttlefish KeyMint TA",
    };

    let rpc_info_v3 = RpcInfoV3 {
        author_name: "Google",
        unique_id: "Cuttlefish KeyMint TA",
        fused: false,
        supported_num_of_keys_in_csr: MINIMUM_SUPPORTED_KEYS_IN_CSR,
    };

    let mut rng = BoringRng::default();
    let clock = clock::StdClock::default();
    let rsa = BoringRsa::default();
    let ec = BoringEc::default();
    let tpm_hkdf = tpm::KeyDerivation::new(trm);
    let soft_hkdf = BoringHmac;
    let hkdf: &dyn kmr_common::crypto::Hkdf =
        if security_level == SecurityLevel::TrustedEnvironment { &tpm_hkdf } else { &soft_hkdf };
    let imp = crypto::Implementation {
        rng: &mut rng,
        clock: Some(&clock),
        compare: &BoringEq,
        aes: &BoringAes,
        des: &BoringDes,
        hmac: &BoringHmac,
        rsa: &rsa,
        ec: &ec,
        ckdf: &BoringAesCmac,
        hkdf,
    };
    let sign_info = attest::CertSignInfo::new();

    let tpm_keys = tpm::Keys::new(trm);
    let soft_keys = SoftwareKeys;
    let keys: &dyn kmr_ta::device::RetrieveKeyMaterial =
        if security_level == SecurityLevel::TrustedEnvironment { &tpm_keys } else { &soft_keys };
    let dev = Implementation {
        // TODO(b/242838132): add TPM-backed implementation
        keys,
        sign_info: &sign_info,
        // HAL populates attestation IDs from properties.
        attest_ids: None,
        // No secure storage.
        sdd_mgr: None,
        // `BOOTLOADER_ONLY` keys not supported.
        bootloader: &BootloaderDone,
        // `STORAGE_KEY` keys not supported.
        sk_wrapper: None,
        // `TRUSTED_USER_PRESENCE_REQUIRED` keys not supported
        tup: &TrustedPresenceUnsupported,
        // No support for converting previous implementation's keyblobs.
        legacy_key: None,
        // TODO (b/260601375): add TPM-backed implementation
        rpc: &NoOpRetrieveRpcArtifacts,
    };
    let mut ta = KeyMintTa::new(hw_info, RpcInfo::V3(rpc_info_v3), imp, dev);

    let mut buf = [0; kmr_wire::MAX_SIZE];
    loop {
        // Read a request message from the pipe, as a 4-byte BE length followed by the message.
        let mut req_len_data = [0u8; 4];
        if let Err(e) = infile.read_exact(&mut req_len_data) {
            error!("FATAL: Failed to read request length from connection: {:?}", e);
            return;
        }
        let req_len = u32::from_be_bytes(req_len_data) as usize;
        if req_len > kmr_wire::MAX_SIZE {
            error!("FATAL: Request too long ({})", req_len);
            return;
        }
        let req_data = &mut buf[..req_len];
        if let Err(e) = infile.read_exact(req_data) {
            error!(
                "FATAL: Failed to read request data of length {} from connection: {:?}",
                req_len, e
            );
            return;
        }

        // Pass to the TA to process.
        debug!("-> TA: received data: (len={}) {}", req_data.len(), hex::encode(&req_data));
        let rsp = ta.process(req_data);
        debug!("<- TA: send data: (len={}) {}", rsp.len(), hex::encode(&rsp));

        // Send the response message down the pipe, as a 4-byte BE length followed by the message.
        let rsp_len: u32 = match rsp.len().try_into() {
            Ok(l) => l,
            Err(_e) => {
                error!("FATAL: Response too long (len={})", rsp.len());
                return;
            }
        };
        let rsp_len_data = rsp_len.to_be_bytes();
        if let Err(e) = outfile.write_all(&rsp_len_data[..]) {
            error!("FATAL: Failed to write response length to connection: {:?}", e);
            return;
        }
        if let Err(e) = outfile.write_all(&rsp) {
            error!(
                "FATAL: Failed to write response data of length {} to connection: {:?}",
                rsp_len, e
            );
            return;
        }
        let _ = outfile.flush();
    }
}

/// Software-only implementation using fake keys.
struct SoftwareKeys;

impl RetrieveKeyMaterial for SoftwareKeys {
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
