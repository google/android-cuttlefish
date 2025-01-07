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
#include <string>
#include <string_view>

#include <google/protobuf/map.h>

#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/selector/selector_common_parser.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {

class CommandRequest {
 public:
  const cvd_common::Envs& Env() const { return env_; }

  const selector::SelectorOptions& Selectors() const { return selectors_; }

  const std::string& Subcommand() const { return subcommand_; }
  const std::vector<std::string>& SubcommandArguments() const {
    return subcommand_arguments_;
  }

 private:
  friend class CommandRequestBuilder;
  CommandRequest(cvd_common::Args args, cvd_common::Envs env,
                 selector::SelectorOptions selectors);

  cvd_common::Args args_;
  cvd_common::Envs env_;
  selector::SelectorOptions selectors_;

  std::string subcommand_;
  std::vector<std::string> subcommand_arguments_;
};

class CommandRequestBuilder {
 public:
  CommandRequestBuilder() = default;

  template <typename T>
  CommandRequestBuilder& AddArguments(T&& args) & {
    for (auto&& arg : args) {
      args_.emplace_back(arg);
    }
    return *this;
  }

  template <typename T>
  CommandRequestBuilder AddArguments(T&& args) && {
    for (auto&& arg : args) {
      args_.emplace_back(arg);
    }
    return *this;
  }

  CommandRequestBuilder& AddArguments(
      std::initializer_list<std::string_view>) &;
  CommandRequestBuilder AddArguments(
      std::initializer_list<std::string_view>) &&;

  template <typename T>
  CommandRequestBuilder& AddSelectorArguments(T&& args) & {
    for (auto&& arg : args) {
      selector_args_.emplace_back(arg);
    }
    return *this;
  }

  template <typename T>
  CommandRequestBuilder AddSelectorArguments(T&& args) && {
    for (auto&& arg : args) {
      selector_args_.emplace_back(arg);
    }
    return *this;
  }

  CommandRequestBuilder& AddSelectorArguments(
      std::initializer_list<std::string_view>) &;
  CommandRequestBuilder AddSelectorArguments(
      std::initializer_list<std::string_view>) &&;

  CommandRequestBuilder& SetEnv(cvd_common::Envs) &;
  CommandRequestBuilder SetEnv(cvd_common::Envs) &&;

  CommandRequestBuilder& AddEnvVar(std::string key, std::string val) &;
  CommandRequestBuilder AddEnvVar(std::string key, std::string val) &&;

  Result<CommandRequest> Build() &&;

 private:
  cvd_common::Args args_;
  cvd_common::Envs env_;
  cvd_common::Args selector_args_;
};

}  // namespace cuttlefish
