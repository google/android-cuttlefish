/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "common/auto_resources/auto_resources.h"

#include <stdio.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::StrEq;

namespace test {
static constexpr size_t kImmutableReserveSize =
    AutoFreeBuffer::kAutoBufferShrinkReserveThreshold;

class AutoFreeBufferTest : public ::testing::Test {
 public:
  AutoFreeBufferTest() = default;
  ~AutoFreeBufferTest() override = default;

  void SetUp() override {}
  void TearDown() override {}

 protected:
  AutoFreeBuffer buffer_;
};

TEST_F(AutoFreeBufferTest, ShrinkingSmallReservationsDoesNotRealloc) {

  buffer_.Reserve(kImmutableReserveSize);
  const void* const data = buffer_.data();

  EXPECT_EQ(0u, buffer_.size());
  EXPECT_EQ(kImmutableReserveSize, buffer_.reserve_size());
  EXPECT_NE(nullptr, data);

  buffer_.Resize(kImmutableReserveSize);
  EXPECT_EQ(kImmutableReserveSize, buffer_.size());
  EXPECT_EQ(data, buffer_.data());

  // Reduce size of buffer.
  buffer_.Reserve(kImmutableReserveSize / 2);
  EXPECT_EQ(kImmutableReserveSize, buffer_.reserve_size());
  EXPECT_EQ(kImmutableReserveSize / 2, buffer_.size());
  EXPECT_EQ(data, buffer_.data());

  buffer_.Clear();

  EXPECT_EQ(0u, buffer_.size());
  EXPECT_EQ(kImmutableReserveSize, buffer_.reserve_size());
  EXPECT_EQ(data, buffer_.data());
}

TEST_F(AutoFreeBufferTest, ShrinkingLargeReservationDoesRealloc) {
  buffer_.Reserve(kImmutableReserveSize + 1);

  EXPECT_EQ(0u, buffer_.size());
  EXPECT_EQ(kImmutableReserveSize + 1, buffer_.reserve_size());

  buffer_.Reserve(kImmutableReserveSize);

  EXPECT_EQ(0u, buffer_.size());
  EXPECT_EQ(kImmutableReserveSize, buffer_.reserve_size());
  // Note: realloc may re-use current memory pointer, so testing data pointer
  // makes no sense.
}

TEST_F(AutoFreeBufferTest, ResizeClearsMemory) {
  constexpr char kTruncWords[] = "This string";
  constexpr char kLastWords[] = "will be truncated to first two words.";
  constexpr char kFullText[] =
      "This string will be truncated to first two words.";
  // Ignore padding \0.
  constexpr size_t kTruncLength = sizeof(kTruncWords) - 1;

  buffer_.SetToString(kFullText);

  // Note: this call treats buffer as raw data, so no padding happens yet.
  buffer_.Resize(kTruncLength);
  EXPECT_THAT(buffer_.data(), StrEq(kFullText));

  buffer_.Resize(kTruncLength + 1);
  EXPECT_THAT(buffer_.data(), StrEq(kTruncWords));

  // Note: we're accessing buffer out of size() bounds, but still within
  // reserve_size() bounds.
  // This confirms that only 1 byte of data has been appended.
  EXPECT_THAT(&buffer_.data()[sizeof(kTruncWords)], StrEq(kLastWords));
}

TEST_F(AutoFreeBufferTest, PrintFTest) {
  constexpr char kFormatString[] = "Printf %s %d %03d %02x Test.";
  constexpr char kParam1[] = "string";
  constexpr int kParam2 = 1234;
  constexpr int kParam3 = 7;
  constexpr int kParam4 = 0x42;

  char temp_buffer[1024];
  size_t vsize = snprintf(&temp_buffer[0], sizeof(temp_buffer),
                          kFormatString, kParam1, kParam2, kParam3, kParam4);

  // Test 1: no reservation => allocate buffer.
  EXPECT_EQ(vsize,
            buffer_.PrintF(kFormatString, kParam1, kParam2, kParam3, kParam4));
  // Check for size + null termination.
  EXPECT_EQ(vsize + 1, buffer_.size());
  EXPECT_THAT(buffer_.data(), StrEq(temp_buffer));

  size_t reservation = buffer_.reserve_size();

  buffer_.Clear();

  // Test 2: buffer reserved: just print and return.
  EXPECT_EQ(vsize,
            buffer_.PrintF(kFormatString, kParam1, kParam2, kParam3, kParam4));
  // Check for size + null termination.
  EXPECT_EQ(vsize + 1, buffer_.size());
  EXPECT_THAT(buffer_.data(), StrEq(temp_buffer));
  EXPECT_EQ(reservation, buffer_.reserve_size());
}

}  // namespace test
