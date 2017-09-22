#include "blackboard.h"
#include "frame_buffer_watcher.h"
#include <utility>
#include <algorithm>

#define LOG_TAG "GceVNCServer"
#include <cutils/log.h>

using avd::vnc::BlackBoard;
using avd::vnc::Stripe;

avd::vnc::SeqNumberVec avd::vnc::MakeSeqNumberVec() {
  return SeqNumberVec(FrameBufferWatcher::StripesPerFrame());
}

void BlackBoard::NewStripeReady(int index, StripeSeqNumber seq_num) {
  std::lock_guard<std::mutex> guard(m_);
  D("new stripe arrived from frame watcher");
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
    ALOG_ASSERT(!clients_.count(conn));
    clients_[conn];  // constructs new state in place
  }
  new_client_cv_.notify_one();
}

void BlackBoard::Unregister(const VncClientConnection* conn) {
  std::lock_guard<std::mutex> guard(m_);
  ALOG_ASSERT(clients_.count(conn));
  clients_.erase(clients_.find(conn));
}

bool BlackBoard::NoNewStripesFor(const SeqNumberVec& seq_nums) const {
  ALOG_ASSERT(seq_nums.size() == most_recent_stripe_seq_nums.size());
  for (auto state_seq_num = seq_nums.begin(),
            held_seq_num = most_recent_stripe_seq_nums_.begin();
       state_seq_num != seq_nums.end(); ++state_seq_num, ++held_seq_num) {
    if (*state_seq_num < *held_seq_num) {
      return false;
    }
  }
  return true;
}

avd::vnc::StripePtrVec BlackBoard::WaitForSenderWork(
    const VncClientConnection* conn) {
  std::unique_lock<std::mutex> guard(m_);
  auto& state = GetStateForClient(conn);
  D("Waiting for stripe...");
  while (!state.closed &&
         (!state.ready_to_receive || NoNewStripesFor(state.stripe_seq_nums))) {
    state.new_frame_cv.wait(guard);
  }
  D("At least one new stripe is available, should unblock %p", conn);
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
  D("Received frame buffer update request");
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
    avd::vnc::FrameBufferWatcher* frame_buffer_watcher) {
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
    ALOGW("Bogus jpeg quality level: %d. Quality must be in range [%d, %d]",
          quality_level, kJpegMinQualityEncoding, kJpegMaxQualityEncoding);
    return;
  }
  jpeg_quality_level_ = 55 + (5 * (quality_level + 32));
  D("jpeg quality level set to %d%%:", jpeg_quality_level_);
}

BlackBoard::ClientFBUState& BlackBoard::GetStateForClient(
    const VncClientConnection* conn) {
  ALOG_ASSERT(clients_.count(conn));
  return clients_[conn];
}
