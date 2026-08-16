//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/libs/web/digest.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "openssl/base.h"
#include "openssl/digest.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr size_t kReadSize = 1 << 16;

Result<std::vector<uint8_t>> DigestFile(const std::string& path,
                                        const EVP_MD* digest) {
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  CF_EXPECTF(fd->IsOpen(), "Could not open '{}' - {}", path, fd->StrError());

  std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> context(EVP_MD_CTX_new(),
                                                             EVP_MD_CTX_free);
  CF_EXPECT(EVP_DigestInit_ex(context.get(), digest, nullptr));

  std::vector<char> buffer(kReadSize);
  while (true) {
    uint64_t read = CF_EXPECTF(fd->Read(buffer.data(), buffer.size()),
                               "Could not read '{}'", path);
    if (read == 0) {
      break;
    }
    CF_EXPECT(EVP_DigestUpdate(context.get(), buffer.data(), read));
  }

  std::vector<uint8_t> value(EVP_MAX_MD_SIZE);
  unsigned int length = 0;
  CF_EXPECT(EVP_DigestFinal_ex(context.get(), value.data(), &length));
  value.resize(length);
  return value;
}

}  // namespace

Result<std::string> Sha256File(const std::string& path) {
  std::vector<uint8_t> value = CF_EXPECT(DigestFile(path, EVP_sha256()));
  return absl::BytesToHexString(std::string_view(
      reinterpret_cast<const char*>(value.data()), value.size()));
}

Result<void> VerifySha256(const std::string& path, std::string_view expected,
                          std::string_view artifact_name) {
  std::string actual = CF_EXPECT(Sha256File(path));
  CF_EXPECTF(actual == absl::AsciiStrToLower(expected),
             "'{}' has sha256 '{}', but '{}' was expected.", artifact_name,
             actual, expected);
  return {};
}

Result<void> VerifyMd5(const std::string& path, std::string_view expected,
                       std::string_view artifact_name) {
  std::vector<uint8_t> value = CF_EXPECT(DigestFile(path, EVP_md5()));
  std::string actual = CF_EXPECT(EncodeBase64(value.data(), value.size()));
  CF_EXPECTF(actual == expected, "'{}' has md5 '{}', but '{}' was expected.",
             artifact_name, actual, expected);
  return {};
}

}  // namespace cuttlefish
