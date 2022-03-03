/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/server_client.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <optional>
#include <queue>
#include <thread>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/unix_sockets.h"

namespace cuttlefish {
namespace {

Result<UnixMessageSocket> GetClient(const SharedFD& client) {
  UnixMessageSocket result(client);
  CF_EXPECT(result.EnableCredentials(true),
            "Unable to enable UnixMessageSocket credentials.");
  return result;
}

Result<std::optional<RequestWithStdio>> GetRequest(const SharedFD& client) {
  UnixMessageSocket reader =
      CF_EXPECT(GetClient(client), "Couldn't get client");
  auto read_result = CF_EXPECT(reader.ReadMessage(), "Couldn't read message");

  if (read_result.data.empty()) {
    LOG(VERBOSE) << "Read empty packet, so the client has probably closed the "
                    "connection.";
    return {};
  };

  std::string serialized(read_result.data.begin(), read_result.data.end());
  cvd::Request request;
  CF_EXPECT(request.ParseFromString(serialized),
            "Unable to parse serialized request proto.");

  CF_EXPECT(read_result.HasFileDescriptors(),
            "Missing stdio fds from request.");
  auto fds = CF_EXPECT(read_result.FileDescriptors(),
                       "Error reading stdio fds from request");
  CF_EXPECT(fds.size() == 3 || fds.size() == 4, "Wrong number of FDs, received "
                                                    << fds.size()
                                                    << ", wanted 3 or 4");

  std::optional<ucred> creds;
  if (read_result.HasCredentials()) {
    // TODO(b/198453477): Use Credentials to control command access.
    creds = CF_EXPECT(read_result.Credentials(), "Failed to get credentials");
    LOG(DEBUG) << "Has credentials, uid=" << creds->uid;
  }

  return RequestWithStdio(std::move(request), std::move(fds), std::move(creds));
}

Result<void> SendResponse(const SharedFD& client,
                          const cvd::Response& response) {
  std::string serialized;
  CF_EXPECT(response.SerializeToString(&serialized),
            "Unable to serialize response proto.");
  UnixSocketMessage message;
  message.data = std::vector<char>(serialized.begin(), serialized.end());

  UnixMessageSocket writer =
      CF_EXPECT(GetClient(client), "Couldn't get client");
  CF_EXPECT(writer.WriteMessage(message));
  return {};
}

}  // namespace

RequestWithStdio::RequestWithStdio(cvd::Request message,
                                   std::vector<SharedFD> fds,
                                   std::optional<ucred> creds)
    : message_(message), fds_(std::move(fds)), creds_(creds) {}

const cvd::Request& RequestWithStdio::Message() const { return message_; }

SharedFD RequestWithStdio::In() const {
  return fds_.size() > 0 ? fds_[0] : SharedFD();
}

SharedFD RequestWithStdio::Out() const {
  return fds_.size() > 1 ? fds_[1] : SharedFD();
}

SharedFD RequestWithStdio::Err() const {
  return fds_.size() > 2 ? fds_[2] : SharedFD();
}

std::optional<SharedFD> RequestWithStdio::Extra() const {
  return fds_.size() > 3 ? fds_[3] : std::optional<SharedFD>{};
}

std::optional<ucred> RequestWithStdio::Credentials() const { return creds_; }

class ClientMessageQueue::Internal {
 public:
  SharedFD client_;
  std::thread thread_;  // TODO(schuffelen): Use a thread pool
  std::atomic_bool running_;
  std::condition_variable request_queue_condition_variable_;
  std::mutex request_queue_mutex_;
  std::queue<RequestWithStdio> request_queue_;
  SharedFD event_;
  std::mutex response_queue_mutex_;
  std::queue<cvd::Response> response_queue_;

  ~Internal();
  Result<void> Stop();
  void Join();
  Result<void> Loop();
};

Result<ClientMessageQueue> ClientMessageQueue::Create(SharedFD client) {
  std::unique_ptr<ClientMessageQueue::Internal> internal(
      new ClientMessageQueue::Internal);
  internal->client_ = client;
  internal->running_ = true;
  internal->event_ = SharedFD::Event();
  CF_EXPECT(internal->event_->IsOpen(),
            "Failed to create event fd: " << internal->event_->StrError());
  internal->thread_ = std::thread([internal = internal.get()]() {
    auto result = internal->Loop();
    if (!result.ok()) {
      LOG(ERROR) << "Client thread error: {\n" << result.error() << "\n}";
    }
    internal->running_ = false;
    internal->request_queue_condition_variable_.notify_all();
  });
  return ClientMessageQueue(std::move(internal));
}

ClientMessageQueue::ClientMessageQueue(std::unique_ptr<Internal> internal)
    : internal_(std::move(internal)) {}

ClientMessageQueue::ClientMessageQueue(ClientMessageQueue&&) = default;

ClientMessageQueue::~ClientMessageQueue() = default;

ClientMessageQueue& ClientMessageQueue::operator=(ClientMessageQueue&&) =
    default;

Result<RequestWithStdio> ClientMessageQueue::WaitForRequest() {
  CF_EXPECT(internal_.get(), "inactive class instance");
  std::unique_lock lock(internal_->request_queue_mutex_);
  while (internal_->running_ && internal_->request_queue_.empty()) {
    internal_->request_queue_condition_variable_.wait(lock);
  }
  CF_EXPECT(!internal_->request_queue_.empty(), "Request queue has stopped");
  auto response = std::move(internal_->request_queue_.front());
  internal_->request_queue_.pop();
  return response;
}

Result<void> ClientMessageQueue::PostResponse(const cvd::Response& response) {
  CF_EXPECT(internal_.get(), "inactive class instance");
  std::scoped_lock lock(internal_->response_queue_mutex_);
  internal_->response_queue_.emplace(response);
  CF_EXPECT(internal_->event_->EventfdWrite(1) == 0,
            internal_->event_->StrError());
  return {};
}

Result<void> ClientMessageQueue::Stop() {
  if (internal_) {
    return internal_->Stop();
  }
  return {};
}

void ClientMessageQueue::Join() {
  if (internal_) {
    internal_->Join();
  }
}

ClientMessageQueue::Internal::~Internal() {
  auto stop_res = Stop();
  CHECK(stop_res.ok()) << stop_res.error().message();
  Join();
}

Result<void> ClientMessageQueue::Internal::Stop() {
  running_ = false;
  if (event_->IsOpen()) {
    CF_EXPECT(event_->EventfdWrite(1) == 0, event_->StrError());
  }
  return {};
}

void ClientMessageQueue::Internal::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

Result<void> ClientMessageQueue::Internal::Loop() {
  while (running_) {
    SharedFDSet read_set;
    read_set.Set(client_);
    read_set.Set(event_);
    SharedFDSet write_set;
    {
      std::scoped_lock lock(response_queue_mutex_);
      if (!response_queue_.empty()) {
        write_set.Set(client_);
      }
    }
    int fds = Select(&read_set, &write_set, nullptr, nullptr);
    CF_EXPECT(fds > 0, strerror(errno));
    if (read_set.IsSet(client_)) {
      auto request = CF_EXPECT(GetRequest(client_));
      if (!request) {
        break;
      }
      std::scoped_lock lock(request_queue_mutex_);
      request_queue_.emplace(*request);
      request_queue_condition_variable_.notify_one();
    }
    if (read_set.IsSet(event_)) {
      eventfd_t eventfd_num;
      CF_EXPECT(event_->EventfdRead(&eventfd_num) == 0);
    }
    if (write_set.IsSet(client_)) {
      cvd::Response response;
      {
        std::scoped_lock lock(response_queue_mutex_);
        CF_EXPECT(!response_queue_.empty());
        response = response_queue_.front();
        response_queue_.pop();
      }
      CF_EXPECT(SendResponse(client_, response));
    }
  }
  return {};
}

}  // namespace cuttlefish
