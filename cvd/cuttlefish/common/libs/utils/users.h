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

#include <grp.h>

#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {

gid_t GroupIdFromName(const std::string& group_name);
bool InGroup(const std::string& group);

/**
 * returns the user's home defined by the system
 *
 * This is done not by using HOME but by calling getpwuid()
 */
Result<std::string> SystemWideUserHome(const uid_t uid);

/**
 * returns SystemWideUserHome(getuid())
 */
Result<std::string> SystemWideUserHome();

}  // namespace cuttlefish
