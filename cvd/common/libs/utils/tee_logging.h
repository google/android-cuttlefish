//
// Copyright (C) 2020 The Android Open Source Project
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

#include <string>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

android::base::LogSeverity ConsoleSeverity();
android::base::LogSeverity LogFileSeverity();

struct SeverityTarget {
  android::base::LogSeverity severity;
  SharedFD target;
};

class TeeLogger {
private:
  std::vector<SeverityTarget> destinations_;
public:
  TeeLogger(const std::vector<SeverityTarget>& destinations);
  ~TeeLogger() = default;

  void operator()(
      android::base::LogId log_id,
      android::base::LogSeverity severity,
      const char* tag,
      const char* file,
      unsigned int line,
      const char* message);
};

TeeLogger LogToFiles(const std::vector<std::string>& files);
TeeLogger LogToStderrAndFiles(const std::vector<std::string>& files);

} // namespace cuttlefish
