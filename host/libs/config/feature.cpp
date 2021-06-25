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

#include <unordered_set>

namespace cuttlefish {

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
  if (!FeatureSuperclass<Feature>::TopologicalVisit(enabled, add_feature)) {
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
