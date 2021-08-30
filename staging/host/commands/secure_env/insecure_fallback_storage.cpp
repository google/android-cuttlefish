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

#include "host/commands/secure_env/insecure_fallback_storage.h"

#include <fstream>

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/json_serializable.h"
#include "host/commands/secure_env/tpm_random_source.h"

namespace cuttlefish {

static constexpr char kEntries[] = "entries";
static constexpr char kKey[] = "key";
static constexpr char kValue[] = "value";

InsecureFallbackStorage::InsecureFallbackStorage(
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

bool InsecureFallbackStorage::Allocate(const Json::Value& key, uint16_t size) {
  if (HasKey(key)) {
    LOG(WARNING) << "Key " << key << " is already defined.";
    return false;
  }
  if (size > sizeof(((TPM2B_MAX_NV_BUFFER*)nullptr)->buffer)) {
    LOG(ERROR) << "Size " << size << " was too large.";
    return false;
  }
  Json::Value entry(Json::objectValue);
  entry[kKey] = key;
  Json::Value value(Json::arrayValue);
  for (int i = 0; i < size; i++) {
    value.append(0);
  }
  entry[kValue] = value;
  index_[kEntries].append(entry);

  if (!WriteProtectedJsonToFile(resource_manager_, index_file_, index_)) {
    LOG(ERROR) << "Failed to save changes to " << index_file_;
    return false;
  }
  return true;
}

const Json::Value* InsecureFallbackStorage::GetEntry(
    const Json::Value& key) const {
  for (auto& entry : index_[kEntries]) {
    if (!entry.isMember(kKey)) {
      LOG(WARNING) << "Index was corrupted";
      return nullptr;
    }
    if (entry[kKey] != key) {
      continue;
    }
    if (!entry.isMember(kValue) || entry[kValue].type() != Json::arrayValue) {
      LOG(WARNING) << "Index was corrupted";
      return nullptr;
    }
    return &entry;
  }
  return nullptr;
}

Json::Value* InsecureFallbackStorage::GetEntry(const Json::Value& key) {
  return const_cast<Json::Value*>(
      static_cast<const InsecureFallbackStorage&>(*this).GetEntry(key));
}

bool InsecureFallbackStorage::HasKey(const Json::Value& key) const {
  return static_cast<bool>(GetEntry(key));
}

std::unique_ptr<TPM2B_MAX_NV_BUFFER> InsecureFallbackStorage::Read(
    const Json::Value& key) const {
  auto entry = GetEntry(key);
  if (!entry) {
    LOG(WARNING) << "Could not read from " << key;
    return {};
  }
  const auto& value = (*entry)[kValue];
  if (value.type() != Json::arrayValue) {
    LOG(WARNING) << "Index was corrupted";
    return {};
  }
  auto ret = std::make_unique<TPM2B_MAX_NV_BUFFER>();
  if (value.size() > sizeof(ret->buffer)) {
    LOG(ERROR) << "Index was corrupted: size of data was too large";
    return {};
  }
  ret->size = value.size();
  for (unsigned int i = 0; i < value.size(); i++) {
    ret->buffer[i] = value[i].asUInt();
  }
  return ret;
}

bool InsecureFallbackStorage::Write(
    const Json::Value& key, const TPM2B_MAX_NV_BUFFER& data) {
  auto entry = GetEntry(key);
  if (!entry) {
    LOG(WARNING) << "Could not read from " << key;
    return false;
  }
  auto& value = (*entry)[kValue];
  if (value.type() != Json::arrayValue) {
    LOG(WARNING) << "Index was corrupted";
    return false;
  }
  if (data.size != value.size()) {
    LOG(ERROR) << "Size of data given was incorrect";
    return false;
  };
  for (unsigned int i = 0; i < value.size(); i++) {
    value[i] = data.buffer[i];
  }

  if (!WriteProtectedJsonToFile(resource_manager_, index_file_, index_)) {
    LOG(ERROR) << "Failed to save changes to " << index_file_;
    return false;
  }
  return true;
}

}  // namespace cuttlefish
