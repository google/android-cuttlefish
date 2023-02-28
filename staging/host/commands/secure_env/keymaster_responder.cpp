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

#include "keymaster_responder.h"

#include <android-base/logging.h>
#include <keymaster/android_keymaster_messages.h>

namespace cuttlefish {

KeymasterResponder::KeymasterResponder(cuttlefish::KeymasterChannel& channel,
                                       keymaster::AndroidKeymaster& keymaster)
    : channel_(channel), keymaster_(keymaster) {}

bool KeymasterResponder::ProcessMessage() {
  auto request = channel_.ReceiveMessage();
  if (!request) {
    LOG(ERROR) << "Could not receive message";
    return false;
  }
  const uint8_t* buffer = request->payload;
  const uint8_t* end = request->payload + request->payload_size;
  switch (request->cmd) {
    using namespace keymaster;
#define HANDLE_MESSAGE(ENUM_NAME, METHOD_NAME)                       \
  case ENUM_NAME: {                                                  \
    METHOD_NAME##Request request(keymaster_.message_version());      \
    if (!request.Deserialize(&buffer, end)) {                        \
      LOG(ERROR) << "Failed to deserialize " #METHOD_NAME "Request"; \
      return false;                                                  \
    }                                                                \
    METHOD_NAME##Response response(keymaster_.message_version());    \
    keymaster_.METHOD_NAME(request, &response);                      \
    return channel_.SendResponse(ENUM_NAME, response);               \
  }
    HANDLE_MESSAGE(GENERATE_KEY, GenerateKey)
    HANDLE_MESSAGE(BEGIN_OPERATION, BeginOperation)
    HANDLE_MESSAGE(UPDATE_OPERATION, UpdateOperation)
    HANDLE_MESSAGE(FINISH_OPERATION, FinishOperation)
    HANDLE_MESSAGE(ABORT_OPERATION, AbortOperation)
    HANDLE_MESSAGE(IMPORT_KEY, ImportKey)
    HANDLE_MESSAGE(EXPORT_KEY, ExportKey)
    HANDLE_MESSAGE(GET_VERSION, GetVersion)
    HANDLE_MESSAGE(GET_SUPPORTED_ALGORITHMS, SupportedAlgorithms)
    HANDLE_MESSAGE(GET_SUPPORTED_BLOCK_MODES, SupportedBlockModes)
    HANDLE_MESSAGE(GET_SUPPORTED_PADDING_MODES, SupportedPaddingModes)
    HANDLE_MESSAGE(GET_SUPPORTED_DIGESTS, SupportedDigests)
    HANDLE_MESSAGE(GET_SUPPORTED_IMPORT_FORMATS, SupportedImportFormats)
    HANDLE_MESSAGE(GET_SUPPORTED_EXPORT_FORMATS, SupportedExportFormats)
    HANDLE_MESSAGE(GET_KEY_CHARACTERISTICS, GetKeyCharacteristics)
    HANDLE_MESSAGE(ATTEST_KEY, AttestKey)
    HANDLE_MESSAGE(UPGRADE_KEY, UpgradeKey)
    HANDLE_MESSAGE(CONFIGURE, Configure)
    HANDLE_MESSAGE(DELETE_KEY, DeleteKey)
    HANDLE_MESSAGE(DELETE_ALL_KEYS, DeleteAllKeys)
    HANDLE_MESSAGE(IMPORT_WRAPPED_KEY, ImportWrappedKey)
    HANDLE_MESSAGE(GENERATE_RKP_KEY, GenerateRkpKey)
    HANDLE_MESSAGE(GENERATE_CSR, GenerateCsr)
    HANDLE_MESSAGE(GENERATE_CSR_V2, GenerateCsrV2)
    HANDLE_MESSAGE(GENERATE_TIMESTAMP_TOKEN, GenerateTimestampToken)
#undef HANDLE_MESSAGE
#define HANDLE_MESSAGE_W_RETURN(ENUM_NAME, METHOD_NAME)              \
  case ENUM_NAME: {                                                  \
    METHOD_NAME##Request request(keymaster_.message_version());      \
    if (!request.Deserialize(&buffer, end)) {                        \
      LOG(ERROR) << "Failed to deserialize " #METHOD_NAME "Request"; \
      return false;                                                  \
    }                                                                \
    auto response = keymaster_.METHOD_NAME(request);                 \
    return channel_.SendResponse(ENUM_NAME, response);               \
  }
    HANDLE_MESSAGE_W_RETURN(COMPUTE_SHARED_HMAC, ComputeSharedHmac)
    HANDLE_MESSAGE_W_RETURN(VERIFY_AUTHORIZATION, VerifyAuthorization)
    HANDLE_MESSAGE_W_RETURN(DEVICE_LOCKED, DeviceLocked)
    HANDLE_MESSAGE_W_RETURN(GET_VERSION_2, GetVersion2)
    HANDLE_MESSAGE_W_RETURN(CONFIGURE_VENDOR_PATCHLEVEL,
                            ConfigureVendorPatchlevel)
    HANDLE_MESSAGE_W_RETURN(CONFIGURE_BOOT_PATCHLEVEL, ConfigureBootPatchlevel)
    HANDLE_MESSAGE_W_RETURN(CONFIGURE_VERIFIED_BOOT_INFO,
                            ConfigureVerifiedBootInfo)
    HANDLE_MESSAGE_W_RETURN(GET_ROOT_OF_TRUST, GetRootOfTrust)
    HANDLE_MESSAGE_W_RETURN(SET_ATTESTATION_IDS, SetAttestationIds)
    HANDLE_MESSAGE_W_RETURN(SET_ATTESTATION_IDS_KM3, SetAttestationIdsKM3)
#undef HANDLE_MESSAGE_W_RETURN
#define HANDLE_MESSAGE_W_RETURN_NO_ARG(ENUM_NAME, METHOD_NAME) \
  case ENUM_NAME: {                                            \
    auto response = keymaster_.METHOD_NAME();                  \
    return channel_.SendResponse(ENUM_NAME, response);         \
  }
    HANDLE_MESSAGE_W_RETURN_NO_ARG(GET_HMAC_SHARING_PARAMETERS,
                                   GetHmacSharingParameters)
    HANDLE_MESSAGE_W_RETURN_NO_ARG(EARLY_BOOT_ENDED, EarlyBootEnded)
    HANDLE_MESSAGE_W_RETURN_NO_ARG(GET_HW_INFO, GetHwInfo)
#undef HANDLE_MESSAGE_W_RETURN_NO_ARG
    case ADD_RNG_ENTROPY: {
      AddEntropyRequest request(keymaster_.message_version());
      if (!request.Deserialize(&buffer, end)) {
        LOG(ERROR) << "Failed to deserialize AddEntropyRequest";
        return false;
      }
      AddEntropyResponse response(keymaster_.message_version());
      ;
      keymaster_.AddRngEntropy(request, &response);
      return channel_.SendResponse(ADD_RNG_ENTROPY, response);
    }
    case DESTROY_ATTESTATION_IDS:
      // Cuttlefish doesn't support ID attestation.
    default:
      LOG(ERROR) << "Unknown request type: " << request->cmd;
      return false;
  }
}

}  // namespace cuttlefish
