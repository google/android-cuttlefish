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

#include <sys/types.h>

#include <vector>

namespace cvd {

// Starts a new child process with the given parameters. Returns the pid of the
// started process or -1 if failed. The function with no environment arguments
// launches the subprocess with the same environment as the parent, to have an
// empty environment just pass an empty vector as second argument.
pid_t subprocess(const std::vector<std::string>& command,
                 const std::vector<std::string>& env);
pid_t subprocess(const std::vector<std::string>& command);

// Same as subprocess, but it waits for the child process before returning.
// Returns zero if the process completed successfully, non zero otherwise.
int execute(const std::vector<std::string>& command,
            const std::vector<std::string>& env);
int execute(const std::vector<std::string>& command);

}  // namespace cvd
