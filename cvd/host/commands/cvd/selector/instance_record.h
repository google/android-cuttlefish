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

#include <functional>
#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace instance_db {

/**
 * TODO(kwstephenkim): add more methods, fields, and abstract out Instance
 *
 * Needs design changes to support both Remote and Local Instances
 */
class LocalInstance {
 public:
  LocalInstance(const int instance_id, const std::string& internal_group_name);
  LocalInstance(const LocalInstance&) = default;
  LocalInstance(LocalInstance&&) = default;
  LocalInstance& operator=(const LocalInstance&) = default;
  LocalInstance& operator=(LocalInstance&&) = default;
  bool operator==(const LocalInstance& target) const { return Compare(target); }

  /* names:
   *
   *  As of 08/21/2022, the name of a cuttlefish instance is cvd-N. For now,
   * instance groups share the "cvd-" prefix. So, "cvd" is the group name, and
   * "N" is the instance specific name. "cvd-N" is the device name.
   *
   * There will be another name the user specify for each instance. However,
   * many components in Cuttlefish traditionally expect the name to be "cvd-N,"
   * and rely on "N" to avoid conflicts in the global resource uses.
   *
   * Thus, we will eventually maintain the internal device name for those
   * existing cuttlefish implementation, and the user-given name.
   *
   */
  const std::string& InternalName() const;
  std::string InternalDeviceName() const;

  int InstanceId() const;

 private:
  bool Compare(const LocalInstance& target) const {
    // list all fields here
    return (instance_id_ == target.instance_id_) &&
           (internal_name_ == target.internal_name_) &&
           (internal_group_name_ == target.internal_group_name_);
  }
  int instance_id_;
  std::string internal_name_;  ///< for now, it is to_string(instance_id_)
  std::string internal_group_name_;
};

}  // namespace instance_db
}  // namespace cuttlefish

template <>
struct std::hash<cuttlefish::instance_db::LocalInstance> {
  using LocalInstance = cuttlefish::instance_db::LocalInstance;
  std::size_t operator()(const LocalInstance& instance) const noexcept {
    return std::hash<int>()(instance.InstanceId());
  }
};
