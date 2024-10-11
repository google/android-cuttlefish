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

#include "host/commands/cvd/command_request.h"

#include <string>

#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/command_request.h"

namespace cuttlefish {

static Result<UnixMessageSocket> GetClient(const SharedFD& client) {
  UnixMessageSocket result(client);
  CF_EXPECT(result.EnableCredentials(true),
            "Unable to enable UnixMessageSocket credentials.");
  return result;
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

CommandRequest::CommandRequest(cvd_common::Args args, cvd_common::Envs env,
                               cvd_common::Args selector_args)
    : args_(std::move(args)),
      env_(std::move(env)),
      selector_args_(std::move(selector_args)) {}

const cvd_common::Args& CommandRequest::Args() const { return args_; }

const cvd_common::Args& CommandRequest::SelectorArgs() const {
  return selector_args_;
}

const cvd_common::Envs& CommandRequest::Env() const { return env_; }

CommandRequestBuilder& CommandRequestBuilder::AddArguments(
    std::initializer_list<std::string_view> args) & {
  return AddArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder CommandRequestBuilder::AddArguments(
    std::initializer_list<std::string_view> args) && {
  return AddArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder& CommandRequestBuilder::AddSelectorArguments(
    std::initializer_list<std::string_view> args) & {
  return AddSelectorArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder CommandRequestBuilder::AddSelectorArguments(
    std::initializer_list<std::string_view> args) && {
  return AddSelectorArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder& CommandRequestBuilder::SetEnv(cvd_common::Envs env) & {
  env_ = std::move(env);
  return *this;
}

CommandRequestBuilder CommandRequestBuilder::SetEnv(cvd_common::Envs env) && {
  env_ = std::move(env);
  return *this;
}

CommandRequestBuilder& CommandRequestBuilder::AddEnvVar(std::string key,
                                                        std::string val) & {
  env_[key] = val;
  return *this;
}

CommandRequestBuilder CommandRequestBuilder::AddEnvVar(std::string key,
                                                       std::string val) && {
  env_[key] = val;
  return *this;
}

Result<CommandRequest> CommandRequestBuilder::Build() && {
  return CommandRequest(std::move(args_), std::move(env_),
                        std::move(selector_args_));
}

}  // namespace cuttlefish
