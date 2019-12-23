/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <set>
#include <string>

#include "common/libs/fs/shared_fd.h"

namespace cvd {
// Creates, or connects to if it already exists, a tap network interface. The
// user needs CAP_NET_ADMIN to create such interfaces or be the owner to connect
// to one.
SharedFD OpenTapInterface(const std::string& interface_name);

// Returns a list of TAP devices that have open file descriptors
std::set<std::string> TapInterfacesInUse();
}
