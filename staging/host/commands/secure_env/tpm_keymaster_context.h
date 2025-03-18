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
 * Implementation of KeymasterContext that wraps its keys with a TPM.
 *
 * See the parent class for details:
 * https://cs.android.com/android/platform/superproject/+/master:system/keymaster/include/keymaster/keymaster_context.h;drc=821acb74d7febb886a9b7cefee4ee3df4cc8c556
 */
class TpmKeymasterContext : public keymaster::KeymasterContext {
 private:
  TpmResourceManager& resource_manager_;
  keymaster::KeymasterEnforcement& enforcement_;
  std::unique_ptr<TpmKeyBlobMaker> key_blob_maker_;
  std::unique_ptr<TpmRandomSource> random_source_;
  std::unique_ptr<TpmAttestationRecordContext> attestation_context_;
  std::unique_ptr<TpmRemoteProvisioningContext> remote_provisioning_context_;
  std::map<keymaster_algorithm_t, std::unique_ptr<keymaster::KeyFactory>>
      key_factories_;
  std::vector<keymaster_algorithm_t> supported_algorithms_;
  uint32_t os_version_;
  uint32_t os_patchlevel_;
  std::optional<uint32_t> vendor_patchlevel_;
  std::optional<uint32_t> boot_patchlevel_;
  std::optional<std::string> bootloader_state_;
  std::optional<std::string> verified_boot_state_;
  std::optional<std::vector<uint8_t>> vbmeta_digest_;

 public:
  TpmKeymasterContext(TpmResourceManager&, keymaster::KeymasterEnforcement&);
  ~TpmKeymasterContext() = default;

  keymaster::KmVersion GetKmVersion() const override {
    return attestation_context_->GetKmVersion();
  }

  keymaster_error_t SetSystemVersion(uint32_t os_version,
                                     uint32_t os_patchlevel) override;
  void GetSystemVersion(uint32_t* os_version,
                        uint32_t* os_patchlevel) const override;

  const keymaster::KeyFactory* GetKeyFactory(
      keymaster_algorithm_t algorithm) const override;
  keymaster::OperationFactory* GetOperationFactory(
      keymaster_algorithm_t algorithm,
      keymaster_purpose_t purpose) const override;
  const keymaster_algorithm_t* GetSupportedAlgorithms(
      size_t* algorithms_count) const override;

  keymaster_error_t UpgradeKeyBlob(
      const keymaster::KeymasterKeyBlob& key_to_upgrade,
      const keymaster::AuthorizationSet& upgrade_params,
      keymaster::KeymasterKeyBlob* upgraded_key) const override;

  keymaster_error_t ParseKeyBlob(
      const keymaster::KeymasterKeyBlob& blob,
      const keymaster::AuthorizationSet& additional_params,
      keymaster::UniquePtr<keymaster::Key>* key) const override;

  keymaster_error_t AddRngEntropy(const uint8_t* buf,
                                  size_t length) const override;

  keymaster::KeymasterEnforcement* enforcement_policy() override;

  keymaster::AttestationContext* attestation_context() override {
    return attestation_context_.get();
  }

  keymaster::CertificateChain GenerateAttestation(
      const keymaster::Key& key,
      const keymaster::AuthorizationSet& attest_params,
      keymaster::UniquePtr<keymaster::Key> attest_key,
      const keymaster::KeymasterBlob& issuer_subject,
      keymaster_error_t* error) const override;

  keymaster::CertificateChain GenerateSelfSignedCertificate(
      const keymaster::Key& key, const keymaster::AuthorizationSet& cert_params,
      bool fake_signature, keymaster_error_t* error) const override;

  keymaster_error_t UnwrapKey(
      const keymaster::KeymasterKeyBlob& wrapped_key_blob,
      const keymaster::KeymasterKeyBlob& wrapping_key_blob,
      const keymaster::AuthorizationSet& wrapping_key_params,
      const keymaster::KeymasterKeyBlob& masking_key,
      keymaster::AuthorizationSet* wrapped_key_params,
      keymaster_key_format_t* wrapped_key_format,
      keymaster::KeymasterKeyBlob* wrapped_key_material) const override;

  keymaster_error_t CheckConfirmationToken(
      const std::uint8_t* input_data, size_t input_data_size,
      const uint8_t confirmation_token[keymaster::kConfirmationTokenSize])
      const;

  keymaster::RemoteProvisioningContext* GetRemoteProvisioningContext()
      const override;

  keymaster_error_t SetVerifiedBootInfo(
      std::string_view verified_boot_state, std::string_view bootloader_state,
      const std::vector<uint8_t>& vbmeta_digest) override;

  keymaster_error_t SetVendorPatchlevel(uint32_t vendor_patchlevel) override;
  keymaster_error_t SetBootPatchlevel(uint32_t boot_patchlevel) override;
  std::optional<uint32_t> GetVendorPatchlevel() const override;
  std::optional<uint32_t> GetBootPatchlevel() const override;

  keymaster_error_t SetAttestationIds(
      const keymaster::SetAttestationIdsRequest& request) override {
    return attestation_context_->SetAttestationIds(request);
  }

  keymaster_error_t SetAttestationIdsKM3(
      const keymaster::SetAttestationIdsKM3Request& request) override {
    return attestation_context_->SetAttestationIdsKM3(request);
  }
};

}  // namespace cuttlefish
