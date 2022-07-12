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

#include <cinttypes>
#include <cstring>
#include <ctime>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/threads.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/environment.h"

using android::base::GetThreadId;
using android::base::FATAL;
using android::base::LogSeverity;
using android::base::StringPrintf;

namespace cuttlefish {

static LogSeverity GuessSeverity(
    const std::string& env_var, LogSeverity default_value) {
  using android::base::VERBOSE;
  using android::base::DEBUG;
  using android::base::INFO;
  using android::base::WARNING;
  using android::base::ERROR;
  using android::base::FATAL_WITHOUT_ABORT;
  using android::base::FATAL;
  std::string env_value = StringFromEnv(env_var, "");
  using android::base::EqualsIgnoreCase;
  if (EqualsIgnoreCase(env_value, "VERBOSE")
      || env_value == std::to_string((int) VERBOSE)) {
    return VERBOSE;
  } else if (EqualsIgnoreCase(env_value, "DEBUG")
      || env_value == std::to_string((int) DEBUG)) {
    return DEBUG;
  } else if (EqualsIgnoreCase(env_value, "INFO")
      || env_value == std::to_string((int) INFO)) {
    return INFO;
  } else if (EqualsIgnoreCase(env_value, "WARNING")
      || env_value == std::to_string((int) WARNING)) {
    return WARNING;
  } else if (EqualsIgnoreCase(env_value, "ERROR")
      || env_value == std::to_string((int) ERROR)) {
    return ERROR;
  } else if (EqualsIgnoreCase(env_value, "FATAL_WITHOUT_ABORT")
      || env_value == std::to_string((int) FATAL_WITHOUT_ABORT)) {
    return FATAL_WITHOUT_ABORT;
  } else if (EqualsIgnoreCase(env_value, "FATAL")
      || env_value == std::to_string((int) FATAL)) {
    return FATAL;
  } else {
    return default_value;
  }
}

LogSeverity ConsoleSeverity() {
  return GuessSeverity("CF_CONSOLE_SEVERITY", android::base::INFO);
}

LogSeverity LogFileSeverity() {
  return GuessSeverity("CF_FILE_SEVERITY", android::base::DEBUG);
}

TeeLogger::TeeLogger(const std::vector<SeverityTarget>& destinations,
                     const std::string& prefix)
    : destinations_(destinations), prefix_(prefix) {}

// Copied from system/libbase/logging_splitters.h
static std::pair<int, int> CountSizeAndNewLines(const char* message) {
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
// This splits the message up line by line, by calling log_function with a pointer to the start of
// each line and the size up to the newline character.  It sends size = -1 for the final line.
template <typename F, typename... Args>
static void SplitByLines(const char* msg, const F& log_function, Args&&... args) {
  const char* newline = strchr(msg, '\n');
  while (newline != nullptr) {
    log_function(msg, newline - msg, args...);
    msg = newline + 1;
    newline = strchr(msg, '\n');
  }

  log_function(msg, -1, args...);
}

// Copied from system/libbase/logging_splitters.h
// This adds the log header to each line of message and returns it as a string intended to be
// written to stderr.
static std::string StderrOutputGenerator(const struct tm& now, int pid, uint64_t tid,
                                         LogSeverity severity, const char* tag, const char* file,
                                         unsigned int line, const char* message) {
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%m-%d %H:%M:%S", &now);

  static const char log_characters[] = "VDIWEFF";
  static_assert(arraysize(log_characters) - 1 == FATAL + 1,
                "Mismatch in size of log_characters and values in LogSeverity");
  char severity_char = log_characters[severity];
  std::string line_prefix;
  if (file != nullptr) {
    line_prefix = StringPrintf("%s %c %s %5d %5" PRIu64 " %s:%u] ", tag ? tag : "nullptr",
                               severity_char, timestamp, pid, tid, file, line);
  } else {
    line_prefix = StringPrintf("%s %c %s %5d %5" PRIu64 " ", tag ? tag : "nullptr", severity_char,
                               timestamp, pid, tid);
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
static std::string StripColorCodes(const std::string& str) {
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

void TeeLogger::operator()(
    android::base::LogId,
    android::base::LogSeverity severity,
    const char* tag,
    const char* file,
    unsigned int line,
    const char* message) {
  for (const auto& destination : destinations_) {
    std::string msg_with_prefix = prefix_ + message;
    std::string output_string;
    if (destination.metadata_level == MetadataLevel::ONLY_MESSAGE) {
      output_string = msg_with_prefix + std::string("\n");
    } else {
      struct tm now;
      time_t t = time(nullptr);
      localtime_r(&t, &now);
      output_string =
          StderrOutputGenerator(now, getpid(), GetThreadId(), severity, tag,
                                file, line, msg_with_prefix.c_str());
    }
    if (severity >= destination.severity) {
      if (destination.target->IsATTY()) {
        WriteAll(destination.target, output_string);
      } else {
        WriteAll(destination.target, StripColorCodes(output_string));
      }
    }
  }
}

static std::vector<SeverityTarget> SeverityTargetsForFiles(
    const std::vector<std::string>& files) {
  std::vector<SeverityTarget> log_severities;
  for (const auto& file : files) {
    auto log_file_fd =
        SharedFD::Open(
          file,
          O_CREAT | O_WRONLY | O_APPEND,
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (!log_file_fd->IsOpen()) {
      LOG(FATAL) << "Failed to create log file: " << log_file_fd->StrError();
    }
    log_severities.push_back(
        SeverityTarget{LogFileSeverity(), log_file_fd, MetadataLevel::FULL});
  }
  return log_severities;
}

TeeLogger LogToFiles(const std::vector<std::string>& files,
                     const std::string& prefix) {
  return TeeLogger(SeverityTargetsForFiles(files), prefix);
}

TeeLogger LogToStderrAndFiles(const std::vector<std::string>& files,
                              const std::string& prefix) {
  std::vector<SeverityTarget> log_severities = SeverityTargetsForFiles(files);
  log_severities.push_back(SeverityTarget{ConsoleSeverity(),
                                          SharedFD::Dup(/* stderr */ 2),
                                          MetadataLevel::ONLY_MESSAGE});
  return TeeLogger(log_severities, prefix);
}

} // namespace cuttlefish
