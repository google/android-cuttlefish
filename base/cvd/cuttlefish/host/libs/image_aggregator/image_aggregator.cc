/*
 * Copyright (C) 2019 The Android Open Source Project
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

/*
 * GUID Partition Table and Composite Disk generation code.
 */

#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <sparse/sparse.h>
#include <zlib.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/host/libs/image_aggregator/cdisk_spec.pb.h"
#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/host/libs/image_aggregator/gpt.h"
#include "cuttlefish/host/libs/image_aggregator/gpt_type_guid.h"
#include "cuttlefish/host/libs/image_aggregator/image_from_file.h"
#include "cuttlefish/host/libs/image_aggregator/mbr.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"

namespace cuttlefish {
namespace {

struct PartitionInfo {
  ImagePartition source;
  uint64_t size;
  uint64_t offset;

  uint64_t AlignedSize() const { return AlignToPartitionSize(size); }
};

/*
 * Returns the expanded file size of `file_path`. Note that the raw size of
 * files doesn't match how large they may appear inside a VM.
 *
 * Supported types: Composite disk image, Qcows2, Android-Sparse, Raw
 *
 * Android-Sparse is a file format invented by Android that optimizes for
 * chunks of zeroes or repeated data. The Android build system can produce
 * sparse files to save on size of disk files after they are extracted from a
 * disk file, as the imag eflashing process also can handle Android-Sparse
 * images.
 */
Result<uint64_t> ExpandedStorageSize(const std::string& file_path) {
  std::unique_ptr<DiskImage> disk = CF_EXPECT(ImageFromFile(file_path));
  CF_EXPECT(disk.get());
  return CF_EXPECT(disk->VirtualSizeBytes());
}

/*
 * strncpy equivalent for u16 data. GPT disks use UTF16-LE for disk labels.
 */
void u16cpy(uint16_t* dest, uint16_t* src, size_t size) {
  while (size > 0 && *src) {
    *dest = *src;
    dest++;
    src++;
    size--;
  }
  if (size > 0) {
    *dest = 0;
  }
}

void SetRandomUuid(uint8_t uuid[16]) {
  // https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_(random)
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<std::mt19937::result_type> dist(0, 0xff);

  for (int i = 0; i < 16; i++) {
    uuid[i] = dist(rng);
  }
  // https://www.rfc-editor.org/rfc/rfc4122#section-4.4
  uuid[7] = (uuid[7] & 0x0F) | 0x40;  // UUID v4
  uuid[9] = (uuid[9] & 0x3F) | 0x80;
}

/**
 * Incremental builder class for producing partition tables. Add partitions
 * one-by-one, then produce specification files
 */
class CompositeDiskBuilder {
 public:
  CompositeDiskBuilder(bool read_only) : read_only_(read_only) {}

  Result<void> AppendPartition(ImagePartition source) {
    uint64_t size = CF_EXPECT(ExpandedStorageSize(source.image_file_path));
    auto aligned_size = AlignToPartitionSize(size);
    CF_EXPECTF(size == aligned_size || read_only_,
               "read-write partition '{}' is not aligned to the size of '{}'",
               source.label, (1 << PARTITION_SIZE_SHIFT));
    partitions_.push_back(PartitionInfo{
        .source = source,
        .size = size,
        .offset = next_disk_offset_,
    });
    next_disk_offset_ = next_disk_offset_ + aligned_size;
    return {};
  }

  uint64_t DiskSize() const {
    return AlignToPowerOf2(next_disk_offset_ + sizeof(GptEnd), DISK_SIZE_SHIFT);
  }

  /**
   * Generates a composite disk specification file, assuming that `header_file`
   * and `footer_file` will be populated with the contents of `Beginning()` and
   * `End()`.
   */
  Result<CompositeDisk> MakeCompositeDiskSpec(
      const std::string& header_file, const std::string& footer_file) const {
    CompositeDisk disk;
    disk.set_version(2);
    disk.set_length(DiskSize());

    ComponentDisk* header = disk.add_component_disks();
    header->set_file_path(header_file);
    header->set_offset(0);

    for (auto& partition : partitions_) {
      ComponentDisk* component = disk.add_component_disks();
      component->set_file_path(partition.source.image_file_path);
      component->set_offset(partition.offset);
      component->set_read_write_capability(
          read_only_ ? ReadWriteCapability::READ_ONLY
                     : ReadWriteCapability::READ_WRITE);
      uint64_t size =
          CF_EXPECT(ExpandedStorageSize(partition.source.image_file_path));
      CF_EXPECT_EQ(partition.size, size);
      // When partition's aligned size differs from its (unaligned) size
      // reading the disk within the guest os would fail due to the gap.
      // Putting any disk bigger than 4K can fill this gap.
      // Here we reuse the header which is always > 4K.
      // We don't fill the "writable" disk's hole and it should be an error
      // because writes in the guest of can't be reflected to the backing file.
      if (partition.AlignedSize() != partition.size) {
        ComponentDisk* component = disk.add_component_disks();
        component->set_file_path(header_file);
        component->set_offset(partition.offset + partition.size);
        component->set_read_write_capability(ReadWriteCapability::READ_ONLY);
      }
    }

    ComponentDisk* footer = disk.add_component_disks();
    footer->set_file_path(footer_file);
    footer->set_offset(next_disk_offset_);

    return disk;
  }

  /*
   * Returns a GUID Partition Table header structure for all the disks that have
   * been added with `AppendDisk`. Includes a protective MBR.
   *
   * This method is not deterministic: some data is generated such as the disk
   * uuids.
   */
  Result<GptBeginning> Beginning() const {
    CF_EXPECT_LE(partitions_.size(), GPT_NUM_PARTITIONS, "Too many partitions");
    GptBeginning gpt = {
        .protective_mbr = ProtectiveMbr(DiskSize()),
        .header =
            {
                .signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
                .revision = {0, 0, 1, 0},
                .header_size = sizeof(GptHeader),
                .current_lba = 1,
                .backup_lba = (DiskSize() / kSectorSize) - 1,
                .first_usable_lba = sizeof(GptBeginning) / kSectorSize,
                .last_usable_lba = (next_disk_offset_ / kSectorSize) - 1,
                .partition_entries_lba = 2,
                .num_partition_entries = GPT_NUM_PARTITIONS,
                .partition_entry_size = sizeof(GptPartitionEntry),
            },
    };
    SetRandomUuid(gpt.header.disk_guid);
    for (size_t i = 0; i < partitions_.size(); i++) {
      const auto& partition = partitions_[i];
      gpt.entries[i] = GptPartitionEntry{
          .first_lba = partition.offset / kSectorSize,
          .last_lba =
              (partition.offset + partition.AlignedSize()) / kSectorSize - 1,
      };
      SetRandomUuid(gpt.entries[i].unique_partition_guid);
      const uint8_t* const type_guid =
          CF_EXPECT(GetPartitionGUID(partition.source.type));
      CF_EXPECT(type_guid != nullptr, "Could not recognize partition guid");
      memcpy(gpt.entries[i].partition_type_guid, type_guid, 16);
      std::u16string wide_name(partitions_[i].source.label.begin(),
                               partitions_[i].source.label.end());
      u16cpy((uint16_t*)gpt.entries[i].partition_name,
             (uint16_t*)wide_name.c_str(), 36);
    }
    // Not sure these are right, but it works for bpttool
    gpt.header.partition_entries_crc32 =
        crc32(0, (uint8_t*)gpt.entries,
              GPT_NUM_PARTITIONS * sizeof(GptPartitionEntry));
    gpt.header.header_crc32 =
        crc32(0, (uint8_t*)&gpt.header, sizeof(GptHeader));
    return gpt;
  }

  /**
   * Generates a GUID Partition Table footer that matches the header in `head`.
   */
  GptEnd End(const GptBeginning& head) const {
    GptEnd gpt;
    std::memcpy((void*)gpt.entries, (void*)head.entries, sizeof(gpt.entries));
    gpt.footer = head.header;
    gpt.footer.partition_entries_lba =
        (DiskSize() - sizeof(gpt.entries)) / kSectorSize - 1;
    std::swap(gpt.footer.current_lba, gpt.footer.backup_lba);
    gpt.footer.header_crc32 = 0;
    gpt.footer.header_crc32 =
        crc32(0, (uint8_t*)&gpt.footer, sizeof(GptHeader));
    return gpt;
  }

 private:
  std::vector<PartitionInfo> partitions_;
  uint64_t next_disk_offset_ = sizeof(GptBeginning);
  bool read_only_ = true;
};

Result<void> WriteBeginning(SharedFD out, const GptBeginning& beginning) {
  std::string begin_str((const char*)&beginning, sizeof(GptBeginning));
  CF_EXPECT_EQ(WriteAll(out, begin_str), begin_str.size(),
               "Could not write GPT beginning: " << out->StrError());
  return {};
}

Result<void> WriteEnd(SharedFD out, const GptEnd& end) {
  auto disk_size = (end.footer.current_lba + 1) * kSectorSize;
  auto footer_start = (end.footer.last_usable_lba + 1) * kSectorSize;
  auto padding = disk_size - footer_start - sizeof(GptEnd);
  std::string padding_str(padding, '\0');

  CF_EXPECT_EQ(WriteAll(out, padding_str), padding_str.size(),
               "Could not write GPT end padding: " << out->StrError());
  CF_EXPECT_EQ(WriteAllBinary(out, &end), sizeof(end),
               "Could not write GPT end contents: " << out->StrError());
  return {};
}

/**
 * Converts any Android-Sparse image files in `partitions` to raw image files.
 *
 * Android-Sparse is a file format invented by Android that optimizes for
 * chunks of zeroes or repeated data. The Android build system can produce
 * sparse files to save on size of disk files after they are extracted from a
 * disk file, as the imag eflashing process also can handle Android-Sparse
 * images.
 *
 * crosvm has read-only support for Android-Sparse files, but QEMU does not
 * support them.
 */
Result<void> DeAndroidSparse(const std::vector<ImagePartition>& partitions) {
  for (const auto& partition : partitions) {
    CF_EXPECT(ForceRawImage(partition.image_file_path));
  }
  return {};
}

Result<void> WriteCompositeDiskToFile(const CompositeDisk& composite_proto,
                                      const std::string& path) {
  std::ofstream composite(path, std::ios::binary | std::ios::trunc);
  CF_EXPECT(!!composite, "Failed to open composite file");
  composite << CompositeDiskImage::MagicString();
  CF_EXPECT(composite_proto.SerializeToOstream(&composite),
            "Failed to serialize composite spec to file");
  composite.flush();
  return {};
}

}  // namespace

uint64_t AlignToPartitionSize(uint64_t size) {
  return AlignToPowerOf2(size, PARTITION_SIZE_SHIFT);
}

Result<void> AggregateImage(const std::vector<ImagePartition>& partitions,
                            const std::string& output_path) {
  CF_EXPECT(DeAndroidSparse(partitions));

  CompositeDiskBuilder builder(false);
  for (auto& partition : partitions) {
    // TODO: b/471069557 - diagnose unused
    Result<void> unused = builder.AppendPartition(partition);
  }

  SharedFD output = SharedFD::Creat(output_path, 0600);
  CF_EXPECTF(output->IsOpen(), "{}", output->StrError());

  GptBeginning beginning = CF_EXPECT(builder.Beginning());
  CF_EXPECTF(WriteBeginning(output, beginning),
             "Could not write GPT beginning to '{}': {}", output_path,
             output->StrError());

  for (auto& disk : partitions) {
    SharedFD disk_fd = SharedFD::Open(disk.image_file_path, O_RDONLY);
    CF_EXPECTF(disk_fd->IsOpen(), "{}", disk_fd->StrError());

    auto file_size = FileSize(disk.image_file_path);
    CF_EXPECTF(output->CopyFrom(*disk_fd, file_size),
               "Could not copy from '{}' to '{}': {}", disk.image_file_path,
               output_path, output->StrError());
    // Handle disk images that are not aligned to PARTITION_SIZE_SHIFT
    uint64_t padding = AlignToPartitionSize(file_size) - file_size;
    std::string padding_str;
    padding_str.resize(padding, '\0');

    CF_EXPECTF(WriteAll(output, padding_str) == padding_str.size(),
               "Could not write partition padding to '{}': {}", output_path,
               output->StrError());
  }
  CF_EXPECTF(WriteEnd(output, builder.End(beginning)),
             "Could not write GPT end to '{}': {}", output_path,
             output->StrError());
  return {};
};

Result<void> CreateOrUpdateCompositeDisk(
    std::vector<ImagePartition> partitions, const std::string& header_file,
    const std::string& footer_file, const std::string& output_composite_path,
    bool read_only) {
  CF_EXPECT(DeAndroidSparse(partitions));

  CompositeDiskBuilder builder(read_only);
  for (auto& partition : partitions) {
    // TODO: b/471069557 - diagnose unused
    Result<void> unused = builder.AppendPartition(partition);
  }
  CompositeDisk composite_proto =
      CF_EXPECT(builder.MakeCompositeDiskSpec(header_file, footer_file));

  Result<CompositeDiskImage> composite_image_res =
      CompositeDiskImage::OpenExisting(output_composite_path);

  if (composite_image_res.ok() &&
      google::protobuf::util::MessageDifferencer::Equals(
          composite_proto, composite_image_res->GetCompositeDisk())) {
    // The existing composite disk matches the given partitions, no need to
    // regenerate
    return {};
  }

  CF_EXPECT(WriteCompositeDiskToFile(composite_proto, output_composite_path));

  SharedFD header = SharedFD::Creat(header_file, 0600);
  CF_EXPECTF(header->IsOpen(), "{}", header->StrError());

  GptBeginning beginning = CF_EXPECT(builder.Beginning());
  CF_EXPECTF(WriteBeginning(header, beginning),
             "Could not write GPT beginning to '{}': {}", header_file,
             header->StrError());

  SharedFD footer = SharedFD::Creat(footer_file, 0600);
  CF_EXPECTF(footer->IsOpen(), "{}", footer->StrError());

  CF_EXPECTF(WriteEnd(footer, builder.End(beginning)),
             "Could not write GPT end to '{}': {}", footer_file,
             footer->StrError());

  return {};
}

}  // namespace cuttlefish
