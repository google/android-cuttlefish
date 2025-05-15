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

#include <optional>
//
#include <keymaster/soft_key_factory.h>
//
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {

/**
 * Encrypts key data using a TPM-resident key and signs it with a TPM-resident
 * key for privacy and integrity.
 *
 * This class is used to encrypt KeyMint data when it leaves the secure_env
 * process, and is sent for storage to Android. When the data comes back, this
 * class decrypts it again for use in Keymaster and other HAL API calls.
 */
class TpmKeyBlobMaker : public keymaster::SoftwareKeyBlobMaker {
public:
  TpmKeyBlobMaker(TpmResourceManager& resource_manager);

  keymaster_error_t CreateKeyBlob(
      const keymaster::AuthorizationSet& key_description,
      keymaster_key_origin_t origin,
      const keymaster::KeymasterKeyBlob& key_material,
      keymaster::KeymasterKeyBlob* blob,
      keymaster::AuthorizationSet* hw_enforced,
      keymaster::AuthorizationSet* sw_enforced) const override;

  keymaster_error_t UnvalidatedCreateKeyBlob(
      const keymaster::KeymasterKeyBlob& key_material,
      const keymaster::AuthorizationSet& hw_enforced,
      const keymaster::AuthorizationSet& sw_enforced,
      const keymaster::AuthorizationSet& hidden,
      keymaster::KeymasterKeyBlob* blob) const;

  /**
   * Intermediate function between KeymasterContext::ParseKeyBlob and
   * KeyFactory::LoadKey, The inputs of this function match the outputs of
   * KeymasterContext::ParseKeyBlob and the outputs of this function match the
   * inputs of KeyFactory::LoadKey.
   *
   * KeymasterContext::ParseKeyBlob is the common entry point for decoding all
   * keys, and is expected to delegate to a KeyFactory depending on the type of
   * the serialized key. This method performs decryption operations shared
   * between all TPM-Keymaster keys.
   */
  keymaster_error_t UnwrapKeyBlob(
      const keymaster_key_blob_t& blob,
      keymaster::AuthorizationSet* hw_enforced,
      keymaster::AuthorizationSet* sw_enforced,
      const keymaster::AuthorizationSet& hidden,
      keymaster::KeymasterKeyBlob* key_material) const;

  keymaster_error_t SetSystemVersion(uint32_t os_version, uint32_t os_patchlevel);
  keymaster_error_t SetVendorPatchlevel(uint32_t vendor_patchlevel);
  keymaster_error_t SetBootPatchlevel(uint32_t boot_patchlevel);

 private:
  TpmResourceManager& resource_manager_;
  uint32_t os_version_;
  uint32_t os_patchlevel_;
  std::optional<uint32_t> vendor_patchlevel_;
  std::optional<uint32_t> boot_patchlevel_;
};

}  // namespace cuttlefish
