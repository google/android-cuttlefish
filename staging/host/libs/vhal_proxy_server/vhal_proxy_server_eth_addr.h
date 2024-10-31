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

namespace cuttlefish::vhal_proxy_server {

// Host-side VHAL GRPC server ethernet address.
constexpr std::string_view kEthAddr = "192.168.98.1";

// Host-side VHAL GRPC server default ethernet port, if there are multiple
// VHAL server instances (by default, a new instance is created for launch_cvd
// call unless vhal_proxy_server_instance_num is specified to reuse an
// existing instance), then the port number will be 9300, 9301, etc.
constexpr int kDefaultEthPort = 9300;

}  // namespace cuttlefish::vhal_proxy_server