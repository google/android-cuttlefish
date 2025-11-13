//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/libs/zip/zip_copy.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <memory>
#include <utility>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"

namespace cuttlefish {

static constexpr size_t kBufferSize = 1 << 26;

Result<void> Copy(ReadableZipSource& input, WritableZipSource& output) {
  ZipSourceReader reader = CF_EXPECT(input.Reader());
  ZipSourceWriter writer = CF_EXPECT(output.Writer());

  std::unique_ptr<std::array<char, kBufferSize>> buf(
      new std::array<char, kBufferSize>);
  uint64_t chunk_read;
  while ((chunk_read = CF_EXPECT(reader.Read(buf.get(), buf->size()))) > 0) {
    uint64_t chunk_written = 0;
    while (chunk_written < chunk_read) {
      uint64_t written = CF_EXPECT(writer.Write(&buf->data()[chunk_written],
                                                chunk_read - chunk_written));
      CF_EXPECT_GT(written, 0, "Premature EOF on writer");
      chunk_written += written;
    }
  }
  CF_EXPECT(ZipSourceWriter::Finalize(std::move(writer)));
  return {};
}

}  // namespace cuttlefish
