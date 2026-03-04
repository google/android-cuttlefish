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

#include "cuttlefish/io/concat.h"

#include <map>
#include <memory>
#include <vector>

#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

ConcatReaderSeeker::ConcatReaderSeeker(
    std::map<uint64_t, std::unique_ptr<ReaderSeeker>> off_to_reader,
    uint64_t length)
    : ReaderFakeSeeker(length), off_to_reader_(std::move(off_to_reader)) {}

Result<uint64_t> ConcatReaderSeeker::PRead(void* buf, uint64_t count,
                                           uint64_t offset) const {
  auto it = off_to_reader_.upper_bound(offset);
  CF_EXPECT(it != off_to_reader_.begin(), "Could not find first reader");
  it--;
  CF_EXPECT(it->second.get());
  // Relies on callees to cut short longer reads, and on callers to retry
  // incomplete reads.
  uint64_t segment_begin = offset - it->first;
  return CF_EXPECT(it->second->PRead(buf, count, segment_begin));
}

Result<ConcatReaderSeeker> ConcatReaderSeeker::Create(
    std::vector<std::unique_ptr<ReaderSeeker>> readers) {
  CF_EXPECT(!readers.empty(), "Received empty list");
  uint64_t offset = 0;
  std::map<uint64_t, std::unique_ptr<ReaderSeeker>> off_to_reader_;
  for (std::unique_ptr<ReaderSeeker>& reader : readers) {
    CF_EXPECT(reader.get());
    uint64_t length = CF_EXPECT(Length(*reader));
    off_to_reader_[offset] = std::move(reader);
    offset += length;
  }
  return ConcatReaderSeeker(std::move(off_to_reader_), offset);
}

}  // namespace cuttlefish
