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

#include <string>

#include <json/json.h>
#include <tss2/tss2_tpm2_types.h>

#include "host/commands/secure_env/gatekeeper_storage.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

/**
 * A GatekeeperStorage fallback implementation that is less secure. It uses an
 * index file that is signed and encrypted by the TPM and the sensitive data
 * is contained inside the index file. This file can be deleted or corrupted
 * to lose access to the data inside, and is also susceptible to replay attacks.
 * If the index file is replaced with an older version and the secure
 * environment is restarted, it will still accept the old file with the old
 * data.
 *
 * This class is not thread-safe, and should be synchronized externally if it
 * is going to be used from multiple threads.
 */
class InsecureFallbackStorage : public GatekeeperStorage {
public:
  InsecureFallbackStorage(TpmResourceManager&, const std::string& index_file);
  ~InsecureFallbackStorage() = default;

  bool Allocate(const Json::Value& key, uint16_t size) override;
  bool HasKey(const Json::Value& key) const override;

  std::unique_ptr<TPM2B_MAX_NV_BUFFER> Read(const Json::Value& key) const
      override;
  bool Write(const Json::Value& key, const TPM2B_MAX_NV_BUFFER& data) override;
private:
  Json::Value* GetEntry(const Json::Value& key);
  const Json::Value* GetEntry(const Json::Value& key) const;

  TpmResourceManager& resource_manager_;
  std::string index_file_;
  Json::Value index_;
};
