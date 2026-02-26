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

#include "cuttlefish/host/commands/assemble_cvd/kernel_module_parser.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

static constexpr std::string_view kSignatureFooter =
    "~Module signature appended~\n";

namespace cuttlefish {

Result<bool> IsKernelModuleSigned(ReaderSeeker& file) {
  CF_EXPECT(file.SeekEnd(-kSignatureFooter.size()));
  return CF_EXPECT(ReadToString(file, kSignatureFooter.size())) ==
         kSignatureFooter;
}

}  // namespace cuttlefish
