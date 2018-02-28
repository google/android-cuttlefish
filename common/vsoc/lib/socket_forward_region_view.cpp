/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <cassert>

#include "common/vsoc/lib/circqueue_impl.h"
#include "common/vsoc/lib/lock_guard.h"
#include "common/vsoc/lib/socket_forward_region_view.h"
#include "common/vsoc/shm/lock.h"
#include "common/vsoc/shm/socket_forward_layout.h"

using vsoc::layout::socket_forward::QueuePair;
using vsoc::layout::socket_forward::QueueState;
// store the read and write direction as variables to keep the ifdefs and macros
// in later code to a minimum
constexpr auto ReadDirection = &QueuePair::
#ifdef CUTTLEFISH_HOST
                                   guest_to_host;
#else
                                   host_to_guest;
#endif

constexpr auto WriteDirection = &QueuePair::
#ifdef CUTTLEFISH_HOST
                                    host_to_guest;
#else
                                    guest_to_host;
#endif

constexpr auto kOtherSideClosed = QueueState::
#ifdef CUTTLEFISH_HOST
    GUEST_CLOSED;
#else
    HOST_CLOSED;
#endif

constexpr auto kThisSideClosed = QueueState::
#ifdef CUTTLEFISH_HOST
    HOST_CLOSED;
#else
    GUEST_CLOSED;
#endif

using vsoc::socket_forward::SocketForwardRegionView;

void SocketForwardRegionView::Recv(int connection_id, Packet* packet) {
  CHECK(packet != nullptr);
  do {
    (data()->queues_[connection_id].*ReadDirection)
        .queue.Read(this, reinterpret_cast<char*>(packet), sizeof *packet);
  } while (packet->IsBegin());
  // TODO(haining) check packet generation number
  CHECK(!packet->empty()) << "zero-size data message received";
  CHECK_LE(packet->payload_length(), kMaxPayloadSize) << "invalid size";
}

bool SocketForwardRegionView::Send(int connection_id, const Packet& packet) {
  CHECK(!packet.empty());
  CHECK_LE(packet.payload_length(), kMaxPayloadSize);

  // NOTE this is check-then-act but I think that it's okay. Worst case is that
  // we send one-too-many packets.
  auto& queue_pair = data()->queues_[connection_id];
  {
    auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
    if ((queue_pair.*WriteDirection).queue_state_ == kOtherSideClosed) {
      LOG(INFO) << "connection closed, not sending\n";
      return false;
    }
    CHECK((queue_pair.*WriteDirection).queue_state_ != QueueState::INACTIVE);
  }
  // TODO(haining) set packet generation number
  (data()->queues_[connection_id].*WriteDirection)
      .queue.Write(this, packet.raw_data(), packet.raw_data_length());
  return true;
}

void SocketForwardRegionView::SendBegin(int connection_id) {
  Send(connection_id, Packet::MakeBegin());
}

void SocketForwardRegionView::SendEnd(int connection_id) {
  Send(connection_id, Packet::MakeEnd());
}

void SocketForwardRegionView::IgnoreUntilBegin(int connection_id) {
  Packet packet{};
  do {
    (data()->queues_[connection_id].*ReadDirection)
        .queue.Read(this, reinterpret_cast<char*>(&packet), sizeof packet);
  } while (!packet.IsBegin());  // TODO(haining) check generation number
}

bool SocketForwardRegionView::IsOtherSideRecvClosed(int connection_id) {
  auto& queue_pair = data()->queues_[connection_id];
  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
  auto& queue = queue_pair.*WriteDirection;
  return queue.queue_state_ == kOtherSideClosed ||
         queue.queue_state_ == QueueState::INACTIVE;
}

// TODO merge these two into a helper since the only difference is one
// Read/Write
void SocketForwardRegionView::MarkSendQueueDisconnected(int connection_id) {
  auto& queue_pair = data()->queues_[connection_id];
  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
  auto& queue = queue_pair.*WriteDirection;
  queue.queue_state_ = queue.queue_state_ == kOtherSideClosed
                           ? QueueState::INACTIVE
                           : kThisSideClosed;
}

void SocketForwardRegionView::MarkRecvQueueDisconnected(int connection_id) {
  auto& queue_pair = data()->queues_[connection_id];
  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
  auto& queue = queue_pair.*ReadDirection;
  queue.queue_state_ = queue.queue_state_ == kOtherSideClosed
                           ? QueueState::INACTIVE
                           : kThisSideClosed;
}

int SocketForwardRegionView::port(int connection_id) {
  return data()->queues_[connection_id].port_;
}

#ifdef CUTTLEFISH_HOST
int SocketForwardRegionView::AcquireConnectionID(int port) {
  int id = 0;
  for (auto&& queue_pair : data()->queues_) {
    LOG(DEBUG) << "locking and checking queue at index " << id;
    auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
    if (queue_pair.host_to_guest.queue_state_ == QueueState::INACTIVE &&
        queue_pair.guest_to_host.queue_state_ == QueueState::INACTIVE) {
      queue_pair.port_ = port;
      queue_pair.host_to_guest.queue_state_ = QueueState::HOST_CONNECTED;
      queue_pair.guest_to_host.queue_state_ = QueueState::HOST_CONNECTED;
      LOG(DEBUG) << "acquired queue " << id
                 << ". current seq_num: " << data()->seq_num;
      ++data()->seq_num;
      return id;
    }
    ++id;
  }
  // TODO(haining) handle all queues being used
  LOG(FATAL) << "no remaining shm queues for connection";
  return -1;
}

std::pair<SocketForwardRegionView::Sender, SocketForwardRegionView::Receiver>
SocketForwardRegionView::OpenConnection(int port) {
  int connection_id = AcquireConnectionID(port);
  LOG(INFO) << "Acquired connection with id " << connection_id;
  return {Sender{this, connection_id}, Receiver{this, connection_id}};
}
#else
int SocketForwardRegionView::GetWaitingConnectionID() {
  while (data()->seq_num == last_seq_number_) {
    WaitForSignal(&data()->seq_num, last_seq_number_);
  }
  ++last_seq_number_;
  int id = 0;
  for (auto&& queue_pair : data()->queues_) {
    LOG(DEBUG) << "locking and checking queue at index " << id;
    auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
    if (queue_pair.host_to_guest.queue_state_ == QueueState::HOST_CONNECTED) {
      CHECK(queue_pair.guest_to_host.queue_state_ ==
            QueueState::HOST_CONNECTED);
      LOG(DEBUG) << "found waiting connection at index " << id;
      queue_pair.host_to_guest.queue_state_ = QueueState::BOTH_CONNECTED;
      queue_pair.guest_to_host.queue_state_ = QueueState::BOTH_CONNECTED;
      return id;
    }
    ++id;
  }
  return -1;
}

std::pair<SocketForwardRegionView::Sender, SocketForwardRegionView::Receiver>
SocketForwardRegionView::AcceptConnection() {
  int connection_id = -1;
  while (connection_id < 0) {
    connection_id = GetWaitingConnectionID();
  }
  LOG(INFO) << "Accepted connection with id " << connection_id;
  return {Sender{this, connection_id}, Receiver{this, connection_id}};
}
#endif

// --- Connection ---- //
void SocketForwardRegionView::Receiver::Recv(Packet* packet) {
  if (!got_begin_) {
    view_->IgnoreUntilBegin(connection_id_);
    got_begin_ = true;
  }
  return view_->Recv(connection_id_, packet);
}

bool SocketForwardRegionView::Sender::closed() const {
  return view_->IsOtherSideRecvClosed(connection_id_);
}

bool SocketForwardRegionView::Sender::Send(const Packet& packet) {
  return view_->Send(connection_id_, packet);
}
