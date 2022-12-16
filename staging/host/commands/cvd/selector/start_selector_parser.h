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

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

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
 */
class StartSelectorParser {
 public:
  static Result<StartSelectorParser> ConductSelectFlagsParser(
      const uid_t uid, const std::vector<std::string>& selector_args,
      const std::vector<std::string>& cmd_args,
      const std::unordered_map<std::string, std::string>& envs);
  std::optional<std::string> GroupName() const;
  std::optional<std::vector<std::string>> PerInstanceNames() const;
  const auto& SubstringQueries() const { return substring_queries_; }
  const std::optional<std::vector<unsigned>>& InstanceIds() const {
    return instance_ids_;
  }
  unsigned RequestedNumInstances() const { return requested_num_instances_; }
  bool IsMaybeDefaultGroup() const { return may_be_default_group_; }

 private:
  StartSelectorParser(const std::string& system_wide_user_home,
                      const std::vector<std::string>& selector_args,
                      const std::vector<std::string>& cmd_args,
                      const std::unordered_map<std::string, std::string>& envs);

  /*
   * Note: name may or may not be valid. A name could be a
   * group name or a device name or an instance name, depending
   * on the context: i.e. the operation.
   *
   * This succeeds only if all selector arguments can be legitimately
   * consumed.
   */
  Result<void> ParseOptions();

  bool IsValidName(const std::string& name) const;
  Result<std::unordered_set<std::string>> FindSubstringsToMatch();
  struct ParsedNameFlags {
    std::optional<std::string> group_name;
    std::optional<std::vector<std::string>> instance_names;
  };
  struct NameFlagsParam {
    std::optional<std::string> names;
    std::optional<std::string> device_names;
    std::optional<std::string> group_name;
    std::optional<std::string> instance_names;
  };
  Result<ParsedNameFlags> HandleNameOpts(
      const NameFlagsParam& name_flags) const;
  /*
   * As --name could give a device list, a group list, or a per-
   * instance list, HandleNames() will set some of them according
   * to the syntax.
   */
  Result<ParsedNameFlags> HandleNames(
      const std::optional<std::string>& names) const;
  struct DeviceNamesPair {
    std::string group_name;
    std::vector<std::string> instance_names;
  };
  Result<DeviceNamesPair> HandleDeviceNames(
      const std::optional<std::string>& device_names) const;
  Result<std::vector<std::string>> HandleInstanceNames(
      const std::optional<std::string>& per_instance_names) const;
  Result<std::string> HandleGroupName(
      const std::optional<std::string>& group_name) const;
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
  std::optional<std::string> group_name_;
  std::optional<std::vector<std::string>> instance_names_;
  std::unordered_set<std::string> substring_queries_;
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

  // temporarily keeps the leftover of the input cmd_args
  const std::string client_user_home_;
  std::vector<std::string> selector_args_;
  std::vector<std::string> cmd_args_;
  std::unordered_map<std::string, std::string> envs_;
};

}  // namespace selector
}  // namespace cuttlefish
