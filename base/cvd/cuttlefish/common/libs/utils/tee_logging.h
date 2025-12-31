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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

constexpr const char* kConsoleSeverityEnvVar = "CF_CONSOLE_SEVERITY";
constexpr const char* kFileSeverityEnvVar = "CF_FILE_SEVERITY";

enum class LogSeverity : int {
  Verbose = 0,
  Debug,
  Info,
  Warning,
  Error,
  Fatal,
};

std::string FromSeverity(LogSeverity severity);
Result<LogSeverity> ToSeverity(const std::string& value);

LogSeverity ConsoleSeverity();
LogSeverity LogFileSeverity();

enum class MetadataLevel { FULL, ONLY_MESSAGE, TAG_AND_MESSAGE };

struct SeverityTarget {
  LogSeverity severity;
  SharedFD target;
  MetadataLevel metadata_level;

  static SeverityTarget FromFile(
      const std::string& path,
      MetadataLevel metadata_level = MetadataLevel::FULL,
      LogSeverity severity = LogSeverity::Verbose);

  static SeverityTarget FromFd(
      SharedFD fd, MetadataLevel metadata_level = MetadataLevel::FULL,
      LogSeverity severity = LogSeverity::Verbose);
};

// Set the new logging destinations, replacing existing ones.
void SetLoggers(std::vector<SeverityTarget> destinations,
                const std::string& log_prefix = "");

// Configure the process to only log to stderr.
void LogToStderr(const std::string& log_prefix = "",
                 MetadataLevel metadata_level = MetadataLevel::ONLY_MESSAGE,
                 std::optional<LogSeverity> severity = std::nullopt);

// Configure process to log to a list of files. Logs of all severities are
// always written in full.
void LogToFiles(const std::vector<std::string>& files,
                const std::string& log_prefix = "");

// Configure the process to log to stderr and some files. Only the severity and
// metadata for the stderr logger can be configured, full logs will be written
// to the files.
void LogToStderrAndFiles(
    const std::vector<std::string>& files, const std::string& log_prefix = "",
    MetadataLevel stderr_level = MetadataLevel::ONLY_MESSAGE,
    std::optional<LogSeverity> stderr_severity = std::nullopt);

class LogSink;
// Adds an extra destination for this process's logs for the duration of the
// lifetime of this logger. Existing logging destinations are not affected.
class ScopedLogger {
 public:
  ScopedLogger(SeverityTarget target, const std::string& prefix);
  ~ScopedLogger();

 private:
  std::unique_ptr<LogSink> log_sink_;
};

}  // namespace cuttlefish
