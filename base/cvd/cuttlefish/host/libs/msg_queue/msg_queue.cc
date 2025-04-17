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

#define DEFAULT_MSGQ_KEY 0x1234
#define HASH_MULTIPLIER 5381

namespace cuttlefish {

key_t GenerateQueueKey(const char* str) {
  if (str == nullptr || *str == '\0') {
    LOG(ERROR) << "Invalid queue name provided: " << str;
    LOG(ERROR) << "Using default msg queue key: " << DEFAULT_MSGQ_KEY;
    return DEFAULT_MSGQ_KEY;
  }

  uint64_t hash = HASH_MULTIPLIER;
  int c;

  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c;
  }

  return static_cast<key_t>(hash);
}

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
    const std::string& queue_name, bool auto_close) {
  key_t key = GenerateQueueKey(queue_name.c_str());

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
  // Ensure data buffer has space for message type
  if (size < sizeof(long)) {
    LOG(ERROR) << "receive: buffer size too small";
    return -1;
  }
  // System call fails with errno set to ENOMSG if queue is empty and
  // non-blocking.
  int msgflg = block ? 0 : IPC_NOWAIT;
  ssize_t result = msgrcv(msgid_, data, size, msgtyp, msgflg);
  if (result == -1) {
    LOG(ERROR) << "receive: failed to receive any messages. Error: "
               << strerror(errno);
  }

  return result;
}

}  // namespace cuttlefish
