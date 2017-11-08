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

#ifndef GCE_HWCOMPOSER_STATS_H_
#define GCE_HWCOMPOSER_STATS_H_

#include <GceFrameBufferControl.h>
#include <MonotonicTime.h>
#include <Pthread.h>
#include <android-base/thread_annotations.h>
#include <deque>
#include <set>

#include "hwcomposer_common.h"

namespace avd {

class CompositionData {
 public:
  CompositionData(avd::time::MonotonicTimePoint time_point,
                  int num_prepares, int num_layers, int num_hwcomposited_layers,
                  avd::time::Nanoseconds prepare_time,
                  avd::time::Nanoseconds set_calls_time)
      : time_point_(time_point),
        num_prepare_calls_(num_prepares),
        num_layers_(num_layers),
        num_hwcomposited_layers_(num_hwcomposited_layers),
        prepare_time_(prepare_time),
        set_calls_time_(set_calls_time) {}

  avd::time::MonotonicTimePoint time_point() const {
    return time_point_;
  }

  int num_prepare_calls() const { return num_prepare_calls_; }

  int num_layers() const { return num_layers_; }

  int num_hwcomposited_layers() const { return num_hwcomposited_layers_; }

  avd::time::Nanoseconds prepare_time() const {
    return prepare_time_;
  }

  avd::time::Nanoseconds set_calls_time() const {
    return set_calls_time_;
  }

 private:
  avd::time::MonotonicTimePoint time_point_;
  int num_prepare_calls_;
  int num_layers_;
  int num_hwcomposited_layers_;
  avd::time::Nanoseconds prepare_time_;
  avd::time::Nanoseconds set_calls_time_;
};

class StatsKeeper {
 public:
  // The timespan parameter indicates for how long we keep stats about the past
  // compositions.
  StatsKeeper(avd::time::TimeDifference timespan,
              int64_t vsync_base,
              int32_t vsync_period);
  StatsKeeper();
  ~StatsKeeper();

  // Record the time at which a call to prepare was made, takes the number of
  // layers received (excluding the framebuffer) as a parameter.
  void RecordPrepareStart(int num_layers);
  // Record the time at which a call to prepare (was about to) returned, takes
  // the number of layers marked for hardware composition as a parameter.
  void RecordPrepareEnd(int num_hwcomposited_layers);
  void RecordSetStart();
  void RecordSetEnd() EXCLUDES(mutex_);

  const CompositionStats& last_composition_stats() { return last_composition_stats_; }

  // Calls to this function are synchronized with calls to 'RecordSetEnd' with a
  // mutex. The other Record* functions do not need such synchronization because
  // they access last_* variables only, which are not read by 'Dump'.
  void SynchronizedDump(char* buffer, int buffer_size) const EXCLUDES(mutex_);

 private:

  avd::time::TimeDifference period_length_;

  // Base and period of the VSYNC signal, allows to accurately calculate the
  // time of the last vsync broadcast.
  int64_t vsync_base_;
  int32_t vsync_period_;
  // Data collected about ongoing composition. These variables are not accessed
  // from Dump(), so they don't need to be guarded by a mutex.
  CompositionStats last_composition_stats_;

  // Aggregated performance data collected from past compositions. These
  // variables are modified when a composition is completed and when old
  // compositions need to be discarded in RecordSetEnd(), and is accessed from
  // Dump(). Non-aggregated data is kept in the raw_composition_data_ deque to
  // be able to discard old values from the aggregated data.
  int num_layers_ GUARDED_BY(mutex_);
  int num_hwcomposited_layers_ GUARDED_BY(mutex_);
  int num_prepare_calls_ GUARDED_BY(mutex_);
  int num_set_calls_ GUARDED_BY(mutex_);
  avd::time::Nanoseconds prepare_call_total_time_ GUARDED_BY(mutex_);
  avd::time::Nanoseconds set_call_total_time_ GUARDED_BY(mutex_);
  // These are kept in multisets to be able to calculate mins and maxs of
  // changing sets of (not necessarily different) values.
  std::multiset<int> prepare_calls_per_set_calls_ GUARDED_BY(mutex_);
  std::multiset<int> layers_per_compositions_ GUARDED_BY(mutex_);
  std::multiset<avd::time::Nanoseconds> prepare_call_times_
      GUARDED_BY(mutex_);
  std::multiset<avd::time::Nanoseconds> set_call_times_
      GUARDED_BY(mutex_);
  std::multiset<int64_t> set_call_times_per_hwcomposited_layer_ns_
      GUARDED_BY(mutex_);

  // Time-ordered list of compositions, used to update the global aggregated
  // performance data when old compositions fall out of the period of interest.
  std::deque<CompositionData> raw_composition_data_ GUARDED_BY(mutex_);

  // TODO(jemoreira): Add min/max/average composition times per layer area units

  std::deque<std::pair<int64_t, int64_t> > composition_areas GUARDED_BY(mutex_);
  int64_t total_layers_area GUARDED_BY(mutex_);
  int64_t total_invisible_area GUARDED_BY(mutex_);

  // Controls access to data from past compositions.
  mutable avd::Mutex mutex_;
};

template <class Composer>
class StatsKeepingComposer {
 public:
  // Keep stats from the last 10 seconds.
  StatsKeepingComposer(int64_t vsync_base_timestamp, int32_t vsync_period_ns)
      : composer_(vsync_base_timestamp, vsync_period_ns),
        stats_keeper_(avd::time::TimeDifference(avd::time::Seconds(10), 1),
                      vsync_base_timestamp,
                      vsync_period_ns) {
    // Don't let the composer broadcast by itself, allow it to return to collect
    // the timings and broadcast then.
    composer_.ReplaceFbBroadcaster(NULL);
  }
  ~StatsKeepingComposer() {}

  int PrepareLayers(size_t num_layers, gce_hwc_layer* layers) {
    stats_keeper_.RecordPrepareStart(num_layers);
    int num_hwc_layers = composer_.PrepareLayers(num_layers, layers);
    stats_keeper_.RecordPrepareEnd(num_hwc_layers);
    return num_hwc_layers;
  }

  int SetLayers(size_t num_layers, gce_hwc_layer* layers) {
    stats_keeper_.RecordSetStart();
    int yoffset = composer_.SetLayers(num_layers, layers);
    stats_keeper_.RecordSetEnd();
    if (yoffset >= 0) {
      GceFrameBufferControl::getInstance().BroadcastFrameBufferChanged(
          yoffset, &stats_keeper_.last_composition_stats());
    } else {
      ALOGE("%s: Error on SetLayers(), yoffset: %d", __FUNCTION__, yoffset);
    }
    return yoffset;
  }

  void Dump(char* buff, int buff_len) {
    stats_keeper_.SynchronizedDump(buff, buff_len);
  }

 private:
  StatsKeeper stats_keeper_;
  Composer composer_;
};

}  // namespace avd

#endif
