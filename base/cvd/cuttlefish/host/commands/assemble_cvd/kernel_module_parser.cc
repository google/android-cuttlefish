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

#include "kernel_module_parser.h"

#include <fcntl.h>
#include "common/libs/fs/shared_fd.h"

static constexpr std::string_view SIGNATURE_FOOTER =
    "~Module signature appended~\n";

namespace cuttlefish {

bool IsKernelModuleSigned(const char *path) {
  auto fd = SharedFD::Open(path, O_RDONLY);
  fd->LSeek(-SIGNATURE_FOOTER.size(), SEEK_END);
  std::array<char, SIGNATURE_FOOTER.size()> buf{};
  fd->Read(buf.data(), buf.size());

  return memcmp(buf.data(), SIGNATURE_FOOTER.data(), SIGNATURE_FOOTER.size()) ==
         0;
}

}  // namespace cuttlefish