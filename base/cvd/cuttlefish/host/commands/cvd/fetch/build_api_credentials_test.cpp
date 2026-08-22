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

#include "cuttlefish/host/commands/cvd/fetch/build_api_credentials.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"
#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/host/commands/cvd/fetch/build_api_flags.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;

// Generated for this test alone, and only so that the service account
// credential source has a key it can sign an assertion with.
constexpr char kTestPrivateKey[] =
    "-----BEGIN PRIVATE KEY-----\\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCr4jcMb0PlSMAl\\n"
    "UO0oams84Ph8HnEYp14sSuNPuarrGaFEm4GPS0BMFBV4BSd7nX/MCqk3Jbd8kolL\\n"
    "oaHa+7uKs5F7lrl3w0/JmA8P4/6D86CU0dCrwriIKL6BNvik3CHZEHCmKI6Ht8ur\\n"
    "C+jQjEOp9bJVW3+NdyyKxChK29a7W6JjuBRp8TohHKeV5BQHCVj8krkylFCTO3VX\\n"
    "KQQpFyy+H6jZyIJY2WSRMLBHg8sVkw+qjXRnGxRdK262SGuPfvzdUzaYbNxqLtXT\\n"
    "IzQCV6kDyaBsTCU8fz7nOGg/cgCBuzocnpXgKTgICEwktUUVAC2PXbwrHHEYC05N\\n"
    "Z5LOY00xAgMBAAECggEAAdP5E+fHCBQ6/uqaaxiepVobKm7Ecyesh7oQKtPlrnRq\\n"
    "U6l3ukdpmqWICOu9HMJzDn96hzyec/O3BBfm+cY9m18HiBH1TQHFwnYciuW42jxo\\n"
    "E80bdAgxIDmWtRcZk99HeOCE4i+CPI1G3D3XLwie25riV6gOdjmzPpKRfyJRaVKu\\n"
    "spQQdjZ19puXbU4ApLL4QhO3RwjhVzECxhHMxmXWKuCdWp11+NQ+fFBJO6FM7ipz\\n"
    "kYDJXRiKTMZK9wOloEhz3CYeukCe4P9HUDfq8TCK4xmEYLawFfSvYSA0Sb7SHrT2\\n"
    "pNsyS2s9/74Y8e63Ea7o8Y0uT3hPqskAnG09ockS8QKBgQDdQEvHo+XJfPF0f1OE\\n"
    "1LrZYJvIwjh9/PNO5W42CYnsJ+KLlgq+ZIrJdUObPQk1Cp3juPPWJuLMtC8oVz1J\\n"
    "wT9dbubnlzVcprDb3NVRrFRSChIdwauew7piyyBP7ciBGAtMtigr9rdhKeZUB5Hj\\n"
    "ZIP9X5XCJtriHAw+MENLyTaTdQKBgQDG4QgD8uHfK36PrtTk4mE+8wWAI+efI+0T\\n"
    "UNSSjMdxipDFZfeU1b4lXzlHBeAegLeQmPQFIoE5lmdAYcHOdPfy88GQnNG2Q0Ao\\n"
    "bNp/IPIMjB3O+k31hhNWHnNl1diPYTV9z5DNlpuOhHqZtbANbx2QnS/4TvRgq657\\n"
    "NFXW/NXHTQKBgG6Ms9Ca+jQE8/iLrkWOrZX0CaL0OJnrC/997+WcOof/Hdk1LUUY\\n"
    "o6gpqZAlnTYdierA/UUhxO0XkwCLJpp1rp2WzlUlXope17vjycq3WqJrWcX4gTIh\\n"
    "Bj5a1FhbrXWjd/HqioP9EH/CGc4ewixmivTND90k4PVdolhocRerAFQJAoGAI3NF\\n"
    "XH7U6FT2cGI3rLz1nKTxHBBKX0GmJsVHvv+9JW4PtEAiy7L1++9nZFOVyZokHnBF\\n"
    "Pw0Rf9Rhf0Ztp4GOGQ5+OGrbruN58jrFD9gtjTMEtTpE3zkRBU7UPxjJS3WGdXCk\\n"
    "XSE1hUf0GqYaRarC2F5MiLR6NykjJu8DRhk3ehkCgYEAkNLfUrtgW2C94lFeFIm8\\n"
    "JEUXNNJx40TWKOtenS4CsB0i+xuemjRzAPCmIRpRVwSoFZvXI0kkGd1W09hDO9DE\\n"
    "G4BLoOcpJEN9OFp1lwi2xFgb9ibrtmDUM/+SuSr3w4oxKmK9RfDk7uTSQtxM4BO3\\n"
    "MeqOwdgF5vKL/MNnxi319Ic=\\n"
    "-----END PRIVATE KEY-----\\n";

constexpr char kTokenResponse[] =
    R"({"access_token": "a-minted-token", "expires_in": 3600})";

std::string ServiceAccountJson() {
  return fmt::format(
      R"({{"client_email": "test@example.com", "private_key": "{}"}})",
      kTestPrivateKey);
}

// The scope a service account asks for travels inside the base64url claim set
// of the assertion it signs.
std::string AssertionClaims(const std::string& request_body) {
  std::string_view body = request_body;
  size_t assertion = body.find("assertion=");
  if (assertion == std::string_view::npos) {
    return "";
  }
  std::vector<std::string_view> parts =
      absl::StrSplit(body.substr(assertion), '.');
  if (parts.size() < 2) {
    return "";
  }
  std::string claims =
      absl::StrReplaceAll(parts[1], {{"-", "+"}, {"_", "/"}, {"%3D", "="}});
  Result<std::vector<uint8_t>> decoded = DecodeBase64(claims);
  if (!decoded.has_value()) {
    return "";
  }
  return std::string(decoded->begin(), decoded->end());
}

class StorageCredentialTests : public ::testing::Test {
 protected:
  void SetUp() override {
    previous_home_ = StringFromEnv("HOME", "");
    // The ~/.boto credential source reads $HOME, which must not be the one of
    // whoever runs the test.
    setenv("HOME", temp_dir_.path, /* overwrite */ 1);
  }

  void TearDown() override {
    setenv("HOME", previous_home_.c_str(), /* overwrite */ 1);
  }

  std::string WriteTempFile(const std::string& name,
                            const std::string& contents) {
    std::string path = fmt::format("{}/{}", temp_dir_.path, name);
    std::ofstream file(path);
    file << contents;
    return path;
  }

  TemporaryDir temp_dir_;
  std::string previous_home_;
  FakeHttpClient http_client_;
};

TEST_F(StorageCredentialTests, CredentialSourceTokenIsNotPresentedToStorage) {
  BuildApiFlags flags;
  flags.credential_source = WriteTempFile("credential", "a-build-api-token");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags,
                                 /*running_on_gce=*/false);

  ASSERT_THAT(credentials, IsOk());
  EXPECT_THAT(credentials->get(), IsNull());
  EXPECT_FALSE(http_client_.RequestMade("metadata.google.internal"));
}

TEST_F(StorageCredentialTests, CredentialSourceFileIsNeverRead) {
  BuildApiFlags flags;
  // A directory cannot be read as a file, so a credential source that opened
  // this path would fail here instead of reaching anonymous access.
  flags.credential_source = temp_dir_.path;

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags,
                                 /*running_on_gce=*/false);

  ASSERT_THAT(credentials, IsOk());
  EXPECT_THAT(credentials->get(), IsNull());
}

TEST_F(StorageCredentialTests, CredentialFilepathIsNotPresentedToStorage) {
  BuildApiFlags flags;
  flags.credential_flags.credential_filepath =
      WriteTempFile("credential", "a-build-api-token");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags,
                                 /*running_on_gce=*/false);

  ASSERT_THAT(credentials, IsOk());
  EXPECT_THAT(credentials->get(), IsNull());
}

TEST_F(StorageCredentialTests, ServiceAccountMintsAStorageScopedToken) {
  BuildApiFlags flags;
  flags.credential_flags.service_account_filepath =
      WriteTempFile("service_account.json", ServiceAccountJson());
  std::string claims;
  http_client_.SetResponse(
      [&claims](const HttpRequest& request) {
        claims = AssertionClaims(request.data_to_write);
        return HttpResponse<std::string>{.data = kTokenResponse,
                                         .http_code = 200};
      },
      "oauth2.googleapis.com/token");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags,
                                 /*running_on_gce=*/false);

  ASSERT_THAT(credentials, IsOk());
  ASSERT_THAT(credentials->get(), NotNull());
  EXPECT_THAT((*credentials)->Credential(), IsOkAndValue("a-minted-token"));
  EXPECT_THAT(claims, HasSubstr("devstorage.read_only"));
  EXPECT_THAT(claims, Not(HasSubstr("androidbuild.internal")));
}

TEST_F(StorageCredentialTests, BotoFileOutranksTheMetadataServer) {
  BuildApiFlags flags;
  WriteTempFile(".boto",
                "[OAuth2]\nclient_id = an-id\nclient_secret = a-secret\n"
                "gs_oauth2_refresh_token = a-refresh-token\n");
  http_client_.SetResponse(kTokenResponse, "oauth2.googleapis.com/token");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags, /*running_on_gce=*/true);

  ASSERT_THAT(credentials, IsOk());
  ASSERT_THAT(credentials->get(), NotNull());
  EXPECT_THAT((*credentials)->Credential(), IsOkAndValue("a-minted-token"));
  EXPECT_FALSE(http_client_.RequestMade("metadata.google.internal"));
}

TEST_F(StorageCredentialTests, GceHostsGetAnAmbientCredential) {
  BuildApiFlags flags;
  http_client_.SetResponse(kTokenResponse, "metadata.google.internal");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags, /*running_on_gce=*/true);

  ASSERT_THAT(credentials, IsOk());
  ASSERT_THAT(credentials->get(), NotNull());
  EXPECT_THAT((*credentials)->Credential(), IsOkAndValue("a-minted-token"));
  EXPECT_TRUE(http_client_.RequestMade("metadata.google.internal"));
}

TEST_F(StorageCredentialTests, UseGceMetadataFlagQueriesTheMetadataServer) {
  BuildApiFlags flags;
  flags.credential_flags.use_gce_metadata = true;
  http_client_.SetResponse(kTokenResponse, "metadata.google.internal");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags,
                                 /*running_on_gce=*/false);

  ASSERT_THAT(credentials, IsOk());
  ASSERT_THAT(credentials->get(), NotNull());
  EXPECT_THAT((*credentials)->Credential(), IsOkAndValue("a-minted-token"));
  EXPECT_TRUE(http_client_.RequestMade("metadata.google.internal"));
}

TEST_F(StorageCredentialTests, LegacyGceValueQueriesTheMetadataServer) {
  BuildApiFlags flags;
  flags.credential_source = "gce";
  http_client_.SetResponse(kTokenResponse, "metadata.google.internal");

  Result<std::unique_ptr<CredentialSource>> credentials =
      GetStorageCredentialSource(http_client_, flags,
                                 /*running_on_gce=*/false);

  ASSERT_THAT(credentials, IsOk());
  ASSERT_THAT(credentials->get(), NotNull());
  EXPECT_THAT((*credentials)->Credential(), IsOkAndValue("a-minted-token"));
  EXPECT_TRUE(http_client_.RequestMade("metadata.google.internal"));
}

}  // namespace
}  // namespace cuttlefish
