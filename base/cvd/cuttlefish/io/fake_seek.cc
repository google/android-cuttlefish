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

#include "cuttlefish/io/fake_seek.h"

#include <stdint.h>

#include <algorithm>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

ReaderFakeSeeker::ReaderFakeSeeker(uint64_t length)
    : seek_pos_(0), length_(length) {}

Result<uint64_t> ReaderFakeSeeker::Read(void* buf, uint64_t count) {
  uint64_t data_read = CF_EXPECT(PRead(buf, count, seek_pos_));
  seek_pos_ += data_read;
  return data_read;
}

Result<uint64_t> ReaderFakeSeeker::SeekSet(const uint64_t offset) {
  return seek_pos_ = std::min(offset, length_);
}

Result<uint64_t> ReaderFakeSeeker::SeekCur(const int64_t off) {
  int64_t seek_pos_signed = std::max(static_cast<int64_t>(seek_pos_) + off, 0L);
  return seek_pos_ = std::min(static_cast<uint64_t>(seek_pos_signed), length_);
}

Result<uint64_t> ReaderFakeSeeker::SeekEnd(int64_t off) {
  int64_t seek_pos_signed = std::max(static_cast<int64_t>(length_) + off, 0L);
  return seek_pos_ = std::min(static_cast<uint64_t>(seek_pos_signed), length_);
}

}  // namespace cuttlefish
