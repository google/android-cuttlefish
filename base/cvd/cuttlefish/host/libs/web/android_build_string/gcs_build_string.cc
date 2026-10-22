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

#include "cuttlefish/host/libs/web/android_build_string/gcs_build_string.h"

#include <optional>
#include <ostream>

#include "fmt/ostream.h"

#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"

namespace cuttlefish {

std::ostream& operator<<(std::ostream& out,
                         const GcsBuildString& build_string) {
  fmt::print(out, "(url=\"{}\", filepath=\"{}\", sha256=\"{}\")",
             ScrubUrl(build_string.url), build_string.filepath.value_or(""),
             build_string.sha256.value_or(""));
  return out;
}

}  // namespace cuttlefish
