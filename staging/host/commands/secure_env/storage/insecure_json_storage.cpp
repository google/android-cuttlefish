//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/secure_env/storage/insecure_json_storage.h"

#include <fstream>

#include <android-base/file.h>
#include <json/json.h>

#include "common/libs/utils/base64.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"

namespace cuttlefish {
namespace secure_env {
namespace {

Result<Json::Value> ReadJson(const std::string& path) {
  std::string json;
  CF_EXPECT(android::base::ReadFileToString(path, &json));
  return CF_EXPECT(ParseJson(json));
}

Result<void> WriteJson(const std::string& path, const Json::Value& root) {
  Json::StreamWriterBuilder builder;
  auto json = Json::writeString(builder, root);
  CF_EXPECT(android::base::WriteStringToFile(json, path));
  return {};
}

} // namespace

InsecureJsonStorage::InsecureJsonStorage(std::string path) : path_(std::move(path)) {}

bool InsecureJsonStorage::Exists() const {
  return ReadJson(path_).ok();
}

Result<bool> InsecureJsonStorage::HasKey(const std::string& key) const {
  if (!FileHasContent(path_)) {
    return false;
  }
  return CF_EXPECT(ReadJson(path_)).isMember(key);
}

Result<ManagedStorageData> InsecureJsonStorage::Read(const std::string& key) const {
  auto root = CF_EXPECT(ReadJson(path_));
  CF_EXPECT(root.isMember(key), "Key: " << key << " not found in " << path_);

  std::vector<uint8_t> base64_buffer;
  CF_EXPECT(DecodeBase64(root[key].asString(), &base64_buffer),
            "Failed to decode base64 to read key: " << key);
  auto storage_data = CF_EXPECT(CreateStorageData(base64_buffer.size()));
  std::memcpy(storage_data->payload, reinterpret_cast<unsigned char *>(base64_buffer.data()),
              base64_buffer.size());
  return storage_data;
}

Result<void> InsecureJsonStorage::Write(const std::string& key, const StorageData& data) {
  Json::Value root;
  if (FileHasContent(path_)) {
    root = CF_EXPECT(ReadJson(path_));
  }

  std::string value_base64;
  CF_EXPECT(EncodeBase64(data.payload, data.size, &value_base64),
            "Failed to encode base64 to write key: " << key);
  root[key] = value_base64;

  CF_EXPECT(WriteJson(path_, root));
  return {};
}

}  // namespace oemlock
}  // namespace cuttlefish
