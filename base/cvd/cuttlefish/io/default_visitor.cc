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

#include "cuttlefish/io/default_visitor.h"

#include <stdint.h>

#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_window_view.h"
#include "cuttlefish/io/shared_fd.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> DefaultIoVisitor::Accept(ConcatReaderSeeker& io) {
  CF_EXPECT(Accept(static_cast<ReaderSeeker&>(io)));
  return {};
}

Result<void> DefaultIoVisitor::Accept(ReadWindowView& io) {
  CF_EXPECT(Accept(static_cast<ReaderSeeker&>(io)));
  return {};
}

Result<void> DefaultIoVisitor::Accept(Reader&) {
  return CF_ERR("Unimplemented");
}

Result<void> DefaultIoVisitor::Accept(ReaderSeeker& io) {
  CF_EXPECT(Accept(static_cast<Reader&>(io)));
  return {};
}

Result<void> DefaultIoVisitor::Accept(ReaderWriterSeeker& io) {
  CF_EXPECT(Accept(static_cast<ReaderSeeker&>(io)));
  return {};
}

Result<void> DefaultIoVisitor::Accept(Seeker&) {
  return CF_ERR("Unimplemented");
}

Result<void> DefaultIoVisitor::Accept(SharedFdIo& io) {
  CF_EXPECT(Accept(static_cast<ReaderWriterSeeker&>(io)));
  return {};
}

Result<void> DefaultIoVisitor::Accept(Writer&) {
  return CF_ERR("Unimplemented");
}

Result<void> DefaultIoVisitor::Accept(WriterSeeker& io) {
  CF_EXPECT(Accept(static_cast<Writer&>(io)));
  return {};
}

}  // namespace cuttlefish
