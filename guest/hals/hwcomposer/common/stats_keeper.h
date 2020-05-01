#pragma once
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

#include <deque>
#include <mutex>
#include <set>

#include <android-base/thread_annotations.h>

#include "common/libs/time/monotonic_time.h"

#include "guest/hals/hwcomposer/common/base_composer.h"
#include "guest/hals/hwcomposer/common/hwcomposer.h"

namespace cvd {

class CompositionData {
 public:
  CompositionData(cvd::time::MonotonicTimePoint time_point, int num_prepares,
                  int num_layers, int num_hwcomposited_layers,
                  cvd::time::Nanoseconds prepare_time,
                  cvd::time::Nanoseconds set_calls_time)
      : time_point_(time_point),
        num_prepare_calls_(num_prepares),
        num_layers_(num_layers),
        num_hwcomposited_layers_(num_hwcomposited_layers),
        prepare_time_(prepare_time),
        set_calls_time_(set_calls_time) {}

  cvd::time::MonotonicTimePoint time_point() const { return time_point_; }

  int num_prepare_calls() const { return num_prepare_calls_; }

  int num_layers() const { return num_layers_; }

  int num_hwcomposited_layers() const { return num_hwcomposited_layers_; }

  cvd::time::Nanoseconds prepare_time() const { return prepare_time_; }

  cvd::time::Nanoseconds set_calls_time() const { return set_calls_time_; }

 private:
  cvd::time::MonotonicTimePoint time_point_;
  int num_prepare_calls_;
  int num_layers_;
  int num_hwcomposited_layers_;
  cvd::time::Nanoseconds prepare_time_;
  cvd::time::Nanoseconds set_calls_time_;
};

struct HWCCompositionStats {
  cvd::time::MonotonicTimePoint prepare_start;
  cvd::time::MonotonicTimePoint prepare_end;
  cvd::time::MonotonicTimePoint set_start;
  cvd::time::MonotonicTimePoint set_end;
  cvd::time::MonotonicTimePoint last_vsync;
  // There may be more than one call to prepare, the timestamps are with regards
  // to the last one (the one that precedes the set call)
  int num_prepare_calls;
  int num_layers;
  // The number of layers composed by the hwcomposer
  int num_hwc_layers;
};

class StatsKeeper {
 public:
  // The timespan parameter indicates for how long we keep stats about the past
  // compositions.
  StatsKeeper(cvd::time::TimeDifference timespan, int64_t vsync_base,
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

  void GetLastCompositionStats(CompositionStats* stats_p);

  // Calls to this function are synchronized with calls to 'RecordSetEnd' with a
  // mutex. The other Record* functions do not need such synchronization because
  // they access last_* variables only, which are not read by 'Dump'.
  void SynchronizedDump(char* buffer, int buffer_size) const EXCLUDES(mutex_);

 private:
  cvd::time::TimeDifference period_length_;

  // Base and period of the VSYNC signal, allows to accurately calculate the
  // time of the last vsync broadcast.
  int64_t vsync_base_;
  int32_t vsync_period_;
  // Data collected about ongoing composition. These variables are not accessed
  // from Dump(), so they don't need to be guarded by a mutex.
  HWCCompositionStats last_composition_stats_;

  // Aggregated performance data collected from past compositions. These
  // variables are modified when a composition is completed and when old
  // compositions need to be discarded in RecordSetEnd(), and is accessed from
  // Dump(). Non-aggregated data is kept in the raw_composition_data_ deque to
  // be able to discard old values from the aggregated data.
  int num_layers_ GUARDED_BY(mutex_);
  int num_hwcomposited_layers_ GUARDED_BY(mutex_);
  int num_prepare_calls_ GUARDED_BY(mutex_);
  int num_set_calls_ GUARDED_BY(mutex_);
  cvd::time::Nanoseconds prepare_call_total_time_ GUARDED_BY(mutex_);
  cvd::time::Nanoseconds set_call_total_time_ GUARDED_BY(mutex_);
  // These are kept in multisets to be able to calculate mins and maxs of
  // changing sets of (not necessarily different) values.
  std::multiset<int> prepare_calls_per_set_calls_ GUARDED_BY(mutex_);
  std::multiset<int> layers_per_compositions_ GUARDED_BY(mutex_);
  std::multiset<cvd::time::Nanoseconds> prepare_call_times_ GUARDED_BY(mutex_);
  std::multiset<cvd::time::Nanoseconds> set_call_times_ GUARDED_BY(mutex_);
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
  mutable std::mutex mutex_;
};

class WrappedScreenView : public ScreenView {
 public:
  WrappedScreenView(std::unique_ptr<ScreenView> screen_view,
                    std::function<void(CompositionStats*)> stats_getter)
      : screen_view_(std::move(screen_view)), stats_getter_(stats_getter) {}
  virtual ~WrappedScreenView() = default;

  void Broadcast(int buffer_id, const CompositionStats*) override {
    // The composer object in stats_keeper produces null stats, use the ones
    // provided by the stats_keeper instead.
    CompositionStats stats;
    stats_getter_(&stats);
    return screen_view_->Broadcast(buffer_id, &stats);
  }

  void* GetBuffer(int buffer_id) override {
    return screen_view_->GetBuffer(buffer_id);
  }

  int32_t x_res() const override { return screen_view_->x_res(); }

  int32_t y_res() const override { return screen_view_->y_res(); }

  int32_t dpi() const override { return screen_view_->dpi(); }

  int32_t refresh_rate() const override { return screen_view_->refresh_rate(); }

  int num_buffers() const override { return screen_view_->num_buffers(); }

 private:
  std::unique_ptr<ScreenView> screen_view_;
  std::function<void(CompositionStats*)> stats_getter_;
};

template <class Composer>
class StatsKeepingComposer : public BaseComposer {
 public:
  // Keep stats from the last 10 seconds.
  StatsKeepingComposer(int64_t vsync_base_timestamp,
                       std::unique_ptr<ScreenView> screen_view)
      : composer_(std::unique_ptr<ScreenView>(
                      new WrappedScreenView(std::move(screen_view),
                                            [this](CompositionStats* stats) {
                                              FinalizeStatsAndGet(stats);
                                            }))),
        stats_keeper_(cvd::time::TimeDifference(cvd::time::Seconds(10), 1),
                      vsync_base_timestamp, 1e9 / composer_.refresh_rate()) {}
  virtual ~StatsKeepingComposer() = default;

  int PrepareLayers(size_t num_layers, hwc_layer_1_t* layers) override {
    stats_keeper_.RecordPrepareStart(num_layers);
    int num_hwc_layers = composer_.PrepareLayers(num_layers, layers);
    stats_keeper_.RecordPrepareEnd(num_hwc_layers);
    return num_hwc_layers;
  }

  int SetLayers(size_t num_layers, hwc_layer_1_t* layers) override {
    stats_keeper_.RecordSetStart();
    return composer_.SetLayers(num_layers, layers);
  }

  void Dump(char* buff, int buff_len) override {
    stats_keeper_.SynchronizedDump(buff, buff_len);
  }

  void FinalizeStatsAndGet(CompositionStats* stats) {
    stats_keeper_.RecordSetEnd();
    stats_keeper_.GetLastCompositionStats(stats);
  }

 private:
  Composer composer_;
  StatsKeeper stats_keeper_;
};

}  // namespace cvd
