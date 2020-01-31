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

#include "guest/hals/hwcomposer/common/stats_keeper.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include <log/log.h>

#include "guest/hals/hwcomposer/common/geometry_utils.h"

using cvd::time::Microseconds;
using cvd::time::MonotonicTimePoint;
using cvd::time::Nanoseconds;
using cvd::time::Seconds;
using cvd::time::TimeDifference;

namespace cvd {

namespace {

// These functions assume that there is at least one suitable element inside
// the multiset.
template <class T>
void MultisetDeleteOne(std::multiset<T>* mset, const T& key) {
  mset->erase(mset->find(key));
}
template <class T>
const T& MultisetMin(const std::multiset<T>& mset) {
  return *mset.begin();
}
template <class T>
const T& MultisetMax(const std::multiset<T>& mset) {
  return *mset.rbegin();
}

void TimeDifferenceToTimeSpec(const TimeDifference& td, timespec* ts) {
  ts->tv_sec = td.seconds();
  ts->tv_nsec = td.subseconds_in_ns();
}

}  // namespace

void StatsKeeper::GetLastCompositionStats(CompositionStats* stats_p) {
  if (stats_p) {
    TimeDifferenceToTimeSpec(last_composition_stats_.prepare_start.SinceEpoch(),
                             &stats_p->prepare_start);
    TimeDifferenceToTimeSpec(last_composition_stats_.prepare_end.SinceEpoch(),
                             &stats_p->prepare_end);
    TimeDifferenceToTimeSpec(last_composition_stats_.set_start.SinceEpoch(),
                             &stats_p->set_start);
    TimeDifferenceToTimeSpec(last_composition_stats_.set_end.SinceEpoch(),
                             &stats_p->set_end);
    TimeDifferenceToTimeSpec(last_composition_stats_.last_vsync.SinceEpoch(),
                             &stats_p->last_vsync);

    stats_p->num_prepare_calls = last_composition_stats_.num_prepare_calls;
    stats_p->num_layers = last_composition_stats_.num_layers;
    stats_p->num_hwcomposited_layers = last_composition_stats_.num_hwc_layers;
  }
}

StatsKeeper::StatsKeeper(TimeDifference timespan, int64_t vsync_base,
                         int32_t vsync_period)
    : period_length_(timespan, 1),
      vsync_base_(vsync_base),
      vsync_period_(vsync_period),
      num_layers_(0),
      num_hwcomposited_layers_(0),
      num_prepare_calls_(0),
      num_set_calls_(0),
      prepare_call_total_time_(0),
      set_call_total_time_(0),
      total_layers_area(0),
      total_invisible_area(0) {
  last_composition_stats_.num_prepare_calls = 0;
}

StatsKeeper::~StatsKeeper() {}

void StatsKeeper::RecordPrepareStart(int num_layers) {
  last_composition_stats_.num_layers = num_layers;
  last_composition_stats_.num_prepare_calls++;
  num_prepare_calls_++;
  last_composition_stats_.prepare_start = MonotonicTimePoint::Now();
  // Calculate the (expected) time of last VSYNC event. We can only make a guess
  // about it because the vsync thread could run late or surfaceflinger could
  // run late and call prepare from a previous vsync cycle.
  int64_t last_vsync =
      Nanoseconds(last_composition_stats_.set_start.SinceEpoch()).count();
  last_vsync -= (last_vsync - vsync_base_) % vsync_period_;
  last_composition_stats_.last_vsync =
      MonotonicTimePoint() + Nanoseconds(last_vsync);
}

void StatsKeeper::RecordPrepareEnd(int num_hwcomposited_layers) {
  last_composition_stats_.prepare_end = MonotonicTimePoint::Now();
  last_composition_stats_.num_hwc_layers = num_hwcomposited_layers;
}

void StatsKeeper::RecordSetStart() {
  last_composition_stats_.set_start = MonotonicTimePoint::Now();
}

void StatsKeeper::RecordSetEnd() {
  last_composition_stats_.set_end = MonotonicTimePoint::Now();
  std::lock_guard lock(mutex_);
  num_set_calls_++;
  while (!raw_composition_data_.empty() &&
         period_length_ < last_composition_stats_.set_end -
                              raw_composition_data_.front().time_point()) {
    const CompositionData& front = raw_composition_data_.front();

    num_prepare_calls_ -= front.num_prepare_calls();
    --num_set_calls_;
    num_layers_ -= front.num_layers();
    num_hwcomposited_layers_ -= front.num_hwcomposited_layers();
    prepare_call_total_time_ =
        Nanoseconds(prepare_call_total_time_ - front.prepare_time());
    set_call_total_time_ =
        Nanoseconds(set_call_total_time_ - front.set_calls_time());

    MultisetDeleteOne(&prepare_calls_per_set_calls_, front.num_prepare_calls());
    MultisetDeleteOne(&layers_per_compositions_, front.num_layers());
    MultisetDeleteOne(&prepare_call_times_, front.prepare_time());
    MultisetDeleteOne(&set_call_times_, front.set_calls_time());
    if (front.num_hwcomposited_layers() != 0) {
      MultisetDeleteOne(
          &set_call_times_per_hwcomposited_layer_ns_,
          front.set_calls_time().count() / front.num_hwcomposited_layers());
    }

    raw_composition_data_.pop_front();
  }
  Nanoseconds last_prepare_call_time_(last_composition_stats_.prepare_end -
                                      last_composition_stats_.prepare_start);
  Nanoseconds last_set_call_total_time_(last_composition_stats_.set_end -
                                        last_composition_stats_.set_start);
  raw_composition_data_.push_back(
      CompositionData(last_composition_stats_.set_end,
                      last_composition_stats_.num_prepare_calls,
                      last_composition_stats_.num_layers,
                      last_composition_stats_.num_hwc_layers,
                      last_prepare_call_time_, last_set_call_total_time_));

  // There may be several calls to prepare before a call to set, but the only
  // valid call is the last one, so we need to compute these here:
  num_layers_ += last_composition_stats_.num_layers;
  num_hwcomposited_layers_ += last_composition_stats_.num_hwc_layers;
  prepare_call_total_time_ =
      Nanoseconds(prepare_call_total_time_ + last_prepare_call_time_);
  set_call_total_time_ =
      Nanoseconds(set_call_total_time_ + last_set_call_total_time_);
  prepare_calls_per_set_calls_.insert(
      last_composition_stats_.num_prepare_calls);
  layers_per_compositions_.insert(last_composition_stats_.num_layers);
  prepare_call_times_.insert(last_prepare_call_time_);
  set_call_times_.insert(last_set_call_total_time_);
  if (last_composition_stats_.num_hwc_layers != 0) {
    set_call_times_per_hwcomposited_layer_ns_.insert(
        last_set_call_total_time_.count() /
        last_composition_stats_.num_hwc_layers);
  }

  // Reset the counter
  last_composition_stats_.num_prepare_calls = 0;
}

void StatsKeeper::SynchronizedDump(char* buffer, int buffer_size) const {
  std::lock_guard lock(mutex_);
  int chars_written = 0;
// Make sure there is enough space to write the next line
#define bprintf(...)                                                           \
  (chars_written += (chars_written < buffer_size)                              \
                        ? (snprintf(&buffer[chars_written],                    \
                                    buffer_size - chars_written, __VA_ARGS__)) \
                        : 0)

  bprintf("HWComposer stats from the %" PRId64
          " seconds just before the last call to "
          "set() (which happended %" PRId64 " seconds ago):\n",
          Seconds(period_length_).count(),
          Seconds(MonotonicTimePoint::Now() - last_composition_stats_.set_end)
              .count());
  bprintf("  Layer count: %d\n", num_layers_);

  if (num_layers_ == 0 || num_prepare_calls_ == 0 || num_set_calls_ == 0) {
    return;
  }

  bprintf("  Layers composited by hwcomposer: %d (%d%%)\n",
          num_hwcomposited_layers_,
          100 * num_hwcomposited_layers_ / num_layers_);
  bprintf("  Number of calls to prepare(): %d\n", num_prepare_calls_);
  bprintf("  Number of calls to set(): %d\n", num_set_calls_);
  if (num_set_calls_ > 0) {
    bprintf(
        "  Maximum number of calls to prepare() before a single call to set(): "
        "%d\n",
        MultisetMax(prepare_calls_per_set_calls_));
  }
  bprintf("  Time spent on prepare() (in microseconds):\n    max: %" PRId64
          "\n    "
          "average: %" PRId64 "\n    min: %" PRId64 "\n    total: %" PRId64
          "\n",
          Microseconds(MultisetMax(prepare_call_times_)).count(),
          Microseconds(prepare_call_total_time_).count() / num_prepare_calls_,
          Microseconds(MultisetMin(prepare_call_times_)).count(),
          Microseconds(prepare_call_total_time_).count());
  bprintf("  Time spent on set() (in microseconds):\n    max: %" PRId64
          "\n    average: "
          "%" PRId64 "\n    min: %" PRId64 "\n    total: %" PRId64 "\n",
          Microseconds(MultisetMax(set_call_times_)).count(),
          Microseconds(set_call_total_time_).count() / num_set_calls_,
          Microseconds(MultisetMin(set_call_times_)).count(),
          Microseconds(set_call_total_time_).count());
  if (num_hwcomposited_layers_ > 0) {
    bprintf(
        "  Per layer compostition time:\n    max: %" PRId64
        "\n    average: %" PRId64
        "\n    "
        "min: %" PRId64 "\n",
        Microseconds(MultisetMax(set_call_times_per_hwcomposited_layer_ns_))
            .count(),
        Microseconds(set_call_total_time_).count() / num_hwcomposited_layers_,
        Microseconds(MultisetMin(set_call_times_per_hwcomposited_layer_ns_))
            .count());
  }
  bprintf("Statistics from last 100 compositions:\n");
  bprintf("  Total area: %" PRId64 " square pixels\n", total_layers_area);
  if (total_layers_area != 0) {
    bprintf(
        "  Total invisible area: %" PRId64 " square pixels, %" PRId64 "%%\n",
        total_invisible_area, 100 * total_invisible_area / total_layers_area);
  }
#undef bprintf
}

}  // namespace cvd
