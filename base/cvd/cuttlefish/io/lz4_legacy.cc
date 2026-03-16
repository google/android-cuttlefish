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

#include "cuttlefish/io/lz4_legacy.h"

#include <endian.h>
#include <stdint.h>

#include <memory>
#include <utility>

#include "lz4.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

constexpr uint32_t kLz4LegacyFrameMagic = 0x184C2102;
constexpr uint32_t kLz4LegacyFrameBlockSize = 8 << 20;

class Lz4LegacyReaderImpl : public Reader {
 public:
  Lz4LegacyReaderImpl(std::unique_ptr<Reader> source)
      : source_(std::move(source)) {}

  Result<uint64_t> Read(void* buf, uint64_t count) override {
    if (decompressed_.empty()) {
      uint32_t length = 0;
      // We should get either an EOF or a block of 4 bytes which is a block
      // length. EOF or a 0 value means we need to end.
      if (CF_EXPECT(source_->Read(reinterpret_cast<char*>(&length), 1)) == 0) {
        return 0;
      }
      // Now we know it's not an EOF, we need the other 3 bytes.
      CF_EXPECT(ReadExact(*source_, reinterpret_cast<char*>(&length) + 1, 3));
      length = le32toh(length);
      if (length == 0) {
        return 0;
      }
      compressed_.resize(length);
      CF_EXPECT(ReadExact(*source_, compressed_.data(), length));
      decompressed_.resize(kLz4LegacyFrameBlockSize);
      int lz4_length =
          LZ4_decompress_safe(compressed_.data(), decompressed_.data(),
                              compressed_.size(), decompressed_.size());
      CF_EXPECT_GE(lz4_length, 0);
      if (lz4_length == 0) {
        return 0;
      }
      decompressed_.resize(lz4_length);
    }
    uint64_t len = std::min(count, decompressed_.size());
    memcpy(buf, decompressed_.data(), std::min(count, len));
    decompressed_.erase(decompressed_.begin(), decompressed_.begin() + len);
    return len;
  }

 private:
  std::unique_ptr<Reader> source_;
  std::vector<char> compressed_;
  std::vector<char> decompressed_;
};

}  // namespace

Result<std::unique_ptr<Reader>> Lz4LegacyReader(
    std::unique_ptr<Reader> source) {
  CF_EXPECT(source.get());
  uint32_t magic = le32toh(CF_EXPECT(ReadExactBinary<uint32_t>(*source)));
  CF_EXPECT_EQ(magic, kLz4LegacyFrameMagic);
  return std::make_unique<Lz4LegacyReaderImpl>(std::move(source));
}

}  // namespace cuttlefish
