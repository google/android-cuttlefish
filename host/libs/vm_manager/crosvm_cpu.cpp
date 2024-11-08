/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "host/libs/vm_manager/crosvm_cpu.h"

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <json/value.h>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

std::string SerializeFreqDomains(
    const std::map<std::string, std::vector<int>>& freq_domains) {
  std::stringstream freq_domain_arg;
  bool first_vector = true;

  for (const std::pair<std::string, std::vector<int>>& pair : freq_domains) {
    if (!first_vector) {
      freq_domain_arg << ",";
    }
    first_vector = false;

    freq_domain_arg << "[" << android::base::Join(pair.second, ",") << "]";
  }

  return {std::format("[{}]", freq_domain_arg.str())};
}

}  // namespace

Result<std::vector<std::string>> CrosvmCpuArguments(
    const Json::Value& vcpu_config_json) {
  std::vector<std::string> cpu_arguments;

  std::map<std::string, std::vector<int>> freq_domains;
  std::string affinity_arg = "--cpu-affinity=";
  std::string capacity_arg = "--cpu-capacity=";
  std::string frequencies_arg = "--cpu-frequencies-khz=";
  std::string cgroup_path_arg = "--vcpu-cgroup-path=";
  std::string freq_domain_arg;

  const std::string parent_cgroup_path =
      CF_EXPECT(GetValue<std::string>(vcpu_config_json, {"cgroup_path"}));
  cgroup_path_arg += parent_cgroup_path;

  const Json::Value cpus_json =
      CF_EXPECT(GetValue<Json::Value>(vcpu_config_json, {"cpus"}),
                "Missing vCPUs config!");

  // Get the number of vCPUs from the number of cpu configurations.
  auto cpus = cpus_json.size();

  for (size_t i = 0; i < cpus; i++) {
    if (i != 0) {
      capacity_arg += ",";
      affinity_arg += ":";
      frequencies_arg += ";";
    }

    std::string cpu_cluster = fmt::format("--cpu-cluster={}", i);

    // Assume that non-contiguous logical CPU ids are malformed.
    std::string cpu = fmt::format("cpu{}", i);
    const Json::Value cpu_json = CF_EXPECT(
        GetValue<Json::Value>(cpus_json, {cpu}), "Missing vCPU config!");

    const std::string affinity =
        CF_EXPECT(GetValue<std::string>(cpu_json, {"affinity"}));
    std::string affine_arg = fmt::format("{}={}", i, affinity);

    const std::string freqs =
        CF_EXPECT(GetValue<std::string>(cpu_json, {"frequencies"}));
    std::string freq_arg = fmt::format("{}={}", i, freqs);

    const std::string capacity =
        CF_EXPECT(GetValue<std::string>(cpu_json, {"capacity"}));
    std::string cap_arg = fmt::format("{}={}", i, capacity);

    const std::string domain =
        CF_EXPECT(GetValue<std::string>(cpu_json, {"freq_domain"}));

    freq_domains[domain].push_back(i);

    freq_domain_arg = SerializeFreqDomains(freq_domains);

    capacity_arg += cap_arg;
    affinity_arg += affine_arg;
    frequencies_arg += freq_arg;

    cpu_arguments.emplace_back(std::move(cpu_cluster));
  }

  cpu_arguments.emplace_back(std::move(affinity_arg));
  cpu_arguments.emplace_back(std::move(capacity_arg));
  cpu_arguments.emplace_back(std::move(frequencies_arg));
  cpu_arguments.emplace_back(std::move(cgroup_path_arg));
  cpu_arguments.emplace_back("--virt-cpufreq-upstream");

  cpu_arguments.emplace_back(
      fmt::format("--cpus={},freq-domains={}", cpus, freq_domain_arg));

  return cpu_arguments;
}

}  // namespace cuttlefish
