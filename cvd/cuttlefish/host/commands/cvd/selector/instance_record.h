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

#include <memory>
#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace instance_db {

/**
 * TODO(kwstephenkim): add more methods, fields, and abstract out Instance
 *
 * Needs design changes to support both Remote and Local Instances
 */
class LocalInstanceGroup;
class LocalInstance {
  friend class LocalInstanceGroup;

 public:
  using LocalInstancePtr = std::unique_ptr<LocalInstance>;

  template <typename... Args>
  static LocalInstancePtr Create(Args&&... args) {
    auto new_instance = new LocalInstance(std::forward<Args>(args)...);
    return std::unique_ptr<LocalInstance>(new_instance);
  }

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
  const LocalInstanceGroup& Parent() const;

 private:
  LocalInstance(const int instance_id, LocalInstanceGroup& parent);

  const int instance_id_;
  const std::string internal_name_;  ///< for now, it is to_string(instance_id_)
  LocalInstanceGroup& parent_;
};

using LocalInstancePtr = LocalInstance::LocalInstancePtr;

}  // namespace instance_db
}  // namespace cuttlefish
