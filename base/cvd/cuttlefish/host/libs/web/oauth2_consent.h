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
#include <unistd.h>

#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

struct Oauth2ConsentRequest {
  std::string client_id;
  std::string client_secret;
  std::vector<std::string> scopes;
};

// Run the user through a consent flow and save the output in local credential
// storage.

Result<std::unique_ptr<CredentialSource>> Oauth2LoginLocal(
    HttpClient&, const Oauth2ConsentRequest&);
Result<std::unique_ptr<CredentialSource>> Oauth2LoginSsh(
    HttpClient&, const Oauth2ConsentRequest&);

// Retrieve the credential for a particular set of scopes from local credential
// storage.

Result<std::unique_ptr<CredentialSource>> CredentialForScopes(
    HttpClient&, const std::vector<std::string>&);

}  // namespace cuttlefish
