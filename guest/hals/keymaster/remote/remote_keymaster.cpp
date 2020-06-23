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
#include <keymaster/android_keymaster_messages.h>
#include <keymaster/keymaster_configuration.h>

namespace keymaster {

RemoteKeymaster::RemoteKeymaster(cuttlefish::KeymasterChannel* channel)
    : channel_(channel) {}

RemoteKeymaster::~RemoteKeymaster() {
}

void RemoteKeymaster::ForwardCommand(AndroidKeymasterCommand command, const Serializable& req,
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

    ConfigureRequest req;
    req.os_version = GetOsVersion();
    req.os_patchlevel = GetOsPatchlevel();

    ConfigureResponse rsp;
    Configure(req, &rsp);

    if (rsp.error != KM_ERROR_OK) {
        LOG(ERROR) << "Failed to configure keymaster: " << rsp.error;
        return false;
    }

    return true;
}

void RemoteKeymaster::GetVersion(const GetVersionRequest& request, GetVersionResponse* response) {
    ForwardCommand(GET_VERSION, request, response);
}

void RemoteKeymaster::SupportedAlgorithms(const SupportedAlgorithmsRequest& request,
                                          SupportedAlgorithmsResponse* response) {
    ForwardCommand(GET_SUPPORTED_ALGORITHMS, request, response);
}

void RemoteKeymaster::SupportedBlockModes(const SupportedBlockModesRequest& request,
                                          SupportedBlockModesResponse* response) {
    ForwardCommand(GET_SUPPORTED_BLOCK_MODES, request, response);
}

void RemoteKeymaster::SupportedPaddingModes(const SupportedPaddingModesRequest& request,
                                            SupportedPaddingModesResponse* response) {
    ForwardCommand(GET_SUPPORTED_PADDING_MODES, request, response);
}

void RemoteKeymaster::SupportedDigests(const SupportedDigestsRequest& request,
                                       SupportedDigestsResponse* response) {
    ForwardCommand(GET_SUPPORTED_DIGESTS, request, response);
}

void RemoteKeymaster::SupportedImportFormats(const SupportedImportFormatsRequest& request,
                                             SupportedImportFormatsResponse* response) {
    ForwardCommand(GET_SUPPORTED_IMPORT_FORMATS, request, response);
}

void RemoteKeymaster::SupportedExportFormats(const SupportedExportFormatsRequest& request,
                                             SupportedExportFormatsResponse* response) {
    ForwardCommand(GET_SUPPORTED_EXPORT_FORMATS, request, response);
}

void RemoteKeymaster::AddRngEntropy(const AddEntropyRequest& request,
                                    AddEntropyResponse* response) {
    ForwardCommand(ADD_RNG_ENTROPY, request, response);
}

void RemoteKeymaster::Configure(const ConfigureRequest& request, ConfigureResponse* response) {
    ForwardCommand(CONFIGURE, request, response);
}

void RemoteKeymaster::GenerateKey(const GenerateKeyRequest& request,
                                  GenerateKeyResponse* response) {
    GenerateKeyRequest datedRequest(request.message_version);
    datedRequest.key_description = request.key_description;

    if (!request.key_description.Contains(TAG_CREATION_DATETIME)) {
        datedRequest.key_description.push_back(TAG_CREATION_DATETIME, java_time(time(NULL)));
    }

    ForwardCommand(GENERATE_KEY, datedRequest, response);
}

void RemoteKeymaster::GetKeyCharacteristics(const GetKeyCharacteristicsRequest& request,
                                            GetKeyCharacteristicsResponse* response) {
    ForwardCommand(GET_KEY_CHARACTERISTICS, request, response);
}

void RemoteKeymaster::ImportKey(const ImportKeyRequest& request, ImportKeyResponse* response) {
    ForwardCommand(IMPORT_KEY, request, response);
}

void RemoteKeymaster::ImportWrappedKey(const ImportWrappedKeyRequest& request,
                                       ImportWrappedKeyResponse* response) {
    ForwardCommand(IMPORT_WRAPPED_KEY, request, response);
}

void RemoteKeymaster::ExportKey(const ExportKeyRequest& request, ExportKeyResponse* response) {
    ForwardCommand(EXPORT_KEY, request, response);
}

void RemoteKeymaster::AttestKey(const AttestKeyRequest& request, AttestKeyResponse* response) {
    ForwardCommand(ATTEST_KEY, request, response);
}

void RemoteKeymaster::UpgradeKey(const UpgradeKeyRequest& request, UpgradeKeyResponse* response) {
    ForwardCommand(UPGRADE_KEY, request, response);
}

void RemoteKeymaster::DeleteKey(const DeleteKeyRequest& request, DeleteKeyResponse* response) {
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
    // Dummy empty buffer to allow ForwardCommand to have something to serialize
    Buffer request;
    GetHmacSharingParametersResponse response;
    ForwardCommand(GET_HMAC_SHARING_PARAMETERS, request, &response);
    return response;
}

ComputeSharedHmacResponse RemoteKeymaster::ComputeSharedHmac(
        const ComputeSharedHmacRequest& request) {
    ComputeSharedHmacResponse response;
    ForwardCommand(COMPUTE_SHARED_HMAC, request, &response);
    return response;
}

VerifyAuthorizationResponse RemoteKeymaster::VerifyAuthorization(
        const VerifyAuthorizationRequest& request) {
    VerifyAuthorizationResponse response;
    ForwardCommand(VERIFY_AUTHORIZATION, request, &response);
    return response;
}

DeviceLockedResponse RemoteKeymaster::DeviceLocked(
        const DeviceLockedRequest& request) {
    DeviceLockedResponse response;
    ForwardCommand(DEVICE_LOCKED, request, &response);
    return response;
}

EarlyBootEndedResponse RemoteKeymaster::EarlyBootEnded() {
    // Dummy empty buffer to allow ForwardCommand to have something to serialize
    Buffer request;
    EarlyBootEndedResponse response;
    ForwardCommand(EARLY_BOOT_ENDED, request, &response);
    return response;
}

}  // namespace keymaster
