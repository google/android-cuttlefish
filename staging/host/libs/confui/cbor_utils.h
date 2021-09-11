/*
 * Copyright 2021, The Android Open Source Project
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

#include <array>
#include <cstdint>

namespace cuttlefish {
namespace confui {
namespace support {
using auth_token_key_t = std::array<std::uint8_t, 32>;
using hmac_t = auth_token_key_t;

template <typename T>
auto bytes_cast(const T& v) -> const uint8_t (&)[sizeof(T)] {
  return *reinterpret_cast<const uint8_t(*)[sizeof(T)]>(&v);
}
template <typename T>
auto bytes_cast(T& v) -> uint8_t (&)[sizeof(T)] {
  return *reinterpret_cast<uint8_t(*)[sizeof(T)]>(&v);
}

template <typename IntType, uint32_t byteOrder>
struct choose_hton;

template <typename IntType>
struct choose_hton<IntType, __ORDER_LITTLE_ENDIAN__> {
  inline static IntType hton(const IntType& value) {
    IntType result = {};
    const unsigned char* inbytes =
        reinterpret_cast<const unsigned char*>(&value);
    unsigned char* outbytes = reinterpret_cast<unsigned char*>(&result);
    for (int i = sizeof(IntType) - 1; i >= 0; --i) {
      *(outbytes++) = inbytes[i];
    }
    return result;
  }
};

template <typename IntType>
struct choose_hton<IntType, __ORDER_BIG_ENDIAN__> {
  inline static IntType hton(const IntType& value) { return value; }
};

template <typename IntType>
inline IntType hton(const IntType& value) {
  return choose_hton<IntType, __BYTE_ORDER__>::hton(value);
}

class ByteBufferProxy {
  template <typename T>
  struct has_data {
    template <typename U>
    static int f(const U*, const void*) {
      return 0;
    }
    template <typename U>
    static int* f(const U* u, decltype(u->data())) {
      return nullptr;
    }
    static constexpr bool value =
        std::is_pointer<decltype(f((T*)nullptr, ""))>::value;
  };

 public:
  template <typename T>
  ByteBufferProxy(const T& buffer, decltype(buffer.data()) = nullptr)
      : data_(reinterpret_cast<const uint8_t*>(buffer.data())),
        size_(buffer.size()) {
    static_assert(sizeof(decltype(*buffer.data())) == 1, "elements to large");
  }

  // this overload kicks in for types that have .c_str() but not .data(), such
  // as hidl_string. std::string has both so we need to explicitly disable this
  // overload if .data() is present.
  template <typename T>
  ByteBufferProxy(
      const T& buffer,
      std::enable_if_t<!has_data<T>::value, decltype(buffer.c_str())> = nullptr)
      : data_(reinterpret_cast<const uint8_t*>(buffer.c_str())),
        size_(buffer.size()) {
    static_assert(sizeof(decltype(*buffer.c_str())) == 1, "elements to large");
  }

  template <size_t size>
  ByteBufferProxy(const char (&buffer)[size])
      : data_(reinterpret_cast<const uint8_t*>(buffer)), size_(size - 1) {
    static_assert(size > 0, "even an empty string must be 0-terminated");
  }

  template <size_t size>
  ByteBufferProxy(const uint8_t (&buffer)[size]) : data_(buffer), size_(size) {}

  ByteBufferProxy() : data_(nullptr), size_(0) {}

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }

  const uint8_t* begin() const { return data_; }
  const uint8_t* end() const { return data_ + size_; }

 private:
  const uint8_t* data_;
  size_t size_;
};

// copied from:
// hardware/interface/confirmationui/support/include/android/hardware/confirmationui/support/confirmationui_utils.h

}  // end of namespace support
}  // end of namespace confui
}  // end of namespace cuttlefish
