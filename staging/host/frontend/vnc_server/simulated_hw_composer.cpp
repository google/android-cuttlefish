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

#include "host/frontend/vnc_server/simulated_hw_composer.h"

#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/config/cuttlefish_config.h"

using cuttlefish::vnc::SimulatedHWComposer;
using ScreenConnector = cuttlefish::vnc::ScreenConnector;

SimulatedHWComposer::SimulatedHWComposer(BlackBoard* bb,
                                         ScreenConnector& screen_connector)
    :
#ifdef FUZZ_TEST_VNC
      engine_{std::random_device{}()},
#endif
      bb_{bb},
      stripes_(kMaxQueueElements, &SimulatedHWComposer::EraseHalfOfElements),
      screen_connector_(screen_connector) {
  stripe_maker_ = std::thread(&SimulatedHWComposer::MakeStripes, this);
  screen_connector_.SetCallback(std::move(GetScreenConnectorCallback()));
}

SimulatedHWComposer::~SimulatedHWComposer() {
  close();
  stripe_maker_.join();
}

cuttlefish::vnc::Stripe SimulatedHWComposer::GetNewStripe() {
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

SimulatedHWComposer::GenerateProcessedFrameCallback
SimulatedHWComposer::GetScreenConnectorCallback() {
  return [](std::uint32_t display_number, std::uint8_t* frame_pixels,
            cuttlefish::vnc::VncScProcessedFrame& processed_frame) {
    processed_frame.display_number_ = display_number;
    // TODO(171305898): handle multiple displays.
    if (display_number != 0) {
      // BUG 186580833: display_number comes from surface_id in crosvm
      // create_surface from virtio_gpu.rs set_scanout.  We cannot use it as
      // the display number. Either crosvm virtio-gpu is incorrectly ignoring
      // scanout id and instead using a monotonically increasing surface id
      // number as the scanout resource is replaced over time, or frontend code
      // here is incorrectly assuming  surface id == display id.
      display_number = 0;
    }
    const std::uint32_t display_w =
        ScreenConnector::ScreenWidth(display_number);
    const std::uint32_t display_h =
        ScreenConnector::ScreenHeight(display_number);
    const std::uint32_t display_stride_bytes =
        ScreenConnector::ScreenStrideBytes(display_number);
    const std::uint32_t display_bpp = ScreenConnector::BytesPerPixel();
    const std::uint32_t display_size_bytes =
        ScreenConnector::ScreenSizeInBytes(display_number);

    auto& raw_screen = processed_frame.raw_screen_;
    raw_screen.assign(frame_pixels, frame_pixels + display_size_bytes);

    static std::uint32_t next_frame_number = 0;

    const auto num_stripes = SimulatedHWComposer::kNumStripes;
    for (int i = 0; i < num_stripes; ++i) {
      std::uint16_t y = (display_h / num_stripes) * i;

      // Last frames on the right and/or bottom handle extra pixels
      // when a screen dimension is not evenly divisible by Frame::kNumSlots.
      std::uint16_t height =
          display_h / num_stripes +
          (i + 1 == num_stripes ? display_h % num_stripes : 0);
      const auto* raw_start = &raw_screen[y * display_w * display_bpp];
      const auto* raw_end = raw_start + (height * display_w * display_bpp);
      // creating a named object and setting individual data members in order
      // to make klp happy
      // TODO (haining) construct this inside the call when not compiling
      // on klp
      Stripe s{};
      s.index = i;
      s.x = 0;
      s.y = y;
      s.width = display_w;
      s.stride = display_stride_bytes;
      s.height = height;
      s.frame_id = next_frame_number++;
      s.raw_data.assign(raw_start, raw_end);
      s.orientation = ScreenOrientation::Portrait;
      processed_frame.stripes_.push_back(std::move(s));
    }

    processed_frame.display_number_ = display_number;
    processed_frame.is_success_ = true;
  };
}

void SimulatedHWComposer::MakeStripes() {
  std::uint64_t stripe_seq_num = 1;
  /*
   * callback should be set before the first WaitForAtLeastOneClientConnection()
   * (b/178504150) and the first OnFrameAfter().
   */
  if (!screen_connector_.IsCallbackSet()) {
    LOG(FATAL) << "ScreenConnector callback hasn't been set before MakeStripes";
  }
  while (!closed()) {
    bb_->WaitForAtLeastOneClientConnection();
    auto sim_hw_processed_frame = screen_connector_.OnNextFrame();
    // sim_hw_processed_frame has display number from the guest
    if (!sim_hw_processed_frame.is_success_) {
      continue;
    }
    while (!sim_hw_processed_frame.stripes_.empty()) {
      /*
       * ScreenConnector that supplies the frames into the queue
       * cannot be aware of stripe_seq_num. The callback was set at the
       * ScreenConnector creation time. ScreenConnector calls the callback
       * function autonomously to make the processed frames to supply the
       * queue with.
       *
       * Besides, ScreenConnector is not VNC specific. Thus, stripe_seq_num,
       * a VNC specific information, is maintained here.
       *
       * OnFrameAfter returns a sim_hw_processed_frame, that contains N consecutive stripes.
       * each stripe s has an invalid seq_number, default-initialzed
       * We set the field properly, and push to the stripes_
       */
      auto& s = sim_hw_processed_frame.stripes_.front();
      stripe_seq_num++;
      s.seq_number = StripeSeqNumber{stripe_seq_num};
      stripes_.Push(std::move(s));
      sim_hw_processed_frame.stripes_.pop_front();
    }
  }
}

int SimulatedHWComposer::NumberOfStripes() { return kNumStripes; }

void SimulatedHWComposer::ReportClientsConnected() {
  screen_connector_.ReportClientsConnected(true);
}
