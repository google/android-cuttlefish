//
// Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/logger.h"

#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include <android-base/logging.h>
#include <android-base/threads.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/server_client.h"

namespace cuttlefish {

ServerLogger::ServerLogger() {
  auto log_callback = [this](android::base::LogId log_buffer_id,
                             android::base::LogSeverity severity,
                             const char* tag, const char* file,
                             unsigned int line, const char* message) {
    auto thread_id = std::this_thread::get_id();
    std::shared_lock lock(thread_loggers_lock_);
    auto logger_it = thread_loggers_.find(thread_id);
    if (logger_it == thread_loggers_.end()) {
      return;
    }
    logger_it->second->LogMessage(log_buffer_id, severity, tag, file, line,
                                  message);
  };
  android::base::SetLogger(log_callback);
}

ServerLogger::~ServerLogger() {
  android::base::SetLogger(android::base::StderrLogger);
}

ServerLogger::ScopedLogger ServerLogger::LogThreadToFd(
    SharedFD target, const android::base::LogSeverity verbosity) {
  return ScopedLogger(*this, std::move(target), verbosity);
}

ServerLogger::ScopedLogger ServerLogger::LogThreadToFd(SharedFD target) {
  return ScopedLogger(*this, std::move(target), kCvdDefaultVerbosity);
}

ServerLogger::ScopedLogger::ScopedLogger(
    ServerLogger& server_logger, SharedFD target,
    const android::base::LogSeverity verbosity)
    : server_logger_(server_logger),
      target_(std::move(target)),
      verbosity_(verbosity) {
  auto thread_id = std::this_thread::get_id();
  std::unique_lock lock(server_logger_.thread_loggers_lock_);
  server_logger_.thread_loggers_[thread_id] = this;
}

ServerLogger::ScopedLogger::ScopedLogger(
    ServerLogger::ScopedLogger&& other) noexcept
    : server_logger_(other.server_logger_),
      target_(std::move(other.target_)),
      verbosity_(std::move(other.verbosity_)) {
  auto thread_id = std::this_thread::get_id();
  std::unique_lock lock(server_logger_.thread_loggers_lock_);
  server_logger_.thread_loggers_[thread_id] = this;
}

ServerLogger::ScopedLogger::~ScopedLogger() {
  auto thread_id = std::this_thread::get_id();
  std::unique_lock lock(server_logger_.thread_loggers_lock_);
  auto logger_it = server_logger_.thread_loggers_.find(thread_id);
  if (logger_it == server_logger_.thread_loggers_.end()) {
    return;
  }
  if (logger_it->second == this) {
    server_logger_.thread_loggers_.erase(logger_it);
  }
}

void ServerLogger::SetSeverity(const LogSeverity severity) {
  std::lock_guard lock(thread_loggers_lock_);
  const auto tid = std::this_thread::get_id();
  if (!Contains(thread_loggers_, tid)) {
    LOG(ERROR) << "Thread logger is not registered for thread #" << tid;
    return;
  }
  thread_loggers_[tid]->SetSeverity(severity);
}

void ServerLogger::ScopedLogger::LogMessage(
    android::base::LogId /* log_buffer_id */,
    android::base::LogSeverity severity, const char* tag, const char* file,
    unsigned int line, const char* message) {
  if (severity < verbosity_) {
    return;
  }

  time_t t = time(nullptr);
  struct tm now;
  localtime_r(&t, &now);
  auto output_string =
      StderrOutputGenerator(now, getpid(), android::base::GetThreadId(),
                            severity, tag, file, line, message);
  WriteAll(target_, output_string);
}

void ServerLogger::ScopedLogger::SetSeverity(const LogSeverity severity) {
  verbosity_ = severity;
}

}  // namespace cuttlefish
