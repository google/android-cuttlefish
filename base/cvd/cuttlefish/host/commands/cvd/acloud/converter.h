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

#pragma once

#include <atomic>

#include <fruit/fruit.h>

#include "host/commands/cvd/server_client.h"

namespace cuttlefish {

struct ConvertedAcloudCreateCommand {
  std::vector<RequestWithStdio> prep_requests;
  RequestWithStdio start_request;
};

class ConvertAcloudCreateCommand {
 public:
  virtual Result<ConvertedAcloudCreateCommand> Convert(
      const RequestWithStdio& request) = 0;
  virtual const std::string& FetchCvdArgsFile() const = 0;
  virtual const std::string& FetchCommandString() const = 0;
  virtual bool Verbose() const = 0;
  /*
   * Android prouction build system appears to mandate virtual
   * destructor.
   */
  virtual ~ConvertAcloudCreateCommand() = 0;
};

fruit::Component<ConvertAcloudCreateCommand>
AcloudCreateConvertCommandComponent();

}  // namespace cuttlefish
