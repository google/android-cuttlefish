//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/type_name.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/kernel_log_pipe_provider.h"

namespace cuttlefish {

template <class...>
constexpr std::false_type CommandAlwaysFalse{};

template <auto Fn, typename R, typename... Args>
class GenericCommandSource : public CommandSource,
                             public KernelLogPipeConsumer {
 public:
  INJECT(GenericCommandSource(Args... args))
      : args_(std::forward_as_tuple(args...)) {}

  Result<void> ResultSetup() override {
    commands_.clear();
    if constexpr (std::is_same_v<R, Result<std::vector<MonitorCommand>>>) {
      commands_ = CF_EXPECT(std::apply(Fn, args_));
    } else if constexpr (std::is_same_v<R, std::vector<MonitorCommand>>) {
      commands_ = std::apply(Fn, args_);
    } else if constexpr (std::is_same_v<R, Result<MonitorCommand>>) {
      commands_.emplace_back(CF_EXPECT(std::apply(Fn, args_)));
    } else if constexpr (std::is_same_v<R, MonitorCommand>) {
      commands_.emplace_back(std::apply(Fn, args_));
    } else if constexpr (std::is_same_v<
                             R, Result<std::optional<MonitorCommand>>>) {
      auto cmd = CF_EXPECT(std::apply(Fn, args_));
      if (cmd) {
        commands_.emplace_back(std::move(*cmd));
      }
    } else if constexpr (std::is_same_v<R, std::optional<MonitorCommand>>) {
      auto cmd = std::apply(Fn, args_);
      if (cmd) {
        commands_.emplace_back(std::move(*cmd));
      }
    } else {
      static_assert(CommandAlwaysFalse<R>, "Unexpected AutoCmd return type");
    }
    return {};
  }

  bool Enabled() const override {
    return true;  // TODO(schuffelen): Delete `Enabled()`, it hasn't been useful
  }

  std::string Name() const override {
    static constexpr auto kName = ValueName<Fn>();
    return std::string(kName);
  }

  std::unordered_set<SetupFeature*> Dependencies() const override {
    return SetupFeatureDeps(args_);
  }

  Result<std::vector<MonitorCommand>> Commands() override {
    return std::move(commands_);
  }

 private:
  std::tuple<Args...> args_;
  std::vector<MonitorCommand> commands_;
};

template <auto Fn1, typename Fn2>
struct GenericCommandImpl;

template <auto Fn, typename R, typename... Args>
struct GenericCommandImpl<Fn, R (*)(Args...)> {
  using Type = GenericCommandSource<Fn, R, Args...>;

  static fruit::Component<
      fruit::Required<typename std::remove_reference_t<Args>...>, Type>
  Component() {
    auto cmd = fruit::createComponent()
                   .template addMultibinding<CommandSource, Type>()
                   .template addMultibinding<SetupFeature, Type>();
    constexpr bool uses_kernel_log_pipe =
        (std::is_base_of_v<KernelLogPipeProvider,
                           std::remove_reference_t<Args>> ||
         ...);
    if constexpr (uses_kernel_log_pipe) {
      return cmd.template addMultibinding<KernelLogPipeConsumer, Type>();
    } else {
      return cmd;
    }
  }
};

template <auto Fn>
using AutoCmd = GenericCommandImpl<Fn, decltype(Fn)>;

}  // namespace cuttlefish
