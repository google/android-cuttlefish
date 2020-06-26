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

#include "tpm_keymaster_context.h"

#include <android-base/logging.h>
#include <keymaster/km_openssl/aes_key.h>
#include <keymaster/km_openssl/ec_key_factory.h>
#include <keymaster/km_openssl/hmac_key.h>
#include <keymaster/km_openssl/rsa_key_factory.h>
#include <keymaster/km_openssl/soft_keymaster_enforcement.h>
#include <keymaster/km_openssl/triple_des_key.h>

#include "host/commands/secure_env/tpm_random_source.h"
#include "host/commands/secure_env/tpm_key_blob_maker.h"

using keymaster::AuthorizationSet;
using keymaster::KeymasterKeyBlob;
using keymaster::KeyFactory;
using keymaster::OperationFactory;

TpmKeymasterContext::TpmKeymasterContext(TpmResourceManager* resource_manager)
    : resource_manager_(resource_manager)
    , key_blob_maker_(new TpmKeyBlobMaker(resource_manager_))
    , random_source_(new TpmRandomSource(resource_manager_->Esys()))
    , enforcement_(new keymaster::SoftKeymasterEnforcement(64, 64)) {
  // TODO(b/155697375): Replace SoftKeymasterEnforcement
  key_factories_.emplace(
      KM_ALGORITHM_RSA, new keymaster::RsaKeyFactory(key_blob_maker_.get()));
  key_factories_.emplace(
      KM_ALGORITHM_EC, new keymaster::EcKeyFactory(key_blob_maker_.get()));
  key_factories_.emplace(
      KM_ALGORITHM_AES,
      new keymaster::AesKeyFactory(
          key_blob_maker_.get(), random_source_.get()));
  key_factories_.emplace(
      KM_ALGORITHM_TRIPLE_DES,
      new keymaster::TripleDesKeyFactory(
          key_blob_maker_.get(), random_source_.get()));
  key_factories_.emplace(
      KM_ALGORITHM_HMAC,
      new keymaster::HmacKeyFactory(
          key_blob_maker_.get(), random_source_.get()));
  for (const auto& it : key_factories_) {
    supported_algorithms_.push_back(it.first);
  }
}

keymaster_error_t TpmKeymasterContext::SetSystemVersion(
    uint32_t os_version, uint32_t os_patchlevel) {
  // TODO(b/155697375): Only accept new values of these from the bootloader
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
  return KM_ERROR_OK;
}

void TpmKeymasterContext::GetSystemVersion(
    uint32_t* os_version, uint32_t* os_patchlevel) const {
  *os_version = os_version_;
  *os_patchlevel = os_patchlevel_;
}

const KeyFactory* TpmKeymasterContext::GetKeyFactory(
    keymaster_algorithm_t algorithm) const {
  auto it = key_factories_.find(algorithm);
  if (it == key_factories_.end()) {
    LOG(ERROR) << "Could not find key factory for " << algorithm;
    return nullptr;
  }
  return it->second.get();
}

const OperationFactory* TpmKeymasterContext::GetOperationFactory(
    keymaster_algorithm_t algorithm, keymaster_purpose_t purpose) const {
  auto key_factory = GetKeyFactory(algorithm);
  if (key_factory == nullptr) {
    LOG(ERROR) << "Tried to get operation factory for " << purpose
              << " for invalid algorithm " << algorithm;
    return nullptr;
  }
  auto operation_factory = key_factory->GetOperationFactory(purpose);
  if (operation_factory == nullptr) {
    LOG(ERROR) << "Could not get operation factory for " << purpose
               << " from key factory for " << algorithm;
  }
  return operation_factory;
}

const keymaster_algorithm_t* TpmKeymasterContext::GetSupportedAlgorithms(
      size_t* algorithms_count) const {
  *algorithms_count = supported_algorithms_.size();
  return supported_algorithms_.data();
}

keymaster_error_t TpmKeymasterContext::UpgradeKeyBlob(
    const KeymasterKeyBlob&,
    const AuthorizationSet&,
    KeymasterKeyBlob*) const {
  LOG(ERROR) << "TODO(b/155697375): Implement UpgradeKeyBlob";
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster_error_t TpmKeymasterContext::ParseKeyBlob(
    const KeymasterKeyBlob& blob,
    const AuthorizationSet& additional_params,
    keymaster::UniquePtr<keymaster::Key>* key) const {
  keymaster::AuthorizationSet hw_enforced;
  keymaster::AuthorizationSet sw_enforced;
  keymaster::KeymasterKeyBlob key_material;

  auto rc =
      key_blob_maker_->UnwrapKeyBlob(
          blob,
          &hw_enforced,
          &sw_enforced,
          &key_material);
  if (rc != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to unwrap key: " << rc;
    return rc;
  }

  keymaster_algorithm_t algorithm;
  if (!hw_enforced.GetTagValue(keymaster::TAG_ALGORITHM, &algorithm) &&
      !sw_enforced.GetTagValue(keymaster::TAG_ALGORITHM, &algorithm)) {
    LOG(ERROR) << "No TAG_ALGORITHM value in hw_enforced or sw_enforced.";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  auto factory = GetKeyFactory(algorithm);
  if (factory == nullptr) {
    LOG(ERROR) << "Unable to find key factory for " << algorithm;
    return KM_ERROR_UNSUPPORTED_ALGORITHM;
  }
  rc =
      factory->LoadKey(
          std::move(key_material),
          additional_params,
          std::move(hw_enforced),
          std::move(sw_enforced),
          key);
  if (rc != KM_ERROR_OK) {
    LOG(ERROR) << "Unable to load unwrapped key: " << rc;
  }
  return rc;
}

keymaster_error_t TpmKeymasterContext::AddRngEntropy(
    const uint8_t*, size_t) const {
  LOG(ERROR) << "TODO(b/155697375): Implement AddRngEntropy";
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster::KeymasterEnforcement* TpmKeymasterContext::enforcement_policy() {
  return enforcement_.get();
}

keymaster_error_t TpmKeymasterContext::GenerateAttestation(
    const keymaster::Key&,
    const AuthorizationSet&,
    keymaster::CertChainPtr*) const {
  LOG(ERROR) << "TODO(b/155697200): Implement GenerateAttestation";
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster_error_t TpmKeymasterContext::UnwrapKey(
    const KeymasterKeyBlob&,
    const KeymasterKeyBlob&,
    const AuthorizationSet&,
    const KeymasterKeyBlob&,
    AuthorizationSet*,
    keymaster_key_format_t*,
    KeymasterKeyBlob*) const {
  LOG(ERROR) << "TODO(b/155697375): Implement UnwrapKey";
  return KM_ERROR_UNIMPLEMENTED;
}
