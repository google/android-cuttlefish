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

#include <webrtc/SCTPStream.h>

#include <android-base/logging.h>

#include <https/Support.h>

std::unique_ptr<SCTPStream> SCTPStream::CreateStream(const uint8_t* data_chunk,
                                                     size_t /*size*/) {
  std::unique_ptr<SCTPStream> stream;
  auto protocol_id = U32_AT(&data_chunk[12]);
  auto stream_id = U16_AT(&data_chunk[8]);
  switch (protocol_id) {
    case 0x32:
    case 0x33:
      stream.reset(new DataChannelStream(stream_id));
      break;
    default:
      stream.reset(new SCTPStream(stream_id));
  }
  return stream;
}

void SCTPStream::InjectPacket(const uint8_t* /*data_chunk*/, size_t size) {
  LOG(INFO) << "Data chunk received, size: " << size;
}

DataChannelStream::DataChannelStream(uint16_t id)
    : SCTPStream(id), on_message_cb_([](const uint8_t*, size_t) {}) {}

void DataChannelStream::InjectPacket(const uint8_t* data_chunk, size_t size) {
  auto protocol_id = U32_AT(&data_chunk[12]);
  auto stream_sn = U16_AT(&data_chunk[10]);
  auto flags = data_chunk[1];
  if ((flags & 4) != 0 && stream_sn != seq_num_) {
    LOG(WARNING) << "Out of order packet!!!";
    // TODO do something about it
  }
  seq_num_++;
  auto message_size = size - 16;
  if (protocol_id == 0x32) {
    ProcessChannelOpen(&data_chunk[16], message_size);
  } else if (protocol_id == 0x33) {
    ProcessMessage(&data_chunk[16], message_size);
  }
}

void DataChannelStream::ProcessChannelOpen(const uint8_t* data, size_t size) {
  if (size < 10) {
    LOG(ERROR) << "DATA_CHANNEL_OPEN_MESSAGE is not big enough: " << size
               << " < 10";
    return;
  }
  auto message_type = data[0];
  if (message_type != 0x03) {
    // A different message type means the packet has an unknown format.
    LOG(ERROR) << "Incompatible message type: " << message_type
               << ", should be 3";
    return;
  }
  channel_type_ = data[1];
  priority_ = U16_AT(&data[2]);
  reliability_ = U32_AT(&data[4]);
  auto label_length = U16_AT(&data[8]);
  auto protocol_length = U16_AT(&data[10]);
  label_ = STR_AT(&data[12], label_length);
  protocol_ = STR_AT(&data[12 + label_length], protocol_length);
}

void DataChannelStream::ProcessMessage(const uint8_t* data, size_t size) {
  if (size == 0) {
    LOG(ERROR) << "DATA chunk should have non-zero size";
    return;
  }
  on_message_cb_(data, size);
}
