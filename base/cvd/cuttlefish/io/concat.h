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

#include <map>
#include <memory>
#include <vector>

#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

/**
 * A ReaderSeeker implementation that concatenates a set of readers.
 *
 * The readers are assumed to have no holes when reading and are assumed to not
 * change in size.
 */
class ConcatReaderSeeker : public ReaderFakeSeeker {
 public:
  static Result<ConcatReaderSeeker> Create(
      std::vector<std::unique_ptr<ReaderSeeker>>);

  virtual Result<uint64_t> PRead(void* buf, uint64_t count,
                                 uint64_t offset) const;

 private:
  explicit ConcatReaderSeeker(
      std::map<uint64_t, std::unique_ptr<ReaderSeeker>> off_to_reader,
      uint64_t length);

  std::map<uint64_t, std::unique_ptr<ReaderSeeker>> off_to_reader_;
};

}  // namespace cuttlefish
