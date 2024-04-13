/*
 * Copyright 2023 The Android Open Source Project
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
 *
 */

#include "host/libs/avb/avb.h"

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Avb::Avb(std::string avbtool_path, std::string algorithm, std::string key)
    : avbtool_path_(std::move(avbtool_path)),
      algorithm_(std::move(algorithm)),
      key_(std::move(key)) {}

Result<void> Avb::AddHashFooter(const std::string& image_path,
                                const std::string& partition_name,
                                off_t partition_size_bytes) const {
  int res;

  Command avb_cmd(avbtool_path_);
  avb_cmd.AddParameter("add_hash_footer");
  avb_cmd.AddParameter("--image");
  avb_cmd.AddParameter(image_path);
  avb_cmd.AddParameter("--partition_size");
  avb_cmd.AddParameter(partition_size_bytes);
  avb_cmd.AddParameter("--partition_name");
  avb_cmd.AddParameter(partition_name);
  avb_cmd.AddParameter("--key");
  avb_cmd.AddParameter(key_);
  avb_cmd.AddParameter("--algorithm");
  avb_cmd.AddParameter(algorithm_);
  res = avb_cmd.Start().Wait();

  CF_EXPECT(res == 0, "Unable to run avbtool. Exited with status " << res);
  return {};
}

fruit::Component<Avb> CuttlefishKeyAvbComponent() {
  return fruit::createComponent().registerProvider([]() -> Avb* {
    return new Avb(HostBinaryPath("avbtool"), "SHA256_RSA4096",
                   TestKeyRsa4096());
  });
}

} // namespace cuttlefish