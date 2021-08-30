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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <tss2/tss2_esys.h>
#include <tss2/tss2_tpm2_types.h>
#include <json/json.h>

#include "host/commands/secure_env/gatekeeper_storage.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {

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
class FragileTpmStorage : public GatekeeperStorage {
public:
  FragileTpmStorage(TpmResourceManager&, const std::string& index_file);
  ~FragileTpmStorage() = default;

  bool Allocate(const Json::Value& key, uint16_t size) override;
  bool HasKey(const Json::Value& key) const override;

  std::unique_ptr<TPM2B_MAX_NV_BUFFER> Read(const Json::Value& key) const
      override;
  bool Write(const Json::Value& key, const TPM2B_MAX_NV_BUFFER& data) override;
private:
  TPM2_HANDLE GetHandle(const Json::Value& key) const;
  TPM2_HANDLE GenerateRandomHandle();

  TpmResourceManager& resource_manager_;
  std::string index_file_;
  Json::Value index_;
};

}  // namespace cuttlefish
