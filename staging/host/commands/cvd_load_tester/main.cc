
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
#include <fstream>
#include <iostream>
#include <string>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "host/commands/cvd/parser/load_configs_parser.h"

DEFINE_string(config_file_path, "", "config file path for default configs");

namespace cuttlefish {
int CvdLoadParserMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto json_configs = cuttlefish::ParseJsonFile(FLAGS_config_file_path);
  if (!json_configs.ok()) {
    LOG(INFO) << "parsing input file failed";
    return 1;
  }

  auto cvd_flags = cuttlefish::ParseCvdConfigs(*json_configs);
  if (!cvd_flags.ok()) {
    LOG(INFO) << "parsing json configs failed";
    return 1;
  }
  LOG(INFO) << "Parsing succeeded";
  for (auto& parsed_launch_flag : cvd_flags->launch_cvd_flags) {
    LOG(INFO) << parsed_launch_flag;
  }

  LOG(INFO) << "credential_source = "
            << cvd_flags->fetch_cvd_flags.credential_source.value_or("");

  int i = 0;
  for (const auto& parsed_fetch_instance_flag :
       cvd_flags->fetch_cvd_flags.instances) {
    LOG(INFO) << i << " -- "
              << parsed_fetch_instance_flag.default_build.value_or("") << ","
              << parsed_fetch_instance_flag.system_build.value_or("") << ","
              << parsed_fetch_instance_flag.kernel_build.value_or("") << ","
              << parsed_fetch_instance_flag.should_fetch;
    i++;
  }

  return 0;
}
}  // namespace cuttlefish
int main(int argc, char** argv) {
  return cuttlefish::CvdLoadParserMain(argc, argv);
}
