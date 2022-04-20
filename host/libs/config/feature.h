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

#include <android-base/logging.h>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cuttlefish {

template <typename Subclass>
class Feature {
 public:
  virtual ~Feature() = default;

  virtual std::string Name() const = 0;

  static bool TopologicalVisit(const std::unordered_set<Subclass*>& features,
                               const std::function<bool(Subclass*)>& callback);

 private:
  virtual std::unordered_set<Subclass*> Dependencies() const = 0;
};

class SetupFeature : public virtual Feature<SetupFeature> {
 public:
  virtual ~SetupFeature();

  static bool RunSetup(const std::vector<SetupFeature*>& features);

  virtual bool Enabled() const = 0;

 private:
  virtual bool Setup() = 0;
};

class FlagFeature : public Feature<FlagFeature> {
 public:
  static bool ProcessFlags(const std::vector<FlagFeature*>& features,
                           std::vector<std::string>& flags);
  static bool WriteGflagsHelpXml(const std::vector<FlagFeature*>& features,
                                 std::ostream& out);

 private:
  // Must be executed in dependency order following Dependencies(). Expected to
  // mutate the `flags` argument to remove handled flags, and possibly introduce
  // new flag values (e.g. from a file).
  virtual bool Process(std::vector<std::string>& flags) = 0;

  // TODO(schuffelen): Migrate the xml help to human-readable help output after
  // the gflags migration is done.

  // Write an xml fragment that is compatible with gflags' `--helpxml` format.
  virtual bool WriteGflagsCompatHelpXml(std::ostream& out) const = 0;
};

template <typename Subclass>
bool Feature<Subclass>::TopologicalVisit(
    const std::unordered_set<Subclass*>& features,
    const std::function<bool(Subclass*)>& callback) {
  enum class Status { UNVISITED, VISITING, VISITED };
  std::unordered_map<Subclass*, Status> features_status;
  for (const auto& feature : features) {
    features_status[feature] = Status::UNVISITED;
  }
  std::function<bool(Subclass*)> visit;
  visit = [&callback, &features_status, &visit](Subclass* feature) -> bool {
    if (features_status.count(feature) == 0) {
      LOG(ERROR) << "Dependency edge to " << feature->Name() << " but it is not"
                 << " part of the feature graph. This feature is either "
                 << "disabled or not correctly registered.";
      return false;
    } else if (features_status[feature] == Status::VISITED) {
      return true;
    } else if (features_status[feature] == Status::VISITING) {
      LOG(ERROR) << "Cycle detected while visiting " << feature->Name();
      return false;
    }
    features_status[feature] = Status::VISITING;
    for (const auto& dependency : feature->Dependencies()) {
      CHECK(dependency != nullptr)
          << "SetupFeature " << feature->Name() << " has a null dependency.";
      if (!visit(dependency)) {
        LOG(ERROR) << "Error detected while visiting " << feature->Name();
        return false;
      }
    }
    features_status[feature] = Status::VISITED;
    if (!callback(feature)) {
      LOG(ERROR) << "Callback error on " << feature->Name();
      return false;
    }
    return true;
  };
  for (const auto& feature : features) {
    if (!visit(feature)) {  // `visit` will log the error chain.
      return false;
    }
  }
  return true;
}

}  // namespace cuttlefish
