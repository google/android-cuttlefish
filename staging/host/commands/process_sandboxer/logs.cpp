/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "host/commands/process_sandboxer/logs.h"

#include <fcntl.h>
#include <unistd.h>

#include <memory>
#include <sstream>
#include <string>

#include <absl/log/log.h>
#include <absl/log/log_sink.h>
#include <absl/log/log_sink_registry.h>
#include <absl/status/statusor.h>

namespace cuttlefish {
namespace process_sandboxer {
namespace {

// Implementation based on absl::log_internal::StderrLogSink
class FileLogSink final : absl::LogSink {
 public:
  static absl::StatusOr<std::unique_ptr<FileLogSink>> FromPath(
      const std::string& path) {
    std::unique_ptr<FileLogSink> sink(new FileLogSink());
    sink->fd_ = open(path.c_str(), O_APPEND | O_CREAT | O_WRONLY, 0644);
    if (sink->fd_ < 0) {
      return absl::ErrnoToStatus(errno, "open failed");
    }
    absl::AddLogSink(sink.get());
    return sink;
  }
  FileLogSink(FileLogSink&) = delete;
  ~FileLogSink() {
    absl::RemoveLogSink(this);
    if (fd_ >= 0 && close(fd_) < 0) {
      PLOG(ERROR) << "Failed to close fd '" << fd_ << "'";
    }
  }

  void Send(const absl::LogEntry& entry) override {
    std::stringstream message_stream;
    if (!entry.stacktrace().empty()) {
      message_stream << entry.stacktrace();
    }
    message_stream << entry.text_message_with_prefix_and_newline();
    auto message = message_stream.str();
    auto written = write(fd_, message.c_str(), message.size());
    if (written < 0) {
      // LOG calls inside here would recurse infinitely because of AddLogSink
      std::cerr << "FileLogSink: write(" << fd_
                << ") failed: " << strerror(errno) << '\n';
    }
  }

 private:
  FileLogSink() = default;

  int fd_ = -1;
};

}  // namespace

absl::Status LogToFiles(const std::vector<std::string>& paths) {
  for (const auto& path : paths) {
    auto sink_status = FileLogSink::FromPath(path);
    if (!sink_status.ok()) {
      return sink_status.status();
    }
    sink_status->release();  // Deliberate leak so LOG always writes here
  }
  return absl::OkStatus();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
