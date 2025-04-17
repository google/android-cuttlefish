/*
 * Copyright 2020, The Android Open Source Project
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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace teeui {

/**
 * Important notice. The UTF8Range only works on verified UTF8 encoded strings.
 * E.g. if the string successfully passed through our CBOR formatting (see cbor.h) it is safe to
 * use with UTF8Range. Alternatively, you can call verify() on a new range.
 */
template <typename CharIterator> class UTF8Range {
  public:
    UTF8Range(CharIterator begin, CharIterator end) : begin_(begin), end_(end) {}
    UTF8Range() : begin_{}, end_{begin_} {};
    UTF8Range(const UTF8Range&) = default;
    UTF8Range(UTF8Range&&) = default;
    UTF8Range& operator=(UTF8Range&&) = default;
    UTF8Range& operator=(const UTF8Range&) = default;

    /**
     * Decodes a header byte of a UTF8 sequence. In UTF8 encoding the number of leading ones
     * indicate the length of the UTF8 sequence. Following bytes start with b10 followed by six
     * payload bits. Sequences of length one start with a 0 followed by 7 payload bits.
     */
    static size_t byteCount(char c) {
        if (0x80 & c) {
            /*
             * CLZ - count leading zeroes.
             * __builtin_clz promotes the argument to unsigned int.
             * We invert c to turn leading ones into leading zeroes.
             * We subtract additional leading zeroes due to the type promotion from the result.
             */
            return __builtin_clz((unsigned char)(~c)) - (sizeof(unsigned int) * 8 - 8);
        } else {
            return 1;
        }
    }
    static unsigned long codePoint(CharIterator begin) {
        unsigned long c = (uint8_t)*begin;
        size_t byte_count = byteCount(c);
        if (byte_count == 1) {
            return c;
        } else {
            // multi byte
            unsigned long result = c & ~(0xffu << (8 - byte_count));
            ++begin;
            for (size_t i = 1; i < byte_count; ++i) {
                result <<= 6;
                result |= *begin & 0x3f;
                ++begin;
            }
            return result;
        }
    }

    class Iter {
        CharIterator begin_;

      public:
        Iter() : begin_{} {}
        Iter(CharIterator begin) : begin_(begin) {}
        Iter(const Iter& rhs) : begin_(rhs.begin_) {}
        Iter& operator=(const Iter& rhs) {
            begin_ = rhs.begin_;
            return *this;
        }
        CharIterator operator*() const { return begin_; }
        Iter& operator++() {
            begin_ += byteCount(*begin_);
            return *this;
        }
        Iter operator++(int) {
            Iter dummy = *this;
            ++(*this);
            return dummy;
        }
        bool operator==(const Iter& rhs) const { return begin_ == rhs.begin_; }
        bool operator!=(const Iter& rhs) const { return !(*this == rhs); }
        unsigned long codePoint() const { return UTF8Range::codePoint(begin_); }
    };
    Iter begin() const { return Iter(begin_); }
    Iter end() const { return Iter(end_); }
    /*
     * Checks if the range is safe to use. If this returns false, iteration over this range is
     * undefined. It may infinite loop and read out of bounds.
     */
    bool verify() {
        for (auto pos = begin_; pos != end_;) {
            // are we out of sync?
            if ((*pos & 0xc0) == 0x80) return false;
            auto byte_count = byteCount(*pos);
            // did we run out of buffer;
            if (end_ - pos < byte_count) return false;
            // we could check if the non header bytes have the wrong header. While this would
            // be malformed UTF8, it does not impact control flow and is thus not security
            // critical.
            pos += byte_count;
        }
        return true;
    }

  private:
    CharIterator begin_;
    CharIterator end_;
    static_assert(std::is_same<std::remove_reference_t<decltype(*begin_)>, const char>::value,
                  "Iterator must dereference to const char");
    static_assert(
        std::is_convertible<std::remove_reference_t<decltype(end_ - begin_)>, size_t>::value,
        "Iterator arithmetic must evaluate to something that is convertible to size_t");
};

}  // namespace teeui
