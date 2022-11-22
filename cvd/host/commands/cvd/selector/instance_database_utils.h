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
#include <memory>
#include <string>

#include "common/libs/utils/collect.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/constant_reference.h"
#include "host/commands/cvd/selector/instance_database_types.h"

namespace cuttlefish {
namespace selector {

// given /a/b/c/d/e, ensures
// all directories from /a through /a/b/c/d/e exist
Result<void> EnsureDirectoryExistsAllTheWay(const std::string& dir);

Result<std::string> GetCuttlefishConfigPath(const std::string& home);

std::string GenInternalGroupName();
std::string GenDefaultGroupName();
std::string LocalDeviceNameRule(const std::string& group_name,
                                const std::string& instance_name);

// [A-Za-z0-9_]+, e.g. 0, tv, my_phone07, etc
bool IsValidInstanceName(const std::string& token);

// [A-Za-z_][A-Za-z0-9_]*, e.g. cool_group, cv0_d, cf, etc
// but can't start with [0-9]
bool IsValidGroupName(const std::string& token);

// <valid group name>-<valid instance name>
bool IsValidDeviceName(const std::string& token);

/**
 * Runs simple tests to see if it could potentially be a host artifacts dir
 *
 */
bool PotentiallyHostArtifactsPath(const std::string& host_binaries_dir);

/**
 * simply returns:
 *
 * "Only up to n must match" or
 * "Only up to n must match by field " + FieldName
 *
 */
std::string GenerateTooManyInstancesErrorMsg(const int n,
                                             const std::string& field_name);

/**
 * return all the elements in container that satisfies predicate.
 *
 * Container has Wrappers, where each Wrapper is typically,
 * std::unique/shared_ptr of T, or some wrapper of T, etc. Set is a set of T.
 *
 * This method returns the Set of T, as long as its corresponding Wrapper in
 * Container meets the predicate.
 */
template <typename T, typename Wrapper, typename Set, typename Container>
Set Collect(const Container& container,
            std::function<bool(const Wrapper&)> predicate,
            std::function<T(const Wrapper&)> convert) {
  Set output;
  for (const auto& t : container) {
    if (!predicate(t)) {
      continue;
    }
    output.insert(convert(t));
  }
  return output;
}

/*
 * Returns a Set of ConstRef<T>, which essentially satisfies "predicate"
 *
 * Container has a list/set of std::unique_ptr<T>. We collect all the
 * const references of each object owned by Container, which meets the
 * condition defined by predicate.
 *
 */
template <typename T, typename Container>
Set<ConstRef<T>> CollectToSet(
    Container&& container,
    std::function<bool(const std::unique_ptr<T>&)> predicate) {
  auto convert = [](const std::unique_ptr<T>& uniq_ptr) {
    return Cref(*uniq_ptr);
  };
  return Collect<ConstRef<T>, std::unique_ptr<T>, Set<ConstRef<T>>>(
      std::forward<Container>(container), std::move(predicate),
      std::move(convert));
}

/**
 * Given:
 *  Containers have a list of n `Container`s. Each Container may have
 *  m Element. Each is stored as a unique_ptr.
 *
 * Goal:
 *  To collect Elements from each Container with Container's APIs. The
 *  collected Elements meet the condition implicitly defined in collector.
 *
 * E.g. InstanceDatabase has InstanceGroups, each has Instances. We want
 * all the Instances its build-target was TV. Then, collector will look
 * like this:
 * [&build_target](const std::unique_ptr<InstanceGroup>& group) {
 *   return group->FindByBuildTarget(build_target);
 * }
 *
 * We take the union of all the returned subsets from each collector call.
 */
template <typename Element, typename Container, typename Containers>
Result<Set<ConstRef<Element>>> CollectAllElements(
    std::function<
        Result<Set<ConstRef<Element>>>(const std::unique_ptr<Container>&)>
        collector,
    const Containers& outermost_container) {
  Set<ConstRef<Element>> output;
  for (const auto& container_ptr : outermost_container) {
    auto subset = CF_EXPECT(collector(container_ptr));
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
    if (Contains(u, e)) {
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

}  // namespace selector
}  // namespace cuttlefish
