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
#include "host/commands/cvd/cli/parser/instance/cf_vm_configs.h"

#include <cstdint>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <google/protobuf/util/json_util.h>
#include "json/json.h"

#include "common/libs/utils/flags_validator.h"
#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"

#define UI_DEFAULTS_MEMORY_MB 2048

namespace cuttlefish {

using android::base::StringReplace;
using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;
using cvd::config::Vm;
using google::protobuf::util::JsonPrintOptions;
using google::protobuf::util::MessageToJsonString;

static std::string VmManager(const Instance& instance) {
  const auto& vm = instance.vm();
  switch (vm.vmm_case()) {
    case Vm::VmmCase::kCrosvm:
    default:
      return "crosvm";
    case Vm::VmmCase::kGem5:
      return "gem5";
    case Vm::VmmCase::kQemu:
      return "qemu_cli";
  }
}

static std::uint32_t Cpus(const Instance& instance) {
  if (instance.vm().has_cpus()) {
    return instance.vm().cpus();
  } else {
    return CF_DEFAULTS_CPUS;
  }
}

static std::uint32_t MemoryMb(const Instance& instance) {
  if (instance.vm().has_memory_mb()) {
    return instance.vm().memory_mb();
  } else {
    return UI_DEFAULTS_MEMORY_MB;
  }
}

static bool UseSdcard(const Instance& instance) {
  if (instance.vm().has_use_sdcard()) {
    return instance.vm().use_sdcard();
  } else {
    return CF_DEFAULTS_USE_SDCARD;
  }
}

static Result<std::string> SetupWizardMode(const Instance& instance) {
  if (instance.vm().has_setupwizard_mode()) {
    CF_EXPECT(ValidateSetupWizardMode(instance.vm().setupwizard_mode()));
    return instance.vm().setupwizard_mode();
  } else {
    return CF_DEFAULTS_SETUPWIZARD_MODE;
  }
}

static std::string Uuid(const Instance& instance) {
  if (instance.vm().has_uuid()) {
    return instance.vm().uuid();
  } else {
    return CF_DEFAULTS_UUID;
  }
}

static bool EnableSandbox(const Instance& instance) {
  const auto& crosvm = instance.vm().crosvm();
  const auto& default_val = CF_DEFAULTS_ENABLE_SANDBOX;
  return crosvm.has_enable_sandbox() ? crosvm.enable_sandbox() : default_val;
}

static bool SimpleMediaDevice(const Instance& instance) {
  const auto& crosvm = instance.vm().crosvm();
  const auto& default_val = CF_DEFAULTS_SIMPLE_MEDIA_DEVICE;
  return crosvm.has_simple_media_device() ? crosvm.simple_media_device() : default_val;
}

static Result<std::string> V4l2Proxy(const Instance& instance) {
  const auto& crosvm = instance.vm().crosvm();
  const auto& default_val = CF_DEFAULTS_V4L2_PROXY;
  return crosvm.has_v4l2_proxy() ? crosvm.v4l2_proxy() : default_val;
}

static Result<std::string> CustomConfigsFlagValue(const Instance& instance) {
  if (instance.vm().custom_actions().empty()) {
    return "unset";
  }
  std::vector<std::string> json_entries;
  for (const auto& action : instance.vm().custom_actions()) {
    std::string json;
    JsonPrintOptions print_opts;
    print_opts.preserve_proto_field_names = true;
    auto to_json_res = MessageToJsonString(action, &json, print_opts);
    CF_EXPECTF(to_json_res.ok(), "{}", to_json_res.ToString());
    json_entries.emplace_back(std::move(json));
  }
  return fmt::format("[{}]", fmt::join(json_entries, ","));
}

static Result<std::vector<std::string>> CustomConfigsFlags(
    const EnvironmentSpecification& cfg) {
  std::vector<std::string> ret;
  for (const auto& instance : cfg.instances()) {
    auto value = CF_EXPECT(CustomConfigsFlagValue(instance));
    ret.emplace_back(fmt::format("--custom_actions={}", value));
  }
  return ret;
}

Result<std::vector<std::string>> GenerateVmFlags(
    const EnvironmentSpecification& cfg) {
  std::vector<std::string> flags = {
      GenerateInstanceFlag("vm_manager", cfg, VmManager),
      GenerateInstanceFlag("cpus", cfg, Cpus),
      GenerateInstanceFlag("memory_mb", cfg, MemoryMb),
      GenerateInstanceFlag("use_sdcard", cfg, UseSdcard),
      CF_EXPECT(ResultInstanceFlag("setupwizard_mode", cfg, SetupWizardMode)),
      GenerateInstanceFlag("uuid", cfg, Uuid),
      GenerateInstanceFlag("enable_sandbox", cfg, EnableSandbox),
  };
  return MergeResults(std::move(flags), CF_EXPECT(CustomConfigsFlags(cfg)));
}

}  // namespace cuttlefish
