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
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include <utils/String8.h>

#define LOG_TAG "GceFrameBufferControl"
#include <cutils/log.h>
#include <system/graphics.h>

#include "GceFrameBufferControl.h"
#include "gralloc_gce_priv.h"

enum { NOT_YET = 0, IN_PROGRESS, DONE };

struct FrameBufferControl {
  pthread_mutex_t mutex;
  pthread_cond_t cond_var;
  uint32_t seq_num;
  volatile int yoffset;
  volatile int initialized;
  volatile uint32_t buffer_bits;
  CompositionStats stats;
};

// __sync_lock_test_and_set is described to work on intel, but not on many other
// targets.
#define ATOMICALLY_SET(x, val) __sync_lock_test_and_set(&(x), (val))
#define ATOMICALLY_COMPARE_AND_SWAP(x, old, val) \
  __sync_val_compare_and_swap(&(x), (old), (val))
// fetch the value, don't modify it (or with 0)
#define ATOMICALLY_GET(x) __sync_or_and_fetch(&(x), 0)

const char* const GceFrameBufferControl::kFrameBufferControlPath =
    "/dev/framebuffer_control";

GceFrameBufferControl& GceFrameBufferControl::getInstance() {
  static GceFrameBufferControl instance;
  // If not initialized before and fails to initialize now
  if (!instance.Initialize()) {
    LOG_ALWAYS_FATAL(
        "Unable to initialize the framebuffer control structure (%s)... "
        "aborting!",
        strerror(errno));
  }
  return instance;
}

uint32_t GceFrameBufferControl::GetAndSetNextAvailableBufferBit(uint32_t filter) {
  if (pthread_mutex_lock(&control_memory_->mutex)) {
    ALOGE("Failed to acquire lock on framebuffer control mutex (%s) - %s",
          strerror(errno), __FUNCTION__);
    return 0;
  }
  uint32_t bit = control_memory_->buffer_bits;
  bit &= filter;
  if (bit == filter) {
    // All bits in the filter are already set in the set
    bit = 0LU;
  } else {
    // Set available bits to 1
    bit = (bit^filter);
    // isolate first available bit
    bit &= ~bit + 1LU;
    // set it on bit set on shared memory
    control_memory_->buffer_bits |= bit;
  }

  pthread_mutex_unlock(&control_memory_->mutex);
  return bit;
}

int GceFrameBufferControl::UnsetBufferBits(uint32_t bits) {
  if (pthread_mutex_lock(&control_memory_->mutex)) {
    ALOGE("Failed to acquire lock on framebuffer control mutex (%s) - %s",
          strerror(errno), __FUNCTION__);
    return -1;
  }

  control_memory_->buffer_bits &= ~bits;

  pthread_mutex_unlock(&control_memory_->mutex);
  return 0;
}

GceFrameBufferControl::GceFrameBufferControl()
    : control_fd_(-1), control_memory_(NULL) {}

namespace {

bool MapFrameBufferControl(FrameBufferControl** control_memory_ptr,
                           int* fbc_fd) {
  size_t control_size = sizeof(FrameBufferControl);
  mode_t fb_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
  int control_fd;
  if ((control_fd = open(GceFrameBufferControl::kFrameBufferControlPath, O_RDWR,
                         fb_mode)) < 0) {
    ALOGE("Failed to open framebuffer control at %s (%s)",
          GceFrameBufferControl::kFrameBufferControlPath, strerror(errno));
    return false;
  }

  if (ftruncate(control_fd, sizeof(FrameBufferControl)) < 0) {
    ALOGE("Failed to truncate framebuffer control at %s (%s)",
          GceFrameBufferControl::kFrameBufferControlPath, strerror(errno));
    return false;
  }

  void* control_memory;
  control_memory =
      mmap(0, control_size, PROT_READ | PROT_WRITE, MAP_SHARED, control_fd, 0);
  if (control_memory == MAP_FAILED) {
    ALOGE("Failed to mmap framebuffer control (%s)", strerror(errno));
    close(control_fd);
    return false;
  }

  *control_memory_ptr = reinterpret_cast<FrameBufferControl*>(control_memory);
  *fbc_fd = control_fd;

  return true;
}

void UnmapFrameBufferControl(FrameBufferControl** control_memory_ptr,
                             int* fbc_fd) {
  munmap(*control_memory_ptr, sizeof(FrameBufferControl));
  *control_memory_ptr = NULL;
  close(*fbc_fd);
  *fbc_fd = -1;
}
}

bool GceFrameBufferControl::Initialize() {
  if (control_fd_ >= 0) {
    return true;
  }

  if (!MapFrameBufferControl(&control_memory_, &control_fd_)) {
    return false;
  }

  int initializing_state = ATOMICALLY_COMPARE_AND_SWAP(
      control_memory_->initialized, NOT_YET, IN_PROGRESS);
  switch (initializing_state) {
    case DONE:
      return true;

    case IN_PROGRESS: {  // wait 1 sec and try again
      do {
        sleep(1);
        initializing_state = ATOMICALLY_GET(control_memory_->initialized);
        if (initializing_state != DONE) {
          ALOGW(
              "Framebuffer control structure has not yet been initialized "
              "after one second. Value of initialized flag: %d",
              initializing_state);
        }
      } while (initializing_state != DONE);
      return true;
    }

    case NOT_YET: {  // flag set to IN_PROGRESS, proceed to initialize
      pthread_mutexattr_t mutex_attr;
      pthread_mutexattr_init(&mutex_attr);
      pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
      int retval = pthread_mutex_init(&(control_memory_->mutex), &mutex_attr);
      if (retval) {
        ALOGE("Failed to acquire lock on framebuffer control mutex (%s) - %s",
              strerror(errno), __FUNCTION__);
        UnmapFrameBufferControl(&control_memory_, &control_fd_);
        return false;
      }
      pthread_mutexattr_destroy(&mutex_attr);

      pthread_condattr_t cond_attr;
      pthread_condattr_init(&cond_attr);
      pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
      retval = pthread_cond_init(&(control_memory_->cond_var), &cond_attr);
      if (retval) {
        ALOGE("Failed to initialize cond var for framebuffer control (%s)",
              strerror(errno));
        pthread_mutex_destroy(&(control_memory_->mutex));
        UnmapFrameBufferControl(&control_memory_, &control_fd_);
        return false;
      }
      pthread_condattr_destroy(&cond_attr);

      ATOMICALLY_SET(control_memory_->buffer_bits, 0LU);
      ATOMICALLY_SET(control_memory_->seq_num, 0);
      ATOMICALLY_SET(control_memory_->initialized, DONE);

      return true;
    }

    default: {  // unrecognized value
      ALOGE("Framebuffer control memory is corrupt, initialized = %d",
            initializing_state);
      UnmapFrameBufferControl(&control_memory_, &control_fd_);
      return false;
    }
  }

  return true;
}

int GceFrameBufferControl::GetCurrentYOffset() const {
  if (!control_memory_) return -1;
  return control_memory_->yoffset;
}

int GceFrameBufferControl::WaitForFrameBufferChangeSince(
    uint32_t previous_fb_seq,
    int* yoffset_p,
    uint32_t* fb_seq_p,
    CompositionStats* stats_p) {
  if (pthread_mutex_lock(&control_memory_->mutex)) {
    ALOGE("Failed to acquire lock on framebuffer control mutex (%s) - %s",
          strerror(errno), __FUNCTION__);
    return -1;
  }
  int retval = 0;

  while (control_memory_->seq_num == previous_fb_seq) {
    retval =
        pthread_cond_wait(&control_memory_->cond_var, &control_memory_->mutex);
  }

  if (fb_seq_p) {
    *fb_seq_p = control_memory_->seq_num;
  }
  if (yoffset_p) {
    *yoffset_p = control_memory_->yoffset;
  }
  if (stats_p) {
    *stats_p = control_memory_->stats;
  }

  pthread_mutex_unlock(&control_memory_->mutex);
  return retval;
}

int GceFrameBufferControl::WaitForFrameBufferChange(int* yoffset_p) {
  return WaitForFrameBufferChangeSince(
      control_memory_->seq_num, yoffset_p, NULL, NULL);
}

int GceFrameBufferControl::BroadcastFrameBufferChanged(int yoffset) {
  return BroadcastFrameBufferChanged(yoffset, NULL);
}

// increments the framebuffer sequential number, ensuring it's never zero
static inline uint32_t seq_inc(uint32_t num) {
  ++num;
  return num? num: 1;
}

int GceFrameBufferControl::BroadcastFrameBufferChanged(
    int yoffset, const CompositionStats* stats) {
  if (pthread_mutex_lock(&control_memory_->mutex)) {
    ALOGE("Failed to acquire lock on framebuffer control mutex (%s)",
          strerror(errno));
    return -1;
  }
  control_memory_->yoffset = yoffset;
  control_memory_->seq_num = seq_inc(control_memory_->seq_num);
  if (stats) { control_memory_->stats = *stats; }
  pthread_cond_broadcast(&control_memory_->cond_var);
  pthread_mutex_unlock(&control_memory_->mutex);

  return 0;
}
