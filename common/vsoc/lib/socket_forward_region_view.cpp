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

using vsoc::socket_forward::Message;
using vsoc::socket_forward::SocketForwardRegionView;

constexpr std::int32_t kConnectionBegin = -1;
constexpr std::int32_t kConnectionEnd = -2;

Message SocketForwardRegionView::Recv(int connection_id) {
  std::int32_t len{};
  (data()->queues_[connection_id].*ReadDirection)
      .Read(this, reinterpret_cast<char*>(&len), sizeof len);
  if (len == kConnectionEnd) {
    return {};
  }
  CHECK_NE(len, 0) << "zero-size message received";
  CHECK_GT(len, 0) << "invalid size";
  Message message(len);
  (data()->queues_[connection_id].*ReadDirection)
      .Read(this, reinterpret_cast<char*>(message.data()), message.size());
  return message;
}

void SocketForwardRegionView::Send(int connection_id, const Message& message) {
  if (message.empty()) {
    return;
  }
  std::int32_t len = message.size();
  (data()->queues_[connection_id].*WriteDirection)
      .Write(this, reinterpret_cast<const char*>(&len), sizeof len);
  (data()->queues_[connection_id].*WriteDirection)
      .Write(this, reinterpret_cast<const char*>(message.data()),
             message.size());
}

void SocketForwardRegionView::SendBegin(int connection_id) {
  (data()->queues_[connection_id].*WriteDirection)
      .Write(this, reinterpret_cast<const char*>(&kConnectionBegin),
             sizeof kConnectionBegin);
}

void SocketForwardRegionView::SendEnd(int connection_id) {
  (data()->queues_[connection_id].*WriteDirection)
      .Write(this, reinterpret_cast<const char*>(&kConnectionEnd),
             sizeof kConnectionEnd);
}

void SocketForwardRegionView::IgnoreUntilBegin(int connection_id) {
  Message ignored(128);
  while (true) {
    std::int32_t len{};
    (data()->queues_[connection_id].*ReadDirection)
        .Read(this, reinterpret_cast<char*>(&len), sizeof len);
    if (len == kConnectionBegin) {
      break;
    } else if (len == kConnectionEnd) {
      continue;
    }

    CHECK_NE(len, 0) << "zero-size message received";
    CHECK_GT(len, 0) << "invalid size";
    ignored.resize(len);
    (data()->queues_[connection_id].*ReadDirection)
        .Read(this, reinterpret_cast<char*>(ignored.data()), ignored.size());
  }
}

#ifdef CUTTLEFISH_HOST
int SocketForwardRegionView::AcquireConnectionID(int port) {
  int id = 0;
  for (auto&& queue_pair : data()->queues_) {
    LOG(DEBUG) << "locking and checking queue at index " << id;
    auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
    if (queue_pair.queue_state_ == QueuePair::INACTIVE) {
      queue_pair.port_ = port;
      queue_pair.queue_state_ = QueuePair::HOST_CONNECTED;
      LOG(DEBUG) << "acquired queue " << id << " . current seq_num: "
                 << data()->seq_num;
      ++data()->seq_num;
      return id;
    }
    ++id;
  }
  // TODO(haining) handle all queues being used
  LOG(FATAL) << "no remaining shm queues for connection";
  return -1;
}
#endif

namespace {
bool OtherSideDisconnected(const QueuePair& queue_pair) {
  constexpr auto kOtherSideClosed = QueuePair::
#ifdef CUTTLEFISH_HOST
      GUEST_CLOSED;
#else
      HOST_CLOSED;
#endif
  return queue_pair.queue_state_ == kOtherSideClosed;
}

void MarkThisSideDisconnected(QueuePair* queue_pair) {
  constexpr auto kThisSideClosed = QueuePair::
#ifdef CUTTLEFISH_HOST
      HOST_CLOSED;
#else
      GUEST_CLOSED;
#endif
  queue_pair->queue_state_ = kThisSideClosed;
}

}  // namespace

bool SocketForwardRegionView::IsOtherSideClosed(int connection_id) {
  auto& queue_pair = data()->queues_[connection_id];
  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
  return OtherSideDisconnected(queue_pair);
}

void SocketForwardRegionView::ReleaseConnectionID(int connection_id) {
  auto& queue_pair = data()->queues_[connection_id];
  auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
  if (OtherSideDisconnected(queue_pair)) {
    queue_pair.port_ = 0;
    queue_pair.queue_state_ = QueuePair::INACTIVE;
  } else {
    Send(connection_id, {});
    MarkThisSideDisconnected(&queue_pair);
  }
}

std::pair<int, int> SocketForwardRegionView::GetWaitingConnectionIDAndPort() {
  while (data()->seq_num == last_seq_number_) {
    WaitForSignal(&data()->seq_num, last_seq_number_);
  }
  ++last_seq_number_;
  int id = 0;
  for (auto&& queue_pair : data()->queues_) {
    LOG(DEBUG) << "locking and checking queue at index " << id;
    auto guard = make_lock_guard(&queue_pair.queue_state_lock_);
    if (queue_pair.queue_state_ == QueuePair::HOST_CONNECTED) {
      LOG(DEBUG) << "found waiting connection at index " << id;
      queue_pair.queue_state_ = QueuePair::BOTH_CONNECTED;
      return {id, queue_pair.port_};
    }
    ++id;
  }
  return {-1, -1};
}

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<SocketForwardRegionView> SocketForwardRegionView::GetInstance(
    const char* domain) {
  return RegionView::GetInstanceImpl<SocketForwardRegionView>(
      [](std::shared_ptr<SocketForwardRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<SocketForwardRegionView> SocketForwardRegionView::GetInstance()
{
  return RegionView::GetInstanceImpl<SocketForwardRegionView>(
      std::mem_fn(&SocketForwardRegionView::Open));
}
#endif

#ifdef CUTTLEFISH_HOST
SocketForwardRegionView::Connection SocketForwardRegionView::OpenConnection(
    int port) {
  return {this, AcquireConnectionID(port), port};
}
#else
SocketForwardRegionView::Connection
SocketForwardRegionView::AcceptConnection() {
  int connection_id = -1;
  int port = -1;
  while (connection_id < 0) {
    // TODO(haining) if ever in C++17, structured binding declaration
    auto id_and_port = GetWaitingConnectionIDAndPort();
    connection_id = id_and_port.first;
    port = id_and_port.second;
  }
  return {this, connection_id, port};
}
#endif

// --- Connection ---- //
SocketForwardRegionView::Connection::Connection(SocketForwardRegionView* view,
                                                int connection_id, int port)
    : view_{view, {connection_id}}, connection_id_{connection_id}, port_{port} {
  LOG(INFO) << "opened connection with id " << connection_id_;
}

SocketForwardRegionView::Sender
SocketForwardRegionView::Connection::MakeSender() {
  CHECK(!sender_created_);
  sender_created_ = true;
  return Sender{this};
}

SocketForwardRegionView::Receiver
SocketForwardRegionView::Connection::MakeReceiver() {
  CHECK(!receiver_created_);
  receiver_created_ = true;
  return Receiver{this};
}

void SocketForwardRegionView::Connection::IgnoreUntilBegin() {
  view_->IgnoreUntilBegin(connection_id_);
}

Message SocketForwardRegionView::Connection::Recv() {
  return view_->Recv(connection_id_);
}

bool SocketForwardRegionView::Connection::closed() const {
  return view_->IsOtherSideClosed(connection_id_);
}

void SocketForwardRegionView::Connection::SendEnd() {
  view_->SendEnd(connection_id_);
}

void SocketForwardRegionView::Connection::SendBegin() {
  view_->SendBegin(connection_id_);
}

void SocketForwardRegionView::Connection::Send(const Message& message) {
  if (closed()) {
    LOG(INFO) << "connection closed, not sending\n";
    return;
  }
  view_->Send(connection_id_, message);
}
