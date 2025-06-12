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

#include "cuttlefish/host/commands/cvd/fetch/credential_flags.h"

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"

namespace cuttlefish {

std::vector<Flag> CredentialFlags::Flags() {
  std::vector<Flag> flags;

  flags.emplace_back(
      GflagsCompatFlag("use_gce_metadata", this->use_gce_metadata)
          .Help("Enforce using GCE metadata credentials."));
  flags.emplace_back(
      GflagsCompatFlag("credential_filepath", this->credential_filepath)
          .Help("Enforce reading credentials from the given filepath."));
  flags.emplace_back(GflagsCompatFlag("service_account_filepath",
                                      this->service_account_filepath)
                         .Help("Enforce reading service account credentials "
                               "from the given filepath."));

  return flags;
}

}  // namespace cuttlefish
