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

class TpmResourceManager;
class TpmKeyBlobMaker;
class TpmRandomSource;

/**
 * Implementation of KeymasterContext that wraps its keys with a TPM.
 *
 * See the parent class for details:
 * https://cs.android.com/android/platform/superproject/+/master:system/keymaster/include/keymaster/keymaster_context.h;drc=821acb74d7febb886a9b7cefee4ee3df4cc8c556
 */
class TpmKeymasterContext : public keymaster::KeymasterContext {
private:
  TpmResourceManager* resource_manager_;
  std::unique_ptr<TpmKeyBlobMaker> key_blob_maker_;
  std::unique_ptr<TpmRandomSource> random_source_;
  std::unique_ptr<keymaster::KeymasterEnforcement> enforcement_;
  std::map<keymaster_algorithm_t, std::unique_ptr<keymaster::KeyFactory>> key_factories_;
  std::vector<keymaster_algorithm_t> supported_algorithms_;
  uint32_t os_version_;
  uint32_t os_patchlevel_;
public:
  TpmKeymasterContext(TpmResourceManager* resource_manager);
  ~TpmKeymasterContext() = default;

  keymaster_error_t SetSystemVersion(
      uint32_t os_version, uint32_t os_patchlevel) override;
  void GetSystemVersion(
      uint32_t* os_version, uint32_t* os_patchlevel) const override;

  const keymaster::KeyFactory* GetKeyFactory(
      keymaster_algorithm_t algorithm) const override;
  const keymaster::OperationFactory* GetOperationFactory(
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

  keymaster_error_t AddRngEntropy(
      const uint8_t* buf, size_t length) const override;

  keymaster::KeymasterEnforcement* enforcement_policy() override;

  keymaster_error_t GenerateAttestation(
      const keymaster::Key& key,
      const keymaster::AuthorizationSet& attest_params,
      keymaster::CertChainPtr* cert_chain) const override;

  keymaster_error_t UnwrapKey(
      const keymaster::KeymasterKeyBlob& wrapped_key_blob,
      const keymaster::KeymasterKeyBlob& wrapping_key_blob,
      const keymaster::AuthorizationSet& wrapping_key_params,
      const keymaster::KeymasterKeyBlob& masking_key,
      keymaster::AuthorizationSet* wrapped_key_params,
      keymaster_key_format_t* wrapped_key_format,
      keymaster::KeymasterKeyBlob* wrapped_key_material) const override;
};
