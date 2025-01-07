/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "host/commands/cvd/cli/commands/login.h"

#include <unistd.h>

#include <memory>
#include <string>

#include <zlib.h>

#include <android-base/strings.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/flag_parser.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/libs/web/http_client/curl_global_init.h"
#include "host/libs/web/http_client/http_client.h"
#include "host/libs/web/oauth2_consent.h"

namespace cuttlefish {
namespace {

using android::base::Tokenize;

constexpr char kSummaryHelpText[] = "Acquire credentials";

constexpr char kHelpMessage[] = R"(
usage: cvd login --client_id=CLIENT_ID --client_secret=SECRET --scopes=SCOPES [--ssh]

  `cvd login` will request a credential to the Android Build API and store it in
  persistent local storage.
)";

class CvdLoginCommand : public CvdCommandHandler {
 public:
  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> args = request.SubcommandArguments();

    // Imperfect detection: the user may ssh into an existing `screen` or `tmux`
    // session.
    bool ssh = StringFromEnv("SSH_CLIENT").has_value() ||
               StringFromEnv("SSH_TTY").has_value();
    Oauth2ConsentRequest oauth2_request;

    std::vector<Flag> flags = {
        GflagsCompatFlag("client_id", oauth2_request.client_id),
        GflagsCompatFlag("client_secret", oauth2_request.client_secret),
        GflagsCompatFlag("scopes", oauth2_request.scopes),
        GflagsCompatFlag("ssh", ssh),
    };
    CF_EXPECT(ConsumeFlags(flags, args), "Failed to parse arguments");

    CurlGlobalInit init;
    std::unique_ptr<HttpClient> http_client =
        HttpClient::CurlClient(NameResolver(), true);
    CF_EXPECT(http_client.get(), "Failed to create a http client");

    if (ssh) {
      CF_EXPECT(Oauth2LoginSsh(*http_client, oauth2_request));
    } else {
      std::cout << "Using SSH? Please run this command again with `--ssh`.\n";
      CF_EXPECT(Oauth2LoginLocal(*http_client, oauth2_request));
    }

    return {};
  }

  cvd_common::Args CmdList() const override { return {"login"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kHelpMessage;
  }
};

}  // namespace

/** Create a credentials file */
std::unique_ptr<CvdCommandHandler> NewLoginCommand() {
  return std::make_unique<CvdLoginCommand>();
}

}  // namespace cuttlefish
