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

#include "cuttlefish/common/libs/utils/container.h"

#include <cstdlib>
#include <string>

#include "cuttlefish/common/libs/utils/files.h"

namespace cuttlefish {
namespace {

bool IsRunningInDocker() {
  // if /.dockerenv exists, it's inside a docker container
  std::string docker_env_path("/.dockerenv");
  return FileExists(docker_env_path) || DirectoryExists(docker_env_path);
}

bool IsRunningInPodman() { return FileExists("/run/.containerenv"); }

}  // namespace

bool IsRunningInContainer() {
  // TODO: add more if we support other containers than docker
  return IsRunningInDocker() || IsRunningInPodman();
}

}  // namespace cuttlefish
