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

#include <string>

#include <google/protobuf/map.h>

#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class RequestWithStdio {
 public:
  static RequestWithStdio StdIo();
  static RequestWithStdio NullIo();
  static RequestWithStdio InheritIo(const RequestWithStdio&);

  std::istream& In() const;
  std::ostream& Out() const;
  std::ostream& Err() const;

  bool IsNullIo() const;

  // Convenient getters/setters to properties in the underlying message
  RequestWithStdio& OverrideRequest(RequestWithStdio) &;
  RequestWithStdio OverrideRequest(RequestWithStdio) &&;

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

  const cvd_common::Args& Args() const;

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

  const cvd_common::Args& SelectorArgs() const;

  RequestWithStdio& ImportEnv(const cvd_common::Envs&) &;
  RequestWithStdio ImportEnv(const cvd_common::Envs&) &&;

  const cvd_common::Envs& Env() const;
  cvd_common::Envs& Env();

  RequestWithStdio& SetEnv(cvd_common::Envs) &;
  RequestWithStdio SetEnv(cvd_common::Envs) &&;

  const std::string& WorkingDirectory() const;
  RequestWithStdio& SetWorkingDirectory(std::string) &;
  RequestWithStdio SetWorkingDirectory(std::string) &&;

 private:
  RequestWithStdio(std::istream&, std::ostream&, std::ostream&);
  cvd_common::Args args_;
  cvd_common::Envs env_;
  cvd_common::Args selector_args_;
  std::string working_directory_;

  std::istream& in_;
  std::ostream& out_;
  std::ostream& err_;
};

Result<void> SendResponse(const SharedFD& client,
                          const cvd::Response& response);

}  // namespace cuttlefish
