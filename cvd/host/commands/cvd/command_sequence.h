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

#include <vector>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_client.h"
#include "host/libs/config/inject.h"

namespace cuttlefish {

class CommandSequenceExecutor : public LateInjected {
 public:
  INJECT(CommandSequenceExecutor());

  Result<void> LateInject(fruit::Injector<>&) override;

  Result<void> Interrupt();
  Result<std::vector<cvd::Response>> Execute(
      const std::vector<RequestWithStdio>&, SharedFD report);

 private:
  std::vector<CvdServerHandler*> server_handlers_;
  std::vector<CvdServerHandler*> handler_stack_;
  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<CommandSequenceExecutor> CommandSequenceExecutorComponent();

}  // namespace cuttlefish
