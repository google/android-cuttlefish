/*
 * Copyright (C) 2017 The Android Open Source Project
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
#pragma once

#include <stdint.h>
#include <unistd.h>
#include <time.h>

#include "common/libs/time/monotonic_time.h"

/**
 * This abstract class simulates a buffer that either fills or empties at
 * a specified rate.
 *
 * The simulated buffer automatically fills or empties at a specific rate.
 *
 * An item is the thing contained in the simulated buffer. Items are moved
 * in and out of the buffer without subdivision.
 *
 * An integral number of items must arrive / depart in each second.
 * This number is stored in items_per_second_
 *
 * items_per_second * 2000000000 must fit within an int64_t. This
 * works if items_per_second is represented by an int32.
 *
 * The base class does have the concept of capacity, but doesn't use it.
 * It is included here to simplify unit testing.
 *
 * For actual use, see SimulatedInputBuffer and SimulatedOutputBuffer below.
 */
class SimulatedBufferBase {
 public:
  static inline int64_t divide_and_round_up(int64_t q, int64_t d) {
    return q / d + ((q % d) != 0);
  }

  SimulatedBufferBase(
      int32_t items_per_second,
      int64_t simulated_item_capacity,
      cvd::time::MonotonicTimePointFactory* clock =
        cvd::time::MonotonicTimePointFactory::GetInstance()) :
    clock_(clock),
    current_item_num_(0),
    base_item_num_(0),
    simulated_item_capacity_(simulated_item_capacity),
    items_per_second_(items_per_second),
    initialize_(true),
    paused_(false) { }

  virtual ~SimulatedBufferBase() { }

  int64_t GetCurrentItemNum() {
    Update();
    return current_item_num_;
  }

  const cvd::time::MonotonicTimePoint GetLastUpdatedTime() const {
    return current_time_;
  }

  // Sleep for the given amount of time. Subclasses may override this to use
  // different sleep calls.
  // Sleep is best-effort. The code assumes that the acutal sleep time may be
  // greater or less than the time requested.
  virtual void SleepUntilTime(const cvd::time::MonotonicTimePoint& in) {
    struct timespec ts;
    in.ToTimespec(&ts);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
  }

  // The time counter may not start at 0. Concrete classes should call this
  // to allow the buffer simulation to read the current time number and
  // initialize its internal state.
  virtual void Init() {
    if (initialize_) {
      clock_->FetchCurrentTime(&base_time_);
      current_time_ = base_time_;
      initialize_ = false;
    }
  }

  virtual void Update() {
    if (initialize_) {
      Init();
    }
    cvd::time::MonotonicTimePoint now;
    clock_->FetchCurrentTime(&now);
    // We can't call FetchCurrentTime() in the constuctor because a subclass may
    // want to override it, so we initialze the times to 0. If we detect this
    // case go ahead and initialize to a current timestamp.
    if (paused_) {
      base_time_ += now - current_time_;
      current_time_ = now;
      return;
    }
    // Avoid potential overflow by limiting the scaling to one time second.
    // There is no round-off error here because the bases are adjusted for full
    // seconds.
    // There is no issue with int64 overflow because 2's compliment subtraction
    // is immune to overflow.
    // However, this does assume that kNanosecondsPerSecond * items_per_second_
    // fits in an int64.
    cvd::time::Seconds seconds(now - base_time_);
    base_time_ += seconds;
    base_item_num_ += seconds.count() * items_per_second_;
    current_time_ = now;
    current_item_num_ =
        cvd::time::Nanoseconds(now - base_time_).count() *
        items_per_second_ / cvd::time::kNanosecondsPerSecond +
        base_item_num_;
  }

  // If set to true new items will not be created.
  bool SetPaused(bool new_state) {
    bool rval = paused_;
    Update();
    paused_ = new_state;
    return rval;
  }

  // Calculate the TimePoint that corresponds to an item.
  // Caution: This may not return a correct time for items in the past.
  cvd::time::MonotonicTimePoint CalculateItemTime(int64_t item) {
    int64_t seconds = (item - base_item_num_) / items_per_second_;
    int64_t new_base_item_num = base_item_num_ + seconds * items_per_second_;
    return base_time_ + cvd::time::Seconds(seconds) +
      cvd::time::Nanoseconds(divide_and_round_up(
          (item - new_base_item_num) *
          cvd::time::kNanosecondsPerSecond,
          items_per_second_));
  }

  // Sleep until the given item number is generated. If the generator is
  // paused unpause it to make the sleep finite.
  void SleepUntilItem(int64_t item) {
    if (paused_) {
      SetPaused(false);
    }
    cvd::time::MonotonicTimePoint desired_time =
        CalculateItemTime(item);
    while (1) {
      Update();
      if (current_item_num_ - item >= 0) {
        return;
      }
      SleepUntilTime(desired_time);
    }
  }

 protected:
  // Source of the timepoints.
  cvd::time::MonotonicTimePointFactory* clock_;
  // Time when the other values in the structure were updated.
  cvd::time::MonotonicTimePoint current_time_;
  // Most recent time when there was no round-off error between the clock and
  // items.
  cvd::time::MonotonicTimePoint base_time_;
  // Number of the current item.
  int64_t current_item_num_;
  // Most recent item number where there was no round-off error between the
  // clock and items.
  int64_t base_item_num_;
  // Simulated_Item_Capacity of the buffer in items.
  int64_t simulated_item_capacity_;
  // Number of items that are created in 1s. A typical number would be 48000.
  int32_t items_per_second_;
  bool initialize_;
  // If true then don't generate new items.
  bool paused_;
};

/**
 * This is a simulation of an output buffer that drains at a constant rate.
 */
class SimulatedOutputBuffer : public SimulatedBufferBase {
 public:
  SimulatedOutputBuffer(
      int64_t item_rate,
      int64_t simulated_item_capacity,
      cvd::time::MonotonicTimePointFactory* clock =
        cvd::time::MonotonicTimePointFactory::GetInstance()) :
      SimulatedBufferBase(item_rate, simulated_item_capacity, clock) {
    output_buffer_item_num_ = current_item_num_;
  }

  void Update() override {
    SimulatedBufferBase::Update();
    if ((output_buffer_item_num_ - current_item_num_) < 0) {
      // We ran out of items at some point in the past. However, the
      // output capactiy can't be negative.
      output_buffer_item_num_ = current_item_num_;
    }
  }

  int64_t AddToOutputBuffer(int64_t num_new_items, bool block) {
    Update();
    // The easy case: num_new_items fit in the bucket.
    if ((output_buffer_item_num_ + num_new_items - current_item_num_) <=
        simulated_item_capacity_) {
      output_buffer_item_num_ += num_new_items;
      return num_new_items;
    }
    // If we're non-blocking accept enough items to fill the output.
    if (!block) {
      int64_t used = current_item_num_ + simulated_item_capacity_ -
          output_buffer_item_num_;
      output_buffer_item_num_ = current_item_num_ + simulated_item_capacity_;
      return used;
    }
    int64_t new_output_buffer_item_num = output_buffer_item_num_ + num_new_items;
    SleepUntilItem(new_output_buffer_item_num - simulated_item_capacity_);
    output_buffer_item_num_ = new_output_buffer_item_num;
    return num_new_items;
  }

  int64_t GetNextOutputBufferItemNum() {
    Update();
    return output_buffer_item_num_;
  }

  cvd::time::MonotonicTimePoint GetNextOutputBufferItemTime() {
    Update();
    return CalculateItemTime(output_buffer_item_num_);
  }

  int64_t GetOutputBufferSize() {
    Update();
    return output_buffer_item_num_ - current_item_num_;
  }

  void Drain() {
    SleepUntilItem(output_buffer_item_num_);
  }

 protected:
  int64_t output_buffer_item_num_;
};

/**
 * Simulates an input buffer that fills at a constant rate.
 */
class SimulatedInputBuffer : public SimulatedBufferBase {
 public:
  SimulatedInputBuffer(
      int64_t item_rate,
      int64_t simulated_item_capacity,
      cvd::time::MonotonicTimePointFactory* clock =
        cvd::time::MonotonicTimePointFactory::GetInstance()) :
      SimulatedBufferBase(item_rate, simulated_item_capacity, clock) {
    input_buffer_item_num_ = current_item_num_;
    lost_input_items_ = 0;
  }

  void Update() override {
    SimulatedBufferBase::Update();
    if ((current_item_num_ - input_buffer_item_num_) >
        simulated_item_capacity_) {
      // The buffer overflowed at some point in the past. Account for the lost
      // times.
      int64_t new_input_buffer_item_num =
          current_item_num_ - simulated_item_capacity_;
      lost_input_items_ +=
          new_input_buffer_item_num - input_buffer_item_num_;
      input_buffer_item_num_ = new_input_buffer_item_num;
    }
  }

  int64_t RemoveFromInputBuffer(int64_t num_items_wanted, bool block) {
    Update();
    if (!block) {
      int64_t num_items_available = current_item_num_ - input_buffer_item_num_;
      if (num_items_available < num_items_wanted) {
        input_buffer_item_num_ += num_items_available;
        return num_items_available;
      } else {
        input_buffer_item_num_ += num_items_wanted;
        return num_items_wanted;
      }
    }
    // Calculate the item number that is being claimed. Sleep until it appears.
    // Advancing input_buffer_item_num_ causes a negative value to be compared
    // to the capacity, effectively disabling the overflow detection code
    // in Update().
    input_buffer_item_num_ += num_items_wanted;
    while (input_buffer_item_num_ - current_item_num_ > 0) {
      SleepUntilItem(input_buffer_item_num_);
    }
    return num_items_wanted;
  }

  int64_t GetLostInputItems() {
    Update();
    int64_t rval = lost_input_items_;
    lost_input_items_ = 0;
    return rval;
  }

 protected:
  int64_t input_buffer_item_num_;
  int64_t lost_input_items_;
};

