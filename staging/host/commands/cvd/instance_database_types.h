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

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cuttlefish {
namespace instance_db {

using FieldName = std::string;
using Value = std::string;
// e.g. if intended to search by --home=/home/vsoc-01,
// field_name_ is "home" and the field_value_ is "/home/vsoc-01"
struct Query {
  FieldName field_name_;
  Value field_value_;
};
using Queries = std::vector<Query>;

template <typename T>
using Set = std::unordered_set<T>;

template <typename K, typename V>
using Map = std::unordered_map<K, V>;

}  // namespace instance_db
}  // namespace cuttlefish
