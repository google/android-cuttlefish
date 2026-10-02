//
// Copyright (C) 2026 The Android Open Source Project
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

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// OAuth2 credentials obtained from a LUCI-compatible credential helper binary
// (https://chromium.googlesource.com/infra/luci/luci-go/+/refs/heads/main/auth/credhelperpb/credhelper.proto).
class LuciCredentialHelperSource : public RefreshingCredentialSource {
 public:
  static std::unique_ptr<CredentialSource> Make(const std::string& scope);

 private:
  LuciCredentialHelperSource(std::string helper_bin, std::string scope);

  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  std::string helper_bin_;
  std::string scope_;
};

}  // namespace cuttlefish
