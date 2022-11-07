/*
 * Copyright 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "remote_keymaster.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <keymaster/android_keymaster_messages.h>
#include <keymaster/keymaster_configuration.h>

namespace keymaster {

RemoteKeymaster::RemoteKeymaster(cuttlefish::KeymasterChannel* channel,
                                 int32_t message_version)
    : channel_(channel), message_version_(message_version) {}

RemoteKeymaster::~RemoteKeymaster() {}

void RemoteKeymaster::ForwardCommand(AndroidKeymasterCommand command,
                                     const Serializable& req,
                                     KeymasterResponse* rsp) {
  if (!channel_->SendRequest(command, req)) {
    LOG(ERROR) << "Failed to send keymaster message: " << command;
    rsp->error = KM_ERROR_UNKNOWN_ERROR;
    return;
  }
  auto response = channel_->ReceiveMessage();
  if (!response) {
    LOG(ERROR) << "Failed to receive keymaster response: " << command;
    rsp->error = KM_ERROR_UNKNOWN_ERROR;
    return;
  }
  const uint8_t* buffer = response->payload;
  const uint8_t* buffer_end = response->payload + response->payload_size;
  if (!rsp->Deserialize(&buffer, buffer_end)) {
    LOG(ERROR) << "Failed to deserialize keymaster response: " << command;
    rsp->error = KM_ERROR_UNKNOWN_ERROR;
    return;
  }
}

bool RemoteKeymaster::Initialize() {
  // We don't need to bother with GetVersion, because CF HAL and remote sides
  // are always compiled together, so will never disagree about message
  // versions.
  ConfigureRequest req(message_version());
  req.os_version = GetOsVersion();
  req.os_patchlevel = GetOsPatchlevel();

  ConfigureResponse rsp(message_version());
  Configure(req, &rsp);

  if (rsp.error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to configure keymaster: " << rsp.error;
    return false;
  }

  // Set the vendor patchlevel to value retrieved from system property (which
  // requires SELinux permission).
  ConfigureVendorPatchlevelRequest vendor_req(message_version());
  vendor_req.vendor_patchlevel = GetVendorPatchlevel();
  ConfigureVendorPatchlevelResponse vendor_rsp =
      ConfigureVendorPatchlevel(vendor_req);
  if (vendor_rsp.error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to configure keymaster vendor patchlevel: "
               << vendor_rsp.error;
    return false;
  }

  // Set the boot patchlevel to value retrieved from system property (which
  // requires SELinux permission).
  ConfigureBootPatchlevelRequest boot_req(message_version());
  static constexpr char boot_prop_name[] = "ro.vendor.boot_security_patch";
  auto boot_prop_value = android::base::GetProperty(boot_prop_name, "");
  boot_prop_value = android::base::StringReplace(boot_prop_value.data(), "-",
                                                 "", /* all */ true);
  boot_req.boot_patchlevel = std::stoi(boot_prop_value);
  ConfigureBootPatchlevelResponse boot_rsp = ConfigureBootPatchlevel(boot_req);
  if (boot_rsp.error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to configure keymaster boot patchlevel: "
               << boot_rsp.error;
    return false;
  }

  // Pass verified boot information to the remote KM implementation
  auto vbmeta_digest = GetVbmetaDigest();
  if (vbmeta_digest) {
    ConfigureVerifiedBootInfoRequest request(
        message_version(), GetVerifiedBootState(), GetBootloaderState(),
        *vbmeta_digest);
    ConfigureVerifiedBootInfoResponse response =
        ConfigureVerifiedBootInfo(request);
    if (response.error != KM_ERROR_OK) {
      LOG(ERROR) << "Failed to configure keymaster verified boot info: "
                 << response.error;
      return false;
    }
  }

  // Pass attestation IDs to the remote KM implementation.
  // Skip IMEI and MEID as those aren't present on emulators.
  SetAttestationIdsRequest request(message_version());

  static constexpr char brand_prop_name[] = "ro.product.brand";
  static constexpr char device_prop_name[] = "ro.product.device";
  static constexpr char product_prop_name[] = "ro.product.name";
  static constexpr char serial_prop_name[] = "ro.serialno";
  static constexpr char manufacturer_prop_name[] = "ro.product.manufacturer";
  static constexpr char model_prop_name[] = "ro.product.model";

  std::string brand_prop_value =
      android::base::GetProperty(brand_prop_name, "");
  std::string device_prop_value =
      android::base::GetProperty(device_prop_name, "");
  std::string product_prop_value =
      android::base::GetProperty(product_prop_name, "");
  std::string serial_prop_value =
      android::base::GetProperty(serial_prop_name, "");
  std::string manufacturer_prop_value =
      android::base::GetProperty(manufacturer_prop_name, "");
  std::string model_prop_value =
      android::base::GetProperty(model_prop_name, "");

  request.brand.Reinitialize(brand_prop_value.data(), brand_prop_value.size());
  request.device.Reinitialize(device_prop_value.data(),
                              device_prop_value.size());
  request.product.Reinitialize(product_prop_value.data(),
                               product_prop_value.size());
  request.serial.Reinitialize(serial_prop_value.data(),
                              serial_prop_value.size());
  request.manufacturer.Reinitialize(manufacturer_prop_value.data(),
                                    manufacturer_prop_value.size());
  request.model.Reinitialize(model_prop_value.data(), model_prop_value.size());

  SetAttestationIdsResponse response = SetAttestationIds(request);
  if (response.error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to configure keymaster attestation IDs: "
               << response.error;
    return false;
  }

  return true;
}

void RemoteKeymaster::GetVersion(const GetVersionRequest& request,
                                 GetVersionResponse* response) {
  ForwardCommand(GET_VERSION, request, response);
}

void RemoteKeymaster::SupportedAlgorithms(
    const SupportedAlgorithmsRequest& request,
    SupportedAlgorithmsResponse* response) {
  ForwardCommand(GET_SUPPORTED_ALGORITHMS, request, response);
}

void RemoteKeymaster::SupportedBlockModes(
    const SupportedBlockModesRequest& request,
    SupportedBlockModesResponse* response) {
  ForwardCommand(GET_SUPPORTED_BLOCK_MODES, request, response);
}

void RemoteKeymaster::SupportedPaddingModes(
    const SupportedPaddingModesRequest& request,
    SupportedPaddingModesResponse* response) {
  ForwardCommand(GET_SUPPORTED_PADDING_MODES, request, response);
}

void RemoteKeymaster::SupportedDigests(const SupportedDigestsRequest& request,
                                       SupportedDigestsResponse* response) {
  ForwardCommand(GET_SUPPORTED_DIGESTS, request, response);
}

void RemoteKeymaster::SupportedImportFormats(
    const SupportedImportFormatsRequest& request,
    SupportedImportFormatsResponse* response) {
  ForwardCommand(GET_SUPPORTED_IMPORT_FORMATS, request, response);
}

void RemoteKeymaster::SupportedExportFormats(
    const SupportedExportFormatsRequest& request,
    SupportedExportFormatsResponse* response) {
  ForwardCommand(GET_SUPPORTED_EXPORT_FORMATS, request, response);
}

void RemoteKeymaster::AddRngEntropy(const AddEntropyRequest& request,
                                    AddEntropyResponse* response) {
  ForwardCommand(ADD_RNG_ENTROPY, request, response);
}

void RemoteKeymaster::Configure(const ConfigureRequest& request,
                                ConfigureResponse* response) {
  ForwardCommand(CONFIGURE, request, response);
}

void RemoteKeymaster::GenerateKey(const GenerateKeyRequest& request,
                                  GenerateKeyResponse* response) {
  if (message_version_ < MessageVersion(KmVersion::KEYMINT_1) &&
      !request.key_description.Contains(TAG_CREATION_DATETIME)) {
    GenerateKeyRequest datedRequest(request.message_version);
    datedRequest.key_description.push_back(TAG_CREATION_DATETIME,
                                           java_time(time(NULL)));
    ForwardCommand(GENERATE_KEY, datedRequest, response);
  } else {
    ForwardCommand(GENERATE_KEY, request, response);
  }
}

void RemoteKeymaster::GenerateRkpKey(const GenerateRkpKeyRequest& request,
                                     GenerateRkpKeyResponse* response) {
  ForwardCommand(GENERATE_RKP_KEY, request, response);
}

void RemoteKeymaster::GenerateCsr(const GenerateCsrRequest& request,
                                  GenerateCsrResponse* response) {
  ForwardCommand(GENERATE_CSR, request, response);
}

void RemoteKeymaster::GenerateCsrV2(const GenerateCsrV2Request& request,
                                    GenerateCsrV2Response* response) {
  ForwardCommand(GENERATE_CSR_V2, request, response);
}

void RemoteKeymaster::GetKeyCharacteristics(
    const GetKeyCharacteristicsRequest& request,
    GetKeyCharacteristicsResponse* response) {
  ForwardCommand(GET_KEY_CHARACTERISTICS, request, response);
}

void RemoteKeymaster::ImportKey(const ImportKeyRequest& request,
                                ImportKeyResponse* response) {
  ForwardCommand(IMPORT_KEY, request, response);
}

void RemoteKeymaster::ImportWrappedKey(const ImportWrappedKeyRequest& request,
                                       ImportWrappedKeyResponse* response) {
  ForwardCommand(IMPORT_WRAPPED_KEY, request, response);
}

void RemoteKeymaster::ExportKey(const ExportKeyRequest& request,
                                ExportKeyResponse* response) {
  ForwardCommand(EXPORT_KEY, request, response);
}

void RemoteKeymaster::AttestKey(const AttestKeyRequest& request,
                                AttestKeyResponse* response) {
  ForwardCommand(ATTEST_KEY, request, response);
}

void RemoteKeymaster::UpgradeKey(const UpgradeKeyRequest& request,
                                 UpgradeKeyResponse* response) {
  ForwardCommand(UPGRADE_KEY, request, response);
}

void RemoteKeymaster::DeleteKey(const DeleteKeyRequest& request,
                                DeleteKeyResponse* response) {
  ForwardCommand(DELETE_KEY, request, response);
}

void RemoteKeymaster::DeleteAllKeys(const DeleteAllKeysRequest& request,
                                    DeleteAllKeysResponse* response) {
  ForwardCommand(DELETE_ALL_KEYS, request, response);
}

void RemoteKeymaster::BeginOperation(const BeginOperationRequest& request,
                                     BeginOperationResponse* response) {
  ForwardCommand(BEGIN_OPERATION, request, response);
}

void RemoteKeymaster::UpdateOperation(const UpdateOperationRequest& request,
                                      UpdateOperationResponse* response) {
  ForwardCommand(UPDATE_OPERATION, request, response);
}

void RemoteKeymaster::FinishOperation(const FinishOperationRequest& request,
                                      FinishOperationResponse* response) {
  ForwardCommand(FINISH_OPERATION, request, response);
}

void RemoteKeymaster::AbortOperation(const AbortOperationRequest& request,
                                     AbortOperationResponse* response) {
  ForwardCommand(ABORT_OPERATION, request, response);
}

GetHmacSharingParametersResponse RemoteKeymaster::GetHmacSharingParameters() {
  // Unused empty buffer to allow ForwardCommand to have something to serialize
  Buffer request;
  GetHmacSharingParametersResponse response(message_version());
  ForwardCommand(GET_HMAC_SHARING_PARAMETERS, request, &response);
  return response;
}

ComputeSharedHmacResponse RemoteKeymaster::ComputeSharedHmac(
    const ComputeSharedHmacRequest& request) {
  ComputeSharedHmacResponse response(message_version());
  ForwardCommand(COMPUTE_SHARED_HMAC, request, &response);
  return response;
}

VerifyAuthorizationResponse RemoteKeymaster::VerifyAuthorization(
    const VerifyAuthorizationRequest& request) {
  VerifyAuthorizationResponse response(message_version());
  ForwardCommand(VERIFY_AUTHORIZATION, request, &response);
  return response;
}

DeviceLockedResponse RemoteKeymaster::DeviceLocked(
    const DeviceLockedRequest& request) {
  DeviceLockedResponse response(message_version());
  ForwardCommand(DEVICE_LOCKED, request, &response);
  return response;
}

EarlyBootEndedResponse RemoteKeymaster::EarlyBootEnded() {
  // Unused empty buffer to allow ForwardCommand to have something to serialize
  Buffer request;
  EarlyBootEndedResponse response(message_version());
  ForwardCommand(EARLY_BOOT_ENDED, request, &response);
  return response;
}

void RemoteKeymaster::GenerateTimestampToken(
    GenerateTimestampTokenRequest& request,
    GenerateTimestampTokenResponse* response) {
  ForwardCommand(GENERATE_TIMESTAMP_TOKEN, request, response);
}

ConfigureVendorPatchlevelResponse RemoteKeymaster::ConfigureVendorPatchlevel(
    const ConfigureVendorPatchlevelRequest& request) {
  ConfigureVendorPatchlevelResponse response(message_version());
  ForwardCommand(CONFIGURE_VENDOR_PATCHLEVEL, request, &response);
  return response;
}

ConfigureBootPatchlevelResponse RemoteKeymaster::ConfigureBootPatchlevel(
    const ConfigureBootPatchlevelRequest& request) {
  ConfigureBootPatchlevelResponse response(message_version());
  ForwardCommand(CONFIGURE_BOOT_PATCHLEVEL, request, &response);
  return response;
}

ConfigureVerifiedBootInfoResponse RemoteKeymaster::ConfigureVerifiedBootInfo(
    const ConfigureVerifiedBootInfoRequest& request) {
  ConfigureVerifiedBootInfoResponse response(message_version());
  ForwardCommand(CONFIGURE_VERIFIED_BOOT_INFO, request, &response);
  return response;
}

GetRootOfTrustResponse RemoteKeymaster::GetRootOfTrust(
    const GetRootOfTrustRequest& request) {
  GetRootOfTrustResponse response(message_version());
  ForwardCommand(GET_ROOT_OF_TRUST, request, &response);
  return response;
}

GetHwInfoResponse RemoteKeymaster::GetHwInfo() {
  // Unused empty buffer to allow ForwardCommand to have something to serialize
  Buffer request;
  GetHwInfoResponse response(message_version());
  ForwardCommand(GET_HW_INFO, request, &response);
  return response;
}

SetAttestationIdsResponse RemoteKeymaster::SetAttestationIds(
    const SetAttestationIdsRequest& request) {
  SetAttestationIdsResponse response(message_version());
  ForwardCommand(SET_ATTESTATION_IDS, request, &response);
  return response;
}

}  // namespace keymaster
