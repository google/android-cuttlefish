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

#include "host/libs/web/http_client/sso_client.h"

#include <iostream>

#include <gtest/gtest.h>

namespace cuttlefish {
namespace http_client {

TEST(SsoClientTest, GetToStringSucceeds) {
  std::string stdout_ =
      "HTTP/1.1 222 Bad Request\r\n"
      "Content-Type: application/json\r\n"
      "Vary: Accept-Encoding\r\n"
      "Date: Tue, 19 Jul 2022 00:00:54 GMT\r\n"
      "Pragma: no-cache\r\n"
      "Expires: Fri, 01 Jan 1990 00:00:00 GMT\r\n"
      "Cache-Control: no-cache, must-revalidate\r\n"
      "\r\n"
      "foo"
      "\n";
  auto exec = [&](Command&&, const std::string*, std::string* out, std::string*,
                  SubprocessOptions) {
    *out = stdout_;
    return 0;
  };
  SsoClient client(exec);

  auto result = client.GetToString("https://some.url");

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->data, "foo");
  EXPECT_EQ(result->http_code, 222);
}

TEST(SsoClientTest, GetToStringSucceedsEmptyBody) {
  std::string stdout_ =
      "HTTP/1.1 222 OK\r\n"
      "Content-Type: application/json\r\n"
      "\r\n"
      "\n";
  auto exec = [&](Command&&, const std::string*, std::string* out, std::string*,
                  SubprocessOptions) {
    *out = stdout_;
    return 0;
  };
  SsoClient client(exec);

  auto result = client.GetToString("https://some.url");

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->data, "");
  EXPECT_EQ(result->http_code, 222);
}

TEST(SsoClientTest, GetToStringVerifyCommandArgs) {
  std::string cmd_as_bash_script;
  auto exec = [&](Command&& cmd, const std::string*, std::string*, std::string*,
                  SubprocessOptions) {
    cmd_as_bash_script = cmd.AsBashScript();
    return 0;
  };
  SsoClient client(exec);

  client.GetToString("https://some.url");

  EXPECT_EQ(cmd_as_bash_script,
            "#!/bin/bash\n\n/usr/bin/sso_client \\\n--dump_header "
            "\\\n--url=https://some.url");
}

TEST(SsoClientTest, GetToStringFailsInvalidResponseFormat) {
  std::string stdout_ = "E0719 13:45:32.891177 2702210 foo failed";
  auto exec = [&](Command&&, const std::string*, std::string* out, std::string*,
                  SubprocessOptions) {
    *out = stdout_;
    return 0;
  };
  SsoClient client(exec);

  auto result = client.GetToString("https://some.url");

  EXPECT_FALSE(result.ok());
}

TEST(SsoClientTest, GetToStringFailsEmptyStdout) {
  auto exec = [&](Command&&, const std::string*, std::string*, std::string*,
                  SubprocessOptions) { return 0; };
  SsoClient client(exec);

  auto result = client.GetToString("https://some.url");

  EXPECT_FALSE(result.ok());
}

TEST(SsoClientTest, GetToStringFailsExecutionFails) {
  std::string stdout_ = "foo";
  std::string stderr_ = "bar";
  auto exec = [&](Command&&, const std::string*, std::string* out,
                  std::string* err, SubprocessOptions) {
    *out = stdout_;
    *err = stderr_;
    return -1;
  };
  SsoClient client(exec);

  auto result = client.GetToString("https://some.url");

  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.error().message().find(stdout_) != std::string::npos);
  EXPECT_TRUE(result.error().message().find(stderr_) != std::string::npos);
}

}  // namespace http_client
}  // namespace cuttlefish
