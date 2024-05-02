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
#include "host/commands/cvd/parser/instance/cf_security_configs.h"

#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

using cvd::config::Instance;
using cvd::config::Launch;

static std::string SerialNumber(const Instance& instance) {
  if (instance.security().has_serial_number()) {
    return instance.security().serial_number();
  } else {
    return CF_DEFAULTS_SERIAL_NUMBER;
  }
}

static bool UseRandomSerial(const Instance& instance) {
  if (instance.security().has_use_random_serial()) {
    return instance.security().use_random_serial();
  } else {
    return CF_DEFAULTS_USE_RANDOM_SERIAL;
  }
}

static bool GuestEnforceSecurity(const Instance& instance) {
  if (instance.security().has_guest_enforce_security()) {
    return instance.security().guest_enforce_security();
  } else {
    return CF_DEFAULTS_GUEST_ENFORCE_SECURITY;
  }
}

std::vector<std::string> GenerateSecurityFlags(const Launch& cfg) {
  return std::vector<std::string>{
      GenerateInstanceFlag("serial_number", cfg, SerialNumber),
      GenerateInstanceFlag("use_random_serial", cfg, UseRandomSerial),
      GenerateInstanceFlag("guest_enforce_security", cfg, GuestEnforceSecurity),
  };
}

}  // namespace cuttlefish
