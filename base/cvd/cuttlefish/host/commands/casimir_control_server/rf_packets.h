/*
 * Copyright 2025 The Android Open Source Project
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

// File generated from <stdin>, with the command:
//  /mnt/disks/build-disk/src/android/main/out/soong/.temp/sbox/9a296958a3bd30ec1088ce7729bb85fe3b298ceb/tools/out/bin/pdl_cxx_generator --namespace casimir::rf --output ./out/rf_packets.h
// /!\ Do not edit by hand

/*
* TODO: b/416777029 - Stop using this file and generate it at compile time
*/

#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <utility>
#include <vector>

#include "host/commands/casimir_control_server/packet_runtime.h"




#ifndef _ASSERT_VALID
#ifdef ASSERT
#define _ASSERT_VALID ASSERT
#else
#include <cassert>
#define _ASSERT_VALID assert
#endif  // ASSERT
#endif  // !_ASSERT_VALID

namespace casimir::rf {
class RfPacketView;
class PollCommandView;
class FieldInfoView;
class NfcAPollResponseView;
class T4ATSelectCommandView;
class T4ATSelectResponseView;
class NfcDepSelectCommandView;
class NfcDepSelectResponseView;
class SelectCommandView;
class DeactivateNotificationView;
class DataView;

enum class Technology : uint8_t {
    NFC_A = 0x0,
    NFC_B = 0x1,
    NFC_F = 0x2,
    NFC_V = 0x3,
    RAW = 0x7,
};

inline std::string TechnologyText(Technology tag) {
    switch (tag) {
        case Technology::NFC_A: return "NFC_A";
        case Technology::NFC_B: return "NFC_B";
        case Technology::NFC_F: return "NFC_F";
        case Technology::NFC_V: return "NFC_V";
        case Technology::RAW: return "RAW";
        default:
            return std::string("Unknown Technology: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

enum class BitRate : uint8_t {
    BIT_RATE_106_KBIT_S = 0x0,
    BIT_RATE_212_KBIT_S = 0x1,
    BIT_RATE_424_KBIT_S = 0x2,
    BIT_RATE_848_KBIT_S = 0x3,
    BIT_RATE_1695_KBIT_S = 0x4,
    BIT_RATE_3390_KBIT_S = 0x5,
    BIT_RATE_6780_KBIT_S = 0x6,
    BIT_RATE_26_KBIT_S = 0x20,
};

inline std::string BitRateText(BitRate tag) {
    switch (tag) {
        case BitRate::BIT_RATE_106_KBIT_S: return "BIT_RATE_106_KBIT_S";
        case BitRate::BIT_RATE_212_KBIT_S: return "BIT_RATE_212_KBIT_S";
        case BitRate::BIT_RATE_424_KBIT_S: return "BIT_RATE_424_KBIT_S";
        case BitRate::BIT_RATE_848_KBIT_S: return "BIT_RATE_848_KBIT_S";
        case BitRate::BIT_RATE_1695_KBIT_S: return "BIT_RATE_1695_KBIT_S";
        case BitRate::BIT_RATE_3390_KBIT_S: return "BIT_RATE_3390_KBIT_S";
        case BitRate::BIT_RATE_6780_KBIT_S: return "BIT_RATE_6780_KBIT_S";
        case BitRate::BIT_RATE_26_KBIT_S: return "BIT_RATE_26_KBIT_S";
        default:
            return std::string("Unknown BitRate: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

enum class Protocol : uint8_t {
    UNDETERMINED = 0x0,
    T1T = 0x1,
    T2T = 0x2,
    T3T = 0x3,
    ISO_DEP = 0x4,
    NFC_DEP = 0x5,
    T5T = 0x6,
    NDEF = 0x7,
};

inline std::string ProtocolText(Protocol tag) {
    switch (tag) {
        case Protocol::UNDETERMINED: return "UNDETERMINED";
        case Protocol::T1T: return "T1T";
        case Protocol::T2T: return "T2T";
        case Protocol::T3T: return "T3T";
        case Protocol::ISO_DEP: return "ISO_DEP";
        case Protocol::NFC_DEP: return "NFC_DEP";
        case Protocol::T5T: return "T5T";
        case Protocol::NDEF: return "NDEF";
        default:
            return std::string("Unknown Protocol: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

enum class RfPacketType : uint8_t {
    DATA = 0x0,
    POLL_COMMAND = 0x1,
    POLL_RESPONSE = 0x2,
    SELECT_COMMAND = 0x3,
    SELECT_RESPONSE = 0x4,
    DEACTIVATE_NOTIFICATION = 0x5,
    FIELD_INFO = 0x6,
};

inline std::string RfPacketTypeText(RfPacketType tag) {
    switch (tag) {
        case RfPacketType::DATA: return "DATA";
        case RfPacketType::POLL_COMMAND: return "POLL_COMMAND";
        case RfPacketType::POLL_RESPONSE: return "POLL_RESPONSE";
        case RfPacketType::SELECT_COMMAND: return "SELECT_COMMAND";
        case RfPacketType::SELECT_RESPONSE: return "SELECT_RESPONSE";
        case RfPacketType::DEACTIVATE_NOTIFICATION: return "DEACTIVATE_NOTIFICATION";
        case RfPacketType::FIELD_INFO: return "FIELD_INFO";
        default:
            return std::string("Unknown RfPacketType: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

class RfPacketView {
public:
    static RfPacketView Create(pdl::packet::slice const& parent) {
        return RfPacketView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    RfPacketType GetPacketType() const {
        _ASSERT_VALID(valid_);
        return packet_type_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    std::vector<uint8_t> GetPayload() const {
        _ASSERT_VALID(valid_);
        return payload_.bytes();
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit RfPacketView(pdl::packet::slice const& parent)
          : bytes_(parent) {
        valid_ = Parse(parent);
    }

    bool Parse(pdl::packet::slice const& parent) {
        // Parse packet field values.
        pdl::packet::slice span = parent;
        if (span.size() < 9) {
            return false;
        }
        sender_ = span.read_le<uint16_t, 2>();
        receiver_ = span.read_le<uint16_t, 2>();
        technology_ = Technology(span.read_le<uint8_t, 1>());
        protocol_ = Protocol(span.read_le<uint8_t, 1>());
        packet_type_ = RfPacketType(span.read_le<uint8_t, 1>());
        bitrate_ = BitRate(span.read_le<uint8_t, 1>());
        power_level_ = span.read_le<uint8_t, 1>();
        payload_ = span;
        span.clear();
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    RfPacketType packet_type_{RfPacketType::DATA};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    pdl::packet::slice payload_;

    friend class PollCommandView;
    friend class FieldInfoView;
    friend class NfcAPollResponseView;
    friend class T4ATSelectCommandView;
    friend class T4ATSelectResponseView;
    friend class NfcDepSelectCommandView;
    friend class NfcDepSelectResponseView;
    friend class SelectCommandView;
    friend class DeactivateNotificationView;
    friend class DataView;
};

class RfPacketBuilder : public pdl::packet::Builder {
public:
    ~RfPacketBuilder() override = default;
    RfPacketBuilder() = default;
    RfPacketBuilder(RfPacketBuilder const&) = default;
    RfPacketBuilder(RfPacketBuilder&&) = default;
    RfPacketBuilder& operator=(RfPacketBuilder const&) = default;
        RfPacketBuilder(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, RfPacketType packet_type, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> payload)
        : sender_(sender), receiver_(receiver), technology_(technology), protocol_(protocol), packet_type_(packet_type), bitrate_(bitrate), power_level_(power_level), payload_(std::move(payload)) {
    
}
    static std::unique_ptr<RfPacketBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, RfPacketType packet_type, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> payload) {
    return std::make_unique<RfPacketBuilder>(sender, receiver, technology, protocol, packet_type, bitrate, power_level, std::move(payload));
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(packet_type_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        output.insert(output.end(), payload_.begin(), payload_.end());
    }

    size_t GetSize() const override {
        return payload_.size() + 9;
    }

    
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    RfPacketType packet_type_{RfPacketType::DATA};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    std::vector<uint8_t> payload_;
};

enum class PollingFrameFormat : uint8_t {
    SHORT = 0x0,
    LONG = 0x1,
};

inline std::string PollingFrameFormatText(PollingFrameFormat tag) {
    switch (tag) {
        case PollingFrameFormat::SHORT: return "SHORT";
        case PollingFrameFormat::LONG: return "LONG";
        default:
            return std::string("Unknown PollingFrameFormat: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

class PollCommandView {
public:
    static PollCommandView Create(RfPacketView const& parent) {
        return PollCommandView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    PollingFrameFormat GetFormat() const {
        _ASSERT_VALID(valid_);
        return format_;
    }
    
    std::vector<uint8_t> GetPayload() const {
        _ASSERT_VALID(valid_);
        return payload_.bytes();
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::POLL_COMMAND;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit PollCommandView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        protocol_ = parent.protocol_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.packet_type_ != RfPacketType::POLL_COMMAND) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        if (span.size() < 1) {
            return false;
        }
        uint8_t chunk0 = span.read_le<uint8_t, 1>();
        format_ = PollingFrameFormat((chunk0 >> 0) & 0x1);
        payload_ = span;
        span.clear();
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    PollingFrameFormat format_{PollingFrameFormat::SHORT};
    pdl::packet::slice payload_;

    
};

class PollCommandBuilder : public RfPacketBuilder {
public:
    ~PollCommandBuilder() override = default;
    PollCommandBuilder() = default;
    PollCommandBuilder(PollCommandBuilder const&) = default;
    PollCommandBuilder(PollCommandBuilder&&) = default;
    PollCommandBuilder& operator=(PollCommandBuilder const&) = default;
        PollCommandBuilder(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, PollingFrameFormat format, std::vector<uint8_t> payload)
        : RfPacketBuilder(sender, receiver, technology, protocol, RfPacketType::POLL_COMMAND, bitrate, power_level, std::vector<uint8_t>{}), format_(format) {
    payload_ = std::move(payload);
}
    static std::unique_ptr<PollCommandBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, PollingFrameFormat format, std::vector<uint8_t> payload) {
    return std::make_unique<PollCommandBuilder>(sender, receiver, technology, protocol, bitrate, power_level, format, std::move(payload));
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::POLL_COMMAND) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(format_) << 0));
        output.insert(output.end(), payload_.begin(), payload_.end());
    }

    size_t GetSize() const override {
        return payload_.size() + 10;
    }

    
    PollingFrameFormat format_{PollingFrameFormat::SHORT};
};

enum class FieldStatus : uint8_t {
    FieldOff = 0x0,
    FieldOn = 0x1,
};

inline std::string FieldStatusText(FieldStatus tag) {
    switch (tag) {
        case FieldStatus::FieldOff: return "FieldOff";
        case FieldStatus::FieldOn: return "FieldOn";
        default:
            return std::string("Unknown FieldStatus: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

class FieldInfoView {
public:
    static FieldInfoView Create(RfPacketView const& parent) {
        return FieldInfoView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    FieldStatus GetFieldStatus() const {
        _ASSERT_VALID(valid_);
        return field_status_;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::FIELD_INFO;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit FieldInfoView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        protocol_ = parent.protocol_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.packet_type_ != RfPacketType::FIELD_INFO) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        if (span.size() < 1) {
            return false;
        }
        field_status_ = FieldStatus(span.read_le<uint8_t, 1>());
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    FieldStatus field_status_{FieldStatus::FieldOff};

    
};

class FieldInfoBuilder : public RfPacketBuilder {
public:
    ~FieldInfoBuilder() override = default;
    FieldInfoBuilder() = default;
    FieldInfoBuilder(FieldInfoBuilder const&) = default;
    FieldInfoBuilder(FieldInfoBuilder&&) = default;
    FieldInfoBuilder& operator=(FieldInfoBuilder const&) = default;
        FieldInfoBuilder(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, FieldStatus field_status)
        : RfPacketBuilder(sender, receiver, technology, protocol, RfPacketType::FIELD_INFO, bitrate, power_level, std::vector<uint8_t>{}), field_status_(field_status) {
    
}
    static std::unique_ptr<FieldInfoBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, FieldStatus field_status) {
    return std::make_unique<FieldInfoBuilder>(sender, receiver, technology, protocol, bitrate, power_level, field_status);
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::FIELD_INFO) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(field_status_) << 0));
    }

    size_t GetSize() const override {
        return 10;
    }

    
    FieldStatus field_status_{FieldStatus::FieldOff};
};

class NfcAPollResponseView {
public:
    static NfcAPollResponseView Create(RfPacketView const& parent) {
        return NfcAPollResponseView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    std::vector<uint8_t> GetNfcid1() const {
        _ASSERT_VALID(valid_);
        pdl::packet::slice span = nfcid1_;
        std::vector<uint8_t> elements;
        while (span.size() >= 1) {
            elements.push_back(span.read_le<uint8_t, 1>());
        }
        return elements;
    }
    
    uint8_t GetIntProtocol() const {
        _ASSERT_VALID(valid_);
        return int_protocol_;
    }
    
    uint8_t GetBitFrameSdd() const {
        _ASSERT_VALID(valid_);
        return bit_frame_sdd_;
    }
    
    Technology GetTechnology() const {
        return Technology::NFC_A;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::POLL_RESPONSE;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit NfcAPollResponseView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        protocol_ = parent.protocol_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.technology_ != Technology::NFC_A) {
            return false;
        }
        
        if (parent.packet_type_ != RfPacketType::POLL_RESPONSE) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        uint8_t nfcid1_size;
        if (span.size() < 1) {
            return false;
        }
        nfcid1_size = span.read_le<uint8_t, 1>();
        if (span.size() < nfcid1_size) {
            return false;
        }
        nfcid1_ = span.subrange(0, nfcid1_size);
        span.skip(nfcid1_size);
        if (span.size() < 2) {
            return false;
        }
        uint8_t chunk0 = span.read_le<uint8_t, 1>();
        int_protocol_ = (chunk0 >> 0) & 0x3;
        bit_frame_sdd_ = span.read_le<uint8_t, 1>();
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Protocol protocol_{Protocol::UNDETERMINED};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    pdl::packet::slice nfcid1_;
    uint8_t int_protocol_{0};
    uint8_t bit_frame_sdd_{0};

    
};

class NfcAPollResponseBuilder : public RfPacketBuilder {
public:
    ~NfcAPollResponseBuilder() override = default;
    NfcAPollResponseBuilder() = default;
    NfcAPollResponseBuilder(NfcAPollResponseBuilder const&) = default;
    NfcAPollResponseBuilder(NfcAPollResponseBuilder&&) = default;
    NfcAPollResponseBuilder& operator=(NfcAPollResponseBuilder const&) = default;
        NfcAPollResponseBuilder(uint16_t sender, uint16_t receiver, Protocol protocol, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> nfcid1, uint8_t int_protocol, uint8_t bit_frame_sdd)
        : RfPacketBuilder(sender, receiver, Technology::NFC_A, protocol, RfPacketType::POLL_RESPONSE, bitrate, power_level, std::vector<uint8_t>{}), nfcid1_(std::move(nfcid1)), int_protocol_(int_protocol), bit_frame_sdd_(bit_frame_sdd) {
    
}
    static std::unique_ptr<NfcAPollResponseBuilder> Create(uint16_t sender, uint16_t receiver, Protocol protocol, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> nfcid1, uint8_t int_protocol, uint8_t bit_frame_sdd) {
    return std::make_unique<NfcAPollResponseBuilder>(sender, receiver, protocol, bitrate, power_level, std::move(nfcid1), int_protocol, bit_frame_sdd);
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Technology::NFC_A) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::POLL_RESPONSE) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(GetNfcid1Size()) << 0));
        output.insert(output.end(), nfcid1_.begin(), nfcid1_.end());
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(int_protocol_ & 0x3) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bit_frame_sdd_ & 0xff) << 0));
    }

    size_t GetSize() const override {
        return GetNfcid1Size() + 12;
    }

    size_t GetNfcid1Size() const {
        return nfcid1_.size() * 1;
    }
    
    std::vector<uint8_t> nfcid1_;
    uint8_t int_protocol_{0};
    uint8_t bit_frame_sdd_{0};
};

class T4ATSelectCommandView {
public:
    static T4ATSelectCommandView Create(RfPacketView const& parent) {
        return T4ATSelectCommandView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    uint8_t GetParam() const {
        _ASSERT_VALID(valid_);
        return param_;
    }
    
    Technology GetTechnology() const {
        return Technology::NFC_A;
    }
    
    Protocol GetProtocol() const {
        return Protocol::ISO_DEP;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::SELECT_COMMAND;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit T4ATSelectCommandView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.technology_ != Technology::NFC_A) {
            return false;
        }
        
        if (parent.protocol_ != Protocol::ISO_DEP) {
            return false;
        }
        
        if (parent.packet_type_ != RfPacketType::SELECT_COMMAND) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        if (span.size() < 1) {
            return false;
        }
        param_ = span.read_le<uint8_t, 1>();
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    uint8_t param_{0};

    
};

class T4ATSelectCommandBuilder : public RfPacketBuilder {
public:
    ~T4ATSelectCommandBuilder() override = default;
    T4ATSelectCommandBuilder() = default;
    T4ATSelectCommandBuilder(T4ATSelectCommandBuilder const&) = default;
    T4ATSelectCommandBuilder(T4ATSelectCommandBuilder&&) = default;
    T4ATSelectCommandBuilder& operator=(T4ATSelectCommandBuilder const&) = default;
        T4ATSelectCommandBuilder(uint16_t sender, uint16_t receiver, BitRate bitrate, uint8_t power_level, uint8_t param)
        : RfPacketBuilder(sender, receiver, Technology::NFC_A, Protocol::ISO_DEP, RfPacketType::SELECT_COMMAND, bitrate, power_level, std::vector<uint8_t>{}), param_(param) {
    
}
    static std::unique_ptr<T4ATSelectCommandBuilder> Create(uint16_t sender, uint16_t receiver, BitRate bitrate, uint8_t power_level, uint8_t param) {
    return std::make_unique<T4ATSelectCommandBuilder>(sender, receiver, bitrate, power_level, param);
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Technology::NFC_A) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Protocol::ISO_DEP) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::SELECT_COMMAND) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(param_ & 0xff) << 0));
    }

    size_t GetSize() const override {
        return 10;
    }

    
    uint8_t param_{0};
};

class T4ATSelectResponseView {
public:
    static T4ATSelectResponseView Create(RfPacketView const& parent) {
        return T4ATSelectResponseView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    std::vector<uint8_t> GetRatsResponse() const {
        _ASSERT_VALID(valid_);
        pdl::packet::slice span = rats_response_;
        std::vector<uint8_t> elements;
        while (span.size() >= 1) {
            elements.push_back(span.read_le<uint8_t, 1>());
        }
        return elements;
    }
    
    Technology GetTechnology() const {
        return Technology::NFC_A;
    }
    
    Protocol GetProtocol() const {
        return Protocol::ISO_DEP;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::SELECT_RESPONSE;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit T4ATSelectResponseView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.technology_ != Technology::NFC_A) {
            return false;
        }
        
        if (parent.protocol_ != Protocol::ISO_DEP) {
            return false;
        }
        
        if (parent.packet_type_ != RfPacketType::SELECT_RESPONSE) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        uint8_t rats_response_size;
        if (span.size() < 1) {
            return false;
        }
        rats_response_size = span.read_le<uint8_t, 1>();
        if (span.size() < rats_response_size) {
            return false;
        }
        rats_response_ = span.subrange(0, rats_response_size);
        span.skip(rats_response_size);
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    pdl::packet::slice rats_response_;

    
};

class T4ATSelectResponseBuilder : public RfPacketBuilder {
public:
    ~T4ATSelectResponseBuilder() override = default;
    T4ATSelectResponseBuilder() = default;
    T4ATSelectResponseBuilder(T4ATSelectResponseBuilder const&) = default;
    T4ATSelectResponseBuilder(T4ATSelectResponseBuilder&&) = default;
    T4ATSelectResponseBuilder& operator=(T4ATSelectResponseBuilder const&) = default;
        T4ATSelectResponseBuilder(uint16_t sender, uint16_t receiver, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> rats_response)
        : RfPacketBuilder(sender, receiver, Technology::NFC_A, Protocol::ISO_DEP, RfPacketType::SELECT_RESPONSE, bitrate, power_level, std::vector<uint8_t>{}), rats_response_(std::move(rats_response)) {
    
}
    static std::unique_ptr<T4ATSelectResponseBuilder> Create(uint16_t sender, uint16_t receiver, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> rats_response) {
    return std::make_unique<T4ATSelectResponseBuilder>(sender, receiver, bitrate, power_level, std::move(rats_response));
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Technology::NFC_A) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Protocol::ISO_DEP) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::SELECT_RESPONSE) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(GetRatsResponseSize()) << 0));
        output.insert(output.end(), rats_response_.begin(), rats_response_.end());
    }

    size_t GetSize() const override {
        return GetRatsResponseSize() + 10;
    }

    size_t GetRatsResponseSize() const {
        return rats_response_.size() * 1;
    }
    
    std::vector<uint8_t> rats_response_;
};

class NfcDepSelectCommandView {
public:
    static NfcDepSelectCommandView Create(RfPacketView const& parent) {
        return NfcDepSelectCommandView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    uint8_t GetLr() const {
        _ASSERT_VALID(valid_);
        return lr_;
    }
    
    Protocol GetProtocol() const {
        return Protocol::NFC_DEP;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::SELECT_COMMAND;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit NfcDepSelectCommandView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.protocol_ != Protocol::NFC_DEP) {
            return false;
        }
        
        if (parent.packet_type_ != RfPacketType::SELECT_COMMAND) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        if (span.size() < 1) {
            return false;
        }
        uint8_t chunk0 = span.read_le<uint8_t, 1>();
        lr_ = (chunk0 >> 0) & 0x3;
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    uint8_t lr_{0};

    
};

class NfcDepSelectCommandBuilder : public RfPacketBuilder {
public:
    ~NfcDepSelectCommandBuilder() override = default;
    NfcDepSelectCommandBuilder() = default;
    NfcDepSelectCommandBuilder(NfcDepSelectCommandBuilder const&) = default;
    NfcDepSelectCommandBuilder(NfcDepSelectCommandBuilder&&) = default;
    NfcDepSelectCommandBuilder& operator=(NfcDepSelectCommandBuilder const&) = default;
        NfcDepSelectCommandBuilder(uint16_t sender, uint16_t receiver, Technology technology, BitRate bitrate, uint8_t power_level, uint8_t lr)
        : RfPacketBuilder(sender, receiver, technology, Protocol::NFC_DEP, RfPacketType::SELECT_COMMAND, bitrate, power_level, std::vector<uint8_t>{}), lr_(lr) {
    
}
    static std::unique_ptr<NfcDepSelectCommandBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, BitRate bitrate, uint8_t power_level, uint8_t lr) {
    return std::make_unique<NfcDepSelectCommandBuilder>(sender, receiver, technology, bitrate, power_level, lr);
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Protocol::NFC_DEP) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::SELECT_COMMAND) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(lr_ & 0x3) << 0));
    }

    size_t GetSize() const override {
        return 10;
    }

    
    uint8_t lr_{0};
};

class NfcDepSelectResponseView {
public:
    static NfcDepSelectResponseView Create(RfPacketView const& parent) {
        return NfcDepSelectResponseView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    std::vector<uint8_t> GetAtrResponse() const {
        _ASSERT_VALID(valid_);
        pdl::packet::slice span = atr_response_;
        std::vector<uint8_t> elements;
        while (span.size() >= 1) {
            elements.push_back(span.read_le<uint8_t, 1>());
        }
        return elements;
    }
    
    Protocol GetProtocol() const {
        return Protocol::NFC_DEP;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::SELECT_RESPONSE;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit NfcDepSelectResponseView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.protocol_ != Protocol::NFC_DEP) {
            return false;
        }
        
        if (parent.packet_type_ != RfPacketType::SELECT_RESPONSE) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        uint8_t atr_response_size;
        if (span.size() < 1) {
            return false;
        }
        atr_response_size = span.read_le<uint8_t, 1>();
        if (span.size() < atr_response_size) {
            return false;
        }
        atr_response_ = span.subrange(0, atr_response_size);
        span.skip(atr_response_size);
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    pdl::packet::slice atr_response_;

    
};

class NfcDepSelectResponseBuilder : public RfPacketBuilder {
public:
    ~NfcDepSelectResponseBuilder() override = default;
    NfcDepSelectResponseBuilder() = default;
    NfcDepSelectResponseBuilder(NfcDepSelectResponseBuilder const&) = default;
    NfcDepSelectResponseBuilder(NfcDepSelectResponseBuilder&&) = default;
    NfcDepSelectResponseBuilder& operator=(NfcDepSelectResponseBuilder const&) = default;
        NfcDepSelectResponseBuilder(uint16_t sender, uint16_t receiver, Technology technology, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> atr_response)
        : RfPacketBuilder(sender, receiver, technology, Protocol::NFC_DEP, RfPacketType::SELECT_RESPONSE, bitrate, power_level, std::vector<uint8_t>{}), atr_response_(std::move(atr_response)) {
    
}
    static std::unique_ptr<NfcDepSelectResponseBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> atr_response) {
    return std::make_unique<NfcDepSelectResponseBuilder>(sender, receiver, technology, bitrate, power_level, std::move(atr_response));
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(Protocol::NFC_DEP) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::SELECT_RESPONSE) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(GetAtrResponseSize()) << 0));
        output.insert(output.end(), atr_response_.begin(), atr_response_.end());
    }

    size_t GetSize() const override {
        return GetAtrResponseSize() + 10;
    }

    size_t GetAtrResponseSize() const {
        return atr_response_.size() * 1;
    }
    
    std::vector<uint8_t> atr_response_;
};

class SelectCommandView {
public:
    static SelectCommandView Create(RfPacketView const& parent) {
        return SelectCommandView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::SELECT_COMMAND;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit SelectCommandView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        protocol_ = parent.protocol_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.packet_type_ != RfPacketType::SELECT_COMMAND) {
            return false;
        }
        
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};

    
};

class SelectCommandBuilder : public RfPacketBuilder {
public:
    ~SelectCommandBuilder() override = default;
    SelectCommandBuilder() = default;
    SelectCommandBuilder(SelectCommandBuilder const&) = default;
    SelectCommandBuilder(SelectCommandBuilder&&) = default;
    SelectCommandBuilder& operator=(SelectCommandBuilder const&) = default;
        SelectCommandBuilder(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level)
        : RfPacketBuilder(sender, receiver, technology, protocol, RfPacketType::SELECT_COMMAND, bitrate, power_level, std::vector<uint8_t>{}) {
    
}
    static std::unique_ptr<SelectCommandBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level) {
    return std::make_unique<SelectCommandBuilder>(sender, receiver, technology, protocol, bitrate, power_level);
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::SELECT_COMMAND) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
    }

    size_t GetSize() const override {
        return 9;
    }

    
    
};

enum class DeactivateType : uint8_t {
    IDLE_MODE = 0x0,
    SLEEP_MODE = 0x1,
    SLEEP_AF_MODE = 0x2,
    DISCOVERY = 0x3,
};

inline std::string DeactivateTypeText(DeactivateType tag) {
    switch (tag) {
        case DeactivateType::IDLE_MODE: return "IDLE_MODE";
        case DeactivateType::SLEEP_MODE: return "SLEEP_MODE";
        case DeactivateType::SLEEP_AF_MODE: return "SLEEP_AF_MODE";
        case DeactivateType::DISCOVERY: return "DISCOVERY";
        default:
            return std::string("Unknown DeactivateType: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

enum class DeactivateReason : uint8_t {
    DH_REQUEST = 0x0,
    ENDPOINT_REQUEST = 0x1,
    RF_LINK_LOSS = 0x2,
    NFC_B_BAD_AFI = 0x3,
    DH_REQUEST_FAILED = 0x4,
};

inline std::string DeactivateReasonText(DeactivateReason tag) {
    switch (tag) {
        case DeactivateReason::DH_REQUEST: return "DH_REQUEST";
        case DeactivateReason::ENDPOINT_REQUEST: return "ENDPOINT_REQUEST";
        case DeactivateReason::RF_LINK_LOSS: return "RF_LINK_LOSS";
        case DeactivateReason::NFC_B_BAD_AFI: return "NFC_B_BAD_AFI";
        case DeactivateReason::DH_REQUEST_FAILED: return "DH_REQUEST_FAILED";
        default:
            return std::string("Unknown DeactivateReason: " +
                   std::to_string(static_cast<uint64_t>(tag)));
    }
}

class DeactivateNotificationView {
public:
    static DeactivateNotificationView Create(RfPacketView const& parent) {
        return DeactivateNotificationView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    DeactivateType GetType() const {
        _ASSERT_VALID(valid_);
        return type__;
    }
    
    DeactivateReason GetReason() const {
        _ASSERT_VALID(valid_);
        return reason_;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::DEACTIVATE_NOTIFICATION;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit DeactivateNotificationView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        protocol_ = parent.protocol_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.packet_type_ != RfPacketType::DEACTIVATE_NOTIFICATION) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        if (span.size() < 2) {
            return false;
        }
        type__ = DeactivateType(span.read_le<uint8_t, 1>());
        reason_ = DeactivateReason(span.read_le<uint8_t, 1>());
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    DeactivateType type__{DeactivateType::IDLE_MODE};
    DeactivateReason reason_{DeactivateReason::DH_REQUEST};

    
};

class DeactivateNotificationBuilder : public RfPacketBuilder {
public:
    ~DeactivateNotificationBuilder() override = default;
    DeactivateNotificationBuilder() = default;
    DeactivateNotificationBuilder(DeactivateNotificationBuilder const&) = default;
    DeactivateNotificationBuilder(DeactivateNotificationBuilder&&) = default;
    DeactivateNotificationBuilder& operator=(DeactivateNotificationBuilder const&) = default;
        DeactivateNotificationBuilder(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, DeactivateType type_, DeactivateReason reason)
        : RfPacketBuilder(sender, receiver, technology, protocol, RfPacketType::DEACTIVATE_NOTIFICATION, bitrate, power_level, std::vector<uint8_t>{}), type__(type_), reason_(reason) {
    
}
    static std::unique_ptr<DeactivateNotificationBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, DeactivateType type_, DeactivateReason reason) {
    return std::make_unique<DeactivateNotificationBuilder>(sender, receiver, technology, protocol, bitrate, power_level, type_, reason);
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::DEACTIVATE_NOTIFICATION) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(type__) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(reason_) << 0));
    }

    size_t GetSize() const override {
        return 11;
    }

    
    DeactivateType type__{DeactivateType::IDLE_MODE};
    DeactivateReason reason_{DeactivateReason::DH_REQUEST};
};

class DataView {
public:
    static DataView Create(RfPacketView const& parent) {
        return DataView(parent);
    }

    uint16_t GetSender() const {
        _ASSERT_VALID(valid_);
        return sender_;
    }
    
    uint16_t GetReceiver() const {
        _ASSERT_VALID(valid_);
        return receiver_;
    }
    
    Technology GetTechnology() const {
        _ASSERT_VALID(valid_);
        return technology_;
    }
    
    Protocol GetProtocol() const {
        _ASSERT_VALID(valid_);
        return protocol_;
    }
    
    BitRate GetBitrate() const {
        _ASSERT_VALID(valid_);
        return bitrate_;
    }
    
    uint8_t GetPowerLevel() const {
        _ASSERT_VALID(valid_);
        return power_level_;
    }
    
    std::vector<uint8_t> GetData() const {
        _ASSERT_VALID(valid_);
        pdl::packet::slice span = data_;
        std::vector<uint8_t> elements;
        while (span.size() >= 1) {
            elements.push_back(span.read_le<uint8_t, 1>());
        }
        return elements;
    }
    
    RfPacketType GetPacketType() const {
        return RfPacketType::DATA;
    }
    
    
    std::string ToString() const {
        return "";
    }
    

    bool IsValid() const {
        return valid_;
    }

    pdl::packet::slice bytes() const {
        return bytes_;
    }

protected:
    explicit DataView(RfPacketView const& parent)
          : bytes_(parent.bytes_) {
        valid_ = Parse(parent);
    }

    bool Parse(RfPacketView const& parent) {
        // Check validity of parent packet.
        if (!parent.IsValid()) {
            return false;
        }
        
        // Copy parent field values.
        sender_ = parent.sender_;
        receiver_ = parent.receiver_;
        technology_ = parent.technology_;
        protocol_ = parent.protocol_;
        bitrate_ = parent.bitrate_;
        power_level_ = parent.power_level_;
        
        if (parent.packet_type_ != RfPacketType::DATA) {
            return false;
        }
        
        // Parse packet field values.
        pdl::packet::slice span = parent.payload_;
        data_ = span;
        span.clear();
        return true;
    }

    bool valid_{false};
    pdl::packet::slice bytes_;
    uint16_t sender_{0};
    uint16_t receiver_{0};
    Technology technology_{Technology::NFC_A};
    Protocol protocol_{Protocol::UNDETERMINED};
    BitRate bitrate_{BitRate::BIT_RATE_106_KBIT_S};
    uint8_t power_level_{0};
    pdl::packet::slice data_;

    
};

class DataBuilder : public RfPacketBuilder {
public:
    ~DataBuilder() override = default;
    DataBuilder() = default;
    DataBuilder(DataBuilder const&) = default;
    DataBuilder(DataBuilder&&) = default;
    DataBuilder& operator=(DataBuilder const&) = default;
        DataBuilder(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> data)
        : RfPacketBuilder(sender, receiver, technology, protocol, RfPacketType::DATA, bitrate, power_level, std::vector<uint8_t>{}), data_(std::move(data)) {
    
}
    static std::unique_ptr<DataBuilder> Create(uint16_t sender, uint16_t receiver, Technology technology, Protocol protocol, BitRate bitrate, uint8_t power_level, std::vector<uint8_t> data) {
    return std::make_unique<DataBuilder>(sender, receiver, technology, protocol, bitrate, power_level, std::move(data));
}

    void Serialize(std::vector<uint8_t>& output) const override {
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(sender_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint16_t, 2>(output, (static_cast<uint16_t>(receiver_ & 0xffff) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(technology_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(protocol_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(RfPacketType::DATA) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(bitrate_) << 0));
        pdl::packet::Builder::write_le<uint8_t, 1>(output, (static_cast<uint8_t>(power_level_ & 0xff) << 0));
        output.insert(output.end(), data_.begin(), data_.end());
    }

    size_t GetSize() const override {
        return GetDataSize() + 9;
    }

    size_t GetDataSize() const {
        return data_.size() * 1;
    }
    
    std::vector<uint8_t> data_;
};
}  // casimir::rf
