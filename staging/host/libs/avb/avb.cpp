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

#include <fcntl.h>

#include <memory>
#include <string>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

constexpr char kAddHashFooter[] = "add_hash_footer";
constexpr char kDefaultAlgorithm[] = "SHA256_RSA4096";
constexpr char kInfoImage[] = "info_image";
constexpr char kMakeVbmetaImage[] = "make_vbmeta_image";
// Taken from external/avb/libavb/avb_slot_verify.c; this define is not in the
// headers
constexpr size_t kVbMetaMaxSize = 65536ul;

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
  return command;
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

Command Avb::GenerateInfoImage(const std::string& image_path,
                               const SharedFD& output_file) const {
  Command command(avbtool_path_);
  command.AddParameter(kInfoImage);
  command.AddParameter("--image");
  command.AddParameter(image_path);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_file);
  return command;
}

Result<void> Avb::WriteInfoImage(const std::string& image_path,
                                 const std::string& output_path) const {
  auto output_file = SharedFD::Creat(output_path, 0666);
  CF_EXPECTF(output_file->IsOpen(), "Unable to create {} with error - {}",
             output_path, output_file->StrError());
  auto command = GenerateInfoImage(image_path, output_file);
  int exit_code = command.Start().Wait();
  CF_EXPECTF(exit_code == 0, "Failure running {} {}. Exited with status {}",
             command.Executable(), kInfoImage, exit_code);
  return {};
}

Command Avb::GenerateMakeVbMetaImage(
    const std::string& output_path,
    const std::vector<ChainPartition>& chained_partitions,
    const std::vector<std::string>& included_partitions,
    const std::vector<std::string>& extra_arguments) {
  Command command(avbtool_path_);
  command.AddParameter(kMakeVbmetaImage);
  command.AddParameter("--algorithm");
  command.AddParameter(algorithm_);
  command.AddParameter("--key");
  command.AddParameter(key_);
  command.AddParameter("--output");
  command.AddParameter(output_path);

  for (const auto& partition : chained_partitions) {
    const std::string argument = partition.name + ":" +
                                 partition.rollback_index + ":" +
                                 partition.key_path;
    command.AddParameter("--chain_partition");
    command.AddParameter(argument);
  }
  for (const auto& partition : included_partitions) {
    command.AddParameter("--include_descriptors_from_image");
    command.AddParameter(partition);
  }
  for (const auto& extra_arg : extra_arguments) {
    command.AddParameter(extra_arg);
  }
  return command;
}

Result<void> Avb::MakeVbMetaImage(
    const std::string& output_path,
    const std::vector<ChainPartition>& chained_partitions,
    const std::vector<std::string>& included_partitions,
    const std::vector<std::string>& extra_arguments) {
  auto command = GenerateMakeVbMetaImage(output_path, chained_partitions,
                                         included_partitions, extra_arguments);
  int exit_code = command.Start().Wait();
  CF_EXPECTF(exit_code == 0, "Failure running {} {}. Exited with status {}",
             command.Executable(), kMakeVbmetaImage, exit_code);
  CF_EXPECT(EnforceVbMetaSize(output_path));
  return {};
}

Result<void> EnforceVbMetaSize(const std::string& path) {
  const auto vbmeta_size = FileSize(path);
  CF_EXPECT_LE(vbmeta_size, kVbMetaMaxSize);
  if (vbmeta_size != kVbMetaMaxSize) {
    auto vbmeta_fd = SharedFD::Open(path, O_RDWR);
    CF_EXPECTF(vbmeta_fd->IsOpen(), "Unable to open {} with error {}", path,
               vbmeta_fd->StrError());
    CF_EXPECTF(vbmeta_fd->Truncate(kVbMetaMaxSize) == 0,
               "Truncating {} failed with error {}", path,
               vbmeta_fd->StrError());
    CF_EXPECTF(vbmeta_fd->Fsync() == 0, "fsync on {} failed with error {}",
               path, vbmeta_fd->StrError());
  }
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

}  // namespace cuttlefish
