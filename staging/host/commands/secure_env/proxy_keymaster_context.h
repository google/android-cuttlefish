//
// Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <map>
#include <vector>

#include <keymaster/key.h>
#include <keymaster/keymaster_context.h>
#include <keymaster/km_openssl/attestation_record.h>

#include "tpm_attestation_record.h"

namespace cuttlefish {

class TpmAttestationRecordContext;
class TpmResourceManager;
class TpmKeyBlobMaker;
class TpmRandomSource;
class TpmRemoteProvisioningContext;

/**
 * Implementation of KeymasterContext that proxies to another implementation.
 *
 * Because AndroidKeymaster wraps a KeymasterContext and puts it into a unique
 * pointer, it doesn't let the implementor manage the lifetime of the
 * KeymasterContext implementation. This proxy breaks that relationship, and
 * allows the lifetimes to be distinct as long as the KeymasterContext instance
 * outlives the AndroidKeymaster instance.
 */
class ProxyKeymasterContext : public keymaster::KeymasterContext {
 public:
  ProxyKeymasterContext(KeymasterContext& wrapped) : wrapped_(wrapped) {}
  ~ProxyKeymasterContext() = default;

  keymaster::KmVersion GetKmVersion() const override {
    return wrapped_.GetKmVersion();
  }

  keymaster_error_t SetSystemVersion(uint32_t os_version,
                                     uint32_t os_patchlevel) override {
    return wrapped_.SetSystemVersion(os_version, os_patchlevel);
  }
  void GetSystemVersion(uint32_t* os_version,
                        uint32_t* os_patchlevel) const override {
    return wrapped_.GetSystemVersion(os_version, os_patchlevel);
  }

  const keymaster::KeyFactory* GetKeyFactory(
      keymaster_algorithm_t algorithm) const override {
    return wrapped_.GetKeyFactory(algorithm);
  }
  const keymaster::OperationFactory* GetOperationFactory(
      keymaster_algorithm_t algorithm,
      keymaster_purpose_t purpose) const override {
    return wrapped_.GetOperationFactory(algorithm, purpose);
  }
  const keymaster_algorithm_t* GetSupportedAlgorithms(
      size_t* algorithms_count) const override {
    return wrapped_.GetSupportedAlgorithms(algorithms_count);
  }

  keymaster_error_t UpgradeKeyBlob(
      const keymaster::KeymasterKeyBlob& key_to_upgrade,
      const keymaster::AuthorizationSet& upgrade_params,
      keymaster::KeymasterKeyBlob* upgraded_key) const override {
    return wrapped_.UpgradeKeyBlob(key_to_upgrade, upgrade_params,
                                   upgraded_key);
  }

  keymaster_error_t ParseKeyBlob(
      const keymaster::KeymasterKeyBlob& blob,
      const keymaster::AuthorizationSet& additional_params,
      keymaster::UniquePtr<keymaster::Key>* key) const override {
    return wrapped_.ParseKeyBlob(blob, additional_params, key);
  }

  keymaster_error_t AddRngEntropy(const uint8_t* buf,
                                  size_t length) const override {
    return wrapped_.AddRngEntropy(buf, length);
  }

  keymaster::KeymasterEnforcement* enforcement_policy() override {
    return wrapped_.enforcement_policy();
  }

  keymaster::AttestationContext* attestation_context() override {
    return wrapped_.attestation_context();
  }

  keymaster::CertificateChain GenerateAttestation(
      const keymaster::Key& key,
      const keymaster::AuthorizationSet& attest_params,
      keymaster::UniquePtr<keymaster::Key> attest_key,
      const keymaster::KeymasterBlob& issuer_subject,
      keymaster_error_t* error) const override {
    return wrapped_.GenerateAttestation(
        key, attest_params, std::move(attest_key), issuer_subject, error);
  }

  keymaster::CertificateChain GenerateSelfSignedCertificate(
      const keymaster::Key& key, const keymaster::AuthorizationSet& cert_params,
      bool fake_signature, keymaster_error_t* error) const override {
    return wrapped_.GenerateSelfSignedCertificate(key, cert_params,
                                                  fake_signature, error);
  }

  keymaster_error_t UnwrapKey(
      const keymaster::KeymasterKeyBlob& wrapped_key_blob,
      const keymaster::KeymasterKeyBlob& wrapping_key_blob,
      const keymaster::AuthorizationSet& wrapping_key_params,
      const keymaster::KeymasterKeyBlob& masking_key,
      keymaster::AuthorizationSet* wrapped_key_params,
      keymaster_key_format_t* wrapped_key_format,
      keymaster::KeymasterKeyBlob* wrapped_key_material) const override {
    return wrapped_.UnwrapKey(
        wrapped_key_blob, wrapping_key_blob, wrapping_key_params, masking_key,
        wrapped_key_params, wrapped_key_format, wrapped_key_material);
  }

  keymaster_error_t CheckConfirmationToken(
      const std::uint8_t* input_data, size_t input_data_size,
      const uint8_t confirmation_token[keymaster::kConfirmationTokenSize])
      const {
    return wrapped_.CheckConfirmationToken(input_data, input_data_size,
                                           confirmation_token);
  }

  keymaster::RemoteProvisioningContext* GetRemoteProvisioningContext()
      const override {
    return wrapped_.GetRemoteProvisioningContext();
  }

  keymaster_error_t SetVendorPatchlevel(uint32_t vendor_patchlevel) override {
    return wrapped_.SetVendorPatchlevel(vendor_patchlevel);
  }
  keymaster_error_t SetBootPatchlevel(uint32_t boot_patchlevel) override {
    return wrapped_.SetBootPatchlevel(boot_patchlevel);
  }
  keymaster_error_t SetVerifiedBootInfo(
      std::string_view verified_boot_state, std::string_view bootloader_state,
      const std::vector<uint8_t>& vbmeta_digest) {
    return wrapped_.SetVerifiedBootInfo(verified_boot_state, bootloader_state,
                                        vbmeta_digest);
  }
  std::optional<uint32_t> GetVendorPatchlevel() const override {
    return wrapped_.GetVendorPatchlevel();
  }
  std::optional<uint32_t> GetBootPatchlevel() const override {
    return wrapped_.GetBootPatchlevel();
  }

  keymaster_error_t SetAttestationIds(
      const keymaster::SetAttestationIdsRequest& request) override {
    return wrapped_.SetAttestationIds(request);
  }

  keymaster_error_t SetAttestationIdsKM3(
      const keymaster::SetAttestationIdsKM3Request& request) override {
    return wrapped_.SetAttestationIdsKM3(request);
  }

 private:
  KeymasterContext& wrapped_;
};

}  // namespace cuttlefish
