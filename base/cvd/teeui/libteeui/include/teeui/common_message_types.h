/*
 *
 * Copyright 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TEEUI_COMMONMESSAGETYPES_H_
#define TEEUI_COMMONMESSAGETYPES_H_

#include <teeui/msg_formatting.h>

#include <tuple>

#include <stdint.h>

#include <teeui/static_vec.h>

namespace teeui {

enum class UIOption : uint32_t;
enum class ResponseCode : uint32_t;
enum class MessageSize : uint32_t;
enum class TestKeyBits : uint8_t;
enum class TestModeCommands : uint64_t;

enum class UIOption : uint32_t {
    AccessibilityInverted = 0u,
    AccessibilityMagnified = 1u,
};

enum class ResponseCode : uint32_t {
    OK = 0u,
    Canceled = 1u,
    Aborted = 2u,
    OperationPending = 3u,
    Ignored = 4u,
    SystemError = 5u,
    Unimplemented = 6u,
    Unexpected = 7u,
    UIError = 0x10000,
    UIErrorMissingGlyph,
    UIErrorMessageTooLong,
    UIErrorMalformedUTF8Encoding,
};

enum class MessageSize : uint32_t {
    MAX = 6144u,
};

enum class TestKeyBits : uint8_t {
    BYTE = 165,
};

enum class TestModeCommands : uint64_t {
    OK_EVENT = 0ull,
    CANCEL_EVENT = 1ull,
};

using MsgString = static_vec<char>;
template <typename T> using MsgVector = static_vec<T>;

template <typename T> inline const uint8_t* copyField(T& field, const uint8_t*(&pos)) {
    auto& s = bytesCast(field);
    std::copy(pos, pos + sizeof(T), s);
    return pos + sizeof(T);
}

template <typename T> inline uint8_t* copyField(const T& field, uint8_t*(&pos)) {
    auto& s = bytesCast(field);
    return std::copy(s, &s[sizeof(T)], pos);
}

/**
 * This actually only reads in place if compiled without TEEUI_USE_STD_VECTOR. See static_vec.h
 * If compiled with TEEUI_USE_STD_VECTOR MsgVector becomes std::vector and the data is actually
 * copied.
 */
template <typename T> std::tuple<ReadStream, MsgVector<T>> readSimpleVecInPlace(ReadStream in) {
    std::tuple<ReadStream, MsgVector<T>> result;
    ReadStream::ptr_t pos = nullptr;
    size_t read_size = 0;
    std::tie(std::get<0>(result), pos, read_size) = read(in);
    if (!std::get<0>(result) || read_size % sizeof(T)) {
        std::get<0>(result).bad();
        return result;
    }
    std::get<1>(result) =
        MsgVector<T>(reinterpret_cast<T*>(const_cast<uint8_t*>(pos)),
                     reinterpret_cast<T*>(const_cast<uint8_t*>(pos)) + (read_size / sizeof(T)));
    return result;
}

template <typename T> WriteStream writeSimpleVec(WriteStream out, const MsgVector<T>& vec) {
    return write(out, reinterpret_cast<const uint8_t*>(vec.data()), vec.size() * sizeof(T));
}

// ResponseCode
inline std::tuple<ReadStream, ResponseCode> read(Message<ResponseCode>, ReadStream in) {
    return readSimpleType<ResponseCode>(in);
}
inline WriteStream write(WriteStream out, const ResponseCode& v) {
    return write(out, bytesCast(v));
}

// TestModeCommands
inline std::tuple<ReadStream, TestModeCommands> read(Message<TestModeCommands>, ReadStream in) {
    return readSimpleType<TestModeCommands>(in);
}
inline WriteStream write(WriteStream out, const TestModeCommands& v) {
    return write(out, bytesCast(v));
}

namespace msg {

// MsgVector<uint8_t>
inline std::tuple<ReadStream, MsgVector<uint8_t>> read(Message<MsgVector<uint8_t>>, ReadStream in) {
    return readSimpleVecInPlace<uint8_t>(in);
}
inline WriteStream write(WriteStream out, const MsgVector<uint8_t>& v) {
    return writeSimpleVec(out, v);
}

// MsgString
inline std::tuple<ReadStream, MsgString> read(Message<MsgString>, ReadStream in) {
    return readSimpleVecInPlace<char>(in);
}
inline WriteStream write(WriteStream out, const MsgString& v) {
    return writeSimpleVec(out, v);
}

// MsgVector<UIOption>
inline std::tuple<ReadStream, MsgVector<UIOption>> read(Message<MsgVector<UIOption>>,
                                                        ReadStream in) {
    return readSimpleVecInPlace<UIOption>(in);
}
inline WriteStream write(WriteStream out, const MsgVector<UIOption>& v) {
    return writeSimpleVec(out, v);
}

}  //  namespace msg

// teeui::Array<uint8_t, size>
template <size_t size>
inline std::tuple<teeui::ReadStream, teeui::Array<uint8_t, size>>
read(teeui::Message<teeui::Array<uint8_t, size>>, teeui::ReadStream in) {
    std::tuple<teeui::ReadStream, teeui::Array<uint8_t, size>> result;
    teeui::ReadStream& in_ = std::get<0>(result);
    auto& result_ = std::get<1>(result);
    teeui::ReadStream::ptr_t pos = nullptr;
    size_t read_size = 0;
    std::tie(in_, pos, read_size) = read(in);
    if (!in_) return result;
    if (read_size != size) {
        in_.bad();
        return result;
    }
    std::copy(pos, pos + size, result_.data());
    return result;
}
template <size_t size>
inline teeui::WriteStream write(teeui::WriteStream out, const teeui::Array<uint8_t, size>& v) {
    return write(out, v.data(), v.size());
}

}  // namespace teeui

#endif  // TEEUI_COMMONMESSAGETYPES_H_
