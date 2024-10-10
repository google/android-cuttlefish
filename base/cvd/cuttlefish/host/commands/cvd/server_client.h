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

#include <initializer_list>
#include <string_view>

#include <google/protobuf/map.h>

#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class RequestWithStdio {
 public:
  RequestWithStdio() = default;

  template <typename T>
  RequestWithStdio& AddArguments(T&& args) & {
    for (auto&& arg : args) {
      args_.emplace_back(arg);
    }
    return *this;
  }

  template <typename T>
  RequestWithStdio AddArguments(T&& args) && {
    for (auto&& arg : args) {
      args_.emplace_back(arg);
    }
    return *this;
  }

  RequestWithStdio& AddArguments(std::initializer_list<std::string_view>) &;
  RequestWithStdio AddArguments(std::initializer_list<std::string_view>) &&;

  const cvd_common::Args& Args() const;

  template <typename T>
  RequestWithStdio& AddSelectorArguments(T&& args) & {
    for (auto&& arg : args) {
      selector_args_.emplace_back(arg);
    }
    return *this;
  }

  template <typename T>
  RequestWithStdio AddSelectorArguments(T&& args) && {
    for (auto&& arg : args) {
      selector_args_.emplace_back(arg);
    }
    return *this;
  }

  RequestWithStdio& AddSelectorArguments(
      std::initializer_list<std::string_view>) &;
  RequestWithStdio AddSelectorArguments(
      std::initializer_list<std::string_view>) &&;

  const cvd_common::Args& SelectorArgs() const;

  const cvd_common::Envs& Env() const;
  cvd_common::Envs& Env();

  RequestWithStdio& SetEnv(cvd_common::Envs) &;
  RequestWithStdio SetEnv(cvd_common::Envs) &&;

 private:
  cvd_common::Args args_;
  cvd_common::Envs env_;
  cvd_common::Args selector_args_;
};

Result<void> SendResponse(const SharedFD& client,
                          const cvd::Response& response);

}  // namespace cuttlefish
