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

using vsoc::layout::socket_forward::Queue;
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

void SocketForwardRegionView::IgnoreUntilBegin(int connection_id,
                                               std::uint32_t generation) {
  Packet packet{};
  do {
    (data()->queues_[connection_id].*ReadDirection)
        .queue.Read(this, reinterpret_cast<char*>(&packet), sizeof packet);
  } while (!packet.IsBegin() || packet.generation() < generation);
}

bool SocketForwardRegionView::IsOtherSideRecvClosed(int connection_id) {
  auto& queue_pair = data()->queues_[connection_id];
  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
  auto& queue = queue_pair.*WriteDirection;
  return queue.queue_state_ == kOtherSideClosed ||
         queue.queue_state_ == QueueState::INACTIVE;
}

void SocketForwardRegionView::ResetQueueStates(QueuePair* queue_pair) {
  using vsoc::layout::socket_forward::Queue;
  auto guard = make_lock_guard(&queue_pair->queue_state_lock_);
  Queue* queues[] = {&queue_pair->host_to_guest, &queue_pair->guest_to_host};
  for (auto* queue : queues) {
    auto& state = queue->queue_state_;
    switch (state) {
      case QueueState::HOST_CONNECTED:
      case kOtherSideClosed:
        LOG(DEBUG)
            << "host_connected or other side is closed, marking inactive";
        state = QueueState::INACTIVE;
        break;

      case QueueState::BOTH_CONNECTED:
        LOG(DEBUG) << "both_connected, marking this side closed";
        state = kThisSideClosed;
        break;

      case kThisSideClosed:
        [[fallthrough]];
      case QueueState::INACTIVE:
        LOG(DEBUG) << "inactive or this side closed, not changing state";
        break;
    }
  }
}

void SocketForwardRegionView::CleanUpPreviousConnections() {
  data()->Recover();
  int connection_id = 0;
  auto current_generation = generation();
  auto begin_packet = Packet::MakeBegin();
  begin_packet.set_generation(current_generation);
  auto end_packet = Packet::MakeEnd();
  end_packet.set_generation(current_generation);
  for (auto&& queue_pair : data()->queues_) {
    QueueState state{};
    {
      auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
      state = (queue_pair.*WriteDirection).queue_state_;
#ifndef CUTTLEFISH_HOST
      if (state == QueueState::HOST_CONNECTED) {
        state = (queue_pair.*WriteDirection).queue_state_ =
            (queue_pair.*ReadDirection).queue_state_ =
                QueueState::BOTH_CONNECTED;
      }
#endif
    }

    if (state == QueueState::BOTH_CONNECTED
#ifdef CUTTLEFISH_HOST
        || state == QueueState::HOST_CONNECTED
#endif
    ) {
      LOG(INFO) << "found connected write queue state, sending begin and end";
      Send(connection_id, begin_packet);
      Send(connection_id, end_packet);
    }
    ResetQueueStates(&queue_pair);
    ++connection_id;
  }
  ++data()->generation_num;
}

void SocketForwardRegionView::MarkQueueDisconnected(
    int connection_id, Queue QueuePair::*direction) {
  auto& queue_pair = data()->queues_[connection_id];
  auto& queue = queue_pair.*direction;

#ifdef CUTTLEFISH_HOST
  // if the host has connected but the guest hasn't seen it yet, wait for the
  // guest to connect so the protocol can follow the normal state transition.
  while (true) {
    auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
    if (queue.queue_state_ != QueueState::HOST_CONNECTED) {
      break;
    }
    LOG(WARNING) << "closing queue in HOST_CONNECTED state. waiting";
    sleep(1);
  }
#endif

  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);

  queue.queue_state_ = queue.queue_state_ == kOtherSideClosed
                           ? QueueState::INACTIVE
                           : kThisSideClosed;
}

void SocketForwardRegionView::MarkSendQueueDisconnected(int connection_id) {
  MarkQueueDisconnected(connection_id, WriteDirection);
}

void SocketForwardRegionView::MarkRecvQueueDisconnected(int connection_id) {
  MarkQueueDisconnected(connection_id, ReadDirection);
}

int SocketForwardRegionView::port(int connection_id) {
  return data()->queues_[connection_id].port_;
}

std::uint32_t SocketForwardRegionView::generation() {
  return data()->generation_num;
}

#ifdef CUTTLEFISH_HOST
int SocketForwardRegionView::AcquireConnectionID(int port) {
  while (true) {
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
        SendSignal(layout::Sides::Peer, &data()->seq_num);
        return id;
      }
      ++id;
    }
    LOG(ERROR) << "no remaining shm queues for connection, sleeping.";
    sleep(10);
  }
}

std::pair<SocketForwardRegionView::Sender, SocketForwardRegionView::Receiver>
SocketForwardRegionView::OpenConnection(int port) {
  int connection_id = AcquireConnectionID(port);
  LOG(INFO) << "Acquired connection with id " << connection_id;
  auto current_generation = generation();
  return {Sender{this, connection_id, current_generation},
          Receiver{this, connection_id, current_generation}};
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

  auto current_generation = generation();
  return {Sender{this, connection_id, current_generation},
          Receiver{this, connection_id, current_generation}};
}
#endif

// --- Connection ---- //
void SocketForwardRegionView::Receiver::Recv(Packet* packet) {
  if (!got_begin_) {
    view_->IgnoreUntilBegin(connection_id_, generation_);
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
