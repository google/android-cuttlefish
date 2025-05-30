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

#include "apacket_reader.h"

#include "adb.h"
#include "adb_trace.h"

APacketReader::APacketReader() {
    prepare_for_next_packet();
}

void APacketReader::add_packet(std::unique_ptr<apacket> packet) {
    VLOG(USB) << "Got packet " << command_to_string(packet->msg.command)
              << ", size=" << packet->msg.data_length;
    packets_.emplace_back(std::move(packet));
    prepare_for_next_packet();
}

APacketReader::AddResult APacketReader::add_bytes(Block&& block) noexcept {
    if (block.remaining() == 0) {
        return OK;
    }

    VLOG(USB) << "Received " << block.remaining() << " bytes";

    header_.fillFrom(block);
    if (!header_.is_full()) {
        // We don't have a full header. Nothing much we can do here, except wait for the next block.
        return OK;
    }

    // From here, we have a full header and we can peek to see how much payload is expected.
    auto m = reinterpret_cast<amessage*>(header_.data());

    // Is the packet buggy?
    if (m->data_length > MAX_PAYLOAD) {
        VLOG(USB) << "Payload > " << MAX_PAYLOAD;
        prepare_for_next_packet();
        return ERROR;
    }

    // Is it a packet without payload? If it is, we have an apacket.
    if (m->data_length == 0) {
        packet_ = std::make_unique<apacket>();
        packet_->msg = *reinterpret_cast<amessage*>(header_.data());
        packet_->payload = Block{0};
        add_packet(std::move(packet_));
        return add_bytes(std::move(block));
    }

    // In most cases (when the USB layer works as intended) this should be where we have the header
    // but no payload. The odds of using a fast (std::move) are good but we don't know yet. If
    // there is nothing remaining, wait until payload packet shows up.
    if (block.remaining() == 0) {
        VLOG(USB) << "Packet " << command_to_string(m->command) << " needs " << m->data_length
                  << " bytes.";
        return OK;
    }

    // We just received the first block for the packet payload. We may be able to use
    // std::move (fast). If we can't std::move it, we allocate to store the payload as a fallback
    // mechanism (slow).
    if (!packet_) {
        packet_ = std::make_unique<apacket>();
        packet_->msg = *reinterpret_cast<amessage*>(header_.data());

        if (block.position() == 0 && block.remaining() == packet_->msg.data_length) {
            // The block is exactly the expected size and nothing was read from it.
            // Move it and we are done.
            VLOG(USB) << "Zero-copy";
            packet_->payload = std::move(block);
            add_packet(std::move(packet_));
            return OK;
        } else {
            VLOG(USB) << "Falling back: Allocating block " << packet_->msg.data_length;
            packet_->payload.resize(packet_->msg.data_length);
        }
    }

    // Fallback (we could not std::move). Fill the payload with incoming block.
    packet_->payload.fillFrom(block);

    // If we have all the bytes we needed for the payload, we have a packet. Add it to the list.
    if (packet_->payload.is_full()) {
        packet_->payload.rewind();
        add_packet(std::move(packet_));
    } else {
        VLOG(USB) << "Need " << packet_->payload.remaining() << " bytes to full packet";
    }

    // If we still have more data, start parsing the next packet via recursion.
    if (block.remaining() > 0) {
        VLOG(USB) << "Detected block with merged payload-header (remaining=" << block.remaining()
                  << " bytes)";
        return add_bytes(std::move(block));
    }

    return OK;
}

std::vector<std::unique_ptr<apacket>> APacketReader::get_packets() noexcept {
    auto ret = std::move(packets_);
    // We moved the vector so it is in undefined state. clear() sets it back into a known state
    packets_.clear();
    return ret;
}

void APacketReader::prepare_for_next_packet() {
    header_.rewind();
    packet_ = std::unique_ptr<apacket>(nullptr);
}
