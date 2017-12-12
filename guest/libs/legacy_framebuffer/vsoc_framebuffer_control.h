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
#include <common/libs/time/monotonic_time.h>

struct FrameBufferControl;

struct CompositionStats {
  cvd::time::MonotonicTimePoint prepare_start;
  cvd::time::MonotonicTimePoint prepare_end;
  cvd::time::MonotonicTimePoint set_start;
  cvd::time::MonotonicTimePoint set_end;
  cvd::time::MonotonicTimePoint last_vsync;
  // There may be more than one call to prepare, the timestamps are with regards to the last one (the one that precedes the set call)
  int num_prepare_calls;
  int num_layers;
  // The number of layers composed by the hwcomposer
  int num_hwc_layers;
};

class VSoCFrameBufferControl {
 public:
  static VSoCFrameBufferControl& getInstance();

  static const char* const kFrameBufferControlPath;

  // The framebuffer control structure mantains a bit set to keep track of the
  // buffers that have been allocated already. This function atomically finds an
  // unset (0) bit in the set, sets it to 1 and returns it. It will only
  // consider bits already set in the filter parameter.
  uint32_t GetAndSetNextAvailableBufferBit(uint32_t filter);
  // Returns 0 on success
  int UnsetBufferBits(uint32_t bits);

  // Returns the yoffset of the last framebuffer update or a negative number on
  // error.
  int GetCurrentYOffset() const;
  // Returns the value returned by the pthread_cond_wait, or -1 if the control
  // structure has not been initialized by the hwcomposer yet.
  int WaitForFrameBufferChange(int* yoffset_p);
  // Uses a sequential number to determine whether the client was notified of
  // the last framebuffer change and therefore needs to wait for a new one or if
  // it can just return with the last one. It also provides the timings of the
  // composition. Any NULL input parameters will be ignored. The sequential
  // numbers are guaranteed to never be zero, so a value of zero can be used to
  // get the last frame without waiting (useful when we want to get a frame for
  // the first time).
  int WaitForFrameBufferChangeSince(uint32_t previous_fb_seq,
                                    int* yoffset_p,
                                    uint32_t* fb_seq_p,
                                    CompositionStats* stats_p);

  // Returns 0 on success, a negative number on error.
  int BroadcastFrameBufferChanged(int yoffset);

  // Returns 0 on success, a negative number on error.
  int BroadcastFrameBufferChanged(int yoffset, const CompositionStats* stats);

 private:
  VSoCFrameBufferControl();

  // Map the control structure to memory and initialize its contents.
  bool Initialize();

  // FD for the frame buffer control.
  int control_fd_;
  // Pointer to the mapped frame buffer control.
  FrameBufferControl* control_memory_;

  // Disallow copy and assign
  VSoCFrameBufferControl(const VSoCFrameBufferControl&) {}
  VSoCFrameBufferControl& operator=(const VSoCFrameBufferControl&) {
    return *this;
  }
};
