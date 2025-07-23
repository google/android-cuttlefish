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

#include <string>

#include "cuttlefish/common/libs/utils/subprocess.h"

namespace cuttlefish {

/*
 * Consumes a Command and runs it, optionally managing the stdio channels.
 *
 * If `stdin` is set, the subprocess stdin will be pipe providing its contents.
 * If `stdout` is set, the subprocess stdout will be captured and saved to it.
 * If `stderr` is set, the subprocess stderr will be captured and saved to it.
 *
 * If `command` exits normally, the lower 8 bits of the return code will be
 * returned in a value between 0 and 255.
 * If some setup fails, `command` fails to start, or `command` exits due to a
 * signal, the return value will be negative.
 */
int RunWithManagedStdio(Command&& cmd_tmp, const std::string* stdin,
                        std::string* stdout, std::string* stderr,
                        SubprocessOptions options = SubprocessOptions());

}  // namespace cuttlefish
