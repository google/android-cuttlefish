/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/commands/assemble_cvd/boot_config.h"

#include <fstream>
#include <sstream>
#include <string>

#include <sys/stat.h>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/kernel_args.h"

namespace {

size_t WriteEnvironment(const vsoc::CuttlefishConfig& config,
                        const std::string& env_path) {
  std::ostringstream env;
  auto kernel_args = KernelCommandLineFromConfig(config);
  env << "bootargs=" << android::base::Join(kernel_args, " ") << '\0';
  if (!config.boot_slot().empty()) {
      env << "android_slot_suffix=_" << config.boot_slot() << '\0';
  }
  env << "bootdevice=0:1" << '\0';
  env << "bootdelay=0" << '\0';
  env << "bootcmd=boot_android virtio -" << '\0';
  env << '\0';
  std::string env_str = env.str();
  std::ofstream file_out(env_path.c_str(), std::ios::binary);
  file_out << env_str;

  if(!file_out.good()) {
    return 0;
  }

  return env_str.length();
}

}  // namespace


bool InitBootloaderEnvPartition(const vsoc::CuttlefishConfig& config,
                                const std::string& output_path) {
  auto tmp_output_path = output_path + ".tmp";
  auto env_file = vsoc::GetDefaultPerInstanceDir() + "/u-boot.env";
  if(!WriteEnvironment(config, env_file)) {
    LOG(ERROR) << "Unable to write out plaintext env.";
    return false;
  }

  auto mkimage_path = vsoc::DefaultHostArtifactsPath("bin/mkenvimage");
  cvd::Command cmd(mkimage_path);
  cmd.AddParameter("-s");
  cmd.AddParameter("4096");
  cmd.AddParameter("-o");
  cmd.AddParameter(tmp_output_path);
  cmd.AddParameter(env_file);
  int success = cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run mkenvimage. Exited with status " << success;
    return false;
  }

  if(!cvd::FileExists(output_path) || cvd::ReadFile(output_path) != cvd::ReadFile(tmp_output_path)) {
    if(!cvd::RenameFile(tmp_output_path, output_path)) {
      LOG(ERROR) << "Unable to delete the old env image.";
      return false;
    }
    LOG(INFO) << "Updated bootloader environment image.";
  } else {
    cvd::RemoveFile(tmp_output_path);
  }

  return true;
}
