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
use kmr_ta_nonsecure::{attest, rpc, soft};
use kmr_wire::keymint::SecurityLevel;
use kmr_wire::rpc::MINIMUM_SUPPORTED_KEYS_IN_CSR;
use log::{error, info, trace};
use std::ffi::CString;
use std::io::{Read, Write};
use std::os::fd::AsFd;
use std::os::fd::AsRawFd;
use std::os::unix::ffi::OsStrExt;

mod clock;
mod sdd;
mod tpm;

#[cfg(test)]
mod tests;

// See `SnapshotSocketMessage` in suspend_resume_handler.h for docs.
const SNAPSHOT_SOCKET_MESSAGE_SUSPEND: u8 = 1;
const SNAPSHOT_SOCKET_MESSAGE_SUSPEND_ACK: u8 = 2;
const SNAPSHOT_SOCKET_MESSAGE_RESUME: u8 = 3;

/// Main routine for the KeyMint TA. Only returns if there is a fatal error.
///
/// # Safety
///
/// TODO: What are the preconditions for `trm`?
pub unsafe fn ta_main(
    mut infile: std::fs::File,
    mut outfile: std::fs::File,
    security_level: SecurityLevel,
    trm: *mut libc::c_void,
    mut snapshot_socket: std::os::unix::net::UnixStream,
) {
    log::set_logger(&AndroidCppLogger).unwrap();
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

    let sign_info = attest::CertSignInfo::new();
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
        sign_info: Box::new(sign_info),
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

    let mut buf = [0; kmr_wire::DEFAULT_MAX_SIZE];
    loop {
        // Wait for data from either `infile` or `snapshot_socket`. If both have data, we prioritize
        // processing only `infile` until it is empty so that there is no pending state when we
        // suspend the loop.
        let mut fd_set = nix::sys::select::FdSet::new();
        fd_set.insert(infile.as_fd());
        fd_set.insert(snapshot_socket.as_fd());
        if let Err(e) = nix::sys::select::select(
            None,
            /*readfds=*/ Some(&mut fd_set),
            None,
            None,
            /*timeout=*/ None,
        ) {
            error!("FATAL: Failed to select on input FDs: {:?}", e);
            return;
        }

        if fd_set.contains(infile.as_fd()) {
            // Read a request message from the pipe, as a 4-byte BE length followed by the message.
            let mut req_len_data = [0u8; 4];
            if let Err(e) = infile.read_exact(&mut req_len_data) {
                error!("FATAL: Failed to read request length from connection: {:?}", e);
                return;
            }
            let req_len = u32::from_be_bytes(req_len_data) as usize;
            if req_len > kmr_wire::DEFAULT_MAX_SIZE {
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
            trace!("-> TA: received data: (len={})", req_data.len());
            let rsp = ta.process(req_data);
            trace!("<- TA: send data: (len={})", rsp.len());

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

            continue;
        }

        if fd_set.contains(snapshot_socket.as_fd()) {
            // Read suspend request.
            let mut suspend_request = 0u8;
            if let Err(e) = snapshot_socket.read_exact(std::slice::from_mut(&mut suspend_request)) {
                error!("FATAL: Failed to read suspend request: {:?}", e);
                return;
            }
            if suspend_request != SNAPSHOT_SOCKET_MESSAGE_SUSPEND {
                error!(
                    "FATAL: Unexpected value from snapshot socket: got {}, expected {}",
                    suspend_request, SNAPSHOT_SOCKET_MESSAGE_SUSPEND
                );
                return;
            }
            // Write ACK.
            if let Err(e) = snapshot_socket.write_all(&[SNAPSHOT_SOCKET_MESSAGE_SUSPEND_ACK]) {
                error!("FATAL: Failed to write suspend ACK request: {:?}", e);
                return;
            }
            // Block until we get a resume request.
            let mut resume_request = 0u8;
            if let Err(e) = snapshot_socket.read_exact(std::slice::from_mut(&mut resume_request)) {
                error!("FATAL: Failed to read resume request: {:?}", e);
                return;
            }
            if resume_request != SNAPSHOT_SOCKET_MESSAGE_RESUME {
                error!(
                    "FATAL: Unexpected value from snapshot socket: got {}, expected {}",
                    resume_request, SNAPSHOT_SOCKET_MESSAGE_RESUME
                );
                return;
            }
        }
    }
}

// TODO(schuffelen): Use android_logger when rust works with host glibc, see aosp/1415969
struct AndroidCppLogger;

impl log::Log for AndroidCppLogger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        // Filtering is done in the underlying C++ logger, so indicate to the Rust code that all
        // logs should be included
        true
    }

    fn log(&self, record: &log::Record) {
        let file = record.file().unwrap_or("(no file)");
        let file_basename =
            std::path::Path::new(file).file_name().unwrap_or(std::ffi::OsStr::new("(no file)"));
        let file = CString::new(file_basename.as_bytes())
            .unwrap_or_else(|_| CString::new("(invalid file)").unwrap());
        let line = record.line().unwrap_or(0);
        let severity = match record.level() {
            log::Level::Trace => 0,
            log::Level::Debug => 1,
            log::Level::Info => 2,
            log::Level::Warn => 3,
            log::Level::Error => 4,
        };
        let tag = CString::new("secure_env::".to_owned() + record.target())
            .unwrap_or_else(|_| CString::new("(invalid tag)").unwrap());
        let msg = CString::new(format!("{}", record.args()))
            .unwrap_or_else(|_| CString::new("(invalid msg)").unwrap());
        // SAFETY: All pointer arguments are generated from valid owned CString instances.
        unsafe {
            secure_env_tpm::secure_env_log(
                file.as_ptr(),
                line,
                severity,
                tag.as_ptr(),
                msg.as_ptr(),
            );
        }
    }

    fn flush(&self) {}
}
