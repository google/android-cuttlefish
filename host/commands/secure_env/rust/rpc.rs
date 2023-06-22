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

//! Emulated implementation of device traits for `IRemotelyProvisionedComponent`.

use core::cell::RefCell;
use kmr_common::crypto::{ec, ec::CoseKeyPurpose, Ec, KeyMaterial};
use kmr_common::{crypto, explicit, rpc_err, vec_try, Error};
use kmr_crypto_boring::{ec::BoringEc, hmac::BoringHmac, rng::BoringRng};
use kmr_ta::device::{
    CsrSigningAlgorithm, DiceInfo, PubDiceArtifacts, RetrieveRpcArtifacts, RpcV2Req,
};
use kmr_wire::coset::{iana, CoseSign1Builder, HeaderBuilder};
use kmr_wire::keymint::{Digest, EcCurve};
use kmr_wire::{cbor::value::Value, coset::AsCborValue, rpc, CborError};

/// Trait to encapsulate deterministic derivation of secret data.
pub trait DeriveBytes {
    /// Derive `output_len` bytes of data from `context`, deterministically.
    fn derive_bytes(&self, context: &[u8], output_len: usize) -> Result<Vec<u8>, Error>;
}

/// Common emulated implementation of RPC artifact retrieval.
pub struct Artifacts<T: DeriveBytes> {
    derive: T,
    sign_algo: CsrSigningAlgorithm,
    // Invariant once populated: `self.dice_info.signing_algorithm` == `self.sign_algo`
    dice_info: RefCell<Option<DiceInfo>>,
    // Invariant once populated: `self.bcc_signing_key` is a variant that matches `self.sign_algo`
    bcc_signing_key: RefCell<Option<ec::Key>>,
}

impl<T: DeriveBytes> RetrieveRpcArtifacts for Artifacts<T> {
    fn derive_bytes_from_hbk(
        &self,
        _hkdf: &dyn crypto::Hkdf,
        context: &[u8],
        output_len: usize,
    ) -> Result<Vec<u8>, Error> {
        self.derive.derive_bytes(context, output_len)
    }

    fn get_dice_info<'a>(&self, _test_mode: rpc::TestMode) -> Result<DiceInfo, Error> {
        if self.dice_info.borrow().is_none() {
            let (dice_info, priv_key) = self.generate_dice_artifacts(rpc::TestMode(false))?;
            *self.dice_info.borrow_mut() = Some(dice_info);
            *self.bcc_signing_key.borrow_mut() = Some(priv_key);
        }

        Ok(self
            .dice_info
            .borrow()
            .as_ref()
            .ok_or_else(|| rpc_err!(Failed, "DICE artifacts are not initialized."))?
            .clone())
    }

    fn sign_data(
        &self,
        ec: &dyn crypto::Ec,
        data: &[u8],
        _rpc_v2: Option<RpcV2Req>,
    ) -> Result<Vec<u8>, Error> {
        // DICE artifacts should have been initialized via `get_dice_info()` by the time this
        // method is called.
        let private_key = self
            .bcc_signing_key
            .borrow()
            .as_ref()
            .ok_or_else(|| rpc_err!(Failed, "DICE artifacts are not initialized."))?
            .clone();

        let mut op = ec.begin_sign(private_key.into(), self.signing_digest())?;
        op.update(data)?;
        let sig = op.finish()?;
        crypto::ec::to_cose_signature(self.signing_curve(), sig)
    }
}

impl<T: DeriveBytes> Artifacts<T> {
    /// Constructor.
    pub fn new(derive: T, sign_algo: CsrSigningAlgorithm) -> Self {
        Self {
            derive,
            sign_algo,
            dice_info: RefCell::new(None),
            bcc_signing_key: RefCell::new(None),
        }
    }

    /// Indicate the curve used in signing.
    fn signing_curve(&self) -> EcCurve {
        match self.sign_algo {
            CsrSigningAlgorithm::ES256 => EcCurve::P256,
            CsrSigningAlgorithm::ES384 => EcCurve::P384,
            CsrSigningAlgorithm::EdDSA => EcCurve::Curve25519,
        }
    }

    /// Indicate the digest used in signing.
    fn signing_digest(&self) -> Digest {
        match self.sign_algo {
            CsrSigningAlgorithm::ES256 => Digest::Sha256,
            CsrSigningAlgorithm::ES384 => Digest::Sha384,
            CsrSigningAlgorithm::EdDSA => Digest::None,
        }
    }

    /// Indicate the COSE algorithm value associated with signing.
    fn signing_cose_algo(&self) -> iana::Algorithm {
        match self.sign_algo {
            CsrSigningAlgorithm::ES256 => iana::Algorithm::ES256,
            CsrSigningAlgorithm::ES384 => iana::Algorithm::ES384,
            CsrSigningAlgorithm::EdDSA => iana::Algorithm::EdDSA,
        }
    }

    fn generate_dice_artifacts(
        &self,
        _test_mode: rpc::TestMode,
    ) -> Result<(DiceInfo, ec::Key), Error> {
        let ec = BoringEc::default();

        let key_material = match self.sign_algo {
            CsrSigningAlgorithm::EdDSA => {
                let secret = self.derive_bytes_from_hbk(&BoringHmac, b"Device Key Seed", 32)?;
                ec::import_raw_ed25519_key(&secret)
            }
            // TODO: generate the *same* key after reboot, by use of the TPM.
            CsrSigningAlgorithm::ES256 => {
                ec.generate_nist_key(&mut BoringRng::default(), ec::NistCurve::P256, &[])
            }
            CsrSigningAlgorithm::ES384 => {
                ec.generate_nist_key(&mut BoringRng::default(), ec::NistCurve::P384, &[])
            }
        }?;
        let (pub_cose_key, private_key) = match key_material {
            KeyMaterial::Ec(curve, curve_type, key) => (
                key.public_cose_key(
                    &ec,
                    curve,
                    curve_type,
                    CoseKeyPurpose::Sign,
                    None, /* no key ID */
                    rpc::TestMode(false),
                )?,
                key,
            ),
            _ => {
                return Err(rpc_err!(
                    Failed,
                    "expected the Ec variant of KeyMaterial for the cdi leaf key."
                ))
            }
        };

        let cose_key_cbor = pub_cose_key.to_cbor_value().map_err(CborError::from)?;
        let cose_key_cbor_data = kmr_ta::rkp::serialize_cbor(&cose_key_cbor)?;

        // Construct `DiceChainEntryPayload`
        let dice_chain_entry_payload = Value::Map(vec_try![
            // Issuer
            (Value::Integer(1.into()), Value::Text(String::from("Issuer"))),
            // Subject
            (Value::Integer(2.into()), Value::Text(String::from("Subject"))),
            // Subject public key
            (Value::Integer((-4670552).into()), Value::Bytes(cose_key_cbor_data)),
            // Key Usage field contains a CBOR byte string of the bits which correspond
            // to `keyCertSign` as per RFC 5280 Section 4.2.1.3 (in little-endian byte order)
            (Value::Integer((-4670553).into()), Value::Bytes(vec_try![0x20]?)),
        ]?);
        let dice_chain_entry_payload_data = kmr_ta::rkp::serialize_cbor(&dice_chain_entry_payload)?;

        // Construct `DiceChainEntry`
        let protected = HeaderBuilder::new().algorithm(self.signing_cose_algo()).build();
        let dice_chain_entry = CoseSign1Builder::new()
            .protected(protected)
            .payload(dice_chain_entry_payload_data)
            .try_create_signature(&[], |input| {
                let mut op = ec.begin_sign(private_key.clone(), self.signing_digest())?;
                op.update(input)?;
                let sig = op.finish()?;
                crypto::ec::to_cose_signature(self.signing_curve(), sig)
            })?
            .build();
        let dice_chain_entry_cbor = dice_chain_entry.to_cbor_value().map_err(CborError::from)?;

        // Construct `DiceCertChain`
        let dice_cert_chain = Value::Array(vec_try![cose_key_cbor, dice_chain_entry_cbor]?);
        let dice_cert_chain_data = kmr_ta::rkp::serialize_cbor(&dice_cert_chain)?;

        // Construct `UdsCerts` as an empty CBOR map
        let uds_certs_data = kmr_ta::rkp::serialize_cbor(&Value::Map(Vec::new()))?;

        let pub_dice_artifacts =
            PubDiceArtifacts { dice_cert_chain: dice_cert_chain_data, uds_certs: uds_certs_data };

        let dice_info = DiceInfo {
            pub_dice_artifacts,
            signing_algorithm: self.sign_algo,
            rpc_v2_test_cdi_priv: None,
        };

        Ok((dice_info, explicit!(private_key)?))
    }
}
