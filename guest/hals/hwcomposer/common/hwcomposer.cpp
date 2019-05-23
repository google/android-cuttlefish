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

#define LOG_TAG "hwc.cf_x86"
#define HWC_REMOVE_DEPRECATED_VERSIONS 1

#include "hwcomposer.h"

#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <string>

#include <log/log.h>

namespace cvd {

void* hwc_vsync_thread(void* data) {
  struct hwc_composer_device_data_t* pdev =
      (struct hwc_composer_device_data_t*)data;
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

  int64_t base_timestamp = pdev->vsync_base_timestamp;
  int64_t last_logged = base_timestamp / 1e9;
  int sent = 0;
  int last_sent = 0;
  static const int log_interval = 60;
  void (*vsync_proc)(const struct hwc_procs*, int, int64_t) = nullptr;
  bool log_no_procs = true, log_no_vsync = true;
  while (true) {
    struct timespec rt;
    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
      LOG_ALWAYS_FATAL("%s:%d error in vsync thread clock_gettime: %s",
        __FILE__, __LINE__, strerror(errno));
    }

    int64_t timestamp = int64_t(rt.tv_sec) * 1e9 + rt.tv_nsec;
    // Given now's timestamp calculate the time of the next timestamp.
    timestamp += pdev->vsync_period_ns -
                 (timestamp - base_timestamp) % pdev->vsync_period_ns;

    rt.tv_sec = timestamp / 1e9;
    rt.tv_nsec = timestamp % static_cast<int32_t>(1e9);
    int err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &rt, NULL);
    if (err == -1) {
      ALOGE("error in vsync thread: %s", strerror(errno));
      if (errno == EINTR) {
        continue;
      }
    }

    // The vsync thread is started on device open, it may run before the
    // registerProcs callback has a chance to be called, so we need to make sure
    // procs is not NULL before dereferencing it.
    if (pdev && pdev->procs) {
      vsync_proc = pdev->procs->vsync;
    } else if (log_no_procs) {
      log_no_procs = false;
      ALOGI("procs is not set yet, unable to deliver vsync event");
    }
    if (vsync_proc) {
      vsync_proc(const_cast<hwc_procs_t*>(pdev->procs), 0, timestamp);
      ++sent;
    } else if (log_no_vsync) {
      log_no_vsync = false;
      ALOGE("vsync callback is null (but procs was already set)");
    }
    if (rt.tv_sec - last_logged > log_interval) {
      ALOGI("Sent %d syncs in %ds", sent - last_sent, log_interval);
      last_logged = rt.tv_sec;
      last_sent = sent;
    }
  }

  return NULL;
}

}  // namespace cvd
