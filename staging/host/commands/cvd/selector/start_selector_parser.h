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

#include <sys/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/selector_common_parser.h"
#include "host/commands/cvd/types.h"

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
 * How it works is, it parses the selector options that are common
 * across operations with SelectorCommonParser first. Following that,
 * StartSelectorParser parses start-specific selector options.
 */
class StartSelectorParser {
 public:
  static Result<StartSelectorParser> ConductSelectFlagsParser(
      const cvd_common::Args& selector_args, const cvd_common::Args& cmd_args,
      const cvd_common::Envs& envs);
  std::optional<std::string> GroupName() const;
  std::optional<std::vector<std::string>> PerInstanceNames() const;
  const std::optional<std::vector<unsigned>>& InstanceIds() const {
    return instance_ids_;
  }
  unsigned RequestedNumInstances() const { return requested_num_instances_; }
  bool IsMaybeDefaultGroup() const { return may_be_default_group_; }
  bool MustAcquireFileLock() const { return must_acquire_file_lock_; }

 private:
  StartSelectorParser(const std::string& system_wide_user_home,
                      const cvd_common::Args& selector_args,
                      const cvd_common::Args& cmd_args,
                      const cvd_common::Envs& envs,
                      SelectorCommonParser&& common_parser);

  Result<void> ParseOptions();

  struct InstanceIdsParams {
    std::optional<std::string> num_instances;
    std::optional<std::string> instance_nums;
    std::optional<std::string> base_instance_num;
    std::optional<unsigned> cuttlefish_instance_env;
    std::optional<unsigned> vsoc_suffix;
  };

  class ParsedInstanceIdsOpt {
    friend class StartSelectorParser;

   private:
    ParsedInstanceIdsOpt(const std::vector<unsigned>& instance_ids)
        : instance_ids_{instance_ids},
          n_instances_{static_cast<unsigned>(instance_ids.size())} {}
    ParsedInstanceIdsOpt(const unsigned n_instances)
        : instance_ids_{std::nullopt}, n_instances_{n_instances} {}
    auto GetInstanceIds() { return std::move(instance_ids_); }
    unsigned GetNumOfInstances() const { return n_instances_; }
    std::optional<std::vector<unsigned>> instance_ids_;
    const unsigned n_instances_;
  };

  /*
   * CF_ERR is meant to be an error:
   *  For example, --num_instances != |--instance_nums|.
   *
   * On the contrary, std::nullopt inside Result is not necessary one.
   * std::nullopt inside Result means that with the given information,
   * the instance_ids_ cannot be yet figured out, so the task is deferred
   * to CreationAnaylizer or so, which has more contexts. For example,
   * if no option at all is given, it is not an error; however, the
   * StartSelectorParser alone cannot figure out the list of instance ids. The
   * InstanceDatabase, UniqueResourceAllocator, InstanceLockFileManager will be
   * involved to automatically generate the valid, numeric instance ids.
   * If that's the case, Result{std::nullopt} could be returned.
   *
   */
  Result<ParsedInstanceIdsOpt> HandleInstanceIds(
      const InstanceIdsParams& instance_id_params);

  struct InstanceFromEnvParam {
    std::optional<unsigned> cuttlefish_instance_env;
    std::optional<unsigned> vsoc_suffix;
    std::optional<unsigned> num_instances;
  };
  std::optional<std::vector<unsigned>> InstanceFromEnvironment(
      const InstanceFromEnvParam& params);

  struct VerifyNumOfInstancesParam {
    std::optional<std::string> num_instances_flag;
    std::optional<std::vector<std::string>> instance_names;
    std::optional<std::string> instance_nums_flag;
  };

  Result<unsigned> VerifyNumOfInstances(
      const VerifyNumOfInstancesParam& params,
      const unsigned default_n_instances = 1) const;
  Result<bool> CalcMayBeDefaultGroup();
  Result<bool> CalcAcquireFileLock();

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
  std::optional<std::vector<unsigned>> instance_ids_;
  unsigned requested_num_instances_;
  bool may_be_default_group_;
  bool must_acquire_file_lock_;
  std::optional<std::string> group_name_;
  std::optional<std::vector<std::string>> per_instance_names_;

  // temporarily keeps the leftover of the input cmd_args
  const std::string client_user_home_;
  cvd_common::Args selector_args_;
  cvd_common::Args cmd_args_;
  cvd_common::Envs envs_;
  SelectorCommonParser common_parser_;
};

}  // namespace selector
}  // namespace cuttlefish
