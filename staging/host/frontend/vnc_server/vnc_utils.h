#pragma once

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

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/tcp_socket.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/screen_connector/screen_connector.h"

namespace cuttlefish {
namespace vnc {

// TODO(haining) when the hwcomposer gives a sequence number type, use that
// instead. It might just work to replace this class with a type alias
// using StripeSeqNumber = whatever_the_hwcomposer_uses;
class StripeSeqNumber {
 public:
  StripeSeqNumber() = default;
  explicit StripeSeqNumber(std::uint64_t t) : t_{t} {}
  bool operator<(const StripeSeqNumber& other) const { return t_ < other.t_; }

  bool operator<=(const StripeSeqNumber& other) const { return t_ <= other.t_; }

 private:
  std::uint64_t t_{};
};

constexpr int32_t kJpegMaxQualityEncoding = -23;
constexpr int32_t kJpegMinQualityEncoding = -32;

enum class ScreenOrientation { Portrait, Landscape };
constexpr int kNumOrientations = 2;

struct Stripe {
  int index = -1;
  std::uint64_t frame_id{};
  std::uint16_t x{};
  std::uint16_t y{};
  std::uint16_t width{};
  std::uint16_t stride{};
  std::uint16_t height{};
  Message raw_data{};
  Message jpeg_data{};
  StripeSeqNumber seq_number{};
  ScreenOrientation orientation{};
};

/**
 * ScreenConnectorImpl will generate this, and enqueue
 *
 * It's basically a (processed) frame, so it:
 *   must be efficiently std::move-able
 * Also, for the sake of algorithm simplicity:
 *   must be default-constructable & assignable
 *
 */
struct VncScProcessedFrame : public ScreenConnectorFrameInfo {
  Message raw_screen_;
  std::deque<Stripe> stripes_;
  std::unique_ptr<VncScProcessedFrame> Clone() {
    VncScProcessedFrame* cloned_frame = new VncScProcessedFrame();
    cloned_frame->raw_screen_ = raw_screen_;
    cloned_frame->stripes_ = stripes_;
    return std::unique_ptr<VncScProcessedFrame>(cloned_frame);
  }
};
using ScreenConnector = cuttlefish::ScreenConnector<VncScProcessedFrame>;

}  // namespace vnc
}  // namespace cuttlefish
