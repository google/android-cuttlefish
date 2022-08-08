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

#include "host/libs/msg_queue/msg_queue.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <fstream>
#include <iostream>
#include <memory>

#include <android-base/logging.h>

namespace cuttlefish {

// class holds `msgid` returned from msg_queue_create, and match the lifetime of
// the message queue to the lifetime of the object.

SysVMessageQueue::SysVMessageQueue(int id, bool auto_close)
    : msgid_(id), auto_close_(auto_close) {}

SysVMessageQueue::~SysVMessageQueue(void) {
  if (auto_close_ && msgctl(msgid_, IPC_RMID, NULL) < 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not remove message queue: " << strerror(error_num);
  }
}

// SysVMessageQueue::Create would return an empty/null std::unique_ptr if
// initialization failed.
std::unique_ptr<SysVMessageQueue> SysVMessageQueue::Create(
    const std::string& path, char proj_id, bool auto_close) {
  // key file must exist before calling ftok
  std::fstream fs;
  fs.open(path, std::ios::out);
  fs.close();

  // only the owning user has access
  key_t key = ftok(path.c_str(), proj_id);
  if (key < 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not ftok to create IPC key: " << strerror(error_num);
    return NULL;
  }
  int queue_id = msgget(key, 0);
  if (queue_id < 0) {
    queue_id = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
  }
  auto msg = std::unique_ptr<SysVMessageQueue>(
      new SysVMessageQueue(queue_id, auto_close));
  return msg;
}

int SysVMessageQueue::Send(void* data, size_t size, bool block) {
  int msgflg = block ? 0 : IPC_NOWAIT;
  if (msgsnd(msgid_, data, size, msgflg) < 0) {
    int error_num = errno;
    if (error_num == EAGAIN) {
      // returns EAGAIN if queue is full and non-blocking
      return EAGAIN;
    }
    LOG(ERROR) << "Could not send message: " << strerror(error_num);
    return error_num;
  }
  return 0;
}

// If msgtyp is 0, then the first message in the queue is read.
// If msgtyp is greater than 0, then the first message in the queue of type
// msgtyp is read.
// If msgtyp is less than 0, then the first message in the queue with the lowest
// type less than or equal to the absolute value of msgtyp will be read.
ssize_t SysVMessageQueue::Receive(void* data, size_t size, long msgtyp,
                                  bool block) {
  // System call fails with errno set to ENOMSG if queue is empty and
  // non-blocking.
  int msgflg = block ? 0 : IPC_NOWAIT;
  return msgrcv(msgid_, data, size, msgtyp, msgflg);
}

}  // namespace cuttlefish
