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
namespace QueueState = vsoc::layout::socket_forward::QueueState;
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

using vsoc::socket_forward::SocketForwardRegionView;

vsoc::socket_forward::Packet vsoc::socket_forward::Packet::MakeBegin(
    std::uint16_t port) {
  auto packet = MakePacket(Header::BEGIN);
  std::memcpy(packet.payload(), &port, sizeof port);
  packet.set_payload_length(sizeof port);
  return packet;
}

void SocketForwardRegionView::Recv(int connection_id, Packet* packet) {
  CHECK(packet != nullptr);
  do {
    (data()->queues_[connection_id].*ReadDirection)
        .queue.Read(this, reinterpret_cast<char*>(packet), sizeof *packet);
  } while (packet->IsBegin());
  CHECK(!packet->empty()) << "zero-size data message received";
  CHECK_LE(packet->payload_length(), kMaxPayloadSize) << "invalid size";
}

bool SocketForwardRegionView::Send(int connection_id, const Packet& packet) {
  CHECK(!packet.empty());
  CHECK_LE(packet.payload_length(), kMaxPayloadSize);

  (data()->queues_[connection_id].*WriteDirection)
      .queue.Write(this, packet.raw_data(), packet.raw_data_length());
  return true;
}

int SocketForwardRegionView::IgnoreUntilBegin(int connection_id) {
  Packet packet{};
  do {
    (data()->queues_[connection_id].*ReadDirection)
        .queue.Read(this, reinterpret_cast<char*>(&packet), sizeof packet);
  } while (!packet.IsBegin());
  return packet.port();
}

constexpr int kNumQueues =
    static_cast<int>(vsoc::layout::socket_forward::kNumQueues);

void SocketForwardRegionView::CleanUpPreviousConnections() {
  data()->Recover();
  int connection_id = 0;
  auto current_generation = generation();
  auto begin_packet = Packet::MakeBegin();
  begin_packet.set_generation(current_generation);
  auto end_packet = Packet::MakeEnd();
  end_packet.set_generation(current_generation);
  for (auto&& queue_pair : data()->queues_) {
    std::uint32_t state{};
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

  static constexpr auto kRestartPacket = Packet::MakeRestart();
  for (int connection_id = 0; connection_id < kNumQueues; ++connection_id) {
    Send(connection_id, kRestartPacket);
  }
}

void SocketForwardRegionView::MarkQueueDisconnected(
    int connection_id, Queue QueuePair::*direction) {
  auto& queue_pair = data()->queues_[connection_id];
  auto& queue = queue_pair.*direction;

#ifdef CUTTLEFISH_HOST
  // if the host has connected but the guest hasn't seen it yet, wait for the
  // guest to connect so the protocol can follow the normal state transition.
  while (queue.queue_state_ == QueueState::HOST_CONNECTED) {
    LOG(WARNING) << "closing queue[" << connection_id
                 << "] in HOST_CONNECTED state. waiting";
    WaitForSignal(&queue.queue_state_, QueueState::HOST_CONNECTED);
  }
  return all_queues;
}

// --- Connection ---- //

void SocketForwardRegionView::ShmConnectionView::Receiver::Recv(Packet* packet) {
  std::unique_lock<std::mutex> guard(receive_thread_data_lock_);
  while (received_packet_free_) {
    receive_thread_data_cv_.wait(guard);
  }
  CHECK(received_packet_.IsData());
  *packet = received_packet_;
  received_packet_free_ = true;
  receive_thread_data_cv_.notify_one();
}

bool SocketForwardRegionView::ShmConnectionView::Receiver::GotRecvClosed() const {
      return received_packet_.IsRecvClosed() || (received_packet_.IsRestart()
#ifdef CUTTLEFISH_HOST
                                              && saw_data_
#endif
                                              );
}

bool SocketForwardRegionView::ShmConnectionView::Receiver::ShouldReceiveAnotherPacket() const {
        return (received_packet_.IsRecvClosed() && !saw_end_) ||
             (saw_end_ && received_packet_.IsEnd())
#ifdef CUTTLEFISH_HOST
             || (received_packet_.IsRestart() && !saw_data_) ||
             (received_packet_.IsBegin())
#endif
             ;
}

void SocketForwardRegionView::ShmConnectionView::Receiver::ReceivePacket() {
  view_->region_view()->Recv(view_->connection_id(), &received_packet_);
}

void SocketForwardRegionView::ShmConnectionView::Receiver::CheckPacketForRecvClosed() {
      if (GotRecvClosed()) {
        saw_recv_closed_ = true;
        view_->MarkOtherSideRecvClosed();
      }
#ifdef CUTTLEFISH_HOST
      if (received_packet_.IsData()) {
        saw_data_ = true;
      }
#endif
}

void SocketForwardRegionView::ShmConnectionView::Receiver::CheckPacketForEnd() {
  if (received_packet_.IsEnd() || received_packet_.IsRestart()) {
    CHECK(!saw_end_ || received_packet_.IsRestart());
    saw_end_ = true;
  }
}


bool SocketForwardRegionView::ShmConnectionView::Receiver::ExpectMorePackets() const {
  return !saw_recv_closed_ || !saw_end_;
}

void SocketForwardRegionView::ShmConnectionView::Receiver::UpdatePacketAndSignalAvailable() {
  if (!received_packet_.IsData()) {
    static constexpr auto kEmptyPacket = Packet::MakeData();
    received_packet_ = kEmptyPacket;
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
      SendSignal(layout::Sides::Peer, &queue_pair.host_to_guest.queue_state_);
      SendSignal(layout::Sides::Peer, &queue_pair.guest_to_host.queue_state_);
      return id;
    }

    do {
      ReceivePacket();
      CheckPacketForRecvClosed();
    } while (ShouldReceiveAnotherPacket());

    if (received_packet_.empty()) {
      LOG(ERROR) << "Received empty packet.";
    }

    CheckPacketForEnd();

    UpdatePacketAndSignalAvailable();
  }
}

auto SocketForwardRegionView::ShmConnectionView::ResetAndConnect()
    -> ShmSenderReceiverPair {
  if (receiver_) {
    receiver_->Join();
  }

  {
    std::lock_guard<std::mutex> guard(*other_side_receive_closed_lock_);
    other_side_receive_closed_ = false;
  }

#ifdef CUTTLEFISH_HOST
  region_view()->IgnoreUntilBegin(connection_id());
  region_view()->Send(connection_id(), Packet::MakeBegin(port_));
#else
  region_view()->Send(connection_id(), Packet::MakeBegin(port_));
  port_ =
      region_view()->IgnoreUntilBegin(connection_id());
#endif

  receiver_.reset(new Receiver{this});
  return {ShmSender{this}, ShmReceiver{this}};
}

#ifdef CUTTLEFISH_HOST
auto SocketForwardRegionView::ShmConnectionView::EstablishConnection(int port)
    -> ShmSenderReceiverPair {
  port_ = port;
  return ResetAndConnect();
}
#else
auto SocketForwardRegionView::ShmConnectionView::WaitForNewConnection()
    -> ShmSenderReceiverPair {
  port_ = 0;
  return ResetAndConnect();
}
#endif

bool SocketForwardRegionView::ShmConnectionView::Send(const Packet& packet) {
  if (packet.empty()) {
    LOG(ERROR) << "Sending empty packet";
  }
  if (packet.IsData() && IsOtherSideRecvClosed()) {
    return false;
  }
  return region_view()->Send(connection_id(), packet);
}

void SocketForwardRegionView::ShmConnectionView::Recv(Packet* packet) {
  receiver_->Recv(packet);
}

void SocketForwardRegionView::ShmReceiver::Recv(Packet* packet) {
  view_->Recv(packet);
}

bool SocketForwardRegionView::ShmSender::Send(const Packet& packet) {
  return view_->Send(packet);
}
