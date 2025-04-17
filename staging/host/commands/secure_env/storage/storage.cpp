/*
 * Copyright 2023 The Android Open Source Project
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

#include "host/commands/secure_env/storage/storage.h"

#include "keymaster/android_keymaster_utils.h"

namespace cuttlefish {
namespace secure_env {

void StorageDataDestroyer::operator()(StorageData* ptr) {
  {
    keymaster::Eraser(ptr, sizeof(StorageData) + ptr->size);
  }
  std::free(ptr);
}

Result<ManagedStorageData> CreateStorageData(size_t size) {
  const auto bytes_to_allocate = sizeof(StorageData) + size;
  auto memory = std::malloc(bytes_to_allocate);
  CF_EXPECT(memory != nullptr,
            "Cannot allocate " << bytes_to_allocate << " bytes for storage data");
  auto data = reinterpret_cast<StorageData*>(memory);
  data->size = size;
  return ManagedStorageData(data);
}

Result<ManagedStorageData> CreateStorageData(const void* data, size_t size) {
  auto managed_data = CF_EXPECT(CreateStorageData(size));
  std::memcpy(managed_data->payload, data, size);
  return managed_data;
}

}  // namespace secure_env
}  // namespace cuttlefish
