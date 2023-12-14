/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/selector/instance_database_types.h"

#include <android-base/parseint.h>
#include <fmt/core.h>

#include "host/commands/cvd/selector/instance_group_record.h"

namespace cuttlefish {
namespace selector {

Query::Query(const std::string& field_name, const std::string& field_value)
    : field_name_(field_name), field_value_(field_value) {}

std::string SerializeTimePoint(const TimeStamp& present) {
  const auto duration =
      std::chrono::duration_cast<CvdTimeDuration>(present.time_since_epoch());
  return fmt::format("{}", duration.count());
}

Result<TimeStamp> DeserializeTimePoint(const Json::Value& group_json) {
  std::string group_name = "unknown";
  if (group_json.isMember(LocalInstanceGroup::kJsonGroupName)) {
    group_name = group_json[LocalInstanceGroup::kJsonGroupName].asString();
  }
  CF_EXPECTF(group_json.isMember(LocalInstanceGroup::kJsonStartTime),
             "The serialized instance database in json file for group \"{}\""
             " is missing the start time field: {}",
             group_name, LocalInstanceGroup::kJsonStartTime);
  std::string serialized(
      group_json[LocalInstanceGroup::kJsonStartTime].asString());

  using CountType = decltype(((const CvdTimeDuration*)nullptr)->count());
  CountType count = 0;
  CF_EXPECTF(android::base::ParseInt(serialized, &count),
             "Failed to serialize: {}", serialized);
  CvdTimeDuration duration(count);
  TimeStamp restored_time(duration);
  LOG(VERBOSE) << "The start time of the group \"" << group_name
               << "\" is restored as: " << Format(restored_time);
  return restored_time;
}

std::string Format(const TimeStamp& time_point) {
  return fmt::format("{:%b-%d-%Y %H:%M:%S}", time_point);
}

}  // namespace selector
}  // namespace cuttlefish
