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

#include "host/frontend/vnc_server/frame_buffer_watcher.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <android-base/logging.h>
#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/screen_connector/screen_connector.h"

using cuttlefish::vnc::FrameBufferWatcher;

FrameBufferWatcher::FrameBufferWatcher(BlackBoard* bb)
    : bb_{bb}, hwcomposer{bb_} {
  for (auto& stripes_vec : stripes_) {
    std::generate_n(std::back_inserter(stripes_vec),
                    SimulatedHWComposer::NumberOfStripes(),
                    std::make_shared<Stripe>);
  }
  bb_->set_frame_buffer_watcher(this);
  auto num_workers = std::max(std::thread::hardware_concurrency(), 1u);
  std::generate_n(std::back_inserter(workers_), num_workers, [this] {
    return std::thread{&FrameBufferWatcher::Worker, this};
  });
}

FrameBufferWatcher::~FrameBufferWatcher() {
  {
    std::lock_guard<std::mutex> guard(m_);
    closed_ = true;
  }
  for (auto& tid : workers_) {
    tid.join();
  }
}

bool FrameBufferWatcher::closed() const {
  std::lock_guard<std::mutex> guard(m_);
  return closed_;
}

cuttlefish::vnc::Stripe FrameBufferWatcher::Rotated(Stripe stripe) {
  if (stripe.orientation == ScreenOrientation::Landscape) {
    LOG(FATAL) << "Rotating a landscape stripe, this is a mistake";
  }
  auto w = stripe.width;
  auto s = stripe.stride;
  auto h = stripe.height;
  const auto& raw = stripe.raw_data;
  Message rotated(raw.size(), 0xAA);
  for (std::uint16_t i = 0; i < w; ++i) {
    for (std::uint16_t j = 0; j < h; ++j) {
      size_t to = (i * h + j) * ScreenConnector::BytesPerPixel();
      size_t from = (w - (i + 1)) * ScreenConnector::BytesPerPixel() + s * j;
      CHECK(from < raw.size());
      CHECK(to < rotated.size());
      std::memcpy(&rotated[to], &raw[from], ScreenConnector::BytesPerPixel());
    }
  }
  std::swap(stripe.x, stripe.y);
  std::swap(stripe.width, stripe.height);
  // The new stride after rotating is the height, as it is not aligned again.
  stripe.stride = stripe.width * ScreenConnector::BytesPerPixel();
  stripe.raw_data = std::move(rotated);
  stripe.orientation = ScreenOrientation::Landscape;
  return stripe;
}

bool FrameBufferWatcher::StripeIsDifferentFromPrevious(
    const Stripe& stripe) const {
  return Stripes(stripe.orientation)[stripe.index]->raw_data != stripe.raw_data;
}

cuttlefish::vnc::StripePtrVec FrameBufferWatcher::StripesNewerThan(
    ScreenOrientation orientation, const SeqNumberVec& seq_numbers) const {
  std::lock_guard<std::mutex> guard(stripes_lock_);
  const auto& stripes = Stripes(orientation);
  CHECK(seq_numbers.size() == stripes.size());
  StripePtrVec new_stripes;
  auto seq_number_it = seq_numbers.begin();
  std::copy_if(stripes.begin(), stripes.end(), std::back_inserter(new_stripes),
               [seq_number_it](const StripePtrVec::value_type& s) mutable {
                 return *(seq_number_it++) < s->seq_number;
               });
  return new_stripes;
}

cuttlefish::vnc::StripePtrVec& FrameBufferWatcher::Stripes(
    ScreenOrientation orientation) {
  return stripes_[static_cast<int>(orientation)];
}

const cuttlefish::vnc::StripePtrVec& FrameBufferWatcher::Stripes(
    ScreenOrientation orientation) const {
  return stripes_[static_cast<int>(orientation)];
}

bool FrameBufferWatcher::UpdateMostRecentSeqNumIfStripeIsNew(
    const Stripe& stripe) {
  if (most_recent_identical_stripe_seq_nums_[stripe.index] <=
      stripe.seq_number) {
    most_recent_identical_stripe_seq_nums_[stripe.index] = stripe.seq_number;
    return true;
  }
  return false;
}

bool FrameBufferWatcher::UpdateStripeIfStripeIsNew(
    const std::shared_ptr<const Stripe>& stripe) {
  std::lock_guard<std::mutex> guard(stripes_lock_);
  if (UpdateMostRecentSeqNumIfStripeIsNew(*stripe)) {
    Stripes(stripe->orientation)[stripe->index] = stripe;
    return true;
  }
  return false;
}

void FrameBufferWatcher::CompressStripe(JpegCompressor* jpeg_compressor,
                                        Stripe* stripe) {
  stripe->jpeg_data = jpeg_compressor->Compress(
      stripe->raw_data, bb_->jpeg_quality_level(), 0, 0, stripe->width,
      stripe->height, stripe->stride);
}

void FrameBufferWatcher::Worker() {
  JpegCompressor jpeg_compressor;
#ifdef FUZZ_TEST_VNC
  std::default_random_engine e{std::random_device{}()};
  std::uniform_int_distribution<int> random{0, 2};
#endif
  while (!closed()) {
    auto portrait_stripe = hwcomposer.GetNewStripe();
    if (closed()) {
      break;
    }
    {
      // TODO(haining) use if (with init) and else for c++17 instead of extra
      // scope and continue
      // if (std::lock_guard guard(stripes_lock_); /*condition*/) { }
      std::lock_guard<std::mutex> guard(stripes_lock_);
      if (!StripeIsDifferentFromPrevious(portrait_stripe)) {
        UpdateMostRecentSeqNumIfStripeIsNew(portrait_stripe);
        continue;
      }
    }
    auto seq_num = portrait_stripe.seq_number;
    auto index = portrait_stripe.index;
    auto landscape_stripe = Rotated(portrait_stripe);
    auto stripes = {std::make_shared<Stripe>(std::move(portrait_stripe)),
                    std::make_shared<Stripe>(std::move(landscape_stripe))};
    for (auto& stripe : stripes) {
#ifdef FUZZ_TEST_VNC
      if (random(e)) {
        usleep(10000);
      }
#endif
      CompressStripe(&jpeg_compressor, stripe.get());
    }
    bool any_new_stripes = false;
    for (auto& stripe : stripes) {
      any_new_stripes = UpdateStripeIfStripeIsNew(stripe) || any_new_stripes;
    }
    if (any_new_stripes) {
      bb_->NewStripeReady(index, seq_num);
    }
  }
}

int FrameBufferWatcher::StripesPerFrame() {
  return SimulatedHWComposer::NumberOfStripes();
}
