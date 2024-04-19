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

#include <memory>
#include <string>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

constexpr char kAddHashFooter[] = "add_hash_footer";
constexpr char kDefaultAlgorithm[] = "SHA256_RSA4096";

}  // namespace

Avb::Avb(std::string avbtool_path) : avbtool_path_(std::move(avbtool_path)) {}

Avb::Avb(std::string avbtool_path, std::string algorithm, std::string key)
    : avbtool_path_(std::move(avbtool_path)),
      algorithm_(std::move(algorithm)),
      key_(std::move(key)) {}

Command Avb::GenerateAddHashFooter(const std::string& image_path,
                                   const std::string& partition_name,
                                   const off_t partition_size_bytes) const {
  Command command(avbtool_path_);
  command.AddParameter(kAddHashFooter);
  if (!algorithm_.empty()) {
    command.AddParameter("--algorithm");
    command.AddParameter(algorithm_);
  }
  if (!key_.empty()) {
    command.AddParameter("--key");
    command.AddParameter(key_);
  }
  command.AddParameter("--image");
  command.AddParameter(image_path);
  command.AddParameter("--partition_name");
  command.AddParameter(partition_name);
  command.AddParameter("--partition_size");
  command.AddParameter(partition_size_bytes);
  return std::move(command);
}

Result<void> Avb::AddHashFooter(const std::string& image_path,
                                const std::string& partition_name,
                                const off_t partition_size_bytes) const {
  auto command =
      GenerateAddHashFooter(image_path, partition_name, partition_size_bytes);
  int exit_code = command.Start().Wait();
  CF_EXPECTF(exit_code == 0, "Failure running {} {}. Exited with status {}",
             command.Executable(), kAddHashFooter, exit_code);
  return {};
}

std::unique_ptr<Avb> GetDefaultAvb() {
  return std::unique_ptr<Avb>(
      new Avb(AvbToolBinary(), kDefaultAlgorithm, TestKeyRsa4096()));
}

fruit::Component<Avb> CuttlefishKeyAvbComponent() {
  return fruit::createComponent().registerProvider(
      []() -> Avb* { return GetDefaultAvb().release(); });
}

} // namespace cuttlefish