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

#include "host/libs/confui/cbor.h"

#include <algorithm>

#include "host/libs/confui/cbor_utils.h"

namespace cuttlefish {
namespace confui {

class HMacImplementation {
 public:
  static std::optional<support::hmac_t> hmac256(
      const support::auth_token_key_t& key,
      std::initializer_list<support::ByteBufferProxy> buffers);
};

std::optional<support::hmac_t> HMacImplementation::hmac256(
    const support::auth_token_key_t& key,
    std::initializer_list<support::ByteBufferProxy> buffers) {
  HMAC_CTX hmacCtx;
  HMAC_CTX_init(&hmacCtx);
  if (!HMAC_Init_ex(&hmacCtx, key.data(), key.size(), EVP_sha256(), nullptr)) {
    return {};
  }
  for (auto& buffer : buffers) {
    if (!HMAC_Update(&hmacCtx, buffer.data(), buffer.size())) {
      return {};
    }
  }
  support::hmac_t result;
  if (!HMAC_Final(&hmacCtx, result.data(), nullptr)) {
    return {};
  }
  return result;
}

bool Cbor::WriteBytes(uint64_t value, uint8_t size) {
  if (!IsOk()) {
    return false;
  }

  if (formatted_message_buffer_.size() + size >= kMax) {
    buffer_status_ = Cbor::Error::OUT_OF_DATA;
    return false;
  }

  switch (size) {
    case 8:
      formatted_message_buffer_.emplace_back(getByte(value, 7));
      formatted_message_buffer_.emplace_back(getByte(value, 6));
      formatted_message_buffer_.emplace_back(getByte(value, 5));
      formatted_message_buffer_.emplace_back(getByte(value, 4));
      [[fallthrough]];
    case 4:
      formatted_message_buffer_.emplace_back(getByte(value, 3));
      formatted_message_buffer_.emplace_back(getByte(value, 2));
      [[fallthrough]];
    case 2:
      formatted_message_buffer_.emplace_back(getByte(value, 1));
      [[fallthrough]];
    case 1:
      formatted_message_buffer_.emplace_back(value);
      break;
    case 0:
      break;
    default:
      buffer_status_ = Cbor::Error::MALFORMED;
      return false;
  }
  return true;
}

// TODO(kwstephenkim): replace these using cbor libraries under external
bool Cbor::WriteHeader(const Cbor::Type type, const std::uint64_t value) {
  /**
   * reference implementation is, as of 08/30/2021, here:
   *
   * https://cs.android.com/android/platform/superproject/+/master:hardware/interfaces/confirmationui/support/src/cbor.cpp;drc=master;bpv=0;bpt=1;l=58
   *
   */
  if (!IsOk()) {
    return false;
  }

  enum class Condition { k24, k0x100, k0x10000, k0x100000000, kBeyond };
  std::map<Condition, const std::uint8_t> bits_to_set = {
      {Condition::k24, static_cast<uint8_t>(value)},
      {Condition::k0x100, 24},
      {Condition::k0x10000, 25},
      {Condition::k0x100000000, 26},
      {Condition::kBeyond, 27}};
  std::map<Condition, int> optional_byte_size = {{Condition::k24, 0},
                                                 {Condition::k0x100, 1},
                                                 {Condition::k0x10000, 2},
                                                 {Condition::k0x100000000, 4},
                                                 {Condition::kBeyond, 8}};
  Condition cond = Condition::k24;

  if (value < 24) {
    cond = Condition::k24;
  } else if (value < 0x100) {
    cond = Condition::k0x100;
  } else if (value < 0x10000) {
    cond = Condition::k0x10000;
  } else if (value < 0x100000000) {
    cond = Condition::k0x100000000;
  } else {
    cond = Condition::kBeyond;
  }

  // see if we have rooms. 1 byte for the mandatory byte in the header
  // n bytes for optional bytes in the header
  if (formatted_message_buffer_.size() + optional_byte_size[cond] + 1 > kMax) {
    buffer_status_ = Cbor::Error::OUT_OF_DATA;
    return false;
  }

  std::uint8_t header = (static_cast<uint8_t>(type) << 5) | bits_to_set[cond];
  formatted_message_buffer_.emplace_back(header);
  return WriteBytes(value, optional_byte_size[cond]);
}

void Cbor::Init() {
  // reference implementation was available in:
  //  1.0/generic/GenericOperation.h
  // See class template Operation, and its init()

  /*
   * formatted_message_buffer_ will be:
   *   header followed by std::string("prompt") followed by prompt_text_ (no
   * null char) followed by std::string("extra") followed by extra_data_ (no
   * ending null, surely)
   *
   */
  if (!WriteHeader(Type::MAP, 2)) {
    return;
  }
  const std::string prompt("prompt");
  const std::string extra("extra");

  (WriteTextToBuffer(prompt) && WriteTextToBuffer(prompt_text_) &&
   WriteTextToBuffer(extra) && WriteBytesToBuffer(extra_data_));
}

bool Cbor::WriteTextToBuffer(const std::string& text) {
  // write(WriteState, const StringBuffer<T, TextStr>() is ported here
  if (!IsOk() || !WriteHeader(Type::TEXT_STRING, text.size())) {
    return false;
  }
  buffer_status_ = CheckUTF8Copy(text);
  return IsOk();
}

bool Cbor::WriteBytesToBuffer(const std::vector<std::uint8_t>& bytes) {
  if (!IsOk() || !WriteHeader(Type::BYTE_STRING, bytes.size())) {
    return false;
  }
  if (formatted_message_buffer_.size() + bytes.size() >= kMax) {
    buffer_status_ = Cbor::Error::OUT_OF_DATA;
    return false;
  }
  formatted_message_buffer_.insert(formatted_message_buffer_.end(),
                                   std::begin(bytes), std::end(bytes));
  return true;
}

Cbor::Error Cbor::CheckUTF8Copy(const std::string& text) {
  auto begin = text.cbegin();
  auto end = text.cend();

  if (!IsOk()) {
    return buffer_status_;
  }

  uint32_t multi_byte_length = 0;
  Cbor::Error err_code = buffer_status_;  // OK

  while (begin != end) {
    if (multi_byte_length) {
      // parsing multi byte character - must start with 10xxxxxx
      --multi_byte_length;
      if ((*begin & 0xc0) != 0x80) {
        return Cbor::Error::MALFORMED_UTF8;
      }
    } else if (!((*begin) & 0x80)) {
      // 7bit character -> nothing to be done
    } else {
      // msb is set and we were not parsing a multi byte character
      // so this must be a header byte
      char c = *begin << 1;
      while (c & 0x80) {
        ++multi_byte_length;
        c <<= 1;
      }
      // headers of the form 10xxxxxx are not allowed
      if (multi_byte_length < 1) {
        return Cbor::Error::MALFORMED_UTF8;
      }
      // chars longer than 4 bytes are not allowed (multi_byte_length does not
      // count the header thus > 3
      if (multi_byte_length > 3) {
        return Cbor::Error::MALFORMED_UTF8;
      }
    }

    if (formatted_message_buffer_.size() < kMax) {
      formatted_message_buffer_.emplace_back(
          static_cast<const std::uint8_t>(*begin));
      begin++;
    } else {
      // not returning with this error to be consistent with the reference
      // implementation
      err_code = Cbor::Error::OUT_OF_DATA;
    }
  }
  // if the string ends in the middle of a multi byte char it is invalid
  if (multi_byte_length) {
    return Cbor::Error::MALFORMED_UTF8;
  }
  return err_code;
}

/**
 * The test key is 32byte word with all bytes set to TestKeyBits::BYTE.
 */
enum class TestKeyBits : uint8_t {
  BYTE = 165 /* 0xA5 */,
};

std::optional<std::vector<std::uint8_t>> sign(
    const std::vector<std::uint8_t>& message) {
  // the same as userConfirm()
  using namespace support;
  auth_token_key_t key;
  key.fill(static_cast<std::uint8_t>(TestKeyBits::BYTE));
  using HMacer = HMacImplementation;
  auto confirm_signed_opt =
      HMacer::hmac256(key, {"confirmation token", message});
  if (!confirm_signed_opt) {
    return std::nullopt;
  }
  auto confirm_signed = confirm_signed_opt.value();
  return {
      std::vector<std::uint8_t>(confirm_signed.begin(), confirm_signed.end())};
}

}  // end of namespace confui
}  // end of namespace cuttlefish
