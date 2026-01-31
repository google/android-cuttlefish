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

#include <stdint.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class SharedFdIo : public ReaderSeeker, public WriterSeeker {
 public:
  explicit SharedFdIo(SharedFD);

  Result<size_t> PartialRead(void* buf, size_t count) override;
  Result<size_t> PartialWrite(const void* buf, size_t count) override;
  Result<size_t> SeekSet(size_t offset) override;
  Result<size_t> SeekCur(ssize_t offset) override;
  Result<size_t> SeekEnd(ssize_t offset) override;
  Result<size_t> PartialReadAt(void* buf, size_t count,
                               size_t offset) const override;
  Result<size_t> PartialWriteAt(const void* buf, size_t count,
                                size_t offset) const override;

 private:
  SharedFD fd_;
};

}  // namespace cuttlefish
