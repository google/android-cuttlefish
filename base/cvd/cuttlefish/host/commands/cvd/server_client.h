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

#pragma once

#include <sys/socket.h>
#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#include <google/protobuf/map.h>

#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class RequestWithStdio {
 public:
  static RequestWithStdio StdIo();
  static RequestWithStdio NullIo();
  static RequestWithStdio InheritIo(const RequestWithStdio&);
  static RequestWithStdio StdIo(cvd::Request);
  static RequestWithStdio NullIo(cvd::Request);
  static RequestWithStdio InheritIo(cvd::Request, const RequestWithStdio&);

  cvd::Request& Message();
  const cvd::Request& Message() const;
  std::istream& In() const;
  std::ostream& Out() const;
  std::ostream& Err() const;

  bool IsNullIo() const;

  // Convenient getters/setters to properties in the underlying message
  RequestWithStdio& AddArgument(std::string) &;
  RequestWithStdio AddArgument(std::string) &&;

  template <typename T>
  RequestWithStdio& AddArguments(const T& args) & {
    for (const std::string& arg : args) {
      AddArgument(arg);
    }
    return *this;
  }

  template <typename T>
  RequestWithStdio AddArguments(const T& args) && {
    for (const std::string& arg : args) {
      AddArgument(arg);
    }
    return *this;
  }

  cvd_common::Args Args() const {
    return cvd_common::ConvertToArgs(Message().command_request().args());
  }

  RequestWithStdio& AddSelectorArgument(std::string) &;
  RequestWithStdio AddSelectorArgument(std::string) &&;

  template <typename T>
  RequestWithStdio& AddSelectorArguments(const T& args) & {
    for (const std::string& arg : args) {
      AddArgument(arg);
    }
    return *this;
  }

  template <typename T>
  RequestWithStdio AddSelectorArguments(const T& args) && {
    for (const std::string& arg : args) {
      AddArgument(arg);
    }
    return *this;
  }

  cvd_common::Args SelectorArgs() const {
    return cvd_common::ConvertToArgs(
        Message().command_request().selector_opts().args());
  }

  RequestWithStdio& ImportEnv(const cvd_common::Envs&) &;
  RequestWithStdio ImportEnv(const cvd_common::Envs&) &&;

  cvd_common::Envs Envs() const {
    return cvd_common::ConvertToEnvs(Message().command_request().env());
  }

  google::protobuf::Map<std::string, std::string>& EnvsProtoMap();
  const google::protobuf::Map<std::string, std::string>& EnvsProtoMap() const;
  RequestWithStdio& SetEnvsProtoMap(
      google::protobuf::Map<std::string, std::string>) &;
  RequestWithStdio SetEnvsProtoMap(
      google::protobuf::Map<std::string, std::string>) &&;

  RequestWithStdio& SetWaitBehavior(cvd::WaitBehavior) &;
  RequestWithStdio SetWaitBehavior(cvd::WaitBehavior) &&;

  const std::string& WorkingDirectory() const;
  RequestWithStdio& SetWorkingDirectory(std::string) &;
  RequestWithStdio SetWorkingDirectory(std::string) &&;

 private:
  RequestWithStdio(cvd::Request, std::istream&, std::ostream&, std::ostream&);

  cvd::Request message_;
  std::istream& in_;
  std::ostream& out_;
  std::ostream& err_;
};

Result<UnixMessageSocket> GetClient(const SharedFD& client);
Result<std::optional<RequestWithStdio>> GetRequest(const SharedFD& client);
Result<void> SendResponse(const SharedFD& client,
                          const cvd::Response& response);

}  // namespace cuttlefish
