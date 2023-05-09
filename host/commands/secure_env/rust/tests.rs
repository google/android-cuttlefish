//! Tests for Cuttlefish-specific code.
use crate::soft;

#[test]
fn test_signing_cert_parse() {
    let sign_info = crate::attest::CertSignInfo::new();
    kmr_tests::test_signing_cert_parse(sign_info, false);
}

#[test]
fn test_rkp_soft_trait() {
    let hmac = kmr_crypto_boring::hmac::BoringHmac;
    let soft_rpc = soft::RpcArtifacts::new(soft::Derive::default());
    kmr_tests::test_retrieve_rpc_artifacts(soft_rpc, &hmac, &hmac);
}
