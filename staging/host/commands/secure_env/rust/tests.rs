//! Tests for Cuttlefish-specific code.

#[test]
fn test_signing_cert_parse() {
    let sign_info = crate::attest::CertSignInfo::new();
    kmr_tests::test_signing_cert_parse(sign_info, false);
}
