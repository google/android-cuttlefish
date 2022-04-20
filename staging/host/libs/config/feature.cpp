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

SetupFeature::~SetupFeature() {}

/* static */ bool SetupFeature::RunSetup(
    const std::vector<SetupFeature*>& features) {
  std::unordered_set<SetupFeature*> enabled;
  for (const auto& feature : features) {
    CHECK(feature != nullptr) << "Received null feature";
    if (feature->Enabled()) {
      enabled.insert(feature);
    }
  }
  // Collect these in a vector first to trigger any obvious dependency issues.
  std::vector<SetupFeature*> ordered_features;
  auto add_feature = [&ordered_features](SetupFeature* feature) -> bool {
    ordered_features.push_back(feature);
    return true;
  };
  if (!Feature<SetupFeature>::TopologicalVisit(enabled, add_feature)) {
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

bool FlagFeature::ProcessFlags(const std::vector<FlagFeature*>& features,
                               std::vector<std::string>& flags) {
  std::unordered_set<FlagFeature*> features_set(features.begin(),
                                                features.end());
  if (features_set.count(nullptr)) {
    LOG(ERROR) << "Received null feature";
    return false;
  }
  auto handle = [&flags](FlagFeature* feature) -> bool {
    return feature->Process(flags);
  };
  if (!Feature<FlagFeature>::TopologicalVisit(features_set, handle)) {
    LOG(ERROR) << "Unable to parse flags.";
    return false;
  }
  return true;
}

bool FlagFeature::WriteGflagsHelpXml(const std::vector<FlagFeature*>& features,
                                     std::ostream& out) {
  // Lifted from external/gflags/src/gflags_reporting.cc:ShowXMLOfFlags
  out << "<?xml version=\"1.0\"?>\n";
  out << "<AllFlags>\n";
  out << "  <program>program</program>\n";
  out << "  <usage>usage</usage>\n";
  for (const auto& feature : features) {
    if (!feature) {
      LOG(ERROR) << "Received null feature";
      return false;
    }
    if (!feature->WriteGflagsCompatHelpXml(out)) {
      LOG(ERROR) << "Failure to write xml";
      return false;
    }
  }
  out << "</AllFlags>";
  return true;
}

}  // namespace cuttlefish
