//
// Copyright (C) 2021 The Android Open Source Project
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

#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <android-base/logging.h>
#include <android-base/result.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/test_gce_driver/gce_api.h"
#include "host/commands/test_gce_driver/key_pair.h"
#include "host/commands/test_gce_driver/scoped_instance.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/curl_wrapper.h"
#include "host/libs/web/install_zip.h"

using android::base::Error;
using android::base::Result;

namespace cuttlefish {
namespace {

android::base::Result<Json::Value> ReadJsonFromFile(const std::string& path) {
  Json::CharReaderBuilder builder;
  std::ifstream ifs(path);
  Json::Value content;
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, &content, &errorMessage)) {
    return android::base::Error()
           << "Could not read config file \"" << path << "\": " << errorMessage;
  }
  return content;
}

}  // namespace

int TestGceDriverMain(int argc, char** argv) {
  std::vector<Flag> flags;
  std::string service_account_json_private_key_path = "";
  flags.emplace_back(GflagsCompatFlag("service-account-json-private-key-path",
                                      service_account_json_private_key_path));
  std::string instance_name = "";
  flags.emplace_back(GflagsCompatFlag("instance-name", instance_name));

  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CHECK(ParseFlags(flags, args)) << "Could not process command line flags.";

  auto service_json = ReadJsonFromFile(service_account_json_private_key_path);
  CHECK(service_json.ok()) << service_json.error();

  static constexpr char COMPUTE_SCOPE[] =
      "https://www.googleapis.com/auth/compute";
  auto curl = CurlWrapper::Create();
  auto credential_source = ServiceAccountOauthCredentialSource::FromJson(
      *curl, *service_json, COMPUTE_SCOPE);
  CHECK(credential_source);

  // TODO(b/216667647): Allow these settings to be configured.
  GceApi gce(*curl, *credential_source, "cloud-android-testing", "us-west1-a");

  auto instance = ScopedGceInstance::CreateDefault(gce, instance_name);
  CHECK(instance.ok()) << "Failed to create GCE instance: " << instance.error();

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::TestGceDriverMain(argc, argv);
}
