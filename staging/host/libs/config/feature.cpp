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

#include "host/libs/config/feature.h"

#include <android-base/logging.h>
#include <unordered_map>
#include <unordered_set>

namespace cuttlefish {

namespace {

bool TopologicalVisit(const std::unordered_set<Feature*>& features,
                      const std::function<bool(Feature*)>& callback) {
  enum class Status { UNVISITED, VISITING, VISITED };
  std::unordered_map<Feature*, Status> features_status;
  for (const auto& feature : features) {
    features_status[feature] = Status::UNVISITED;
  }
  std::function<bool(Feature*)> visit;
  visit = [&callback, &features_status, &visit](Feature* feature) -> bool {
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

}  // namespace

Feature::~Feature() {}

/* static */ bool Feature::RunSetup(const std::vector<Feature*>& features) {
  std::unordered_set<Feature*> enabled;
  for (const auto& feature : features) {
    CHECK(feature != nullptr) << "Received null feature";
    if (feature->Enabled()) {
      enabled.insert(feature);
    }
  }
  // Collect these in a vector first to trigger any obvious dependency issues.
  std::vector<Feature*> ordered_features;
  auto add_feature = [&ordered_features](Feature* feature) -> bool {
    ordered_features.push_back(feature);
    return true;
  };
  if (!TopologicalVisit(enabled, add_feature)) {
    LOG(ERROR) << "Dependency issue detected, not performing any setup.";
    return false;
  }
  // TODO(b/189153501): This can potentially be parallelized.
  for (auto& feature : ordered_features) {
    LOG(DEBUG) << "Running setup for " << feature->Name();
    if (!feature->Setup()) {
      LOG(ERROR) << "Setup failed for " << feature->Name();
      return false;
    }
  }
  return true;
}

}  // namespace cuttlefish
