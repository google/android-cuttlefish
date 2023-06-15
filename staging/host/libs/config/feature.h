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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/utils/result.h"

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

}  // namespace cuttlefish
