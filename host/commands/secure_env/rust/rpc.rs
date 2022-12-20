//! Emulated implementation of device traits for `IRemotelyProvisionedComponent`.

use core::cell::RefCell;
use kmr_common::crypto::{ec, ec::CoseKeyPurpose, Ec, KeyMaterial};
use kmr_common::{crypto, explicit, rpc_err, vec_try, Error};
use kmr_crypto_boring::{ec::BoringEc, hmac::BoringHmac};
use kmr_ta::device::{
    CsrSigningAlgorithm, DiceInfo, PubDiceArtifacts, RetrieveRpcArtifacts, RpcV2Req,
};
use kmr_wire::coset::{iana, CoseSign1Builder, HeaderBuilder};
use kmr_wire::keymint::Digest;
use kmr_wire::{cbor::value::Value, coset::AsCborValue, rpc, CborError};

/// Trait to encapsulate deterministic derivation of secret data.
pub trait DeriveBytes {
    /// Derive `output_len` bytes of data from `context`, deterministically.
    fn derive_bytes(&self, context: &[u8], output_len: usize) -> Result<Vec<u8>, Error>;
}

/// Common emulated implementation of RPC artifact retrieval.
pub struct Artifacts<T: DeriveBytes> {
    derive: T,
    dice_artifacts: RefCell<Option<(DiceInfo, crypto::ec::Key)>>,
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
        if self.dice_artifacts.borrow().is_none() {
            self.generate_dice_artifacts(rpc::TestMode(false))?;
        }

        let (dice_info, _) = self
            .dice_artifacts
            .borrow()
            .as_ref()
            .ok_or_else(|| rpc_err!(Failed, "DICE artifacts are not initialized."))?
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
            .ok_or_else(|| rpc_err!(Failed, "DICE artifacts are not initialized."))?
            .clone();

        let mut op = match dice_info.signing_algorithm {
            CsrSigningAlgorithm::ES256 => ec.begin_sign(private_key.into(), Digest::Sha256)?,
            CsrSigningAlgorithm::EdDSA => ec.begin_sign(private_key.into(), Digest::None)?,
        };
        op.update(data)?;
        op.finish()
    }
}

impl<T: DeriveBytes> Artifacts<T> {
    /// Constructor.
    pub fn new(derive: T) -> Self {
        Self { derive, dice_artifacts: RefCell::new(None) }
    }

    fn generate_dice_artifacts(&self, _test_mode: rpc::TestMode) -> Result<(), Error> {
        let ec = BoringEc::default();
        let secret = self.derive_bytes_from_hbk(&BoringHmac, b"Device Key Seed", 32)?;
        let (pub_cose_key, private_key) = match ec::import_raw_ed25519_key(&secret)? {
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
        let protected = HeaderBuilder::new().algorithm(iana::Algorithm::EdDSA).build();
        let dice_chain_entry = CoseSign1Builder::new()
            .protected(protected)
            .payload(dice_chain_entry_payload_data)
            .try_create_signature(&[], |input| {
                let mut op = ec.begin_sign(private_key.clone(), Digest::None)?;
                op.update(input)?;
                op.finish()
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
            signing_algorithm: CsrSigningAlgorithm::EdDSA,
            rpc_v2_test_cdi_priv: None,
        };

        *self.dice_artifacts.borrow_mut() = Some((dice_info, explicit!(private_key)?));

        Ok(())
    }
}
