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

  return RequestWithStdio(client, std::move(request), std::move(fds),
                          std::move(creds));
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

RequestWithStdio::RequestWithStdio(SharedFD client_fd, cvd::Request message,
                                   std::vector<SharedFD> fds,
                                   std::optional<ucred> creds)
    : client_fd_(client_fd),
      message_(message),
      fds_(std::move(fds)),
      creds_(creds) {}

SharedFD RequestWithStdio::Client() const { return client_fd_; }

const cvd::Request& RequestWithStdio::Message() const { return message_; }

const std::vector<SharedFD>& RequestWithStdio::FileDescriptors() const {
  return fds_;
}

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

}  // namespace cuttlefish
