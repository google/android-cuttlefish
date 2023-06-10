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

#pragma once

#include <mutex>
#include <optional>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"

namespace cuttlefish {

struct RunWithManagedIoParam {
  Command cmd_;
  const bool redirect_stdout_ = false;
  const bool redirect_stderr_ = false;
  const std::string* stdin_;
  std::function<Result<void>(void)> callback_;
  const SubprocessOptions options_ = SubprocessOptions();
};

struct RunOutput { // a better name please
  std::string stdout_;
  std::string stderr_;
};

class SubprocessWaiter {
 public:
  INJECT(SubprocessWaiter()) {}

  Result<void> Setup(Subprocess subprocess);
  Result<siginfo_t> Wait();
  Result<void> Interrupt();

  Result<RunOutput> RunWithManagedStdioInterruptable(RunWithManagedIoParam& param);

 private:
  std::optional<Subprocess> subprocess_;
  std::mutex interruptible_;
  bool interrupted_ = false;
};

}  // namespace cuttlefish
