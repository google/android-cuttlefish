/*
 *
 * Copyright 2017, The Android Open Source Project
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

#ifndef TEEUI_MSG_FORMATTING_H_
#define TEEUI_MSG_FORMATTING_H_

#include <algorithm>
#include <stddef.h>
#include <stdint.h>
#include <tuple>
#include <type_traits>
#include <utility>

#include <teeui/utils.h>

namespace teeui {
namespace msg {

template <typename... fields> class Message {};

template <size_t... idx, typename... T>
std::tuple<std::remove_reference_t<T>&&...> tuple_move_helper(std::index_sequence<idx...>,
                                                              std::tuple<T...>&& t) {
    return {std::move(std::get<idx>(t))...};
}

template <typename... T>
std::tuple<std::remove_reference_t<T>&&...> tuple_move(std::tuple<T...>&& t) {
    return tuple_move_helper(std::make_index_sequence<sizeof...(T)>(), std::move(t));
}

template <typename... T>
std::tuple<std::remove_reference_t<T>&&...> tuple_move(std::tuple<T...>& t) {
    return tuple_move_helper(std::make_index_sequence<sizeof...(T)>(), std::move(t));
}

void zero(volatile uint8_t* begin, const volatile uint8_t* end);

template <typename T> class StreamState {
  public:
    static_assert(
        sizeof(T) == 1,
        "StreamState must be instantiated with 1 byte sized type, e.g., uint8_t or const uint8_t.");
    using ptr_t = T*;
    template <size_t size>
    StreamState(T (&buffer)[size]) : begin_(&buffer[0]), end_(&buffer[size]), pos_(begin_) {}
    StreamState(T* buffer, size_t size) : begin_(buffer), end_(buffer + size), pos_(begin_) {}
    StreamState() : begin_(nullptr), end_(nullptr), pos_(nullptr) {}
    StreamState& operator+=(size_t offset) {
        auto good_ = pos_ != nullptr && pos_ + offset <= end_;
        if (good_) {
            pos_ += offset;
        } else {
            pos_ = nullptr;
        }
        return *this;
    }

    operator bool() const { return pos_ != nullptr; }
    ptr_t pos() const { return pos_; };

    template <typename U = T>
    bool insertFieldSize(typename std::enable_if<!std::is_const<U>::value, uint32_t>::type size) {
        // offset to the nearest n * 8 + 4 boundary from beginning of the buffer! (not memory).
        uintptr_t pos = pos_ - begin_;
        auto offset = (((pos + 11UL) & ~7UL) - 4UL) - pos;
        if (*this += offset + sizeof(size)) {
            // zero out the gaps
            zero(pos_ - offset - sizeof(size), pos_ - sizeof(size));
            *reinterpret_cast<uint32_t*>(pos_ - sizeof(size)) = size;
            return true;
        }
        return false;
    }

    template <typename U = T>
    typename std::enable_if<std::is_const<U>::value, uint32_t>::type extractFieldSize() {
        // offset to the nearest n * 8 + 4 boundary from beginning of the buffer! (not memory).
        uintptr_t pos = pos_ - begin_;
        auto offset = (((pos + 11UL) & ~7UL) - 4UL) - pos;
        if (*this += offset + sizeof(uint32_t)) {
            return *reinterpret_cast<const uint32_t*>(pos_ - sizeof(uint32_t));
        }
        return 0;
    }

    void bad() { pos_ = nullptr; };

  private:
    ptr_t begin_;
    ptr_t end_;
    ptr_t pos_;
};

using WriteStream = StreamState<uint8_t>;
using ReadStream = StreamState<const uint8_t>;

inline void zero(const volatile uint8_t*, const volatile uint8_t*) {}
//// This odd alignment function aligns the stream position to a 4byte and never 8byte boundary
//// It is to accommodate the 4 byte size field which is then followed by 8byte aligned data.
// template <typename T>
// StreamState<T> unalign(StreamState<T> s) {
//    auto result = s;
//    auto offset = (((uintptr_t(s.pos_) + 11UL) & ~7UL) - 4UL) - uintptr_t(s.pos_);
//    result += offset;
//    // zero out the gaps when writing
//    if (result) zero(s.pos_, result.pos_);
//    return result;
//}

WriteStream write(WriteStream out, const uint8_t* buffer, uint32_t size);

template <uint32_t size> WriteStream write(WriteStream out, const uint8_t (&v)[size]) {
    return write(out, v, size);
}

std::tuple<ReadStream, ReadStream::ptr_t, uint32_t> read(ReadStream in);

template <typename T> std::tuple<ReadStream, T> readSimpleType(ReadStream in) {
    auto [in_, pos, size] = read(in);
    T result = {};
    if (in_ && size == sizeof(T))
        result = *reinterpret_cast<const T*>(pos);
    else
        in_.bad();
    return {in_, result};
}

inline WriteStream write(Message<>, WriteStream out) {
    return out;
}

template <typename Head, typename... Tail>
WriteStream write(Message<Head, Tail...>, WriteStream out, const Head& head, const Tail&... tail) {
    out = write(out, head);
    return write(Message<Tail...>(), out, tail...);
}

template <typename... Msg> std::tuple<ReadStream, Msg...> read(Message<Msg...>, ReadStream in) {
    return {in, [&in]() -> Msg {
                Msg result;
                std::tie(in, result) = read(Message<Msg>(), in);
                return result;
            }()...};
}

template <typename T> struct msg2tuple {};

template <typename... T> struct msg2tuple<Message<T...>> { using type = std::tuple<T...>; };

template <typename T> using msg2tuple_t = typename msg2tuple<T>::type;

template <size_t first_idx, size_t... idx, typename HEAD, typename... T>
std::tuple<T&&...> tuple_tail(std::index_sequence<first_idx, idx...>, std::tuple<HEAD, T...>&& t) {
    return {std::move(std::get<idx>(t))...};
}

template <size_t first_idx, size_t... idx, typename HEAD, typename... T>
std::tuple<const T&...> tuple_tail(std::index_sequence<first_idx, idx...>,
                                   const std::tuple<HEAD, T...>& t) {
    return {std::get<idx>(t)...};
}

template <typename HEAD, typename... Tail>
std::tuple<Tail&&...> tuple_tail(std::tuple<HEAD, Tail...>&& t) {
    return tuple_tail(std::make_index_sequence<sizeof...(Tail) + 1>(), std::move(t));
}

template <typename HEAD, typename... Tail>
std::tuple<const Tail&...> tuple_tail(const std::tuple<HEAD, Tail...>& t) {
    return tuple_tail(std::make_index_sequence<sizeof...(Tail) + 1>(), t);
}

}  // namespace msg

using msg::Message;
using msg::msg2tuple;
using msg::msg2tuple_t;
using msg::read;
using msg::readSimpleType;
using msg::ReadStream;
using msg::tuple_tail;
using msg::write;
using msg::WriteStream;

}  // namespace teeui

#endif  // TEEUI_MSG_FORMATTING_H_
