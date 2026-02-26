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

#include <fcntl.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

static constexpr std::string_view kSignatureFooter =
    "~Module signature appended~\n";

namespace cuttlefish {

Result<bool> IsKernelModuleSigned(std::string_view path) {
  SharedFD fd = SharedFD::Open(std::string(path), O_RDONLY);
  CF_EXPECT(fd->IsOpen(), fd->StrError());

  CF_EXPECT_GE(fd->LSeek(-kSignatureFooter.size(), SEEK_END), 0, fd->StrError());

  std::array<char, kSignatureFooter.size()> buf{};
  CF_EXPECT_EQ(ReadExact(fd, buf.data(), buf.size()), buf.size(), fd->StrError());

  return memcmp(buf.data(), kSignatureFooter.data(), kSignatureFooter.size()) ==
         0;
}

}  // namespace cuttlefish
