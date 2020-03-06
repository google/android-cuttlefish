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

#include <cinttypes>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

class SCTPStream {
 public:
  static std::unique_ptr<SCTPStream> CreateStream(const uint8_t* data_chunk,
                                                  size_t size);

  SCTPStream(uint16_t stream_id) : stream_id_(stream_id) {}
  virtual ~SCTPStream() = default;
  uint16_t stream_id() const { return stream_id_; }
  virtual void InjectPacket(const uint8_t*, size_t size);
  virtual bool IsDataChannel() const {return false;}

 private:
  uint16_t stream_id_;
};

class DataChannelStream : public SCTPStream {
  public:
    DataChannelStream(uint16_t id);
    ~DataChannelStream() override = default;
    void InjectPacket(const uint8_t*, size_t) override;
    const std::string& label() const {
      return label_;
    }
    void OnMessage(std::function<void(const uint8_t*, size_t)> cb) {
      on_message_cb_ = cb;
    }
    bool IsDataChannel() const override {return true;}
  private:
    void ProcessChannelOpen(const uint8_t* data, size_t size);
    void ProcessMessage(const uint8_t* data, size_t size);

    uint16_t seq_num_ = 0;
    std::string label_;
    std::string protocol_;
    uint8_t channel_type_;
    uint32_t reliability_;
    uint16_t priority_;
    std::function<void(const uint8_t*, size_t)> on_message_cb_;
};
