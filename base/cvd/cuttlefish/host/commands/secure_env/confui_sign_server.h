//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/confui_sign.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {
class ConfUiSignServer {
 public:
  ConfUiSignServer(TpmResourceManager& tpm_resource_manager,
                   SharedFD server_fd);
  [[noreturn]] void MainLoop();

 private:
  TpmResourceManager& tpm_resource_manager_;
  std::string server_socket_path_;
  SharedFD server_fd_;
};
}  // end of namespace cuttlefish
