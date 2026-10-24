//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/credential_source/fixed_credential_source.h"

#include <memory>
#include <string>

#include "cuttlefish/host/libs/web/credential_source/credential_source.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

FixedCredentialSource::FixedCredentialSource(const std::string& credential) {
  this->credential_ = credential;
}

Result<std::string> FixedCredentialSource::Credential() { return credential_; }

std::unique_ptr<CredentialSource> FixedCredentialSource::Make(
    const std::string& credential) {
  return std::unique_ptr<CredentialSource>(
      new FixedCredentialSource(credential));
}

}  // namespace cuttlefish
