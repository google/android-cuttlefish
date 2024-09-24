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

#include <ctime>
#include <iomanip>
#include <sstream>

#include <android-base/parseint.h>
#include <fmt/core.h>

namespace cuttlefish {
namespace selector {

Query::Query(const std::string& field_name, const std::string& field_value)
    : field_name_(field_name), field_value_(field_value) {}

std::string SerializeTimePoint(const TimeStamp& present) {
  const auto duration =
      std::chrono::duration_cast<CvdTimeDuration>(present.time_since_epoch());
  return fmt::format("{}", duration.count());
}

Result<TimeStamp> DeserializeTimePoint(const Json::Value& time_point_json) {
  std::string serialized = time_point_json.asString();

  using CountType = decltype(std::declval<CvdTimeDuration>().count());
  CountType count = 0;
  CF_EXPECTF(android::base::ParseInt(serialized, &count),
             "Failed to serialize: {}", serialized);
  CvdTimeDuration duration(count);
  TimeStamp restored_time(duration);
  return restored_time;
}

std::string Format(const TimeStamp& time_point) {
  auto tc = CvdServerClock::to_time_t(time_point);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&tc), "%F %T");
  return ss.str();
}

}  // namespace selector
}  // namespace cuttlefish
