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

#include <algorithm>
#include <iostream>

#include "android-base/logging.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/tee.h"

static const std::size_t READ_SIZE = 512;

namespace cvd {

TeeSubscriber* Tee::AddSubscriber(TeeSubscriber subscriber) {
  if (reader_.joinable()) {
    return nullptr;
  }
  return &targets_.emplace_back(std::move(subscriber)).handler;
}

void Tee::Start(SharedFD source) {
  reader_ = std::thread([this, source]() {
    while (true) {
      // TODO(schfufelen): Use multiple buffers at once for readv
      // TODO(schuffelen): Reuse buffers
      TeeBufferPtr buffer = std::make_shared<std::vector<char>>(READ_SIZE);
      ssize_t read = source->Read(buffer->data(), buffer->size());
      if (read <= 0) {
        for (auto& target : targets_) {
          target.content_queue.Push(nullptr);
        }
        break;
      }
      buffer->resize(read);
      for (auto& target : targets_) {
        target.content_queue.Push(buffer);
      }
    }
  });
  for (auto& target : targets_) {
    target.runner = std::thread([&target]() {
      while (true) {
        auto queue_chunk = target.content_queue.PopAll();
        // TODO(schuffelen): Pass multiple buffers to support writev
        for (auto& buffer : queue_chunk) {
          if (!buffer) {
            return;
          }
          target.handler(buffer);
        }
      }
    });
  }
}

Tee::~Tee() {
  Join();
}

void Tee::Join() {
  if (reader_.joinable()) {
    reader_.join();
  }
  auto it = targets_.begin();
  while (it != targets_.end()) {
    if (it->runner.joinable()) {
      it->runner.join();
    }
    it = targets_.erase(it);
  }
}

TeeSubscriber SharedFDWriter(SharedFD fd) {
  return [fd](const TeeBufferPtr buffer) { WriteAll(fd, *buffer); };
}

// An alternative to this would have been to modify the logger, but that would
// not capture logs from subprocesses.
TeeStderrToFile::TeeStderrToFile() {
  original_stderr_ = SharedFD::Dup(2);

  SharedFD stderr_read, stderr_write;
  SharedFD::Pipe(&stderr_read, &stderr_write);
  stderr_write->UNMANAGED_Dup2(2);
  stderr_write->Close();

  tee_.AddSubscriber(SharedFDWriter(original_stderr_));
  tee_.AddSubscriber(
      [this](cvd::TeeBufferPtr data) {
        std::unique_lock lock(mutex_);
        while (!log_file_->IsOpen()) {
          notifier_.wait(lock);
        }
        cvd::WriteAll(log_file_, *data);
      });
  tee_.Start(std::move(stderr_read));
}

TeeStderrToFile::~TeeStderrToFile() {
  original_stderr_->UNMANAGED_Dup2(2);
}

void TeeStderrToFile::SetFile(SharedFD file) {
  std::lock_guard lock(mutex_);
  log_file_ = file;
  notifier_.notify_all();
}

} // namespace
