//
// Copyright (C) 2019 The Android Open Source Project
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

#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/thread_safe_queue/thread_safe_queue.h"

namespace cvd {

using TeeBufferPtr = std::shared_ptr<std::vector<char>>;
using TeeSubscriber = std::function<void(const TeeBufferPtr)>;

struct TeeTarget {
  std::thread runner;
  ThreadSafeQueue<TeeBufferPtr> content_queue;
  TeeSubscriber handler;

  TeeTarget(TeeSubscriber handler) : handler(handler) {}
};

class Tee {
  std::thread reader_;
  std::list<TeeTarget> targets_;
public:
  ~Tee();

  TeeSubscriber* AddSubscriber(TeeSubscriber);

  void Start(SharedFD source);
  void Join();
};

TeeSubscriber SharedFDWriter(SharedFD fd);

class TeeStderrToFile {
  cvd::SharedFD log_file_;
  cvd::SharedFD original_stderr_;
  std::condition_variable notifier_;
  std::mutex mutex_;
  Tee tee_; // This should be destroyed first, so placed last.
public:
  TeeStderrToFile();
  ~TeeStderrToFile();

  void SetFile(SharedFD file);
};

} // namespace cvd
