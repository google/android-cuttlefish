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

#include "host/commands/suspend_cvd/parse.h"

#include <iostream>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

constexpr char instance_num_help[] = "Which instance to suspend.";

constexpr char wait_for_launcher_help[] =
    "How many seconds to wait for the launcher to respond to the status "
    "command. A value of zero means wait indefinitely.";

constexpr char boot_timeout_help[] =
    "How many seconds to wait for the device to "
    "reboot.";

Flag GetInt32Flag(const std::string& name, int& value_buf,
                  const std::string& help_msg) {
  return GflagsCompatFlag(name, value_buf).Help(help_msg);
}

Flag InstanceNumFlag(int& instance_num) {
  return GetInt32Flag("instance_num", instance_num, instance_num_help);
}

Flag WaitForLauncherFlag(int& wait_for_launcher) {
  return GetInt32Flag("wait_for_launcher", wait_for_launcher,
                      wait_for_launcher_help);
}

Flag BootTimeoutFlag(int& boot_timeout) {
  return GetInt32Flag("boot_timeout", boot_timeout, boot_timeout_help);
}

}  // namespace

Result<Parsed> Parse(int argc, char** argv) {
  auto args = ArgsToVec(argc, argv);
  auto parsed = CF_EXPECT(Parse(args));
  return parsed;
}

Result<Parsed> Parse(std::vector<std::string>& args) {
  Parsed parsed{
      .instance_num = GetInstance(),
      .wait_for_launcher = 30,
      .boot_timeout = 500,
  };
  std::vector<Flag> flags;
  bool help_xml = false;
  flags.push_back(InstanceNumFlag(parsed.instance_num));
  flags.push_back(WaitForLauncherFlag(parsed.wait_for_launcher));
  flags.push_back(BootTimeoutFlag(parsed.boot_timeout));
  flags.push_back(HelpFlag(flags));
  flags.push_back(HelpXmlFlag(flags, std::cout, help_xml));
  flags.push_back(UnexpectedArgumentGuard());
  CF_EXPECT(ParseFlags(flags, args), "Flag parsing failed");
  return parsed;
}

}  // namespace cuttlefish
