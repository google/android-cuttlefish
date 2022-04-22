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

#include <functional>
#include <string>

#include <tss2/tss2_esys.h>

#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {

class PrimaryKeyBuilder {
public:
  PrimaryKeyBuilder();

  void SigningKey();
  void ParentKey();
  void UniqueData(const std::string&);

  TpmObjectSlot CreateKey(TpmResourceManager& resource_manager);

  static TpmObjectSlot CreateSigningKey(TpmResourceManager& resource_manager,
                                        const std::string& unique_data);

 private:
  TPMT_PUBLIC public_area_;
};

std::function<TpmObjectSlot(TpmResourceManager&)>
SigningKeyCreator(const std::string& unique);

std::function<TpmObjectSlot(TpmResourceManager&)>
ParentKeyCreator(const std::string& unique);

}  // namespace cuttlefish
