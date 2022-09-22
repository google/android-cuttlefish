//
// Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/remote/output.h"

#include <iostream>

#include <gtest/gtest.h>

namespace cuttlefish {

TEST(CVDOutputTest, NonVerbose) {
  CVDOutput o{
    service_url : "http://xyzzy.com",
    zone : "central1-b",
    host : "foo",
    verbose : false,
    name : "bar"
  };

  auto result = o.ToString();

  EXPECT_EQ(result, "bar (foo)");
}

TEST(CVDOutputTest, InstanceVerbose) {
  CVDOutput o{
    service_url : "http://xyzzy.com",
    zone : "central1-b",
    host : "foo",
    verbose : true,
    name : "bar"
  };

  auto result = o.ToString();

  // clang-format off
  const char *expected =
      "bar (foo)\n"
      "  webrtcstream_url: http://xyzzy.com/v1/zones/central1-b/hosts/foo/devices/bar/files/client.html\n";
  // clang-format on
  EXPECT_EQ(result, expected);
}

}  // namespace cuttlefish
