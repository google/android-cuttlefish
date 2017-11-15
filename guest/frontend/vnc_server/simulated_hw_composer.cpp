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

#include "simulated_hw_composer.h"

using cvd::vnc::SimulatedHWComposer;

SimulatedHWComposer::SimulatedHWComposer(BlackBoard* bb)
    :
#ifdef FUZZ_TEST_VNC
      engine_{std::random_device{}()},
#endif
      control_{VSoCFrameBufferControl::getInstance()},
      bb_{bb},
      stripes_(kMaxQueueElements, &SimulatedHWComposer::EraseHalfOfElements) {
  void* p{};
  VSoCFrameBuffer::OpenAndMapFrameBuffer(&p, &frame_buffer_fd_);
  frame_buffer_memory_ = static_cast<char*>(p);
  stripe_maker_ = std::thread(&SimulatedHWComposer::MakeStripes, this);
}

SimulatedHWComposer::~SimulatedHWComposer() {
  close();
  stripe_maker_.join();
  VSoCFrameBuffer::UnmapAndCloseFrameBuffer(frame_buffer_memory_,
                                           frame_buffer_fd_);
}

cvd::vnc::Stripe SimulatedHWComposer::GetNewStripe() {
  auto s = stripes_.Pop();
#ifdef FUZZ_TEST_VNC
  if (random_(engine_)) {
    usleep(7000);
    stripes_.Push(std::move(s));
    s = stripes_.Pop();
  }
#endif
  return s;
}

bool SimulatedHWComposer::closed() {
  std::lock_guard<std::mutex> guard(m_);
  return closed_;
}

void SimulatedHWComposer::close() {
  std::lock_guard<std::mutex> guard(m_);
  closed_ = true;
}

// Assuming the number of stripes is less than half the size of the queue
// this will be safe as the newest stripes won't be lost. In the real
// hwcomposer, where stripes are coming in a different order, the full
// queue case would probably need a different approach to be safe.
void SimulatedHWComposer::EraseHalfOfElements(
    ThreadSafeQueue<Stripe>::QueueImpl* q) {
  q->erase(q->begin(), std::next(q->begin(), kMaxQueueElements / 2));
}

void SimulatedHWComposer::MakeStripes() {
  std::uint32_t previous_seq_num{};
  auto screen_height = ActualScreenHeight();
  Message raw_screen;
  std::uint64_t stripe_seq_num = 1;
  while (!closed()) {
    bb_->WaitForAtLeastOneClientConnection();
    int y_offset{};
    control_.WaitForFrameBufferChangeSince(previous_seq_num, &y_offset,
                                           &previous_seq_num, nullptr);

    const auto* frame_start =
        frame_buffer_memory_ + y_offset * ActualScreenWidth() * BytesPerPixel();
    raw_screen.assign(frame_start, frame_start + ScreenSizeInBytes());

    for (int i = 0; i < kNumStripes; ++i) {
      ++stripe_seq_num;
      std::uint16_t y = (screen_height / kNumStripes) * i;

      // Last frames on the right and/or bottom handle extra pixels
      // when a screen dimension is not evenly divisible by Frame::kNumSlots.
      std::uint16_t height =
          screen_height / kNumStripes +
          (i + 1 == kNumStripes ? screen_height % kNumStripes : 0);
      const auto* raw_start =
          &raw_screen[y * ActualScreenWidth() * BytesPerPixel()];
      const auto* raw_end =
          raw_start + (height * ActualScreenWidth() * BytesPerPixel());
      // creating a named object and setting individual data members in order
      // to make klp happy
      // TODO (haining) construct this inside the call when not compiling
      // on klp
      Stripe s{};
      s.index = i;
      s.frame_id = previous_seq_num;
      s.x = 0;
      s.y = y;
      s.width = ActualScreenWidth();
      s.height = height;
      s.raw_data.assign(raw_start, raw_end);
      s.seq_number = StripeSeqNumber{stripe_seq_num};
      s.orientation = ScreenOrientation::Portrait;
      stripes_.Push(std::move(s));
    }
  }
}

int SimulatedHWComposer::NumberOfStripes() {
  return kNumStripes;
}
