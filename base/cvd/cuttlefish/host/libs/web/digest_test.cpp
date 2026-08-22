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

#include <string>
#include <string_view>

#include "absl/strings/ascii.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;

constexpr char kAbcSha256[] =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr char kAbcMd5[] = "kAFQmDzST7DWlj99KOF/cg==";
constexpr char kEmptySha256[] =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

std::string WriteFile(const TemporaryDir& directory,
                      std::string_view contents) {
  std::string path = std::string(directory.path) + "/artifact";
  EXPECT_TRUE(android::base::WriteStringToFile(std::string(contents), path));
  return path;
}

TEST(DigestTests, Sha256OfAFileSuccess) {
  TemporaryDir directory;
  EXPECT_THAT(Sha256File(WriteFile(directory, "abc")),
              IsOkAndValue(kAbcSha256));
}

TEST(DigestTests, Sha256OfAnEmptyFileSuccess) {
  TemporaryDir directory;
  EXPECT_THAT(Sha256File(WriteFile(directory, "")), IsOkAndValue(kEmptySha256));
}

TEST(DigestTests, Sha256OfMoreThanOneReadSuccess) {
  TemporaryDir directory;

  EXPECT_THAT(
      Sha256File(WriteFile(directory, std::string(1 << 20, 'x'))),
      IsOkAndValue(
          "8f990ba0b577b51cf009ea049368c16bbda1b21e1b93be07a824758bb253c39b"));
}

TEST(DigestTests, Sha256OfAStringSuccess) {
  EXPECT_EQ(Sha256Hex("abc"), kAbcSha256);
  EXPECT_EQ(Sha256Hex(""), kEmptySha256);
}

TEST(DigestTests, MissingFileFail) {
  TemporaryDir directory;
  EXPECT_THAT(Sha256File(std::string(directory.path) + "/absent"),
              IsErrorAndMessage(HasSubstr("absent")));
}

TEST(DigestTests, VerifySha256IgnoresCaseSuccess) {
  TemporaryDir directory;
  std::string path = WriteFile(directory, "abc");

  EXPECT_THAT(VerifySha256(path, kAbcSha256, "artifact"), IsOk());
  EXPECT_THAT(VerifySha256(path, absl::AsciiStrToUpper(kAbcSha256), "artifact"),
              IsOk());
}

TEST(DigestTests, VerifySha256NamesBothDigestsFail) {
  TemporaryDir directory;
  std::string path = WriteFile(directory, "abc");
  std::string expected(64, 'a');

  EXPECT_THAT(
      VerifySha256(path, expected, "phone-img-1.zip"),
      IsErrorAndMessage(AllOf(HasSubstr("phone-img-1.zip"),
                              HasSubstr(kAbcSha256), HasSubstr(expected))));
}

TEST(DigestTests, VerifyMd5OfABase64DigestSuccess) {
  TemporaryDir directory;
  EXPECT_THAT(VerifyMd5(WriteFile(directory, "abc"), kAbcMd5, "artifact"),
              IsOk());
}

TEST(DigestTests, VerifyMd5NamesBothDigestsFail) {
  TemporaryDir directory;

  EXPECT_THAT(
      VerifyMd5(WriteFile(directory, "abc"),
                "AAAAAAAAAAAAAAAAAAAAAA==", "phone-img-1.zip"),
      IsErrorAndMessage(AllOf(HasSubstr("phone-img-1.zip"), HasSubstr(kAbcMd5),
                              HasSubstr("AAAAAAAAAAAAAAAAAAAAAA=="))));
}

}  // namespace
}  // namespace cuttlefish
