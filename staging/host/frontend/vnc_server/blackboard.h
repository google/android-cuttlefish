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


#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "common/libs/threads/thread_annotations.h"
#include "host/frontend/vnc_server/vnc_utils.h"

namespace cuttlefish {
namespace vnc {

class VncClientConnection;
class FrameBufferWatcher;
using StripePtrVec = std::vector<std::shared_ptr<const Stripe>>;
using SeqNumberVec = std::vector<StripeSeqNumber>;

SeqNumberVec MakeSeqNumberVec();

class BlackBoard {
 private:
  struct ClientFBUState {
    bool ready_to_receive{};
    ScreenOrientation orientation{};
    std::condition_variable new_frame_cv;
    SeqNumberVec stripe_seq_nums = MakeSeqNumberVec();
    bool closed{};
  };

 public:
  class Registerer {
   public:
    Registerer(BlackBoard* bb, const VncClientConnection* conn)
        : bb_{bb}, conn_{conn} {
      bb->Register(conn);
    }
    ~Registerer() { bb_->Unregister(conn_); }
    Registerer(const Registerer&) = delete;
    Registerer& operator=(const Registerer&) = delete;

   private:
    BlackBoard* bb_{};
    const VncClientConnection* conn_{};
  };

  BlackBoard() = default;
  BlackBoard(const BlackBoard&) = delete;
  BlackBoard& operator=(const BlackBoard&) = delete;

  bool NoNewStripesFor(const SeqNumberVec& seq_nums) const REQUIRES(m_);
  void NewStripeReady(int index, StripeSeqNumber seq_num);
  void Register(const VncClientConnection* conn);
  void Unregister(const VncClientConnection* conn);

  StripePtrVec WaitForSenderWork(const VncClientConnection* conn);

  void WaitForAtLeastOneClientConnection();

  void FrameBufferUpdateRequestReceived(const VncClientConnection* conn);
  // Setting orientation implies needing the entire screen
  void SetOrientation(const VncClientConnection* conn,
                      ScreenOrientation orientation);
  void SignalClientNeedsEntireScreen(const VncClientConnection* conn);

  void StopWaiting(const VncClientConnection* conn);

  void set_frame_buffer_watcher(FrameBufferWatcher* frame_buffer_watcher);

  // quality_level must be the value received from the client, in the range
  // [kJpegMinQualityEncoding, kJpegMaxQualityEncoding], else it is ignored.
  void set_jpeg_quality_level(int quality_level);

  int jpeg_quality_level() const {
    std::lock_guard<std::mutex> guard(m_);
    return jpeg_quality_level_;
  }

 private:
  ClientFBUState& GetStateForClient(const VncClientConnection* conn)
      REQUIRES(m_);
  static void ResetToZero(SeqNumberVec* seq_nums);

  mutable std::mutex m_;
  SeqNumberVec most_recent_stripe_seq_nums_ GUARDED_BY(m_) = MakeSeqNumberVec();
  std::unordered_map<const VncClientConnection*, ClientFBUState> clients_
      GUARDED_BY(m_);
  int jpeg_quality_level_ GUARDED_BY(m_) = 100;
  std::condition_variable new_client_cv_;
  // NOTE the FrameBufferWatcher pointer itself should be
  // guarded, but not the pointee.
  FrameBufferWatcher* frame_buffer_watcher_ GUARDED_BY(m_){};
};

}  // namespace vnc
}  // namespace cuttlefish
