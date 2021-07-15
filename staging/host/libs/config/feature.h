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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cuttlefish {

// TODO(schuffelen): Rename this "Feature"
template <typename Subclass>
class FeatureSuperclass {
 public:
  virtual ~FeatureSuperclass() = default;

  virtual std::string Name() const = 0;
  virtual std::unordered_set<Subclass*> Dependencies() const = 0;

  static bool TopologicalVisit(const std::unordered_set<Subclass*>& features,
                               const std::function<bool(Subclass*)>& callback);
};

// TODO(schuffelen): Rename this "SetupFeature"
class Feature : public virtual FeatureSuperclass<Feature> {
 public:
  virtual ~Feature();

  static bool RunSetup(const std::vector<Feature*>& features);

  virtual bool Enabled() const = 0;

 protected:
  virtual bool Setup() = 0;
};

template <typename Subclass>
bool FeatureSuperclass<Subclass>::TopologicalVisit(
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
          << "Feature " << feature->Name() << " has a null dependency.";
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
