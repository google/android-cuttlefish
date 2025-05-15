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

#include "host/commands/secure_env/storage/storage.h"

namespace cuttlefish {
namespace secure_env {

class InsecureJsonStorage : public secure_env::Storage {
 public:
  InsecureJsonStorage(std::string path);

  Result<bool> HasKey(const std::string& key) const override;
  Result<ManagedStorageData> Read(const std::string& key) const override;
  Result<void> Write(const std::string& key, const StorageData& data) override;
  bool Exists() const override;

 private:
  std::string path_;
};

}  // namespace secure_env
}  // namespace cuttlefish
