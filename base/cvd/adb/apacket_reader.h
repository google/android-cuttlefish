/*
 * Copyright (C) 2024 The Android Open Source Project
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

#pragma once

#include <memory>
#include <vector>

#include "types.h"

// Historically, adb expects apackets to be transferred over USB with two transfers. One for the
// header and one for the payload. This usually translates into two Blocks. Buggy drivers and
// "bridges" / IO libs can lead to merged transfers (e.g.: a header and a payload, or a payload
// and the next header).
// This class is able to read inbound Blocks containing apackets chopped/merged on any boundaries.
class APacketReader {
  public:
    APacketReader();
    ~APacketReader() = default;

    enum AddResult { OK, ERROR };
    AddResult add_bytes(Block&& block) noexcept;

    // Returns all packets parsed so far. Upon return, the internal apacket vector is emptied.
    std::vector<std::unique_ptr<apacket>> get_packets() noexcept;

    // Clear blocks so we can start parsing the next packet.
    void prepare_for_next_packet();

  private:
    void add_packet(std::unique_ptr<apacket> packet);
    Block header_{sizeof(amessage)};
    std::unique_ptr<apacket> packet_;

    // We keep packets in this internal vector. It is empty after a `get_packets` call.
    std::vector<std::unique_ptr<apacket>> packets_;
};