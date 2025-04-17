/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <string>
#include <tuple>
#include <vector>

#include <fruit/fruit.h>

namespace cuttlefish {

class DiagnosticInformation {
 public:
  virtual ~DiagnosticInformation();
  virtual std::vector<std::string> Diagnostics() const = 0;

  static void PrintAll(const std::vector<DiagnosticInformation*>&);
};

template <auto Fn, typename R, typename... Args>
class DiagnosticInformationFn : public DiagnosticInformation {
 public:
  INJECT(DiagnosticInformationFn(Args... args))
      : args_(std::forward_as_tuple(args...)) {}

  std::vector<std::string> Diagnostics() const override {
    if constexpr (std::is_same_v<R, std::vector<std::string>>) {
      return std::apply(Fn, args_);
    } else if constexpr (std::is_same_v<R, std::string>) {
      return {std::apply(Fn, args_)};
    } else {
      static_assert(false, "Unexpected AutoDiagnostic return type");
    }
  }

 private:
  std::tuple<Args...> args_;
};

template <auto Fn1, typename Fn2>
struct DiagnosticInformationFnImpl;

template <auto Fn, typename R, typename... Args>
struct DiagnosticInformationFnImpl<Fn, R (*)(Args...)> {
  using Type = DiagnosticInformationFn<Fn, R, Args...>;

  static fruit::Component<
      fruit::Required<typename std::remove_reference_t<Args>...>, Type>
  Component() {
    return fruit::createComponent()
        .template addMultibinding<DiagnosticInformation, Type>();
  }
};

template <auto Fn>
using AutoDiagnostic = DiagnosticInformationFnImpl<Fn, decltype(Fn)>;

}  // namespace cuttlefish
