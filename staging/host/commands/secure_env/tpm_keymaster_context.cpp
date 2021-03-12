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
#include <keymaster/contexts/soft_attestation_cert.h>
#include <keymaster/km_openssl/aes_key.h>
#include <keymaster/km_openssl/asymmetric_key.h>
#include <keymaster/km_openssl/attestation_utils.h>
#include <keymaster/km_openssl/certificate_utils.h>
#include <keymaster/km_openssl/ec_key_factory.h>
#include <keymaster/km_openssl/hmac_key.h>
#include <keymaster/km_openssl/rsa_key_factory.h>
#include <keymaster/km_openssl/soft_keymaster_enforcement.h>
#include <keymaster/km_openssl/triple_des_key.h>

#include "host/commands/secure_env/tpm_attestation_record.h"
#include "host/commands/secure_env/tpm_random_source.h"
#include "host/commands/secure_env/tpm_key_blob_maker.h"

using keymaster::AuthorizationSet;
using keymaster::KeymasterKeyBlob;
using keymaster::KeyFactory;
using keymaster::OperationFactory;

TpmKeymasterContext::TpmKeymasterContext(
    TpmResourceManager& resource_manager,
    keymaster::KeymasterEnforcement& enforcement)
    : resource_manager_(resource_manager)
    , enforcement_(enforcement)
    , key_blob_maker_(new TpmKeyBlobMaker(resource_manager_))
    , random_source_(new TpmRandomSource(resource_manager_.Esys()))
    , attestation_context_(new TpmAttestationRecordContext()) {
  key_factories_.emplace(
      KM_ALGORITHM_RSA, new keymaster::RsaKeyFactory(*key_blob_maker_, *this));
  key_factories_.emplace(
      KM_ALGORITHM_EC, new keymaster::EcKeyFactory(*key_blob_maker_, *this));
  key_factories_.emplace(
      KM_ALGORITHM_AES,
      new keymaster::AesKeyFactory(*key_blob_maker_, *random_source_));
  key_factories_.emplace(
      KM_ALGORITHM_TRIPLE_DES,
      new keymaster::TripleDesKeyFactory(*key_blob_maker_, *random_source_));
  key_factories_.emplace(
      KM_ALGORITHM_HMAC,
      new keymaster::HmacKeyFactory(*key_blob_maker_, *random_source_));
  for (const auto& it : key_factories_) {
    supported_algorithms_.push_back(it.first);
  }
}

keymaster_error_t TpmKeymasterContext::SetSystemVersion(
    uint32_t os_version, uint32_t os_patchlevel) {
  // TODO(b/155697375): Only accept new values of these from the bootloader
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
  key_blob_maker_->SetSystemVersion(os_version, os_patchlevel);
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

// Based on https://cs.android.com/android/platform/superproject/+/master:system/keymaster/key_blob_utils/software_keyblobs.cpp;l=44;drc=master

static bool UpgradeIntegerTag(
    keymaster_tag_t tag,
    uint32_t value,
    AuthorizationSet* set,
    bool* set_changed) {
  int index = set->find(tag);
  if (index == -1) {
    keymaster_key_param_t param;
    param.tag = tag;
    param.integer = value;
    set->push_back(param);
    *set_changed = true;
    return true;
  }

  if (set->params[index].integer > value) {
    return false;
  }

  if (set->params[index].integer != value) {
    set->params[index].integer = value;
    *set_changed = true;
  }
  return true;
}

// Based on https://cs.android.com/android/platform/superproject/+/master:system/keymaster/key_blob_utils/software_keyblobs.cpp;l=310;drc=master

keymaster_error_t TpmKeymasterContext::UpgradeKeyBlob(
    const KeymasterKeyBlob& blob_to_upgrade,
    const AuthorizationSet& upgrade_params,
    KeymasterKeyBlob* upgraded_key) const {
  keymaster::UniquePtr<keymaster::Key> key;
  auto error = ParseKeyBlob(blob_to_upgrade, upgrade_params, &key);
  if (error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to parse key blob";
    return error;
  }

  bool set_changed = false;

  if (os_version_ == 0) {
    // We need to allow "upgrading" OS version to zero, to support upgrading
    // from proper numbered releases to unnumbered development and preview
    // releases.

    int key_os_version_pos = key->hw_enforced().find(keymaster::TAG_OS_VERSION);
    if (key_os_version_pos != -1) {
      uint32_t key_os_version = key->hw_enforced()[key_os_version_pos].integer;
      if (key_os_version != 0) {
        key->hw_enforced()[key_os_version_pos].integer = os_version_;
        set_changed = true;
      }
    }
  }

  auto update_os = UpgradeIntegerTag(keymaster::TAG_OS_VERSION, os_version_,
                                     &key->hw_enforced(), &set_changed);

  auto update_patchlevel =
      UpgradeIntegerTag(keymaster::TAG_OS_PATCHLEVEL, os_patchlevel_,
                        &key->hw_enforced(), &set_changed);

  if (!update_os || !update_patchlevel) {
    LOG(ERROR) << "One of the version fields would have been a downgrade. "
               << "Not allowed.";
    return KM_ERROR_INVALID_ARGUMENT;
  }

  if (!set_changed) {
    // Don't need an upgrade.
    return KM_ERROR_OK;
  }

  return key_blob_maker_->UnvalidatedCreateKeyBlob(
      key->key_material(), key->hw_enforced(), key->sw_enforced(),
      upgraded_key);
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
    const uint8_t* buffer, size_t size) const {
  return random_source_->AddRngEntropy(buffer, size);
}

keymaster::KeymasterEnforcement* TpmKeymasterContext::enforcement_policy() {
  return &enforcement_;
}

// Based on https://cs.android.com/android/platform/superproject/+/master:system/keymaster/contexts/pure_soft_keymaster_context.cpp;l=261;drc=8367d5351c4d417a11f49b12394b63a413faa02d

keymaster::CertificateChain TpmKeymasterContext::GenerateAttestation(
    const keymaster::Key& key, const keymaster::AuthorizationSet& attest_params,
    keymaster::UniquePtr<keymaster::Key> /* attest_key */,
    const keymaster::KeymasterBlob& /* issuer_subject */,
    keymaster_error_t* error) const {
  LOG(INFO) << "TODO(b/155697200): Link attestation back to the TPM";
  keymaster_algorithm_t key_algorithm;
  if (!key.authorizations().GetTagValue(keymaster::TAG_ALGORITHM,
                                        &key_algorithm)) {
    *error = KM_ERROR_UNKNOWN_ERROR;
    return {};
  }

  if ((key_algorithm != KM_ALGORITHM_RSA && key_algorithm != KM_ALGORITHM_EC)) {
    *error = KM_ERROR_INCOMPATIBLE_ALGORITHM;
    return {};
  }

  // We have established that the given key has the correct algorithm, and
  // because this is the TpmKeymasterContext we can assume that the Key is an
  // AsymmetricKey. So we can downcast.
  const keymaster::AsymmetricKey& asymmetric_key =
      static_cast<const keymaster::AsymmetricKey&>(key);

  // DEVICE_UNIQUE_ATTESTATION is only allowed for strongbox devices. See
  // hardware/interfaces/security/keymint/aidl/android/hardware/security/keymint/Tag.aidl:845
  // at commit beefae4790ccd4f1ee75ea69603d4c9c2a45c0aa .
  // While the specification says to return ErrorCode::INVALID_ARGUMENT , the
  // relevant VTS test actually tests for ErrorCode::UNIMPLEMENTED . See
  // hardware/interfaces/keymaster/4.1/vts/functional/DeviceUniqueAttestationTest.cpp:203
  // at commit 36dcf1a404a9cf07ca5a2a6ad92371507194fe1b .
  if (attest_params.find(keymaster::TAG_DEVICE_UNIQUE_ATTESTATION) != -1) {
    *error = KM_ERROR_UNIMPLEMENTED;
    return {};
  }

  return keymaster::generate_attestation(asymmetric_key, attest_params,
                                         {} /* attest_key */,
                                         *attestation_context_, error);
}

keymaster::CertificateChain TpmKeymasterContext::GenerateSelfSignedCertificate(
    const keymaster::Key& key, const keymaster::AuthorizationSet& cert_params,
    bool fake_signature, keymaster_error_t* error) const {
  keymaster_algorithm_t key_algorithm;
  if (!key.authorizations().GetTagValue(keymaster::TAG_ALGORITHM, &key_algorithm)) {
      *error = KM_ERROR_UNKNOWN_ERROR;
      return {};
  }

  if ((key_algorithm != KM_ALGORITHM_RSA && key_algorithm != KM_ALGORITHM_EC)) {
      *error = KM_ERROR_INCOMPATIBLE_ALGORITHM;
      return {};
  }

  // We have established that the given key has the correct algorithm, and because this is the
  // SoftKeymasterContext we can assume that the Key is an AsymmetricKey. So we can downcast.
  const keymaster::AsymmetricKey& asymmetric_key =
      static_cast<const keymaster::AsymmetricKey&>(key);

  return generate_self_signed_cert(asymmetric_key, cert_params, fake_signature, error);
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
