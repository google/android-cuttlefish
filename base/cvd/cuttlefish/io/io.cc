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

#include "cuttlefish/io/io.h"

#include <stdint.h>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> Reader::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<void> Writer::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<void> Seeker::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<void> ReaderSeeker::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<void> WriterSeeker::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<void> ReaderWriterSeeker::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<void> WriterSeeker::Truncate(uint64_t size) {
  return CF_ERR("Unimplemented");
}

Result<void> ReaderWriterSeeker::Truncate(uint64_t size) {
  return CF_ERR("Unimplemented");
}

}  // namespace cuttlefish
