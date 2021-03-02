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

#include "host/commands/secure_env/json_serializable.h"

#include <fstream>

#include <android-base/logging.h>
#include <keymaster/serializable.h>

#include "host/commands/secure_env/encrypted_serializable.h"
#include "host/commands/secure_env/hmac_serializable.h"
#include "host/commands/secure_env/primary_key_builder.h"

static constexpr char kUniqueKey[] = "JsonSerializable";

class JsonSerializable : public keymaster::Serializable {
public:
  JsonSerializable(Json::Value& json);

  size_t SerializedSize() const override;

  uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;

  bool Deserialize(const uint8_t** buf_ptr, const uint8_t* buf_end) override;
private:
  Json::Value& json_;
};

JsonSerializable::JsonSerializable(Json::Value& json) : json_(json) {}

size_t JsonSerializable::SerializedSize() const {
  Json::StreamWriterBuilder factory;
  auto serialized = Json::writeString(factory, json_);
  return serialized.size() + sizeof(uint32_t);
}

uint8_t* JsonSerializable::Serialize(uint8_t* buf, const uint8_t* end) const {
  Json::StreamWriterBuilder factory;
  auto serialized = Json::writeString(factory, json_);
  if (end - buf < serialized.size() + sizeof(uint32_t)) {
    LOG(ERROR) << "Not enough space to serialize json";
    return buf;
  }
  return keymaster::append_size_and_data_to_buf(
      buf, end, serialized.data(), serialized.size());
}

bool JsonSerializable::Deserialize(
    const uint8_t** buf_ptr, const uint8_t* buf_end) {
  size_t size;
  keymaster::UniquePtr<uint8_t[]> json_bytes;
  bool success = keymaster::copy_size_and_data_from_buf(
      buf_ptr, buf_end, &size, &json_bytes);
  if (!success) {
    LOG(ERROR) << "Failed to deserialize json bytes";
    return false;
  }
  auto doc_begin = reinterpret_cast<const char*>(json_bytes.get());
  auto doc_end = doc_begin + size;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  std::string errorMessage;
  if (!reader->parse(doc_begin, doc_end, &json_, &errorMessage)) {
    LOG(ERROR) << "Failed to parse json: " << errorMessage;
    return false;
  }
  return true;
}

bool WriteProtectedJsonToFile(
    TpmResourceManager& resource_manager,
    const std::string& filename,
    Json::Value json) {
  JsonSerializable sensitive_material(json);
  auto parent_key_fn = ParentKeyCreator(kUniqueKey);
  EncryptedSerializable encryption(
      resource_manager, parent_key_fn, sensitive_material);
  auto signing_key_fn = SigningKeyCreator(kUniqueKey);
  HmacSerializable sign_check(
      resource_manager, signing_key_fn, TPM2_SHA256_DIGEST_SIZE, &encryption);

  auto size = sign_check.SerializedSize();
  LOG(INFO) << "size : " << size;
  std::vector<uint8_t> data(size + 1);
  uint8_t* buf = data.data();
  uint8_t* buf_end = buf + data.size();
  buf = sign_check.Serialize(buf, buf_end);
  if (buf != (buf_end - 1)) {
    LOG(ERROR) << "Serialized size did not match up with actual usage.";
    return false;
  }

  std::ofstream file_stream(filename, std::ios::trunc | std::ios::binary);
  file_stream.write(reinterpret_cast<char*>(data.data()), data.size() - 1);
  if (!file_stream) {
    LOG(ERROR) << "Failed to save data to " << filename;
    return false;
  }
  return true;
}

Json::Value ReadProtectedJsonFromFile(
    TpmResourceManager& resource_manager, const std::string& filename) {
  std::ifstream file_stream(filename, std::ios::binary | std::ios::ate);
  std::streamsize size = file_stream.tellg();
  file_stream.seekg(0, std::ios::beg);

  if (size <= 0) {
    LOG(VERBOSE) << "File " << filename << " was empty.";
    return {};
  }

  std::vector<char> buffer(size);
  if (!file_stream.read(buffer.data(), size)) {
    LOG(ERROR) << "Unable to read from " << filename;
    return {};
  }
  if (!file_stream) {
    LOG(ERROR) << "Unable to read from " << filename;
    return {};
  }

  Json::Value json;
  JsonSerializable sensitive_material(json);
  auto parent_key_fn = ParentKeyCreator(kUniqueKey);
  EncryptedSerializable encryption(
      resource_manager, parent_key_fn, sensitive_material);
  auto signing_key_fn = SigningKeyCreator(kUniqueKey);
  HmacSerializable sign_check(
      resource_manager, signing_key_fn, TPM2_SHA256_DIGEST_SIZE, &encryption);

  auto buf = reinterpret_cast<const uint8_t*>(buffer.data());
  auto buf_end = buf + buffer.size();
  if (!sign_check.Deserialize(&buf, buf_end)) {
    LOG(ERROR) << "Failed to deserialize json data from " << filename;
    return {};
  }

  return json;
}
