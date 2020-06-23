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

#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "common/libs/threads/thread_annotations.h"
#include "host/frontend/vnc_server/blackboard.h"
#include "host/frontend/vnc_server/jpeg_compressor.h"
#include "host/frontend/vnc_server/simulated_hw_composer.h"

namespace cuttlefish {
namespace vnc {
class FrameBufferWatcher {
 public:
  explicit FrameBufferWatcher(BlackBoard* bb);
  FrameBufferWatcher(const FrameBufferWatcher&) = delete;
  FrameBufferWatcher& operator=(const FrameBufferWatcher&) = delete;
  ~FrameBufferWatcher();

  StripePtrVec StripesNewerThan(ScreenOrientation orientation,
                                const SeqNumberVec& seq_num) const;
  static int StripesPerFrame();

 private:
  static Stripe Rotated(Stripe stripe);

  bool closed() const;
  bool StripeIsDifferentFromPrevious(const Stripe& stripe) const
      REQUIRES(stripes_lock_);
  // returns true if stripe is still considered new and seq number was updated
  bool UpdateMostRecentSeqNumIfStripeIsNew(const Stripe& stripe)
      REQUIRES(stripes_lock_);
  // returns true if stripe is still considered new and was updated
  bool UpdateStripeIfStripeIsNew(const std::shared_ptr<const Stripe>& stripe)
      EXCLUDES(stripes_lock_);
  // Compresses stripe->raw_data to stripe->jpeg_data
  void CompressStripe(JpegCompressor* jpeg_compressor, Stripe* stripe);
  void Worker();
  void Updater();

  StripePtrVec& Stripes(ScreenOrientation orientation) REQUIRES(stripes_lock_);
  const StripePtrVec& Stripes(ScreenOrientation orientation) const
      REQUIRES(stripes_lock_);

  std::vector<std::thread> workers_;
  mutable std::mutex stripes_lock_;
  std::array<StripePtrVec, kNumOrientations> stripes_ GUARDED_BY(stripes_lock_);
  SeqNumberVec most_recent_identical_stripe_seq_nums_
      GUARDED_BY(stripes_lock_) = MakeSeqNumberVec();
  mutable std::mutex m_;
  bool closed_ GUARDED_BY(m_){};
  BlackBoard* bb_{};
  SimulatedHWComposer hwcomposer{bb_};
};

}  // namespace vnc
}  // namespace cuttlefish
