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

#include "host/frontend/vnc_server/blackboard.h"

#include <algorithm>
#include <utility>

#include <gflags/gflags.h>
#include <android-base/logging.h>
#include "host/frontend/vnc_server/frame_buffer_watcher.h"

DEFINE_bool(debug_blackboard, false,
            "Turn on detailed logging for the blackboard");

#define DLOG(LEVEL)                                 \
  if (FLAGS_debug_blackboard) LOG(LEVEL)

using cuttlefish::vnc::BlackBoard;
using cuttlefish::vnc::Stripe;

cuttlefish::vnc::SeqNumberVec cuttlefish::vnc::MakeSeqNumberVec() {
  return SeqNumberVec(FrameBufferWatcher::StripesPerFrame());
}

void BlackBoard::NewStripeReady(int index, StripeSeqNumber seq_num) {
  std::lock_guard<std::mutex> guard(m_);
  DLOG(INFO) << "new stripe arrived from frame watcher";
  auto& current_seq_num = most_recent_stripe_seq_nums_[index];
  current_seq_num = std::max(current_seq_num, seq_num);
  for (auto& client : clients_) {
    if (client.second.ready_to_receive) {
      client.second.new_frame_cv.notify_one();
    }
  }
}

void BlackBoard::Register(const VncClientConnection* conn) {
  {
    std::lock_guard<std::mutex> guard(m_);
    CHECK(!clients_.count(conn));
    clients_[conn];  // constructs new state in place
  }
  new_client_cv_.notify_one();
}

void BlackBoard::Unregister(const VncClientConnection* conn) {
  std::lock_guard<std::mutex> guard(m_);
  CHECK(clients_.count(conn));
  clients_.erase(clients_.find(conn));
}

bool BlackBoard::NoNewStripesFor(const SeqNumberVec& seq_nums) const {
  CHECK(seq_nums.size() == most_recent_stripe_seq_nums_.size());
  for (auto state_seq_num = seq_nums.begin(),
            held_seq_num = most_recent_stripe_seq_nums_.begin();
       state_seq_num != seq_nums.end(); ++state_seq_num, ++held_seq_num) {
    if (*state_seq_num < *held_seq_num) {
      return false;
    }
  }
  return true;
}

cuttlefish::vnc::StripePtrVec BlackBoard::WaitForSenderWork(
    const VncClientConnection* conn) {
  std::unique_lock<std::mutex> guard(m_);
  auto& state = GetStateForClient(conn);
  DLOG(INFO) << "Waiting for stripe...";
  while (!state.closed &&
         (!state.ready_to_receive || NoNewStripesFor(state.stripe_seq_nums))) {
    state.new_frame_cv.wait(guard);
  }
  DLOG(INFO) << "At least one new stripe is available, should unblock " << conn;
  state.ready_to_receive = false;
  auto new_stripes = frame_buffer_watcher_->StripesNewerThan(
      state.orientation, state.stripe_seq_nums);
  for (auto& s : new_stripes) {
    state.stripe_seq_nums[s->index] = s->seq_number;
  }
  return new_stripes;
}

void BlackBoard::WaitForAtLeastOneClientConnection() {
  std::unique_lock<std::mutex> guard(m_);
  while (clients_.empty()) {
    new_client_cv_.wait(guard);
  }
}

void BlackBoard::SetOrientation(const VncClientConnection* conn,
                                ScreenOrientation orientation) {
  std::lock_guard<std::mutex> guard(m_);
  auto& state = GetStateForClient(conn);
  state.orientation = orientation;
  // After an orientation change the vnc client will need all stripes from
  // the new orientation, regardless of age.
  ResetToZero(&state.stripe_seq_nums);
}

void BlackBoard::SignalClientNeedsEntireScreen(
    const VncClientConnection* conn) {
  std::lock_guard<std::mutex> guard(m_);
  ResetToZero(&GetStateForClient(conn).stripe_seq_nums);
}

void BlackBoard::ResetToZero(SeqNumberVec* seq_nums) {
  seq_nums->assign(FrameBufferWatcher::StripesPerFrame(), StripeSeqNumber{});
}

void BlackBoard::FrameBufferUpdateRequestReceived(
    const VncClientConnection* conn) {
  std::lock_guard<std::mutex> guard(m_);
  DLOG(INFO) << "Received frame buffer update request";
  auto& state = GetStateForClient(conn);
  state.ready_to_receive = true;
  state.new_frame_cv.notify_one();
}

void BlackBoard::StopWaiting(const VncClientConnection* conn) {
  std::lock_guard<std::mutex> guard(m_);
  auto& state = GetStateForClient(conn);
  state.closed = true;
  // Wake up the thread that might be in WaitForSenderWork()
  state.new_frame_cv.notify_one();
}

void BlackBoard::set_frame_buffer_watcher(
    cuttlefish::vnc::FrameBufferWatcher* frame_buffer_watcher) {
  std::lock_guard<std::mutex> guard(m_);
  frame_buffer_watcher_ = frame_buffer_watcher;
}

void BlackBoard::set_jpeg_quality_level(int quality_level) {
  // NOTE all vnc clients share a common jpeg quality level because the
  // server doesn't compress per-client. The quality level for all clients
  // will be whatever the most recent set was by any client.
  std::lock_guard<std::mutex> guard(m_);
  if (quality_level < kJpegMinQualityEncoding ||
      quality_level > kJpegMaxQualityEncoding) {
    LOG(WARNING) << "Bogus jpeg quality level: " << quality_level
                 << ". Quality must be in range [" << kJpegMinQualityEncoding
                 << ", " << kJpegMaxQualityEncoding << "]";
    return;
  }
  jpeg_quality_level_ = 55 + (5 * (quality_level + 32));
  DLOG(INFO) << "jpeg quality level set to " << jpeg_quality_level_ << "%";
}

BlackBoard::ClientFBUState& BlackBoard::GetStateForClient(
    const VncClientConnection* conn) {
  CHECK(clients_.count(conn));
  return clients_[conn];
}
