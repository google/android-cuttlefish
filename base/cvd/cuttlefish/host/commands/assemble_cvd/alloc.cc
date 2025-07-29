/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/alloc.h"

#include <iomanip>
#include <sstream>

#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvdalloc/interface.h"
#include "cuttlefish/host/libs/allocd/request.h"
#include "cuttlefish/host/libs/allocd/utils.h"

namespace cuttlefish {

static Result<std::string> InterfaceName(
    const CuttlefishConfig::InstanceSpecific& instance,
    const std::string& name) {
  int num;
  CF_EXPECT(absl::SimpleAtoi(instance.id(), &num));

  if (instance.use_cvdalloc()) {
    return CvdallocInterfaceName(name, num);
  }

  return absl::StrFormat("%s-%s-%02d", kDefaultInterfacePrefix, name, num);
}

Result<IfaceConfig> DefaultNetworkInterfaces(
    const CuttlefishConfig::InstanceSpecific& instance) {
  IfaceConfig config{};

  config.mobile_tap.name = CF_EXPECT(InterfaceName(instance, "mtap"));
  config.mobile_tap.resource_id = 0;
  config.mobile_tap.session_id = 0;

  config.bridged_wireless_tap.name = CF_EXPECT(InterfaceName(instance, "wtap"));
  config.bridged_wireless_tap.resource_id = 0;
  config.bridged_wireless_tap.session_id = 0;

  config.non_bridged_wireless_tap.name =
      CF_EXPECT(InterfaceName(instance, "wifiap"));
  config.non_bridged_wireless_tap.resource_id = 0;
  config.non_bridged_wireless_tap.session_id = 0;

  config.ethernet_tap.name = CF_EXPECT(InterfaceName(instance, "etap"));
  config.ethernet_tap.resource_id = 0;
  config.ethernet_tap.session_id = 0;

  return config;
}

} // namespace cuttlefish
