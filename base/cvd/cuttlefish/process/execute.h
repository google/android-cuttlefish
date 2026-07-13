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
#pragma once

#include <sys/wait.h>

#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/process/subprocess_options.h"

namespace cuttlefish {

/**
 * Returns the exit status on success, negative values on error
 *
 * If failed in fork() or exec(), returns -1.
 * If the child exited from an unhandled signal, returns -1.
 * Otherwise, returns the exit status.
 *
 * TODO: Changes return type to Result<int>
 *
 *   For now, too many callsites expects int, and needs quite a lot of changes
 *   if we change the return type.
 */
int Execute(std::vector<std::string> command);

/**
 * Similar as the one above but returns CF_ERR instead of -1, and siginfo_t
 * instead of the exit status.
 */
Result<siginfo_t> Execute(std::vector<std::string> command,
                          SubprocessOptions subprocess_options,
                          int wait_options);

}  // namespace cuttlefish
