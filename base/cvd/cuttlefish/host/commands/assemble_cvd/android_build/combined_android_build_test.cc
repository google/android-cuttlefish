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

#include "cuttlefish/host/commands/assemble_cvd/android_build/combined_android_build.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/fake_android_build.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

Result<std::unique_ptr<AndroidBuild>> CombineFakeBuilds(
    std::vector<FakeAndroidBuild> fakes) {
  std::vector<std::unique_ptr<AndroidBuild>> build_ptrs;
  for (FakeAndroidBuild fake : fakes) {
    build_ptrs.emplace_back(
        std::make_unique<FakeAndroidBuild>(std::move(fake)));
  }
  return CF_EXPECT(CombinedAndroidBuild("Fakes", std::move(build_ptrs)));
}

TEST(CombinedAndroidBuild, Construct) {
  EXPECT_THAT(CombineFakeBuilds({FakeAndroidBuild(), FakeAndroidBuild()}),
              IsOk());
}

TEST(CombinedAndroidBuild, MergesImages) {
  FakeAndroidBuild with_a;
  with_a.AddExtractedImage("a", "a_file");

  FakeAndroidBuild with_b;
  with_b.AddExtractedImage("b", "b_file");

  std::unique_ptr<AndroidBuild> combined = *CombineFakeBuilds({with_a, with_b});

  std::set<std::string, std::less<void>> expected = {"a", "b"};
  EXPECT_THAT(combined->Images(), IsOkAndValue(expected));
  EXPECT_THAT(combined->ImageFile("a"), IsOkAndValue("a_file"));
  EXPECT_THAT(combined->ImageFile("b"), IsOkAndValue("b_file"));
}

TEST(CombinedAndroidBuild, PrefersExtractedImage) {
  FakeAndroidBuild unextracted;
  unextracted.AddUnextractedImage("img");

  FakeAndroidBuild extracted;
  extracted.AddExtractedImage("img", "extracted");

  std::unique_ptr<AndroidBuild> combined =
      *CombineFakeBuilds({extracted, unextracted});

  EXPECT_THAT(combined->ImageFile("img", false), IsOkAndValue("extracted"));
  EXPECT_THAT(combined->ImageFile("img", true), IsOkAndValue("extracted"));
}

TEST(CombinedAndroidBuild, IgnoresMissingImage) {
  FakeAndroidBuild unextracted;
  unextracted.AddUnextractedImage("img");
  EXPECT_THAT(unextracted.SetExtractDir("extract"), IsOk());

  FakeAndroidBuild missing;
  missing.AddMissingImage("img");

  std::unique_ptr<AndroidBuild> combined =
      *CombineFakeBuilds({missing, unextracted});

  EXPECT_THAT(combined->ImageFile("img", false), IsError());
  EXPECT_THAT(combined->ImageFile("img", true), IsOkAndValue("extract/img"));
}

TEST(CombinedAndroidBuild, MergesLogicalPartitions) {
  FakeAndroidBuild with_a;
  with_a.SetLogicalPartitions({"a"});

  FakeAndroidBuild with_b;
  with_b.SetLogicalPartitions({"b"});

  std::unique_ptr<AndroidBuild> combined = *CombineFakeBuilds({with_a, with_b});

  std::set<std::string, std::less<void>> expected = {"a", "b"};
  EXPECT_THAT(combined->LogicalPartitions(), IsOkAndValue(expected));
}

}  // namespace
}  // namespace cuttlefish
