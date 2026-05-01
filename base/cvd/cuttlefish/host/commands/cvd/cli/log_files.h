/*
 * Copyright (C) 2026 The Android Open Source Project
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
#include <vector>

namespace cuttlefish {

/**
 * Returns a vector containing the path to a new timestamped log file for the
 * cvd command. The log file is created in CvdDir() + "/logs/" with a name like
 * cvd_YYYYMMDD_HHMMSS.ms.log. If the logs directory cannot be created, the
 * function returns an empty vector.
 */
std::vector<std::string> GetLogFiles();

}  // namespace cuttlefish
