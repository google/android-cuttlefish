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
#include <keymaster/operation.h>
#include <keymaster/wrapped_key.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_attestation_record.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "host/commands/secure_env/tpm_key_blob_maker.h"
#include "host/commands/secure_env/tpm_random_source.h"
#include "host/commands/secure_env/tpm_remote_provisioning_context.h"

namespace cuttlefish {

namespace {
using keymaster::AuthorizationSet;
using keymaster::KeyFactory;
using keymaster::KeymasterBlob;
using keymaster::KeymasterKeyBlob;
using keymaster::OperationFactory;

keymaster::AuthorizationSet GetHiddenTags(
    const AuthorizationSet& authorizations) {
  keymaster::AuthorizationSet output;
  keymaster_blob_t entry;
  if (authorizations.GetTagValue(keymaster::TAG_APPLICATION_ID, &entry)) {
    output.push_back(keymaster::TAG_APPLICATION_ID, entry.data,
                     entry.data_length);
  }
  if (authorizations.GetTagValue(keymaster::TAG_APPLICATION_DATA, &entry)) {
    output.push_back(keymaster::TAG_APPLICATION_DATA, entry.data,
                     entry.data_length);
  }
  return output;
}

keymaster_error_t TranslateAuthorizationSetError(AuthorizationSet::Error err) {
  switch (err) {
    case AuthorizationSet::OK:
      return KM_ERROR_OK;
    case AuthorizationSet::ALLOCATION_FAILURE:
      return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    case AuthorizationSet::MALFORMED_DATA:
      return KM_ERROR_UNKNOWN_ERROR;
  }
  return KM_ERROR_UNKNOWN_ERROR;
}

}  // namespace

TpmKeymasterContext::TpmKeymasterContext(
    TpmResourceManager& resource_manager,
    keymaster::KeymasterEnforcement& enforcement)
    : resource_manager_(resource_manager),
      enforcement_(enforcement),
      key_blob_maker_(new TpmKeyBlobMaker(resource_manager_)),
      random_source_(new TpmRandomSource(resource_manager_.Esys())),
      attestation_context_(new TpmAttestationRecordContext),
      remote_provisioning_context_(
          new TpmRemoteProvisioningContext(resource_manager_)) {
  key_factories_.emplace(KM_ALGORITHM_RSA,
                         new keymaster::RsaKeyFactory(*key_blob_maker_, *this));
  key_factories_.emplace(KM_ALGORITHM_EC,
                         new keymaster::EcKeyFactory(*key_blob_maker_, *this));
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
  remote_provisioning_context_->SetSystemVersion(os_version_, os_patchlevel_);
  return KM_ERROR_OK;
}

void TpmKeymasterContext::GetSystemVersion(uint32_t* os_version,
                                           uint32_t* os_patchlevel) const {
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

OperationFactory* TpmKeymasterContext::GetOperationFactory(
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

// Based on
// https://cs.android.com/android/platform/superproject/+/master:system/keymaster/key_blob_utils/software_keyblobs.cpp;l=44;drc=master

static bool UpgradeIntegerTag(keymaster_tag_t tag, uint32_t value,
                              AuthorizationSet* set, bool* set_changed) {
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

// Based on
// https://cs.android.com/android/platform/superproject/+/master:system/keymaster/key_blob_utils/software_keyblobs.cpp;l=310;drc=master

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
      GetHiddenTags(upgrade_params), upgraded_key);
}

keymaster_error_t TpmKeymasterContext::ParseKeyBlob(
    const KeymasterKeyBlob& blob, const AuthorizationSet& additional_params,
    keymaster::UniquePtr<keymaster::Key>* key) const {
  keymaster::AuthorizationSet hw_enforced;
  keymaster::AuthorizationSet sw_enforced;
  keymaster::KeymasterKeyBlob key_material;

  keymaster::AuthorizationSet hidden = GetHiddenTags(additional_params);

  auto rc = key_blob_maker_->UnwrapKeyBlob(blob, &hw_enforced, &sw_enforced,
                                           hidden, &key_material);
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
  rc = factory->LoadKey(std::move(key_material), additional_params,
                        std::move(hw_enforced), std::move(sw_enforced), key);
  if (rc != KM_ERROR_OK) {
    LOG(ERROR) << "Unable to load unwrapped key: " << rc;
  }
  return rc;
}

keymaster_error_t TpmKeymasterContext::AddRngEntropy(const uint8_t* buffer,
                                                     size_t size) const {
  return random_source_->AddRngEntropy(buffer, size);
}

keymaster::KeymasterEnforcement* TpmKeymasterContext::enforcement_policy() {
  return &enforcement_;
}

// Based on
// https://cs.android.com/android/platform/superproject/+/master:system/keymaster/contexts/pure_soft_keymaster_context.cpp;l=261;drc=8367d5351c4d417a11f49b12394b63a413faa02d

keymaster::CertificateChain TpmKeymasterContext::GenerateAttestation(
    const keymaster::Key& key, const keymaster::AuthorizationSet& attest_params,
    keymaster::UniquePtr<keymaster::Key> attest_key,
    const keymaster::KeymasterBlob& issuer_subject,
    keymaster_error_t* error) const {
  LOG(INFO) << "TODO(b/155697200): Link attestation back to the TPM";
  keymaster_algorithm_t key_algorithm;
  if (!key.authorizations().GetTagValue(keymaster::TAG_ALGORITHM,
                                        &key_algorithm)) {
    LOG(ERROR) << "Cannot find key algorithm (TAG_ALGORITHM)";
    *error = KM_ERROR_UNKNOWN_ERROR;
    return {};
  }

  if ((key_algorithm != KM_ALGORITHM_RSA && key_algorithm != KM_ALGORITHM_EC)) {
    LOG(ERROR) << "Invalid algorithm: " << key_algorithm;
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
    LOG(ERROR) << "TAG_DEVICE_UNIQUE_ATTESTATION not supported";
    *error = KM_ERROR_UNIMPLEMENTED;
    return {};
  }

  keymaster::AttestKeyInfo attest_key_info(attest_key, &issuer_subject, error);
  if (*error != KM_ERROR_OK) {
    LOG(ERROR)
        << "Error creating attestation key info from given key and subject";
    return {};
  }

  return keymaster::generate_attestation(asymmetric_key, attest_params,
                                         std::move(attest_key_info),
                                         *attestation_context_, error);
}

keymaster::CertificateChain TpmKeymasterContext::GenerateSelfSignedCertificate(
    const keymaster::Key& key, const keymaster::AuthorizationSet& cert_params,
    bool fake_signature, keymaster_error_t* error) const {
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
  // because this is the SoftKeymasterContext we can assume that the Key is an
  // AsymmetricKey. So we can downcast.
  const keymaster::AsymmetricKey& asymmetric_key =
      static_cast<const keymaster::AsymmetricKey&>(key);

  return generate_self_signed_cert(asymmetric_key, cert_params, fake_signature,
                                   error);
}

keymaster_error_t TpmKeymasterContext::UnwrapKey(
    const KeymasterKeyBlob& wrapped_key_blob,
    const KeymasterKeyBlob& wrapping_key_blob,
    const AuthorizationSet& wrapping_key_params,
    const KeymasterKeyBlob& masking_key, AuthorizationSet* wrapped_key_params,
    keymaster_key_format_t* wrapped_key_format,
    KeymasterKeyBlob* wrapped_key_material) const {
  keymaster_error_t error = KM_ERROR_OK;

  if (wrapped_key_material == nullptr) {
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }

  // Parse wrapping key.
  keymaster::UniquePtr<keymaster::Key> wrapping_key;
  error = ParseKeyBlob(wrapping_key_blob, wrapping_key_params, &wrapping_key);
  if (error != KM_ERROR_OK) {
    return error;
  }

  keymaster::AuthProxy wrapping_key_auths(wrapping_key->hw_enforced(),
                                          wrapping_key->sw_enforced());

  // Check Wrapping Key Purpose
  if (!wrapping_key_auths.Contains(keymaster::TAG_PURPOSE, KM_PURPOSE_WRAP)) {
    LOG(ERROR) << "Wrapping key did not have KM_PURPOSE_WRAP";
    return KM_ERROR_INCOMPATIBLE_PURPOSE;
  }

  // Check Padding mode is RSA_OAEP and digest is SHA_2_256 (spec
  // mandated)
  if (!wrapping_key_auths.Contains(keymaster::TAG_DIGEST,
                                   KM_DIGEST_SHA_2_256)) {
    LOG(ERROR) << "Wrapping key lacks authorization for SHA2-256";
    return KM_ERROR_INCOMPATIBLE_DIGEST;
  }
  if (!wrapping_key_auths.Contains(keymaster::TAG_PADDING, KM_PAD_RSA_OAEP)) {
    LOG(ERROR) << "Wrapping key lacks authorization for padding OAEP";
    return KM_ERROR_INCOMPATIBLE_PADDING_MODE;
  }

  // Check that that was also the padding mode and digest specified
  if (!wrapping_key_params.Contains(keymaster::TAG_DIGEST,
                                    KM_DIGEST_SHA_2_256)) {
    LOG(ERROR) << "Wrapping key must use SHA2-256";
    return KM_ERROR_INCOMPATIBLE_DIGEST;
  }
  if (!wrapping_key_params.Contains(keymaster::TAG_PADDING, KM_PAD_RSA_OAEP)) {
    LOG(ERROR) << "Wrapping key must use OAEP padding";
    return KM_ERROR_INCOMPATIBLE_PADDING_MODE;
  }

  // Parse wrapped key data.
  KeymasterBlob iv;
  KeymasterKeyBlob transit_key;
  KeymasterKeyBlob secure_key;
  KeymasterBlob tag;
  KeymasterBlob wrapped_key_description;
  error = parse_wrapped_key(wrapped_key_blob, &iv, &transit_key, &secure_key,
                            &tag, wrapped_key_params, wrapped_key_format,
                            &wrapped_key_description);
  if (error != KM_ERROR_OK) {
    return error;
  }

  // Decrypt encryptedTransportKey (transit_key) with wrapping_key
  keymaster::OperationFactory* operation_factory =
      wrapping_key->key_factory()->GetOperationFactory(KM_PURPOSE_DECRYPT);
  if (operation_factory == NULL) {
    return KM_ERROR_UNKNOWN_ERROR;
  }

  AuthorizationSet out_params;
  keymaster::OperationPtr operation(operation_factory->CreateOperation(
      std::move(*wrapping_key), wrapping_key_params, &error));
  if ((operation.get() == nullptr) || (error != KM_ERROR_OK)) {
    return error;
  }

  error = operation->Begin(wrapping_key_params, &out_params);
  if (error != KM_ERROR_OK) {
    return error;
  }

  keymaster::Buffer input;
  if (!input.Reinitialize(transit_key.key_material,
                          transit_key.key_material_size)) {
    return KM_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  keymaster::Buffer output;
  error = operation->Finish(wrapping_key_params, input,
                            keymaster::Buffer() /* signature */, &out_params,
                            &output);
  if (error != KM_ERROR_OK) {
    return error;
  }

  // decrypt the encrypted key material with the transit key
  KeymasterKeyBlob transport_key = {output.peek_read(),
                                    output.available_read()};

  // XOR the transit key with the masking key
  if (transport_key.key_material_size != masking_key.key_material_size) {
    return KM_ERROR_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < transport_key.key_material_size; i++) {
    transport_key.writable_data()[i] ^= masking_key.key_material[i];
  }

  auto transport_key_authorizations =
      keymaster::AuthorizationSetBuilder()
          .AesEncryptionKey(256)
          .Padding(KM_PAD_NONE)
          .Authorization(keymaster::TAG_BLOCK_MODE, KM_MODE_GCM)
          .Authorization(keymaster::TAG_NONCE, iv)
          .Authorization(keymaster::TAG_MIN_MAC_LENGTH, 128)
          .build();
  if (transport_key_authorizations.is_valid() != AuthorizationSet::Error::OK) {
    return TranslateAuthorizationSetError(
        transport_key_authorizations.is_valid());
  }

  auto gcm_params = keymaster::AuthorizationSetBuilder()
                        .Padding(KM_PAD_NONE)
                        .Authorization(keymaster::TAG_BLOCK_MODE, KM_MODE_GCM)
                        .Authorization(keymaster::TAG_NONCE, iv)
                        .Authorization(keymaster::TAG_MAC_LENGTH, 128)
                        .build();
  if (gcm_params.is_valid() != AuthorizationSet::Error::OK) {
    return TranslateAuthorizationSetError(
        transport_key_authorizations.is_valid());
  }

  auto aes_factory = GetKeyFactory(KM_ALGORITHM_AES);
  if (!aes_factory) {
    return KM_ERROR_UNKNOWN_ERROR;
  }

  keymaster::UniquePtr<keymaster::Key> aes_transport_key;
  error = aes_factory->LoadKey(std::move(transport_key), gcm_params,
                               std::move(transport_key_authorizations),
                               AuthorizationSet(), &aes_transport_key);
  if (error != KM_ERROR_OK) {
    return error;
  }

  keymaster::OperationFactory* aes_operation_factory =
      GetOperationFactory(KM_ALGORITHM_AES, KM_PURPOSE_DECRYPT);
  if (!aes_operation_factory) {
    return KM_ERROR_UNKNOWN_ERROR;
  }

  keymaster::OperationPtr aes_operation(aes_operation_factory->CreateOperation(
      std::move(*aes_transport_key), gcm_params, &error));
  if (!aes_operation.get()) {
    return error;
  }

  error = aes_operation->Begin(gcm_params, &out_params);
  if (error != KM_ERROR_OK) {
    return error;
  }

  size_t total_key_size = secure_key.key_material_size + tag.data_length;
  keymaster::Buffer plaintext_key;
  if (!plaintext_key.Reinitialize(total_key_size)) {
    return KM_ERROR_MEMORY_ALLOCATION_FAILED;
  }
  keymaster::Buffer encrypted_key;
  if (!encrypted_key.Reinitialize(total_key_size)) {
    return KM_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  // Concatenate key data and authentication tag.
  if (!encrypted_key.write(secure_key.key_material,
                           secure_key.key_material_size)) {
    return KM_ERROR_UNKNOWN_ERROR;
  }
  if (!encrypted_key.write(tag.data, tag.data_length)) {
    return KM_ERROR_UNKNOWN_ERROR;
  }

  auto update_params = keymaster::AuthorizationSetBuilder()
                           .Authorization(keymaster::TAG_ASSOCIATED_DATA,
                                          wrapped_key_description.data,
                                          wrapped_key_description.data_length)
                           .build();
  if (update_params.is_valid() != AuthorizationSet::Error::OK) {
    return TranslateAuthorizationSetError(update_params.is_valid());
  }

  size_t update_consumed = 0;
  AuthorizationSet update_outparams;
  error = aes_operation->Update(update_params, encrypted_key, &update_outparams,
                                &plaintext_key, &update_consumed);
  if (error != KM_ERROR_OK) {
    return error;
  }

  AuthorizationSet finish_params;
  AuthorizationSet finish_out_params;
  keymaster::Buffer finish_input;
  error = aes_operation->Finish(finish_params, finish_input,
                                keymaster::Buffer() /* signature */,
                                &finish_out_params, &plaintext_key);
  if (error != KM_ERROR_OK) {
    return error;
  }

  *wrapped_key_material = {plaintext_key.peek_read(),
                           plaintext_key.available_read()};
  if (!wrapped_key_material->key_material && plaintext_key.peek_read()) {
    return KM_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  return error;
}

keymaster_error_t TpmKeymasterContext::CheckConfirmationToken(
    const std::uint8_t* input_data, size_t input_data_size,
    const uint8_t confirmation_token[keymaster::kConfirmationTokenSize]) const {
  auto hmac = TpmHmacWithContext(resource_manager_, "confirmation_token",
                                 input_data, input_data_size);
  if (!hmac) {
    LOG(ERROR) << "Could not calculate confirmation token hmac";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  CHECK(hmac->size == keymaster::kConfirmationTokenSize)
      << "Hmac size for confirmation UI must be "
      << keymaster::kConfirmationTokenSize;

  std::vector<std::uint8_t> hmac_buffer(hmac->buffer,
                                        hmac->buffer + hmac->size);

  const auto is_equal =
      std::equal(hmac_buffer.cbegin(), hmac_buffer.cend(), confirmation_token);
  return is_equal ? KM_ERROR_OK : KM_ERROR_NO_USER_CONFIRMATION;
}

keymaster::RemoteProvisioningContext*
TpmKeymasterContext::GetRemoteProvisioningContext() const {
  return remote_provisioning_context_.get();
}

std::string ToHexString(const std::vector<uint8_t>& binary) {
  std::string hex;
  hex.reserve(binary.size() * 2);
  for (uint8_t byte : binary) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02x", byte);
    hex.append(buf);
  }
  return hex;
}

keymaster_error_t TpmKeymasterContext::SetVerifiedBootInfo(
    std::string_view verified_boot_state, std::string_view bootloader_state,
    const std::vector<uint8_t>& vbmeta_digest) {
  if (verified_boot_state_ && verified_boot_state != *verified_boot_state_) {
    LOG(ERROR) << "Invalid set verified boot state attempt. "
               << "Old verified boot state: \"" << *verified_boot_state_
               << "\","
               << "new verified boot state: \"" << verified_boot_state << "\"";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  if (bootloader_state_ && bootloader_state != *bootloader_state_) {
    LOG(ERROR) << "Invalid set bootloader state attempt. "
               << "Old bootloader state: \"" << *bootloader_state_ << "\","
               << "new bootloader state: \"" << bootloader_state << "\"";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  if (vbmeta_digest_ && vbmeta_digest != *vbmeta_digest_) {
    LOG(ERROR) << "Invalid set vbmeta digest state attempt. "
               << "Old vbmeta digest state: \"" << ToHexString(*vbmeta_digest_)
               << "\","
               << "new vbmeta digest state: \"" << ToHexString(vbmeta_digest)
               << "\"";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  verified_boot_state_ = verified_boot_state;
  bootloader_state_ = bootloader_state;
  vbmeta_digest_ = vbmeta_digest;
  attestation_context_->SetVerifiedBootInfo(verified_boot_state,
                                            bootloader_state, vbmeta_digest);
  remote_provisioning_context_->SetVerifiedBootInfo(
      verified_boot_state, bootloader_state, vbmeta_digest);
  return KM_ERROR_OK;
}

keymaster_error_t TpmKeymasterContext::SetVendorPatchlevel(
    uint32_t vendor_patchlevel) {
  if (vendor_patchlevel_.has_value() &&
      vendor_patchlevel != vendor_patchlevel_.value()) {
    // Can't set patchlevel to a different value.
    LOG(ERROR) << "Invalid set vendor patchlevel attempt. Old patchlevel: \""
               << *vendor_patchlevel_ << "\", new patchlevel: \""
               << vendor_patchlevel << "\"";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  vendor_patchlevel_ = vendor_patchlevel;
  remote_provisioning_context_->SetVendorPatchlevel(vendor_patchlevel);
  return key_blob_maker_->SetVendorPatchlevel(*vendor_patchlevel_);
}

keymaster_error_t TpmKeymasterContext::SetBootPatchlevel(
    uint32_t boot_patchlevel) {
  if (boot_patchlevel_.has_value() &&
      boot_patchlevel != boot_patchlevel_.value()) {
    // Can't set patchlevel to a different value.
    LOG(ERROR) << "Invalid set boot patchlevel attempt. Old patchlevel: \""
               << *boot_patchlevel_ << "\", new patchlevel: \""
               << boot_patchlevel << "\"";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  boot_patchlevel_ = boot_patchlevel;
  remote_provisioning_context_->SetBootPatchlevel(boot_patchlevel);
  return key_blob_maker_->SetBootPatchlevel(*boot_patchlevel_);
}

std::optional<uint32_t> TpmKeymasterContext::GetVendorPatchlevel() const {
  return vendor_patchlevel_;
}

std::optional<uint32_t> TpmKeymasterContext::GetBootPatchlevel() const {
  return boot_patchlevel_;
}

}  // namespace cuttlefish
