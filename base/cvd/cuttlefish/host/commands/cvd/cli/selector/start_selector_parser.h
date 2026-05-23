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

#include <stddef.h>
#include <sys/types.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace selector {

/**
 * This class parses the separated SelectorOptions defined in
 * cvd_server.proto.
 *
 * Note that the parsing is from the perspective of syntax.
 *
 * In other words, this does not check the following, for example:
 *  1. If the numeric instance id is duplicated
 *  2. If the group name is already taken
 *
 * Extends the common selector options with start specific ones.
 */
class StartSelectorParser {
 public:
  static Result<StartSelectorParser> ConductSelectFlagsParser(
      const std::optional<std::vector<std::string>>& instance_names_opt,
      const cvd_common::Args& cmd_args, const cvd_common::Envs& envs);
  const std::vector<unsigned>& InstanceIds() const { return instance_ids_; }
  unsigned RequestedNumInstances() const { return requested_num_instances_; }

 private:
  StartSelectorParser(
      const std::optional<std::vector<std::string>>& instance_names_opt,
      const cvd_common::Args& cmd_args, const cvd_common::Envs& envs);

  Result<void> ParseOptions();

  struct InstanceIdsParams {
    std::optional<std::string> num_instances;
    std::optional<std::string> instance_nums;
    std::optional<std::string> base_instance_num;
  };

  class ParsedInstanceIdsOpt {
    friend class StartSelectorParser;

   private:
    ParsedInstanceIdsOpt(const std::vector<unsigned>& instance_ids)
        : instance_ids_{instance_ids},
          n_instances_{static_cast<unsigned>(instance_ids.size())} {}
    ParsedInstanceIdsOpt(const unsigned n_instances)
        : n_instances_{n_instances} {}
    auto GetInstanceIds() { return std::move(instance_ids_); }
    unsigned GetNumOfInstances() const { return n_instances_; }
    std::vector<unsigned> instance_ids_;
    const unsigned n_instances_;
  };

  /*
   * Returns the number of instances and maybe the instance ids.
   */
  Result<ParsedInstanceIdsOpt> HandleInstanceIds(
      const InstanceIdsParams& instance_id_params);

  struct VerifyNumOfInstancesParam {
    std::optional<std::string> num_instances_flag;
    std::optional<size_t> num_instance_names;
    std::optional<std::string> instance_nums_flag;
  };

  Result<unsigned> VerifyNumOfInstances(const VerifyNumOfInstancesParam& params,
                                        unsigned default_n_instances = 1) const;

  /**
   * The following are considered, and left empty if can't be figured out.
   *
   * --base_instance_num, --instance_nums, --num_instances,
   * instance_names_.size(), CUTTLEFISH_INSTANCE, and vsoc-suffix if
   * it is the user name.
   *
   * instance_names_.size() is effectively another --num_instances.
   * CUTTLEFISH_INSTANCE and the suffix in order are considered as
   * --base_instance_num if --base_instance_num is not given and
   * --instance_nums is not given.
   *
   */
  std::vector<unsigned> instance_ids_;
  unsigned requested_num_instances_;

  std::optional<size_t> num_instance_names_opt_;
  // temporarily keeps the leftover of the input cmd_args
  cvd_common::Args cmd_args_;
  cvd_common::Envs envs_;
};

}  // namespace selector
}  // namespace cuttlefish
