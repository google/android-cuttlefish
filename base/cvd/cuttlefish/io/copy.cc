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

#include "cuttlefish/io/copy.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

static constexpr size_t kBufferSize = 1 << 26;

Result<void> Copy(Reader& reader, Writer& writer, const size_t buffer_size) {
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
  return {};
}

Result<void> SparseCopy(Reader& reader, WriterSeeker& writer,
                        size_t buffer_size) {
  CF_EXPECT(writer.SeekSet(0));
  std::unique_ptr<std::array<char, kBufferSize>> buf(
      new std::array<char, kBufferSize>);
  uint64_t chunk_read;
  uint64_t total_size = 0;
  while ((chunk_read = CF_EXPECT(reader.Read(buf.get(), buf->size()))) > 0) {
    total_size += chunk_read;
    if ((*buf)[0] == '\0' && (*buf)[chunk_read - 1] == 0) {
      bool zero_chunk = true;
      for (const char c : *buf) {
        if (c != '\0') {
          zero_chunk = false;
          break;
        }
      }
      if (zero_chunk) {
        CF_EXPECT(writer.Truncate(total_size));
        CF_EXPECT(writer.SeekCur(chunk_read));
        continue;
      }
    }
    uint64_t chunk_written = 0;
    while (chunk_written < chunk_read) {
      uint64_t written = CF_EXPECT(writer.Write(&buf->data()[chunk_written],
                                                chunk_read - chunk_written));
      CF_EXPECT_GT(written, 0, "Premature EOF on writer");
      chunk_written += written;
    }
  }
  CF_EXPECT(writer.Write(nullptr, 0));
  return {};
}

}  // namespace cuttlefish
