/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <sys/types.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/instance_database.h"
#include "host/commands/cvd/selector/selector_common_parser.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace selector {

class GroupSelector {
 public:
  static Result<LocalInstanceGroup> Select(
      const cvd_common::Args& selector_args, const uid_t uid,
      const InstanceDatabase& instance_database, const cvd_common::Envs& envs);

 private:
  GroupSelector(const uid_t uid, SelectorCommonParser&& selector_common_parser,
                const cvd_common::Envs& envs,
                const InstanceDatabase& instance_database)
      : client_uid_{uid},
        common_parser_(std::move(selector_common_parser)),
        envs_(envs),
        instance_database_(instance_database) {}

  /*
   * If default, try running single instance group. If multiple, try to find
   * HOME == SystemWideUserHome. If not exists, give up.
   *
   * If group given, find group, and check if all instance names are included
   *
   * If group not given, not yet supported. Will be in next CLs
   */
  Result<LocalInstanceGroup> FindGroup();
  bool RequestsDefaultGroup() const;
  Result<LocalInstanceGroup> FindDefaultGroup();
  bool IsHomeOverridden() const;
  Result<LocalInstanceGroup> FindByHomeIfOverridden() const;

  const uid_t client_uid_;
  SelectorCommonParser common_parser_;
  const cvd_common::Envs& envs_;
  const InstanceDatabase& instance_database_;
};

}  // namespace selector
}  // namespace cuttlefish
