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

#pragma once

#include <chrono>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <json/json.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace database_impl {

template <typename ValueType>
using ToStringTypeReturnType =
    decltype(void(std::to_string(std::declval<ValueType&>())));

template <typename T, typename = void>
struct IsToStringOk : std::false_type {};

template <typename T>
struct IsToStringOk<T, ToStringTypeReturnType<T>> : std::true_type {};

}  // namespace database_impl

template <typename T>
using Set = std::unordered_set<T>;

template <typename K, typename V>
using Map = std::unordered_map<K, V>;

using CvdServerClock = std::chrono::system_clock;
using TimeStamp = std::chrono::time_point<CvdServerClock>;
using CvdTimeDuration = std::chrono::milliseconds;

std::string SerializeTimePoint(const TimeStamp&);
Result<TimeStamp> DeserializeTimePoint(const Json::Value& group_json);
std::string Format(const TimeStamp&);

}  // namespace cuttlefish
