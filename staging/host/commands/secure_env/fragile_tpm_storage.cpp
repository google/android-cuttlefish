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

#include "host/commands/secure_env/fragile_tpm_storage.h"

#include <fstream>

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/json_serializable.h"
#include "host/commands/secure_env/tpm_random_source.h"

static constexpr char kEntries[] = "entries";
static constexpr char kKey[] = "key";
static constexpr char kHandle[] = "handle";

FragileTpmStorage::FragileTpmStorage(
    TpmResourceManager& resource_manager, const std::string& index_file)
    : resource_manager_(resource_manager), index_file_(index_file) {
  index_ = ReadProtectedJsonFromFile(resource_manager_, index_file);
  if (!index_.isMember(kEntries)
      || index_[kEntries].type() != Json::arrayValue) {
    if (index_.empty()) {
      LOG(DEBUG) << "Initializing secure index file";
    } else {
      LOG(WARNING) << "Index file missing entries, likely corrupted.";
    }
    index_[kEntries] = Json::Value(Json::arrayValue);
  } else {
    LOG(DEBUG) << "Restoring index from file";
  }
}

TPM2_HANDLE FragileTpmStorage::GenerateRandomHandle() {
  TpmRandomSource random_source{resource_manager_.Esys()};
  TPM2_HANDLE handle = 0;
  random_source.GenerateRandom(
      reinterpret_cast<uint8_t*>(&handle), sizeof(handle));
  if (handle == 0) {
    LOG(WARNING) << "TPM randomness failed. Falling back to software RNG.";
    handle = rand();
  }
  handle = handle % (TPM2_NV_INDEX_LAST + 1 - TPM2_NV_INDEX_FIRST);
  handle += TPM2_NV_INDEX_FIRST;
  return handle;
}

static constexpr size_t MAX_HANDLE_ATTEMPTS = 1;

bool FragileTpmStorage::Allocate(const Json::Value& key, uint16_t size) {
  if (HasKey(key)) {
    LOG(WARNING) << "Key " << key << " is already defined.";
    return false;
  }
  TPM2_HANDLE handle;
  for (int i = 0; i < MAX_HANDLE_ATTEMPTS; i++) {
    handle = GenerateRandomHandle();
    TPM2B_NV_PUBLIC public_info = {
      .size = 0,
      .nvPublic = {
        .nvIndex = handle,
        .nameAlg = TPM2_ALG_SHA1,
        .attributes = TPMA_NV_AUTHWRITE | TPMA_NV_AUTHREAD,
        .authPolicy = { .size = 0, .buffer = {} },
        .dataSize = size,
      }
    };
    TPM2B_AUTH auth = { .size = 0, .buffer = {} };
    Esys_TR_SetAuth(resource_manager_.Esys(), ESYS_TR_RH_OWNER, &auth);
    ESYS_TR nv_handle;
    auto rc = Esys_NV_DefineSpace(
      /* esysContext */ resource_manager_.Esys(),
      /* authHandle */ ESYS_TR_RH_OWNER,
      /* shandle1 */ ESYS_TR_PASSWORD,
      /* shandle2 */ ESYS_TR_NONE,
      /* shandle3 */ ESYS_TR_NONE,
      /* auth */ &auth,
      /* publicInfo */ &public_info,
      /* nvHandle */ &nv_handle);
    if (rc == TPM2_RC_NV_DEFINED) {
      LOG(VERBOSE) << "Esys_NV_DefineSpace failed with TPM2_RC_NV_DEFINED";
      continue;
    } else if (rc == TPM2_RC_SUCCESS) {
      Esys_TR_Close(resource_manager_.Esys(), &nv_handle);
      break;
    } else {
      LOG(DEBUG) << "Esys_NV_DefineSpace failed with " << rc << ": "
                 << Tss2_RC_Decode(rc);
    }
  }
  Json::Value entry(Json::objectValue);
  entry[kKey] = key;
  entry[kHandle] = handle;
  index_[kEntries].append(entry);

  if (!WriteProtectedJsonToFile(resource_manager_, index_file_, index_)) {
    LOG(ERROR) << "Failed to save changes to " << index_file_;
    return false;
  }
  return true;
}

TPM2_HANDLE FragileTpmStorage::GetHandle(const Json::Value& key) const {
  for (const auto& entry : index_[kEntries]) {
    if (!entry.isMember(kKey)) {
      LOG(ERROR) << "Index was corrupted";
      return 0;
    }
    if (entry[kKey] != key) {
      continue;
    }
    if (!entry.isMember(kHandle)) {
      LOG(ERROR) << "Index was corrupted";
      return 0;
    }
    return entry[kHandle].asUInt();
  }
  return 0;
}

bool FragileTpmStorage::HasKey(const Json::Value& key) const {
  return GetHandle(key) != 0;
}

std::unique_ptr<TPM2B_MAX_NV_BUFFER> FragileTpmStorage::Read(
    const Json::Value& key) const {
  auto handle = GetHandle(key);
  if (handle == 0) {
    LOG(WARNING) << "Could not read from " << key;
    return {};
  }
  auto close_tr = [this](ESYS_TR* handle) {
    Esys_TR_Close(resource_manager_.Esys(), handle);
    delete handle;
  };
  std::unique_ptr<ESYS_TR, decltype(close_tr)> nv_handle(new ESYS_TR, close_tr);
  auto rc = Esys_TR_FromTPMPublic(
      /* esysContext */ resource_manager_.Esys(),
      /* tpm_handle */ handle,
      /* optionalSession1 */ ESYS_TR_NONE,
      /* optionalSession2 */ ESYS_TR_NONE,
      /* optionalSession3 */ ESYS_TR_NONE,
      /* object */ nv_handle.get());
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_FromTPMPublic failed: " << rc << ": "
               << Tss2_RC_Decode(rc);
    return {};
  }
  TPM2B_AUTH auth = { .size = 0, .buffer = {} };
  Esys_TR_SetAuth(resource_manager_.Esys(), *nv_handle, &auth);

  TPM2B_NV_PUBLIC* public_area;
  rc = Esys_NV_ReadPublic(
      /* esysContext */ resource_manager_.Esys(),
      /* nvIndex */ *nv_handle,
      /* shandle1 */ ESYS_TR_NONE,
      /* shandle2 */ ESYS_TR_NONE,
      /* shandle3 */ ESYS_TR_NONE,
      /* nvPublic */ &public_area,
      /* nvName */ nullptr);
  if (rc != TPM2_RC_SUCCESS || public_area == nullptr) {
    LOG(ERROR) << "Esys_NV_ReadPublic failed: " << rc << ": "
               << Tss2_RC_Decode(rc);
    return {};
  }
  std::unique_ptr<TPM2B_NV_PUBLIC, decltype(Esys_Free)*>
      public_deleter(public_area, Esys_Free);
  TPM2B_MAX_NV_BUFFER* buffer = nullptr;
  rc = Esys_NV_Read(
      /* esysContext */ resource_manager_.Esys(),
      /* authHandle */ *nv_handle,
      /* nvIndex */ *nv_handle,
      /* shandle1 */ ESYS_TR_PASSWORD,
      /* shandle2 */ ESYS_TR_NONE,
      /* shandle3 */ ESYS_TR_NONE,
      /* size */ public_area->nvPublic.dataSize,
      /* offset */ 0,
      /* data */ &buffer);
  if (rc != TSS2_RC_SUCCESS || buffer == nullptr) {
    LOG(ERROR) << "Esys_NV_Read failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  auto ret = std::make_unique<TPM2B_MAX_NV_BUFFER>(*buffer);
  return ret;
}

bool FragileTpmStorage::Write(
    const Json::Value& key, const TPM2B_MAX_NV_BUFFER& data) {
  auto handle = GetHandle(key);
  if (handle == 0) {
    LOG(WARNING) << "Could not read from " << key;
    return false;
  }
  ESYS_TR nv_handle;
  auto rc = Esys_TR_FromTPMPublic(
      /* esysContext */ resource_manager_.Esys(),
      /* tpm_handle */ handle,
      /* optionalSession1 */ ESYS_TR_NONE,
      /* optionalSession2 */ ESYS_TR_NONE,
      /* optionalSession3 */ ESYS_TR_NONE,
      /* object */ &nv_handle);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_FromTPMPublic failed: " << rc << ": "
               << Tss2_RC_Decode(rc);
    return false;
  }
  TPM2B_AUTH auth = { .size = 0, .buffer = {} };
  Esys_TR_SetAuth(resource_manager_.Esys(), nv_handle, &auth);

  rc = Esys_NV_Write(
      /* esysContext */ resource_manager_.Esys(),
      /* authHandle */ nv_handle,
      /* nvIndex */ nv_handle,
      /* shandle1 */ ESYS_TR_PASSWORD,
      /* shandle2 */ ESYS_TR_NONE,
      /* shandle3 */ ESYS_TR_NONE,
      /* data */ &data,
      /* offset */ 0);
  Esys_TR_Close(resource_manager_.Esys(), &nv_handle);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_NV_Write failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return false;
  }
  return true;
}
