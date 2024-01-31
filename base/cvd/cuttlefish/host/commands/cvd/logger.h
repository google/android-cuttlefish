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

#pragma once

#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <android-base/logging.h>
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

/** Per-thread logging state manager class. */
class ServerLogger {
  friend class CvdServer;

 public:
  /**
   * Thread-specific logger instance.
   *
   * When a `LOG(severity)` message is written on the same thread where this
   * object was created, the message will be sent to the file descriptor stored
   * in this object.
   */
  class ScopedLogger {
   public:
    friend ServerLogger;
    using LogSeverity = android::base::LogSeverity;

    ScopedLogger(ScopedLogger&&) noexcept;
    ~ScopedLogger();

   private:
    ScopedLogger(ServerLogger&, SharedFD target,
                 const android::base::LogSeverity verbosity);

    /** Callback for `LOG(severity)` messages */
    void LogMessage(android::base::LogId log_buffer_id,
                    android::base::LogSeverity severity, const char* tag,
                    const char* file, unsigned int line, const char* message);
    void SetSeverity(const LogSeverity);

    ServerLogger& server_logger_;
    SharedFD target_;
    android::base::LogSeverity verbosity_;
  };
  ServerLogger();
  ~ServerLogger();

  /**
   * Configure `LOG(severity)` messages to write to the given file descriptor
   * for the lifetime of the returned object.
   */
  ScopedLogger LogThreadToFd(SharedFD, const android::base::LogSeverity);
  ScopedLogger LogThreadToFd(SharedFD);

 private:
  using LogSeverity = android::base::LogSeverity;
  void SetSeverity(const LogSeverity);

  std::shared_mutex thread_loggers_lock_;
  std::unordered_map<std::thread::id, ScopedLogger*> thread_loggers_;
};

}  // namespace cuttlefish
