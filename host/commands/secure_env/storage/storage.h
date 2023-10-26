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

#pragma once

#include <any>
#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace secure_env {

struct StorageData {
  uint32_t size;
  uint8_t payload[0];

  Result<uint8_t> asUint8() {
    CF_EXPECT(size == sizeof(uint8_t), "Size of payload is not matched with uint8 size");
    return *reinterpret_cast<uint8_t*>(payload);
  }
};

/**
 * A destroyer for StorageData instances created with
 * CreateStorageData. Wipes memory from the StorageData instances.
 */
class StorageDataDestroyer {
 public:
  void operator()(StorageData* ptr);
};

/** An owning pointer for a StorageData instance. */
using ManagedStorageData = std::unique_ptr<StorageData, StorageDataDestroyer>;

/**
 * Allocates memory for a StorageData carrying a message of size
 * `size`.
 */
Result<ManagedStorageData> CreateStorageData(size_t size);
Result<ManagedStorageData> CreateStorageData(const void* data, size_t size);

/**
 * Storage abstraction to store binary blobs associated with string key
*/
class Storage {
 public:
  virtual Result<bool> HasKey(const std::string& key) const = 0;
  virtual Result<ManagedStorageData> Read(const std::string& key) const = 0;
  virtual Result<void> Write(const std::string& key, const StorageData& data) = 0;
  virtual bool Exists() const = 0;

  virtual ~Storage() = default;
};

} // namespace secure_env
} // namespace cuttlefish
