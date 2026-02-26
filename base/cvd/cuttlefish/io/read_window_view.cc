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

#include "cuttlefish/io/read_window_view.h"

#include <stdint.h>

#include <algorithm>

#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

ReadWindowView::ReadWindowView(const ReaderSeeker& data_provider,
                               const uint64_t begin, const uint64_t length)
    : ReaderFakeSeeker(length),
      data_provider_(&data_provider),
      begin_(begin),
      length_(length) {}

Result<uint64_t> ReadWindowView::PRead(void* const buf, uint64_t count,
                                       uint64_t offset) const {
  if (offset >= length_) {
    return 0;
  }
  count = std::min(count, length_ - offset);
  offset += begin_;
  return CF_EXPECT(data_provider_->PRead(buf, count, offset));
}

}  // namespace cuttlefish
