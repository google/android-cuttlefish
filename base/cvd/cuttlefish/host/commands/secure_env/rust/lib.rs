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

//! KeyMint TA core for Cuttlefish.

extern crate alloc;

use kmr_common::crypto;
use kmr_crypto_boring::{
    aes::BoringAes, aes_cmac::BoringAesCmac, des::BoringDes, ec::BoringEc, eq::BoringEq,
    hmac::BoringHmac, rng::BoringRng, rsa::BoringRsa, sha256::BoringSha256,
};
use kmr_ta::device::{
    BootloaderDone, CsrSigningAlgorithm, Implementation, TrustedPresenceUnsupported,
};
use kmr_ta::{HardwareInfo, KeyMintTa, RpcInfo, RpcInfoV3};
use kmr_ta_nonsecure::{rpc, soft};
use kmr_wire::keymint::SecurityLevel;
use kmr_wire::rpc::MINIMUM_SUPPORTED_KEYS_IN_CSR;
use log::{error, info};
use secure_env_common::run_ta_loop;
use std::os::fd::AsRawFd;

mod clock;
mod sdd;
mod tpm;

#[cfg(test)]
mod tests;

/// Main routine for the KeyMint TA. Only returns if there is a fatal error.
///
/// # Safety
///
/// TODO: What are the preconditions for `trm`?
pub unsafe fn ta_main(
    infile: std::fs::File,
    outfile: std::fs::File,
    security_level: SecurityLevel,
    trm: *mut libc::c_void,
    snapshot_socket: std::os::unix::net::UnixStream,
) {
    log::set_logger(&secure_env_common::logger::AndroidCppLogger).unwrap();
    log::set_max_level(log::LevelFilter::Debug); // Filtering happens elsewhere
    info!(
        "KeyMint Rust TA running with infile={}, outfile={}, security_level={:?}",
        infile.as_raw_fd(),
        outfile.as_raw_fd(),
        security_level,
    );

    let hw_info = HardwareInfo {
        version_number: 1,
        security_level,
        impl_name: "Rust reference implementation for Cuttlefish",
        author_name: "Google",
        unique_id: "Cuttlefish KeyMint TA",
    };

    let rpc_sign_algo = CsrSigningAlgorithm::EdDSA;
    let rpc_info_v3 = RpcInfoV3 {
        author_name: "Google",
        unique_id: "Cuttlefish KeyMint TA",
        fused: false,
        supported_num_of_keys_in_csr: MINIMUM_SUPPORTED_KEYS_IN_CSR,
    };

    let mut rng = BoringRng;
    let sdd_mgr: Option<Box<dyn kmr_common::keyblob::SecureDeletionSecretManager>> =
        match sdd::HostSddManager::new(&mut rng) {
            Ok(v) => Some(Box::new(v)),
            Err(e) => {
                error!("Failed to initialize secure deletion data manager: {:?}", e);
                None
            }
        };
    let clock = clock::StdClock;
    let rsa = BoringRsa::default();
    let ec = BoringEc::default();
    let hkdf: Box<dyn kmr_common::crypto::Hkdf> =
        if security_level == SecurityLevel::TrustedEnvironment {
            Box::new(tpm::KeyDerivation::new(trm))
        } else {
            Box::new(BoringHmac)
        };
    let imp = crypto::Implementation {
        rng: Box::new(rng),
        clock: Some(Box::new(clock)),
        compare: Box::new(BoringEq),
        aes: Box::new(BoringAes),
        des: Box::new(BoringDes),
        hmac: Box::new(BoringHmac),
        rsa: Box::new(rsa),
        ec: Box::new(ec),
        ckdf: Box::new(BoringAesCmac),
        hkdf,
        sha256: Box::new(BoringSha256),
    };

    let keys: Box<dyn kmr_ta::device::RetrieveKeyMaterial> =
        if security_level == SecurityLevel::TrustedEnvironment {
            Box::new(tpm::Keys::new(trm))
        } else {
            Box::new(soft::Keys)
        };
    let rpc: Box<dyn kmr_ta::device::RetrieveRpcArtifacts> =
        if security_level == SecurityLevel::TrustedEnvironment {
            Box::new(tpm::RpcArtifacts::new(tpm::TpmHmac::new(trm), rpc_sign_algo))
        } else {
            Box::new(soft::RpcArtifacts::new(soft::Derive::default(), rpc_sign_algo))
        };
    let dev = Implementation {
        keys,
        // Cuttlefish has `remote_provisioning.tee.rkp_only=1` so don't support batch signing
        // of keys.  This can be reinstated with:
        // ```
        // sign_info: Some(kmr_ta_nonsecure::attest::CertSignInfo::new()),
        // ```
        sign_info: None,
        // HAL populates attestation IDs from properties.
        attest_ids: None,
        sdd_mgr,
        // `BOOTLOADER_ONLY` keys not supported.
        bootloader: Box::new(BootloaderDone),
        // `STORAGE_KEY` keys not supported.
        sk_wrapper: None,
        // `TRUSTED_USER_PRESENCE_REQUIRED` keys not supported
        tup: Box::new(TrustedPresenceUnsupported),
        // No support for converting previous implementation's keyblobs.
        legacy_key: None,
        rpc,
    };
    let mut ta = KeyMintTa::new(hw_info, RpcInfo::V3(rpc_info_v3), imp, dev);

    run_ta_loop(infile, outfile, snapshot_socket, kmr_wire::DEFAULT_MAX_SIZE, |req| {
        ta.process(req)
    });
}
