/*
 * Copyright 2022 The Android Open Source Project
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

#include "VsyncThread.h"

#include <utils/ThreadDefs.h>

#include <thread>

#include "Time.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

// Returns the timepoint of the next vsync after the 'now' timepoint that is
// a multiple of 'vsyncPeriod' in-phase/offset-from 'previousSync'.
//
// Some examples:
//  * vsyncPeriod=50ns previousVsync=500ns now=510ns => 550ns
//  * vsyncPeriod=50ns previousVsync=300ns now=510ns => 550ns
//  * vsyncPeriod=50ns previousVsync=500ns now=550ns => 550ns
TimePoint GetNextVsyncInPhase(Nanoseconds vsyncPeriod, TimePoint previousVsync,
                              TimePoint now) {
  const auto elapsed = Nanoseconds(now - previousVsync);
  const auto nextMultiple = (elapsed / vsyncPeriod) + 1;
  return previousVsync + (nextMultiple * vsyncPeriod);
}

}  // namespace

VsyncThread::VsyncThread(int64_t displayId) : mDisplayId(displayId) {
  mPreviousVsync = std::chrono::steady_clock::now() - mVsyncPeriod;
}

VsyncThread::~VsyncThread() { stop(); }

HWC3::Error VsyncThread::start(int32_t vsyncPeriodNanos) {
  DEBUG_LOG("%s for display:%" PRIu64, __FUNCTION__, mDisplayId);

  mVsyncPeriod = Nanoseconds(vsyncPeriodNanos);

  mThread = std::thread([this]() { threadLoop(); });

  // Truncate to 16 chars (15 + null byte) to satisfy pthread_setname_np max
  // name length requirement.
  const std::string name =
      std::string("display_" + std::to_string(mDisplayId) + "_vsync_thread")
          .substr(15);

  int ret = pthread_setname_np(mThread.native_handle(), name.c_str());
  if (ret != 0) {
    ALOGE("%s: failed to set Vsync thread name: %s", __FUNCTION__,
          strerror(ret));
  }

  struct sched_param param = {
      .sched_priority = ANDROID_PRIORITY_DISPLAY,
  };
  ret = pthread_setschedparam(mThread.native_handle(), SCHED_FIFO, &param);
  if (ret != 0) {
    ALOGE("%s: failed to set Vsync thread priority: %s", __FUNCTION__,
          strerror(ret));
  }

  return HWC3::Error::None;
}

HWC3::Error VsyncThread::stop() {
  mShuttingDown.store(true);
  mThread.join();

  return HWC3::Error::None;
}

HWC3::Error VsyncThread::setCallbacks(
    const std::shared_ptr<IComposerCallback>& callback) {
  DEBUG_LOG("%s for display:%" PRIu64, __FUNCTION__, mDisplayId);

  std::unique_lock<std::mutex> lock(mStateMutex);

  mCallbacks = callback;

  return HWC3::Error::None;
}

HWC3::Error VsyncThread::setVsyncEnabled(bool enabled) {
  DEBUG_LOG("%s for display:%" PRIu64 " enabled:%d", __FUNCTION__, mDisplayId,
            enabled);

  std::unique_lock<std::mutex> lock(mStateMutex);

  mVsyncEnabled = enabled;

  return HWC3::Error::None;
}

HWC3::Error VsyncThread::scheduleVsyncUpdate(
    int32_t newVsyncPeriod, const VsyncPeriodChangeConstraints& constraints,
    VsyncPeriodChangeTimeline* outTimeline) {
  DEBUG_LOG("%s for display:%" PRIu64, __FUNCTION__, mDisplayId);

  PendingUpdate update;
  update.period = Nanoseconds(newVsyncPeriod);
  update.updateAfter = asTimePoint(constraints.desiredTimeNanos);

  std::unique_lock<std::mutex> lock(mStateMutex);
  mPendingUpdate.emplace(std::move(update));

  TimePoint nextVsync =
      GetNextVsyncInPhase(mVsyncPeriod, mPreviousVsync, update.updateAfter);

  outTimeline->newVsyncAppliedTimeNanos = asNanosTimePoint(nextVsync);
  outTimeline->refreshRequired = false;
  outTimeline->refreshTimeNanos = 0;

  return HWC3::Error::None;
}

Nanoseconds VsyncThread::updateVsyncPeriodLocked(TimePoint now) {
  if (mPendingUpdate && now > mPendingUpdate->updateAfter) {
    mVsyncPeriod = mPendingUpdate->period;
    mPendingUpdate.reset();
  }

  return mVsyncPeriod;
}

void VsyncThread::threadLoop() {
  ALOGI("Vsync thread for display:%" PRId64 " starting", mDisplayId);

  Nanoseconds vsyncPeriod = mVsyncPeriod;

  int vsyncs = 0;
  TimePoint previousLog = std::chrono::steady_clock::now();

  while (!mShuttingDown.load()) {
    TimePoint now = std::chrono::steady_clock::now();
    TimePoint nextVsync = GetNextVsyncInPhase(vsyncPeriod, mPreviousVsync, now);

    std::this_thread::sleep_until(nextVsync);
    {
      std::unique_lock<std::mutex> lock(mStateMutex);

      mPreviousVsync = nextVsync;

      // Display has finished refreshing at previous vsync period. Update the
      // vsync period if there was a pending update.
      vsyncPeriod = updateVsyncPeriodLocked(mPreviousVsync);
    }

    if (mVsyncEnabled) {
      if (mCallbacks) {
        DEBUG_LOG("%s: for display:%" PRIu64 " calling vsync", __FUNCTION__,
                  mDisplayId);
        mCallbacks->onVsync(mDisplayId, asNanosTimePoint(nextVsync),
                            static_cast<int32_t>(asNanosDuration(vsyncPeriod)));
      }
    }

    static constexpr const int kLogIntervalSeconds = 60;
    if (now > (previousLog + std::chrono::seconds(kLogIntervalSeconds))) {
      DEBUG_LOG("%s: for display:%" PRIu64 " send %" PRIu32
                " in last %d seconds",
                __FUNCTION__, mDisplayId, vsyncs, kLogIntervalSeconds);
      (void)vsyncs;
      previousLog = now;
      vsyncs = 0;
    }
    ++vsyncs;
  }

  ALOGI("Vsync thread for display:%" PRId64 " finished", mDisplayId);
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
