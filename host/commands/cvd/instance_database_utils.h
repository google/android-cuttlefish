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

#include <algorithm>
#include <string>

#include "common/libs/utils/collect.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_database_types.h"

namespace cuttlefish {
namespace instance_db {

Result<std::string> GetCuttlefishConfigPath(const std::string& home);

std::string GenInternalGroupName();
std::string LocalDeviceNameRule(const std::string& group_name,
                                const std::string& instance_name);

bool IsValidInstanceName(const std::string& token);
/**
 * Runs simple tests to see if it could potentially be a host binaries dir
 *
 */
bool PotentiallyHostBinariesDir(const std::string& host_binaries_dir);

/**
 * simply returns:
 *
 * "Only up to n must match" or
 * "Only up to n must match by field " + FieldName
 *
 */
std::string TooManyInstancesFound(const int n, const std::string& field_name);

// effectively partial specialization of cuttlefish::Collect
// with instance_db::Set<T>
template <typename T, typename Container>
Set<T> CollectToSet(Container&& container,
                    std::function<bool(const T&)> predicate) {
  return Collect<T, Set<T>>(std::forward<Container>(container), predicate);
}

/**
 * Specialized version of cuttlefish::Flatten
 *
 *  a. The result is stored in instance_db::Set<T>
 *  b. As not all Container candidate supports iterator over
 *    the elements, collect is responsible for gathering all
 *    elements in each container.
 *  c. Not all elements has to be collected
 *
 */
template <typename Element, typename Container, typename Containers>
Result<Set<Element>> CollectAllElements(
    std::function<Result<Set<Element>>(const Container&)> collector,
    const Containers& inputs) {
  Set<Element> output;
  for (const auto& container : inputs) {
    auto subset = CF_EXPECT(collector(container));
    output.insert(subset.cbegin(), subset.cend());
  }
  return {output};
}

template <typename S>
Result<typename std::remove_reference<S>::type> AtMostOne(
    S&& s, const std::string& err_msg) {
  CF_EXPECT(AtMostN(std::forward<S>(s), 1), err_msg);
  return {std::forward<S>(s)};
}

template <typename RetSet, typename AnyContainer>
RetSet Intersection(const RetSet& u, AnyContainer&& v) {
  RetSet result;
  for (auto const& e : v) {
    if (u.find(e) != u.end()) {
      result.insert(e);
    }
  }
  return result;
}

template <typename RetSet, typename AnyContainer, typename... Containers>
RetSet Intersection(const RetSet& u, AnyContainer&& v, Containers&&... s) {
  RetSet first = Intersection(u, std::forward<AnyContainer>(v));
  return Intersection(first, std::forward<Containers>(s)...);
}

}  // namespace instance_db
}  // namespace cuttlefish
