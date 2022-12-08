//! KeyMint TA core for Cuttlefish.

extern crate alloc;

use core::cell::RefCell;
use kmr_common::crypto::{ec, ec::CoseKeyPurpose, Ec, Hkdf, KeyMaterial, Rng};
use kmr_common::{crypto, explicit, rpc_err, vec_try, Error};
use kmr_crypto_boring::{
    aes::BoringAes, aes_cmac::BoringAesCmac, des::BoringDes, ec::BoringEc, eq::BoringEq,
    hmac::BoringHmac, rng::BoringRng, rsa::BoringRsa,
};
use kmr_ta::device::{
    BootloaderDone, CsrSigningAlgorithm, DiceInfo, Implementation, PubDiceArtifacts,
    RetrieveKeyMaterial, RetrieveRpcArtifacts, RpcV2Req, TrustedPresenceUnsupported,
};
use kmr_ta::{HardwareInfo, KeyMintTa, RpcInfo, RpcInfoV3};
use kmr_wire::coset::{iana, CoseSign1Builder, HeaderBuilder};
use kmr_wire::keymint::{Digest, SecurityLevel};
use kmr_wire::rpc::MINIMUM_SUPPORTED_KEYS_IN_CSR;
use kmr_wire::{cbor::cbor, cbor::value::Value, coset::AsCborValue, CborError};
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
        rpc: &SoftRetrieveRpcArtifacts::new(),
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

/// Software implementation of `RetrieveRpcArtifacts` trait (only for IRPC V3 for now).
struct SoftRetrieveRpcArtifacts {
    hbk: Vec<u8>,
    dice_artifacts: RefCell<Option<(DiceInfo, crypto::ec::Key)>>,
}

impl RetrieveRpcArtifacts for SoftRetrieveRpcArtifacts {
    fn derive_bytes_from_hbk(&self, context: &[u8], output_len: usize) -> Result<Vec<u8>, Error> {
        let hkdf = BoringHmac;
        let derived_key = hkdf.hkdf(&[], &self.hbk, context, output_len)?;
        Ok(derived_key)
    }

    fn get_dice_info<'a>(&self, _test_mode: bool) -> Result<DiceInfo, Error> {
        if self.dice_artifacts.borrow().is_none() {
            // TODO: Add an enum to the device trait to distinguish the test mode vs
            // production mode.
            self.generate_dice_artifacts(false /*test mode*/)?;
        }

        let (dice_info, _) = self
            .dice_artifacts
            .borrow()
            .as_ref()
            .ok_or_else(|| rpc_err!(Failed, "DICE artifacts is not initialized."))?
            .clone();
        Ok(dice_info)
    }

    fn sign_data<'a>(
        &self,
        ec: &dyn crypto::Ec,
        data: &[u8],
        _rpc_v2: Option<RpcV2Req<'a>>,
    ) -> Result<Vec<u8>, Error> {
        // DICE artifacts should have been initialized via `get_dice_info` by the time this
        // method is called.
        let (dice_info, private_key) = self
            .dice_artifacts
            .borrow()
            .as_ref()
            .ok_or_else(|| rpc_err!(Failed, "DICE artifacts is not initialized."))?
            .clone();

        let mut op = match dice_info.signing_algorithm {
            CsrSigningAlgorithm::ES256 => ec.begin_sign(private_key.into(), Digest::Sha256)?,
            CsrSigningAlgorithm::EdDSA => ec.begin_sign(private_key.into(), Digest::None)?,
        };
        op.update(data)?;
        op.finish()
    }
}

impl SoftRetrieveRpcArtifacts {
    fn new() -> Self {
        // Use random data as an emulation of a hardware-backed key.
        let mut hbk = vec![0; 32];
        let mut rng = BoringRng::default();
        rng.fill_bytes(&mut hbk);
        SoftRetrieveRpcArtifacts { hbk, dice_artifacts: RefCell::new(None) }
    }

    fn generate_dice_artifacts(&self, _test_mode: bool) -> Result<(), Error> {
        let secret = self.derive_bytes_from_hbk(b"Device Key Seed", 32)?;
        let cdi_leaf_key = ec::import_raw_ed25519_key(&secret)?;
        let ec = BoringEc::default();
        let (pub_cose_key, private_key) =
            if let KeyMaterial::Ec(curve, curve_type, key) = cdi_leaf_key {
                (
                    key.public_cose_key(
                        &ec,
                        curve,
                        curve_type,
                        CoseKeyPurpose::Sign,
                        None,
                        false, /*test mode*/
                    )?,
                    key,
                )
            } else {
                return Err(rpc_err!(
                    Failed,
                    "expected the Ec variant of KeyMaterial for the cdi leaf key."
                ));
            };

        let cose_key_cbor = pub_cose_key.to_cbor_value().map_err(CborError::from)?;
        let cose_key_cbor_data = kmr_ta::rkp::serialize_cbor(&cose_key_cbor)?;

        // Key Usage = `keyCertSign` as per RFC 5280 Section 4.2.1.3
        let key_usage = Value::Bytes(vec_try![0x20]?);

        // Construct `DiceChainEntryPayload`
        let dice_chain_entry_payload = cbor!({
            // Issuer
            1 => Value::Text(String::from("Issuer")),
            // Subject
            2 => Value::Text(String::from("Subject")),
            // Subject public key
            -4670552 => Value::Bytes(cose_key_cbor_data),
            // Key Usage field contains a CBOR byte string of the bits which correspond
            // to `keyCertSign` as per RFC 5280 Section 4.2.1.3 (in little-endian byte order)
            -4670553 => key_usage,
        })?;

        let dice_chain_entry_payload_data = kmr_ta::rkp::serialize_cbor(&dice_chain_entry_payload)?;

        // Construct `DiceChainEntry`
        let protected = HeaderBuilder::new().algorithm(iana::Algorithm::EdDSA).build();
        let dice_chain_entry = CoseSign1Builder::new()
            .protected(protected)
            .payload(dice_chain_entry_payload_data)
            .try_create_signature(&[], |input| -> Result<Vec<u8>, Error> {
                let mut op = ec.begin_sign(private_key.clone(), Digest::None)?;
                op.update(input)?;
                op.finish()
            })?
            .build();
        let dice_chain_entry_cbor = dice_chain_entry.to_cbor_value().map_err(CborError::from)?;

        // Construct `DiceCertChain`
        let dice_cert_chain = Value::Array(vec![cose_key_cbor, dice_chain_entry_cbor]);

        let dice_cert_chain_data = kmr_ta::rkp::serialize_cbor(&dice_cert_chain)?;

        // Construct `UdsCerts` as an empty cbor map
        let uds_certs_data = kmr_ta::rkp::serialize_cbor(&cbor!({})?)?;

        let pub_dice_artifacts =
            PubDiceArtifacts { dice_cert_chain: dice_cert_chain_data, uds_certs: uds_certs_data };

        let dice_info = DiceInfo {
            pub_dice_artifacts,
            signing_algorithm: CsrSigningAlgorithm::EdDSA,
            rpc_v2_test_cdi_priv: None,
        };

        *self.dice_artifacts.borrow_mut() = Some((dice_info, explicit!(private_key)?));

        Ok(())
    }
}
