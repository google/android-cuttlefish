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

#include <ostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/type_name.h"

namespace cuttlefish {

template <typename Subclass>
class Feature {
 public:
  virtual ~Feature() = default;

  virtual std::string Name() const = 0;

  static Result<void> TopologicalVisit(
      const std::unordered_set<Subclass*>& features,
      const std::function<Result<void>(Subclass*)>& callback);

 private:
  virtual std::unordered_set<Subclass*> Dependencies() const = 0;
};

class SetupFeature : public virtual Feature<SetupFeature> {
 public:
  virtual ~SetupFeature();

  static Result<void> RunSetup(const std::vector<SetupFeature*>& features);

  virtual bool Enabled() const = 0;

 private:
  virtual Result<void> ResultSetup() = 0;
};

template <typename T>
class ReturningSetupFeature : public SetupFeature {
 public:
  ReturningSetupFeature() {
    if constexpr (std::is_void_v<T>) {
      calculated_ = false;
    } else {
      calculated_ = {};
    }
  }
  template <typename S = T>
  std::enable_if_t<!std::is_void_v<S>, S&> operator*() {
    CHECK(calculated_.has_value()) << "precondition violation";
    return *calculated_;
  }
  template <typename S = T>
  std::enable_if_t<!std::is_void_v<S>, const S&> operator*() const {
    CHECK(calculated_.has_value()) << "precondition violation";
    return *calculated_;
  }
  template <typename S = T>
  std::enable_if_t<!std::is_void_v<S>, S*> operator->() {
    CHECK(calculated_.has_value()) << "precondition violation";
    return &*calculated_;
  }
  template <typename S = T>
  std::enable_if_t<!std::is_void_v<S>, const S*> operator->() const {
    CHECK(calculated_.has_value()) << "precondition violation";
    return &*calculated_;
  }
  template <typename S = T>
  std::enable_if_t<!std::is_void_v<S>, S> Move() {
    CHECK(calculated_.has_value()) << "precondition violation";
    return std::move(*calculated_);
  }

 private:
  Result<void> ResultSetup() override final {
    if constexpr (std::is_void_v<T>) {
      CF_EXPECT(!calculated_, "precondition violation");
      CF_EXPECT(Calculate());
      calculated_ = true;
    } else {
      CF_EXPECT(!calculated_.has_value(), "precondition violation");
      calculated_ = CF_EXPECT(Calculate());
    }
    return {};
  }

  virtual Result<T> Calculate() = 0;

  std::conditional_t<std::is_void_v<T>, bool, std::optional<T>> calculated_;
};

class FlagFeature : public Feature<FlagFeature> {
 public:
  static Result<void> ProcessFlags(const std::vector<FlagFeature*>& features,
                                   std::vector<std::string>& flags);
  static bool WriteGflagsHelpXml(const std::vector<FlagFeature*>& features,
                                 std::ostream& out);

 private:
  // Must be executed in dependency order following Dependencies(). Expected to
  // mutate the `flags` argument to remove handled flags, and possibly introduce
  // new flag values (e.g. from a file).
  virtual Result<void> Process(std::vector<std::string>& flags) = 0;

  // TODO(schuffelen): Migrate the xml help to human-readable help output after
  // the gflags migration is done.

  // Write an xml fragment that is compatible with gflags' `--helpxml` format.
  virtual bool WriteGflagsCompatHelpXml(std::ostream& out) const = 0;
};

template <typename Subclass>
Result<void> Feature<Subclass>::TopologicalVisit(
    const std::unordered_set<Subclass*>& features,
    const std::function<Result<void>(Subclass*)>& callback) {
  enum class Status { UNVISITED, VISITING, VISITED };
  std::unordered_map<Subclass*, Status> features_status;
  for (const auto& feature : features) {
    features_status[feature] = Status::UNVISITED;
  }
  std::function<Result<void>(Subclass*)> visit;
  visit = [&callback, &features_status,
           &visit](Subclass* feature) -> Result<void> {
    CF_EXPECT(features_status.count(feature) > 0,
              "Dependency edge to "
                  << feature->Name() << " but it is not part of the feature "
                  << "graph. This feature is either disabled or not correctly "
                  << "registered.");
    if (features_status[feature] == Status::VISITED) {
      return {};
    }
    CF_EXPECT(features_status[feature] != Status::VISITING,
              "Cycle detected while visiting " << feature->Name());
    features_status[feature] = Status::VISITING;
    for (const auto& dependency : feature->Dependencies()) {
      CF_EXPECT(dependency != nullptr,
                "SetupFeature " << feature->Name() << " has a null dependency.");
      CF_EXPECT(visit(dependency),
                "Error detected while visiting " << feature->Name());
    }
    features_status[feature] = Status::VISITED;
    CF_EXPECT(callback(feature), "Callback error on " << feature->Name());
    return {};
  };
  for (const auto& feature : features) {
    CF_EXPECT(visit(feature));  // `visit` will log the error chain.
  }
  return {};
}

template <typename... Args>
std::unordered_set<SetupFeature*> SetupFeatureDeps(
    const std::tuple<Args...>& args) {
  std::unordered_set<SetupFeature*> deps;
  std::apply(
      [&deps](auto&&... arg) {
        (
            [&] {
              using ArgType = std::remove_reference_t<decltype(arg)>;
              if constexpr (std::is_base_of_v<SetupFeature, ArgType>) {
                deps.insert(static_cast<SetupFeature*>(&arg));
              }
            }(),
            ...);
      },
      args);
  return deps;
}

template <auto Fn, typename R, typename... Args>
class GenericReturningSetupFeature : public ReturningSetupFeature<R> {
 public:
  INJECT(GenericReturningSetupFeature(Args... args))
      : args_(std::forward_as_tuple(args...)) {}

  bool Enabled() const override { return true; }

  std::string Name() const override {
    static constexpr auto kName = ValueName<Fn>();
    return std::string(kName);
  }

  std::unordered_set<SetupFeature*> Dependencies() const override {
    return SetupFeatureDeps(args_);
  }

 private:
  Result<R> Calculate() override {
    if constexpr (std::is_void_v<R>) {
      CF_EXPECT(std::apply(Fn, args_));
      return {};
    } else {
      return CF_EXPECT(std::apply(Fn, args_));
    }
  }
  std::tuple<Args...> args_;
};

template <auto Fn1, typename Fn2>
struct GenericSetupImpl;

template <auto Fn, typename R, typename... Args>
struct GenericSetupImpl<Fn, Result<R> (*)(Args...)> {
  using Type = GenericReturningSetupFeature<Fn, R, Args...>;

  static fruit::Component<
      fruit::Required<typename std::remove_reference_t<Args>...>, Type>
  Component() {
    return fruit::createComponent()
        .template addMultibinding<SetupFeature, Type>();
  }
};

template <auto Fn>
using AutoSetup = GenericSetupImpl<Fn, decltype(Fn)>;

}  // namespace cuttlefish
