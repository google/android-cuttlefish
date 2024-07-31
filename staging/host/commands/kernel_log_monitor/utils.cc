/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/commands/kernel_log_monitor/utils.h"

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish::monitor {

std::optional<ReadEventResult> ReadEvent(SharedFD fd) {
  size_t length;
  ssize_t bytes_read = ReadExactBinary(fd, &length);
  if (bytes_read <= 0) {
    LOG(ERROR) << "Failed to read event buffer size: " << fd->StrError();
    return std::nullopt;
  }
  std::string buf(length, ' ');
  bytes_read = ReadExact(fd, &buf);
  if (bytes_read <= 0) {
    LOG(ERROR) << "Failed to read event buffer: " << fd->StrError();
    return std::nullopt;
  }

  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  std::string errorMessage;
  Json::Value message;
  if (!reader->parse(&*buf.begin(), &*buf.end(), &message, &errorMessage)) {
    LOG(ERROR) << "Unable to parse event JSON: " << errorMessage;
    return std::nullopt;
  }

  ReadEventResult result = {static_cast<Event>(message["event"].asInt()),
                            message["metadata"]};
  return result;
}

bool WriteEvent(SharedFD fd, const Json::Value& event_message) {
  Json::StreamWriterBuilder factory;
  std::string message_string = Json::writeString(factory, event_message);
  size_t length = message_string.length();
  ssize_t retval = WriteAllBinary(fd, &length);
  if (retval <= 0) {
    LOG(ERROR) << "Failed to write event buffer size: " << fd->StrError();
    return false;
  }
  retval = WriteAll(fd, message_string);
  if (retval <= 0) {
    LOG(ERROR) << "Failed to write event buffer: " << fd->StrError();
    return false;
  }
  return true;
}

}  // namespace cuttlefish::monitor
