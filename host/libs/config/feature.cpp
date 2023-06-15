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

#include "common/libs/utils/result.h"

namespace cuttlefish {

SetupFeature::~SetupFeature() {}

/* static */ Result<void> SetupFeature::RunSetup(
    const std::vector<SetupFeature*>& features) {
  std::unordered_set<SetupFeature*> enabled;
  for (const auto& feature : features) {
    CF_EXPECT(feature != nullptr, "Received null feature");
    if (feature->Enabled()) {
      enabled.insert(feature);
    }
  }
  // Collect these in a vector first to trigger any obvious dependency issues.
  std::vector<SetupFeature*> ordered_features;
  auto add_feature =
      [&ordered_features](SetupFeature* feature) -> Result<void> {
    ordered_features.push_back(feature);
    return {};
  };
  CF_EXPECT(Feature<SetupFeature>::TopologicalVisit(enabled, add_feature),
            "Dependency issue detected, not performing any setup.");
  // TODO(b/189153501): This can potentially be parallelized.
  for (auto& feature : ordered_features) {
    LOG(DEBUG) << "Running setup for " << feature->Name();
    CF_EXPECT(feature->ResultSetup(), "Setup failed for " << feature->Name());
  }
  return {};
}

Result<void> FlagFeature::ProcessFlags(
    const std::vector<FlagFeature*>& features,
    std::vector<std::string>& flags) {
  std::unordered_set<FlagFeature*> features_set(features.begin(),
                                                features.end());
  CF_EXPECT(features_set.count(nullptr) == 0, "Received null feature");
  auto handle = [&flags](FlagFeature* feature) -> Result<void> {
    CF_EXPECT(feature->Process(flags));
    return {};
  };
  CF_EXPECT(
      Feature<FlagFeature>::TopologicalVisit(features_set, handle),
      "Unable to parse flags.");
  return {};
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
