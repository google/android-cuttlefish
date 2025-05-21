/*
 * Copyright 2024 The Android Open Source Project
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

#include "common/libs/transport/channel_sharedfd.h"
#include "jcardsim_interface.h"

namespace cuttlefish {
using cuttlefish::transport::ManagedMessage;
using cuttlefish::transport::SharedFdChannel;

class JcardSimResponder {
 public:
  JcardSimResponder(SharedFdChannel& channel,
                    const JCardSimInterface& jcs_interface);

  Result<void> ProcessMessage();

 private:
  Result<ManagedMessage> ToMessage(const std::vector<uint8_t>& data);
  SharedFdChannel& channel_;
  const JCardSimInterface& jcs_interface_;
};

}  // namespace cuttlefish
