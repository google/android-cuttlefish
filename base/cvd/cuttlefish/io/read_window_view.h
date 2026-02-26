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

#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

/**
 * Wraps another `ReaderSeeker` implementation and presents a view to a subset
 * of the data that can be read from the wrapped instance.
 */
class ReadWindowView : public ReaderFakeSeeker {
 public:
  ReadWindowView(const ReaderSeeker&, uint64_t begin, uint64_t length);

  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override;

 private:
  ReaderSeeker const* data_provider_;
  uint64_t begin_ = 0;
  uint64_t length_ = 0;
};

}  // namespace cuttlefish
