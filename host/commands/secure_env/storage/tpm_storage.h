//
// Copyright (C) 2020-2023 The Android Open Source Project
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

#include "host/commands/secure_env/storage/storage.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tss2/tss2_esys.h>
#include <tss2/tss2_tpm2_types.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {
namespace secure_env {

/**
 * Manager for data stored inside the TPM with an index outside of the TPM. The
 * contents of the data cannot be corrupted or decrypted by accessing the index,
 * but the index can be corrupted by an attacker.
 *
 * As the actual data is stored inside the TPM, a replay attack can be used to
 * restore deleted index entries or hide revert to before an index entry was
 * added, but not change the contents that an index points to if it still
 * exists.
 *
 * This class is not thread-safe, and should be synchronized externally if it
 * is going to be used from multiple threads.
 */
class TpmStorage : public secure_env::Storage {
 public:
  TpmStorage(TpmResourceManager& resource_manager, const std::string& index_file);

  Result<bool> HasKey(const std::string& key) const override;
  Result<ManagedStorageData> Read(const std::string& key) const override;
  Result<void> Write(const std::string& key, const StorageData& data) override;
  bool Exists() const override;

 private:
  Result<std::optional<TPM2_HANDLE>> GetHandle(const std::string& key) const;
  TPM2_HANDLE GenerateRandomHandle();
  Result<void> Allocate(const std::string& key, uint16_t size);

  TpmResourceManager& resource_manager_;
  std::string index_file_;
  Json::Value index_;

  std::string path_;
};

}  // namespace secure_env
}  // namespace cuttlefish
