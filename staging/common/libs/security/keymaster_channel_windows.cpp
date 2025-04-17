/*
 * Copyright 2020 The Android Open Source Project
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

#include "common/libs/security/keymaster_channel_windows.h"

#include <windows.h>

#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <namedpipeapi.h>
#include <chrono>
#include <cstdlib>
#include <thread>

#include <android-base/logging.h>
#include <keymaster/android_keymaster_utils.h>

namespace cuttlefish {
using keymaster::keymaster_message;

std::unique_ptr<KeymasterWindowsChannel> KeymasterWindowsChannel::Create(
    HANDLE pipe_handle) {
  auto keymaster_channel =
      std::unique_ptr<KeymasterWindowsChannel>(new KeymasterWindowsChannel());
  if (!keymaster_channel->WaitForConnection(pipe_handle)) {
    return nullptr;
  }

  return keymaster_channel;
}

bool KeymasterWindowsChannel::WaitForConnection(HANDLE pipe_handle) {
  assert(pipe_handle_ == NULL);
  pipe_handle_ = pipe_handle;

  DWORD flags;
  if (GetNamedPipeInfo(pipe_handle_,
                       /*lpFlags= */ &flags,
                       /*lpOutBufferSize= */ NULL,
                       /* lpInBufferSize= */ NULL,
                       /* lpMaxInstances= */ NULL) == 0) {
    LOG(ERROR)
        << "Could not query Keymaster named pipe handle info. Got error code "
        << GetLastError();
    return false;
  }

  if ((flags & PIPE_SERVER_END) == 0) {
    LOG(ERROR) << "Keymaster handle is not the server end of a named pipe!";
    return false;
  }

  // Create the event object
  HANDLE event_handle =
      CreateEventA(/* lpEventAttributes= */ NULL, /* bManualReset= */ true,
                   /* bInitialState= */ 0, /* lpName= */ NULL);
  if (event_handle == NULL) {
    LOG(ERROR)
        << "Error: Could not create keymaster event object. Got error code "
        << GetLastError();
    return false;
  }
  pipe_overlapped_.hEvent = event_handle;

  // Wait for client to connect to the pipe
  ConnectNamedPipe(pipe_handle_, &pipe_overlapped_);
  LOG(INFO) << "Listening to existing keymaster pipe handle.";

  if (WaitForSingleObject(pipe_overlapped_.hEvent, INFINITE) != WAIT_OBJECT_0) {
    LOG(ERROR) << "Could not wait for Keymaster pipe's overlapped to be "
                  "signalled. Got Windows error code "
               << GetLastError();
    return false;
  }
  if (!ResetEvent(pipe_overlapped_.hEvent)) {
    LOG(ERROR) << "Could not reset Keymaster pipe's overlapped. Got Windows "
                  "error code "
               << GetLastError();
    return false;
  }
  return true;
}

KeymasterWindowsChannel::~KeymasterWindowsChannel() {
  if (pipe_handle_) {
    CloseHandle(pipe_handle_);
  }

  if (pipe_overlapped_.hEvent) {
    CloseHandle(pipe_overlapped_.hEvent);
  }
}

bool KeymasterWindowsChannel::SendRequest(
    AndroidKeymasterCommand command, const keymaster::Serializable& message) {
  return SendMessage(command, false, message);
}

bool KeymasterWindowsChannel::SendResponse(
    AndroidKeymasterCommand command, const keymaster::Serializable& message) {
  return SendMessage(command, true, message);
}

bool KeymasterWindowsChannel::SendMessage(
    AndroidKeymasterCommand command, bool is_response,
    const keymaster::Serializable& message) {
  auto payload_size = message.SerializedSize();
  if (payload_size > 1024 * 1024) {
    LOG(WARNING) << "Sending large message with id: " << command << " and size "
                 << payload_size;
  }

  auto to_send = CreateKeymasterMessage(command, is_response, payload_size);
  message.Serialize(to_send->payload, to_send->payload + payload_size);
  auto write_size = payload_size + sizeof(keymaster_message);
  auto to_send_bytes = reinterpret_cast<const char*>(to_send.get());
  if (!WriteFile(pipe_handle_, to_send_bytes, write_size, NULL,
                 &pipe_overlapped_) &&
      GetLastError() != ERROR_IO_PENDING) {
    LOG(ERROR) << "Could not write Keymaster Message. Got Windows error code "
               << GetLastError();
    return false;
  }

  // Vsock pipes are overlapped (asynchronous) and we need to wait for the
  // overlapped event to be signaled.
  // https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject#return-value
  if (WaitForSingleObject(pipe_overlapped_.hEvent, INFINITE) != WAIT_OBJECT_0) {
    LOG(ERROR) << "Could not wait for Keymaster pipe's overlapped to be "
                  "signalled. Got Windows error code "
               << GetLastError();
    return false;
  }
  if (!ResetEvent(pipe_overlapped_.hEvent)) {
    LOG(ERROR) << "Could not reset Keymaster pipe's overlapped. Got Windows "
                  "error code "
               << GetLastError();
    return false;
  }
  return true;
}

bool KeymasterWindowsChannel::ReadFromPipe(LPVOID buffer, DWORD size) {
  if (ReadFile(pipe_handle_, buffer, size, NULL, &pipe_overlapped_) == FALSE) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      LOG(INFO) << "Keymaster pipe was closed.";
      return false;
    } else if (GetLastError() != ERROR_IO_PENDING) {
      LOG(ERROR) << "Could not read Keymaster message. Got Windows error code "
                 << GetLastError();
      return false;
    }

    // Wait for the asynchronous read to finish.
    DWORD unused_bytes_read;
    if (GetOverlappedResult(pipe_handle_, &pipe_overlapped_, &unused_bytes_read,
                            /*bWait=*/TRUE) == FALSE) {
      if (GetLastError() == ERROR_BROKEN_PIPE) {
        LOG(INFO) << "Keymaster pipe was closed.";
        return false;
      }

      LOG(ERROR) << "Error receiving Keymaster data. Got Windows error code "
                 << GetLastError();
      return false;
    }
  }

  if (ResetEvent(pipe_overlapped_.hEvent) == 0) {
    LOG(ERROR) << "Error calling ResetEvent for Keymaster data. Got "
                  "Windows error code "
               << GetLastError();

    return false;
  }

  return true;
}

ManagedKeymasterMessage KeymasterWindowsChannel::ReceiveMessage() {
  struct keymaster_message message_header;
  if (!ReadFromPipe(&message_header, sizeof(message_header))) {
    return {};
  }

  if (message_header.payload_size > 1024 * 1024) {
    LOG(WARNING) << "Received large message with id: " << message_header.cmd
                 << " and size " << message_header.payload_size;
  }

  auto message =
      CreateKeymasterMessage(message_header.cmd, message_header.is_response,
                             message_header.payload_size);
  auto message_bytes = reinterpret_cast<char*>(message->payload);

  if (!ReadFromPipe(message_bytes, message->payload_size)) {
    return {};
  }

  return message;
}

}  // namespace cuttlefish