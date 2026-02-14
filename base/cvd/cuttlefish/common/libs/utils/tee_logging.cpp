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

#include "tee_logging.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstring>
#include <ctime>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/macros.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/threads.h>
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/proc_file_utils.h"
#include "cuttlefish/result/result.h"

using android::base::GetThreadId;
using android::base::StringPrintf;

namespace cuttlefish {
namespace {

std::string ToUpper(const std::string& input) {
  std::string output = input;
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char ch) { return std::toupper(ch); });
  return output;
}

std::vector<SeverityTarget> SeverityTargetsForFiles(
    const std::vector<std::string>& files) {
  std::vector<SeverityTarget> log_severities;
  for (const auto& file : files) {
    log_severities.emplace_back(
        SeverityTarget::FromFile(file, MetadataLevel::FULL, LogFileSeverity()));
  }
  return log_severities;
}

LogSeverity FromLogEntry(const absl::LogEntry& log_entry) {
  switch (log_entry.log_severity()) {
    case absl::LogSeverity::kFatal:
      return LogSeverity::Fatal;
    case absl::LogSeverity::kError:
      return LogSeverity::Error;
    case absl::LogSeverity::kWarning:
      return LogSeverity::Warning;
    case absl::LogSeverity::kInfo:
      switch (log_entry.verbosity()) {
        case absl::LogEntry::kNoVerbosityLevel:
          return LogSeverity::Info;
        case 0:
          return LogSeverity::Debug;
        default:
          return LogSeverity::Verbose;
      }
  }
}

// Copied from system/libbase/logging_splitters.h
std::pair<int, int> CountSizeAndNewLines(const char* message) {
  int size = 0;
  int new_lines = 0;
  while (*message != '\0') {
    size++;
    if (*message == '\n') {
      ++new_lines;
    }
    ++message;
  }
  return {size, new_lines};
}

// Copied from system/libbase/logging_splitters.h
// This splits the message up line by line, by calling log_function with a
// pointer to the start of each line and the size up to the newline character.
// It sends size = -1 for the final line.
template <typename F, typename... Args>
void SplitByLines(const char* msg, const F& log_function, Args&&... args) {
  const char* newline = strchr(msg, '\n');
  while (newline != nullptr) {
    log_function(msg, newline - msg, args...);
    msg = newline + 1;
    newline = strchr(msg, '\n');
  }

  log_function(msg, -1, args...);
}

// Copied from system/libbase/logging_splitters.h
// This adds the log header to each line of message and returns it as a string
// intended to be written to stderr.
std::string StderrOutputGenerator(const struct tm& now, int pid, uint64_t tid,
                                  LogSeverity severity, const char* tag,
                                  const char* file, unsigned int line,
                                  const char* message) {
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%m-%d %H:%M:%S", &now);

  static const char log_characters[] = "VDIWEF";
  static_assert(
      arraysize(log_characters) - 1 == static_cast<int>(LogSeverity::Fatal) + 1,
      "Mismatch in size of log_characters and values in LogSeverity");
  char severity_char = log_characters[static_cast<int>(severity)];
  std::string line_prefix;
  if (file != nullptr) {
    line_prefix =
        StringPrintf("%s %c %s %5d %5" PRIu64 " %s:%u] ", tag ? tag : "nullptr",
                     severity_char, timestamp, pid, tid, file, line);
  } else {
    line_prefix =
        StringPrintf("%s %c %s %5d %5" PRIu64 " ", tag ? tag : "nullptr",
                     severity_char, timestamp, pid, tid);
  }

  auto [size, new_lines] = CountSizeAndNewLines(message);
  std::string output_string;
  output_string.reserve(size + new_lines * line_prefix.size() + 1);

  auto concat_lines = [&](const char* message, int size) {
    output_string.append(line_prefix);
    if (size == -1) {
      output_string.append(message);
    } else {
      output_string.append(message, size);
    }
    output_string.append("\n");
  };
  SplitByLines(message, concat_lines);
  return output_string;
}

// TODO(schuffelen): Do something less primitive.
std::string StripColorCodes(const std::string& str) {
  std::stringstream sstream;
  bool in_color_code = false;
  for (char c : str) {
    if (c == '\033') {
      in_color_code = true;
    }
    if (!in_color_code) {
      sstream << c;
    }
    if (c == 'm') {
      in_color_code = false;
    }
  }
  return sstream.str();
}

}  // namespace

SeverityTarget SeverityTarget::FromFile(const std::string& path,
                                        MetadataLevel metadata_level,
                                        LogSeverity severity) {
  auto log_file_fd =
      SharedFD::Open(path, O_CREAT | O_WRONLY | O_APPEND,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (!log_file_fd->IsOpen()) {
    LOG(FATAL) << "Failed to create log file: " << log_file_fd->StrError();
  }
  return SeverityTarget{severity, log_file_fd, metadata_level};
}

SeverityTarget SeverityTarget::FromFd(SharedFD fd, MetadataLevel metadata_level,
                                      LogSeverity severity) {
  return SeverityTarget{severity, fd, metadata_level};
}

class LogSink : public absl::LogSink {
 public:
  LogSink(SeverityTarget destination, const std::string& prefix)
      : destination_(std::move(destination)), prefix_(prefix) {
    Result<std::string> exe = GetExecutablePath(getpid());
    if (!exe.ok()) {
      executable_ = std::to_string(getpid());
      return;
    }
    executable_ =
        fmt::format("{}({}) ", android::base::Basename(*exe), getpid());
  }

  void Send(const absl::LogEntry& entry) override {
    LogSeverity severity = FromLogEntry(entry);
    if (severity < destination_.severity) {
      return;
    }
    std::string output_string;
    switch (destination_.metadata_level) {
      case MetadataLevel::ONLY_MESSAGE:
        output_string = fmt::format("{}{}\n", prefix_, entry.text_message());
        break;
      case MetadataLevel::TAG_AND_MESSAGE:
        output_string = fmt::format("{}] {}{}{}", executable_, prefix_,
                                    entry.text_message(), "\n");
        break;
      default:
        struct tm now;
        time_t t = time(nullptr);
        localtime_r(&t, &now);
        std::string message_with_prefix =
            fmt::format("{}{}", prefix_, entry.text_message());
        output_string = StderrOutputGenerator(
            now, getpid(), GetThreadId(), severity, executable_.c_str(),
            std::string(entry.source_basename()).c_str(), entry.source_line(),
            message_with_prefix.c_str());
        break;
    }
    if (severity >= destination_.severity) {
      if (destination_.target->IsATTY()) {
        WriteAll(destination_.target, output_string);
      } else {
        WriteAll(destination_.target, StripColorCodes(output_string));
      }
    }
  }

 private:
  SeverityTarget destination_;
  std::string prefix_;
  std::string executable_;
};

std::string FromSeverity(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::Verbose:
      return "VERBOSE";
    case LogSeverity::Debug:
      return "DEBUG";
    case LogSeverity::Info:
      return "INFO";
    case LogSeverity::Warning:
      return "WARNING";
    case LogSeverity::Error:
      return "ERROR";
    case LogSeverity::Fatal:
      return "FATAL";
  }
  return "Unexpected severity";
}

Result<LogSeverity> ToSeverity(const std::string& value) {
  const std::unordered_map<std::string, LogSeverity> string_to_severity{
      {"VERBOSE", LogSeverity::Verbose}, {"DEBUG", LogSeverity::Debug},
      {"INFO", LogSeverity::Info},       {"WARNING", LogSeverity::Warning},
      {"ERROR", LogSeverity::Error},     {"FATAL", LogSeverity::Fatal},
  };

  const auto upper_value = ToUpper(value);
  if (auto it = string_to_severity.find(upper_value);
      it != string_to_severity.end()) {
    return it->second;
  }
  int value_int;
  CF_EXPECT(absl::SimpleAtoi(value, &value_int),
            "Unable to determine severity from \"" << value << "\"");
  for (const auto& [name, value] : string_to_severity) {
    if (static_cast<int>(value) == value_int) {
      return value;
    }
  }
  return CF_ERRF("Unable to determine severity from \"{}\"", value);
}

static LogSeverity GuessSeverity(const std::string& env_var,
                                 LogSeverity default_value) {
  std::string env_value = StringFromEnv(env_var, "");
  return ToSeverity(env_value).value_or(default_value);
}

LogSeverity ConsoleSeverity() {
  return GuessSeverity(kConsoleSeverityEnvVar, LogSeverity::Info);
}

LogSeverity LogFileSeverity() {
  LogSeverity severity = GuessSeverity(kFileSeverityEnvVar, LogSeverity::Debug);
  if (severity > LogSeverity::Debug) {
    // Debug or higher severity logs must always be written to files.
    return LogSeverity::Debug;
  }
  return severity;
}

void SetLoggers(std::vector<SeverityTarget> destinations,
                const std::string& log_prefix) {
  static std::vector<LogSink>& log_sinks = *new std::vector<LogSink>;
  for (LogSink& log_sink : log_sinks) {
    // In rare cases this function may called more than once per process
    absl::RemoveLogSink(&log_sink);
  }
  log_sinks.clear();
  for (SeverityTarget& destination : destinations) {
    log_sinks.emplace_back(std::move(destination), log_prefix);
  }
  for (LogSink& log_sink : log_sinks) {
    absl::AddLogSink(&log_sink);
  }
  // A custom log sink is typically used for stderr too
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
  // Logs are filtered based on the serverity setting of each destination.
  absl::SetGlobalVLogLevel(std::numeric_limits<int>::max());

  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    absl::InitializeLog();
  }
}

void LogToStderr(const std::string& log_prefix, MetadataLevel metadata_level,
                 std::optional<LogSeverity> severity) {
  LogToStderrAndFiles({}, log_prefix, metadata_level, severity);
}

void LogToFiles(const std::vector<std::string>& files,
                const std::string& log_prefix) {
  SetLoggers(SeverityTargetsForFiles(files), log_prefix);
}

void LogToStderrAndFiles(const std::vector<std::string>& files,
                         const std::string& log_prefix,
                         MetadataLevel stderr_level,
                         std::optional<LogSeverity> stderr_severity) {
  std::vector<SeverityTarget> log_severities = SeverityTargetsForFiles(files);
  log_severities.push_back(
      SeverityTarget{stderr_severity ? *stderr_severity : ConsoleSeverity(),
                     SharedFD::Dup(/* stderr */ 2), stderr_level});
  SetLoggers(log_severities, log_prefix);
}

ScopedLogger::ScopedLogger(SeverityTarget target, const std::string& prefix)
    : log_sink_(new LogSink(std::move(target), prefix)) {
  absl::AddLogSink(log_sink_.get());
}

ScopedLogger::~ScopedLogger() { absl::RemoveLogSink(log_sink_.get()); }

}  // namespace cuttlefish
