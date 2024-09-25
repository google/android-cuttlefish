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

#include <fstream>
#include <optional>
#include <string>

#include "google/protobuf/map.h"

#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/unix_sockets.h"
#include "cuttlefish/host/commands/cvd/server_client.h"

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

  return RequestWithStdio::StdIo(std::move(request));
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

RequestWithStdio RequestWithStdio::StdIo() {
  return RequestWithStdio::StdIo({});
}

RequestWithStdio RequestWithStdio::NullIo() {
  return RequestWithStdio::NullIo({});
}

RequestWithStdio RequestWithStdio::InheritIo(const RequestWithStdio& other) {
  return RequestWithStdio::InheritIo({}, other);
}

RequestWithStdio RequestWithStdio::StdIo(cvd::Request message) {
  return RequestWithStdio(std::move(message), std::cin, std::cout, std::cerr);
}

static std::istream& NullIn() {
  static std::ifstream* in = new std::ifstream("/dev/null");
  return *in;
}

static std::ostream& NullOut() {
  static std::ofstream* out = new std::ofstream("/dev/null");
  return *out;
}

RequestWithStdio RequestWithStdio::NullIo(cvd::Request message) {
  return RequestWithStdio(std::move(message), NullIn(), NullOut(), NullOut());
}

RequestWithStdio RequestWithStdio::InheritIo(cvd::Request message,
                                             const RequestWithStdio& other) {
  return RequestWithStdio(std::move(message), other.in_, other.out_,
                          other.err_);
}

RequestWithStdio::RequestWithStdio(cvd::Request message, std::istream& in,
                                   std::ostream& out, std::ostream& err)
    : message_(std::move(message)), in_(in), out_(out), err_(err) {}

cvd::Request& RequestWithStdio::Message() { return message_; }

const cvd::Request& RequestWithStdio::Message() const { return message_; }

std::istream& RequestWithStdio::In() const { return in_; }

std::ostream& RequestWithStdio::Out() const { return out_; }

std::ostream& RequestWithStdio::Err() const { return err_; }

bool RequestWithStdio::IsNullIo() const {
  return &in_ == &NullIn() && &out_ == &NullOut() && &err_ == &NullOut();
}

RequestWithStdio& RequestWithStdio::AddArgument(std::string argument) & {
  message_.mutable_command_request()->mutable_args()->Add(std::move(argument));
  return *this;
}

RequestWithStdio RequestWithStdio::AddArgument(std::string argument) && {
  message_.mutable_command_request()->mutable_args()->Add(std::move(argument));
  return *this;
}

RequestWithStdio& RequestWithStdio::AddSelectorArgument(
    std::string argument) & {
  message_.mutable_command_request()
      ->mutable_selector_opts()
      ->mutable_args()
      ->Add(std::move(argument));
  return *this;
}

RequestWithStdio RequestWithStdio::AddSelectorArgument(
    std::string argument) && {
  message_.mutable_command_request()
      ->mutable_selector_opts()
      ->mutable_args()
      ->Add(std::move(argument));
  return *this;
}

RequestWithStdio& RequestWithStdio::ImportEnv(const cvd_common::Envs& env) & {
  for (const auto& [key, value] : env) {
    EnvsProtoMap()[key] = value;
  }
  return *this;
}

RequestWithStdio RequestWithStdio::ImportEnv(const cvd_common::Envs& env) && {
  for (const auto& [key, value] : env) {
    EnvsProtoMap()[key] = value;
  }
  return *this;
}

google::protobuf::Map<std::string, std::string>&
RequestWithStdio::EnvsProtoMap() {
  return *message_.mutable_command_request()->mutable_env();
}

const google::protobuf::Map<std::string, std::string>&
RequestWithStdio::EnvsProtoMap() const {
  return message_.command_request().env();
}

RequestWithStdio& RequestWithStdio::SetEnvsProtoMap(
    google::protobuf::Map<std::string, std::string> map) & {
  *message_.mutable_command_request()->mutable_env() = map;
  return *this;
}

RequestWithStdio RequestWithStdio::SetEnvsProtoMap(
    google::protobuf::Map<std::string, std::string> map) && {
  *message_.mutable_command_request()->mutable_env() = map;
  return *this;
}

const std::string& RequestWithStdio::WorkingDirectory() const {
  return Message().command_request().working_directory();
}

RequestWithStdio& RequestWithStdio::SetWorkingDirectory(
    std::string working_directory) & {
  *message_.mutable_command_request()->mutable_working_directory() =
      std::move(working_directory);
  return *this;
}

RequestWithStdio RequestWithStdio::SetWorkingDirectory(
    std::string working_directory) && {
  *message_.mutable_command_request()->mutable_working_directory() =
      std::move(working_directory);
  return *this;
}

}  // namespace cuttlefish
