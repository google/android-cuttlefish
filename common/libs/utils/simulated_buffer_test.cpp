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
#include "common/libs/utils/simulated_buffer.h"
#include <gtest/gtest.h>

using cvd::time::MonotonicTimePoint;
using cvd::time::MonotonicTimePointFactory;
using cvd::time::Seconds;
using cvd::time::Milliseconds;
using cvd::time::Nanoseconds;
using cvd::time::kNanosecondsPerSecond;

class MockTimepointFactory : public MonotonicTimePointFactory {
 public:
  virtual void FetchCurrentTime(MonotonicTimePoint* dest) const override {
    *dest = system_time_;
  }

  void SetTime(const MonotonicTimePoint& in) {
    system_time_ = in;
  }

 protected:
  MonotonicTimePoint system_time_;
};

template <typename T> class MockSimulatedBuffer : public T {
 public:
  MockSimulatedBuffer(
      int64_t sample_rate,
      int64_t capacity,
      MockTimepointFactory* factory) :
      T(sample_rate, capacity, factory),
      factory_(factory) { }

  void FetchCurrentTime(MonotonicTimePoint* dest) const {
    return factory_->FetchCurrentTime(dest);
  }

  void SleepUntilTime(const MonotonicTimePoint& tick) {
    factory_->SetTime(tick);
  }

 protected:
  // Save a redundant pointer to avoid downcasting
  MockTimepointFactory* factory_;
};

static const int64_t kItemRate = 48000;
static const int64_t kBufferCapacity = 4800;

class SimulatedBufferTest : public ::testing::Test {
 public:
  MockTimepointFactory clock;
  MockSimulatedBuffer<SimulatedBufferBase> buffer;

  SimulatedBufferTest() : buffer(kItemRate, kBufferCapacity, &clock) { }
};

TEST_F(SimulatedBufferTest, TimeMocking) {
  // Ensure that the mocked clock starts at the epoch.
  MonotonicTimePoint epoch_time;
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(epoch_time, actual_time);

  // Ensure that sleeping works
  MonotonicTimePoint test_time = actual_time + Seconds(10);
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);

  // Try one more sleep to make sure that time moves forward
  test_time += Seconds(5);
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
}

TEST_F(SimulatedBufferTest, ItemScaling) {
  // Make certain that we start at item 0
  EXPECT_EQ(0, buffer.GetCurrentItemNum());

  // Make certain that the expected number of items appear in 1 second
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  MonotonicTimePoint test_time = actual_time + Seconds(1);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(kItemRate, buffer.GetCurrentItemNum());

  // Sleep an additional 10 seconds to make certain that the item numbers
  // increment
  test_time += Seconds(10);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(11 * kItemRate, buffer.GetCurrentItemNum());

  // Make certain that partial seconds work
  test_time += Milliseconds(1500);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(12.5 * kItemRate, buffer.GetCurrentItemNum());

  // Make certain that we don't get new items when paused
  buffer.SetPaused(true);
  test_time += Seconds(10);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(12.5 * kItemRate, buffer.GetCurrentItemNum());

  // Make certain that we start getting items when pausing stops
  buffer.SetPaused(false);
  test_time += Milliseconds(500);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(13 * kItemRate, buffer.GetCurrentItemNum());
}

TEST_F(SimulatedBufferTest, ItemSleeping) {
  // See if sleeping on an time causes the right amount of time to pass
  EXPECT_EQ(0, buffer.GetCurrentItemNum());
  MonotonicTimePoint base_time;
  buffer.FetchCurrentTime(&base_time);

  // Wait for 1500ms worth of samples
  buffer.SleepUntilItem(kItemRate * 1500 / 1000);
  EXPECT_EQ(kItemRate * 1500 / 1000, buffer.GetCurrentItemNum());
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(1500, Milliseconds(actual_time - base_time).count());

  // Now wait again for more samples
  buffer.SleepUntilItem(kItemRate * 2500 / 1000);
  EXPECT_EQ(kItemRate * 2500 / 1000, buffer.GetCurrentItemNum());
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(2500, Milliseconds(actual_time - base_time).count());
}

class OutputBufferTest : public ::testing::Test {
 public:
  MockTimepointFactory clock;
  MockSimulatedBuffer<SimulatedOutputBuffer> buffer;

  OutputBufferTest() : buffer(kItemRate, kBufferCapacity, &clock) { }
};

TEST_F(OutputBufferTest, NonBlockingQueueing) {
  int64_t half_buffer = kBufferCapacity / 2;
  EXPECT_EQ(0, buffer.GetCurrentItemNum());

  // Filling half of the buffer should not block
  MonotonicTimePoint test_time;
  buffer.FetchCurrentTime(&test_time);
  EXPECT_EQ(half_buffer, buffer.AddToOutputBuffer(half_buffer, false));
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(half_buffer, buffer.GetOutputBufferSize());

  // Filling all but one entry of the buffer should not block
  EXPECT_EQ(half_buffer - 1,
            buffer.AddToOutputBuffer(half_buffer - 1, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity - 1, buffer.GetOutputBufferSize());

  // Filling the entire buffer should not block
  EXPECT_EQ(1, buffer.AddToOutputBuffer(half_buffer, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(actual_time, test_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // The buffer should reject additional data but not block
  EXPECT_EQ(0, buffer.AddToOutputBuffer(half_buffer, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // One quarter of the buffer should drain in the expected time
  Nanoseconds quarter_drain_time(
      kBufferCapacity / 4 * kNanosecondsPerSecond / kItemRate);
  test_time += quarter_drain_time;
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(actual_time, test_time);
  EXPECT_EQ(kBufferCapacity * 3 / 4, buffer.GetOutputBufferSize());

  // The buffer should now accept new data without blocking
  EXPECT_EQ(kBufferCapacity / 4,
            buffer.AddToOutputBuffer(half_buffer, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // Now that the buffer is full it should reject additional data but
  // not block
  EXPECT_EQ(0, buffer.AddToOutputBuffer(half_buffer, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // Wait for 3/4 of the buffer to drain
  test_time += Nanoseconds(3 * quarter_drain_time.count());
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity / 4, buffer.GetOutputBufferSize());

  // The entire buffer should drain on schedule
  test_time += Nanoseconds(quarter_drain_time.count() - 1);
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(1, buffer.GetOutputBufferSize());
  test_time += Nanoseconds(1);
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetOutputBufferSize());

  // It should be possible to fill the buffer in a single shot
  EXPECT_EQ(kBufferCapacity,
            buffer.AddToOutputBuffer(kBufferCapacity, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // The buffer shouldn't accept additional data but shouldn't block
  EXPECT_EQ(0, buffer.AddToOutputBuffer(1, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // The buffer should underflow sanely
  test_time += Nanoseconds(6 * quarter_drain_time.count());
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetOutputBufferSize());

  // The underflow shouldn't increase the buffer's capacity
  EXPECT_EQ(kBufferCapacity,
            buffer.AddToOutputBuffer(kBufferCapacity + 1, false));
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
}

TEST_F(OutputBufferTest, BlockingQueueing) {
  int64_t half_buffer = kBufferCapacity / 2;

  // Check the initial setup
  EXPECT_EQ(0, buffer.GetCurrentItemNum());
  MonotonicTimePoint test_time;
  buffer.FetchCurrentTime(&test_time);

  // Filling half the buffer works without blocking
  EXPECT_EQ(half_buffer, buffer.AddToOutputBuffer(half_buffer, true));
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(half_buffer, buffer.GetOutputBufferSize());

  // Filling all but one entry of the buffer also works without blocking
  EXPECT_EQ(half_buffer - 1,
            buffer.AddToOutputBuffer(half_buffer - 1, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity - 1, buffer.GetOutputBufferSize());

  // Putting the last sample into the buffer doesn't block
  EXPECT_EQ(1, buffer.AddToOutputBuffer(1, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // Putting more data into the buffer causes blocking
  EXPECT_EQ(half_buffer, buffer.AddToOutputBuffer(half_buffer, true));
  Nanoseconds half_drain_time(
      ((kBufferCapacity / 2) * kNanosecondsPerSecond + kItemRate - 1) /
      kItemRate);
  Nanoseconds quarter_drain_time(half_drain_time.count() / 2);
  test_time += half_drain_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // The buffer drains as expected
  test_time += quarter_drain_time;
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity * 3 / 4, buffer.GetOutputBufferSize());

  // Overfilling the drained buffer also causes blocking
  EXPECT_EQ(half_buffer, buffer.AddToOutputBuffer(half_buffer, true));
  test_time += quarter_drain_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // The buffer drains on schedule
  test_time += Nanoseconds(half_drain_time.count() * 2 - 1);
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(1, buffer.GetOutputBufferSize());
  test_time += Nanoseconds(1);
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetOutputBufferSize());

  // It's possible to fill the entire output buffer in 1 shot without blocking
  EXPECT_EQ(kBufferCapacity,
            buffer.AddToOutputBuffer(kBufferCapacity, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());

  // Adding a single extra sample causes some blocking
  EXPECT_EQ(1, buffer.AddToOutputBuffer(1, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_LT(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity, buffer.GetOutputBufferSize());
}

class InputBufferTest : public ::testing::Test {
 public:
  MockTimepointFactory clock;
  MockSimulatedBuffer<SimulatedInputBuffer> buffer;

  InputBufferTest() : buffer(kItemRate, kBufferCapacity, &clock) { }
};

TEST_F(InputBufferTest, NonBlockingInput) {
  Nanoseconds quarter_fill_time(kBufferCapacity / 4 * kNanosecondsPerSecond /
                                kItemRate);
  // Verify that the buffer starts empty
  EXPECT_EQ(0, buffer.GetCurrentItemNum());
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(0, buffer.RemoveFromInputBuffer(kBufferCapacity, false));
  EXPECT_EQ(0, buffer.GetLostInputItems());

  // Wait for 1/4 of the buffer to fill
  MonotonicTimePoint test_time = actual_time + quarter_fill_time;
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetLostInputItems());

  // Verify that we can read the samples in two groups
  EXPECT_EQ(kBufferCapacity / 8,
            buffer.RemoveFromInputBuffer(kBufferCapacity / 8, false));
  EXPECT_EQ(kBufferCapacity / 8,
            buffer.RemoveFromInputBuffer(kBufferCapacity, false));

  // Verify that there are no samples left and that we did not block
  EXPECT_EQ(0, buffer.RemoveFromInputBuffer(kBufferCapacity, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);

  // Verify that the buffer fills on schedule
  test_time += Nanoseconds(4 * quarter_fill_time.count() - 1);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(kBufferCapacity - 1,
            buffer.RemoveFromInputBuffer(kBufferCapacity, false));
  test_time += Nanoseconds(1);
  buffer.SleepUntilTime(test_time);
  EXPECT_EQ(1, buffer.RemoveFromInputBuffer(kBufferCapacity, false));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetLostInputItems());

  // Verify that the buffer overflows as expected
  test_time += Nanoseconds(5 * quarter_fill_time.count());
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity / 4, buffer.GetLostInputItems());
  EXPECT_EQ(0, buffer.GetLostInputItems());

  EXPECT_EQ(kBufferCapacity,
            buffer.RemoveFromInputBuffer(2 * kBufferCapacity, false));
  EXPECT_EQ(0, buffer.RemoveFromInputBuffer(kBufferCapacity, false));
}

TEST_F(InputBufferTest, BlockingInput) {
  Nanoseconds quarter_fill_time(kBufferCapacity / 4 * kNanosecondsPerSecond /
                                kItemRate);
  // Verify that the buffer starts empty
  EXPECT_EQ(0, buffer.GetCurrentItemNum());
  MonotonicTimePoint actual_time;
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(0, buffer.GetLostInputItems());

  // Wait for 1/4 of the buffer to fill
  MonotonicTimePoint test_time = actual_time + quarter_fill_time;
  EXPECT_EQ(kBufferCapacity / 4,
            buffer.RemoveFromInputBuffer(kBufferCapacity / 4, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetLostInputItems());

  // Verify that the buffer fills on schedule
  test_time += Nanoseconds(4 * quarter_fill_time.count());
  EXPECT_EQ(kBufferCapacity,
            buffer.RemoveFromInputBuffer(kBufferCapacity, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(0, buffer.GetLostInputItems());

  // Verify that the buffer overflows as expected
  test_time += Nanoseconds(5 * quarter_fill_time.count());
  buffer.SleepUntilTime(test_time);
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
  EXPECT_EQ(kBufferCapacity / 4, buffer.GetLostInputItems());
  EXPECT_EQ(0, buffer.GetLostInputItems());
  EXPECT_EQ(kBufferCapacity,
            buffer.RemoveFromInputBuffer(kBufferCapacity, true));
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);

  // Verify that reads bigger than the buffer work as expected
  test_time += Nanoseconds(8 * quarter_fill_time.count());
  EXPECT_EQ(kBufferCapacity * 2,
            buffer.RemoveFromInputBuffer(kBufferCapacity * 2, true));
  EXPECT_EQ(0, buffer.GetLostInputItems());
  buffer.FetchCurrentTime(&actual_time);
  EXPECT_EQ(test_time, actual_time);
}
