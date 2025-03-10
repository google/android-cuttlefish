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

#include "host/libs/image_aggregator/image_aggregator.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <fstream>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <cdisk_spec.pb.h>
#include <google/protobuf/text_format.h>
#include <sparse/sparse.h>
#include <uuid.h>
#include <zlib.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/mbr.h"

namespace cuttlefish {
namespace {

constexpr int GPT_NUM_PARTITIONS = 128;

/**
 * Creates a "Protective" MBR Partition Table header. The GUID
 * Partition Table Specification recommends putting this on the first sector
 * of the disk, to protect against old disk formatting tools from misidentifying
 * the GUID Partition Table later and doing the wrong thing.
 */
MasterBootRecord ProtectiveMbr(std::uint64_t size) {
  MasterBootRecord mbr = {
    .partitions =  {{
      .partition_type = 0xEE,
      .first_lba = 1,
      .num_sectors = (std::uint32_t) size / SECTOR_SIZE,
    }},
    .boot_signature = { 0x55, 0xAA },
  };
  return mbr;
}

struct __attribute__((packed)) GptHeader {
  std::uint8_t signature[8];
  std::uint8_t revision[4];
  std::uint32_t header_size;
  std::uint32_t header_crc32;
  std::uint32_t reserved;
  std::uint64_t current_lba;
  std::uint64_t backup_lba;
  std::uint64_t first_usable_lba;
  std::uint64_t last_usable_lba;
  std::uint8_t disk_guid[16];
  std::uint64_t partition_entries_lba;
  std::uint32_t num_partition_entries;
  std::uint32_t partition_entry_size;
  std::uint32_t partition_entries_crc32;
};

static_assert(sizeof(GptHeader) == 92);

struct __attribute__((packed)) GptPartitionEntry {
  std::uint8_t partition_type_guid[16];
  std::uint8_t unique_partition_guid[16];
  std::uint64_t first_lba;
  std::uint64_t last_lba;
  std::uint64_t attributes;
  std::uint16_t partition_name[36]; // UTF-16LE
};

static_assert(sizeof(GptPartitionEntry) == 128);

struct __attribute__((packed)) GptBeginning {
  MasterBootRecord protective_mbr;
  GptHeader header;
  std::uint8_t header_padding[420];
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  std::uint8_t partition_alignment[3072];
};

static_assert(sizeof(GptBeginning) == SECTOR_SIZE * 40);

struct __attribute__((packed)) GptEnd {
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  GptHeader footer;
  std::uint8_t footer_padding[420];
};

static_assert(sizeof(GptEnd) == SECTOR_SIZE * 33);

struct PartitionInfo {
  MultipleImagePartition source;
  std::uint64_t guest_size;
  std::uint64_t host_size;
  std::uint64_t offset;
};

/*
 * Returns the file size of `file_path`. If `file_path` is an Android-Sparse
 * file, returns the file size it would have after being converted to a raw
 * file.
 *
 * Android-Sparse is a file format invented by Android that optimizes for
 * chunks of zeroes or repeated data. The Android build system can produce
 * sparse files to save on size of disk files after they are extracted from a
 * disk file, as the imag eflashing process also can handle Android-Sparse
 * images.
 */
std::uint64_t UnsparsedSize(const std::string& file_path) {
  auto fd = open(file_path.c_str(), O_RDONLY);
  CHECK(fd >= 0) << "Could not open \"" << file_path << "\""
                 << strerror(errno);
  auto sparse = sparse_file_import(fd, /* verbose */ false, /* crc */ false);
  auto size =
      sparse ? sparse_file_len(sparse, false, true) : FileSize(file_path);
  close(fd);
  return size;
}

/*
 * strncpy equivalent for u16 data. GPT disks use UTF16-LE for disk labels.
 */
void u16cpy(std::uint16_t* dest, std::uint16_t* src, std::size_t size) {
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

MultipleImagePartition ToMultipleImagePartition(ImagePartition source) {
  return MultipleImagePartition{
      .label = source.label,
      .image_file_paths = std::vector{source.image_file_path},
      .type = source.type,
      .read_only = source.read_only,
  };
}

/**
 * Incremental builder class for producing partition tables. Add partitions
 * one-by-one, then produce specification files
 */
class CompositeDiskBuilder {
private:
  std::vector<PartitionInfo> partitions_;
  std::uint64_t next_disk_offset_;

  static const char* GetPartitionGUID(MultipleImagePartition source) {
    // Due to some endianness mismatch in e2fsprogs GUID vs GPT, the GUIDs are
    // rearranged to make the right GUIDs appear in gdisk
    switch (source.type) {
      case kLinuxFilesystem:
        // Technically 0FC63DAF-8483-4772-8E79-3D69D8477DE4
        return "AF3DC60F-8384-7247-8E79-3D69D8477DE4";
      case kEfiSystemPartition:
        // Technically C12A7328-F81F-11D2-BA4B-00A0C93EC93B
        return "28732AC1-1FF8-D211-BA4B-00A0C93EC93B";
      default:
        LOG(FATAL) << "Unknown partition type: " << (int) source.type;
    }
  }

public:
  CompositeDiskBuilder() : next_disk_offset_(sizeof(GptBeginning)) {}

  void AppendPartition(ImagePartition source) {
    AppendPartition(ToMultipleImagePartition(source));
  }

  void AppendPartition(MultipleImagePartition source) {
    uint64_t host_size = 0;
    for (const auto& path : source.image_file_paths) {
      host_size += UnsparsedSize(path);
    }
    auto guest_size = AlignToPowerOf2(host_size, PARTITION_SIZE_SHIFT);
    CHECK(host_size == guest_size || source.read_only)
        << "read-write partition " << source.label
        << " is not aligned to the size of " << (1 << PARTITION_SIZE_SHIFT);
    partitions_.push_back(PartitionInfo{
        .source = source,
        .guest_size = guest_size,
        .host_size = host_size,
        .offset = next_disk_offset_,
    });
    next_disk_offset_ =
        AlignToPowerOf2(next_disk_offset_ + guest_size, PARTITION_SIZE_SHIFT);
  }

  std::uint64_t DiskSize() const {
    std::uint64_t val = next_disk_offset_ + sizeof(GptEnd);
    return AlignToPowerOf2(val, DISK_SIZE_SHIFT);
  }

  /**
   * Generates a composite disk specification file, assuming that `header_file`
   * and `footer_file` will be populated with the contents of `Beginning()` and
   * `End()`.
   */
  CompositeDisk MakeCompositeDiskSpec(const std::string& header_file,
                                      const std::string& footer_file) const {
    CompositeDisk disk;
    disk.set_version(1);
    disk.set_length(DiskSize());

    ComponentDisk* header = disk.add_component_disks();
    header->set_file_path(AbsolutePath(header_file));
    header->set_offset(0);

    for (auto& partition : partitions_) {
      uint64_t host_size = 0;
      for (const auto& path : partition.source.image_file_paths) {
        ComponentDisk* component = disk.add_component_disks();
        component->set_file_path(AbsolutePath(path));
        component->set_offset(partition.offset + host_size);
        component->set_read_write_capability(
            partition.source.read_only ? ReadWriteCapability::READ_ONLY
                                       : ReadWriteCapability::READ_WRITE);
        host_size += UnsparsedSize(path);
      }
      CHECK(partition.host_size == host_size);
      // When partition's size differs from its size on the host
      // reading the disk within the guest os would fail due to the gap.
      // Putting any disk bigger than 4K can fill this gap.
      // Here we reuse the header which is always > 4K.
      // We don't fill the "writable" disk's hole and it should be an error
      // because writes in the guest of can't be reflected to the backing file.
      if (partition.guest_size != partition.host_size) {
        ComponentDisk* component = disk.add_component_disks();
        component->set_file_path(AbsolutePath(header_file));
        component->set_offset(partition.offset + partition.host_size);
        component->set_read_write_capability(ReadWriteCapability::READ_ONLY);
      }
    }

    ComponentDisk* footer = disk.add_component_disks();
    footer->set_file_path(AbsolutePath(footer_file));
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
  GptBeginning Beginning() const {
    if (partitions_.size() > GPT_NUM_PARTITIONS) {
      LOG(FATAL) << "Too many partitions: " << partitions_.size();
      return {};
    }
    GptBeginning gpt = {
      .protective_mbr = ProtectiveMbr(DiskSize()),
      .header = {
        .signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
        .revision = {0, 0, 1, 0},
        .header_size = sizeof(GptHeader),
        .current_lba = 1,
        .backup_lba = (next_disk_offset_ + sizeof(GptEnd)) / SECTOR_SIZE - 1,
        .first_usable_lba = sizeof(GptBeginning) / SECTOR_SIZE,
        .last_usable_lba = (next_disk_offset_ - SECTOR_SIZE) / SECTOR_SIZE,
        .partition_entries_lba = 2,
        .num_partition_entries = GPT_NUM_PARTITIONS,
        .partition_entry_size = sizeof(GptPartitionEntry),
      },
    };
    uuid_generate(gpt.header.disk_guid);
    for (std::size_t i = 0; i < partitions_.size(); i++) {
      const auto& partition = partitions_[i];
      gpt.entries[i] = GptPartitionEntry{
          .first_lba = partition.offset / SECTOR_SIZE,
          .last_lba =
              (partition.offset + partition.guest_size) / SECTOR_SIZE - 1,
      };
      uuid_generate(gpt.entries[i].unique_partition_guid);
      if (uuid_parse(GetPartitionGUID(partition.source),
                     gpt.entries[i].partition_type_guid)) {
        LOG(FATAL) << "Could not parse partition guid";
      }
      std::u16string wide_name(partitions_[i].source.label.begin(),
                              partitions_[i].source.label.end());
      u16cpy((std::uint16_t*) gpt.entries[i].partition_name,
            (std::uint16_t*) wide_name.c_str(), 36);
    }
    // Not sure these are right, but it works for bpttool
    gpt.header.partition_entries_crc32 =
        crc32(0, (std::uint8_t*) gpt.entries,
              GPT_NUM_PARTITIONS * sizeof(GptPartitionEntry));
    gpt.header.header_crc32 =
        crc32(0, (std::uint8_t*) &gpt.header, sizeof(GptHeader));
    return gpt;
  }

  /**
   * Generates a GUID Partition Table footer that matches the header in `head`.
   */
  GptEnd End(const GptBeginning& head) const {
    GptEnd gpt;
    std::memcpy((void*) gpt.entries, (void*) head.entries, 128 * 128);
    gpt.footer = head.header;
    gpt.footer.partition_entries_lba = next_disk_offset_ / SECTOR_SIZE;
    std::swap(gpt.footer.current_lba, gpt.footer.backup_lba);
    gpt.footer.header_crc32 = 0;
    gpt.footer.header_crc32 =
        crc32(0, (std::uint8_t*) &gpt.footer, sizeof(GptHeader));
    return gpt;
  }
};

bool WriteBeginning(SharedFD out, const GptBeginning& beginning) {
  std::string begin_str((const char*) &beginning, sizeof(GptBeginning));
  if (WriteAll(out, begin_str) != begin_str.size()) {
    LOG(ERROR) << "Could not write GPT beginning: " << out->StrError();
    return false;
  }
  return true;
}

bool WriteEnd(SharedFD out, const GptEnd& end, std::int64_t padding) {
  std::string end_str((const char*) &end, sizeof(GptEnd));
  end_str.resize(end_str.size() + padding, '\0');
  if (WriteAll(out, end_str) != end_str.size()) {
    LOG(ERROR) << "Could not write GPT end: " << out->StrError();
    return false;
  }
  return true;
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
void DeAndroidSparse(const std::vector<ImagePartition>& partitions) {
  for (const auto& partition : partitions) {
    auto fd = open(partition.image_file_path.c_str(), O_RDONLY);
    if (fd < 0) {
      PLOG(FATAL) << "Could not open \"" << partition.image_file_path;
      break;
    }
    auto sparse = sparse_file_import(fd, /* verbose */ false, /* crc */ false);
    if (!sparse) {
      close(fd);
      continue;
    }
    LOG(INFO) << "Desparsing " << partition.image_file_path;
    std::string out_file_name = partition.image_file_path + ".desparse";
    auto write_fd = open(out_file_name.c_str(), O_RDWR | O_CREAT | O_TRUNC,
                         S_IRUSR | S_IWUSR | S_IRGRP);
    if (write_fd < 0) {
      PLOG(FATAL) << "Could not open " << out_file_name;
    }
    int write_status = sparse_file_write(sparse, write_fd, /* gz */ false,
                                         /* sparse */ false, /* crc */ false);
    if (write_status < 0) {
      LOG(FATAL) << "Failed to desparse \"" << partition.image_file_path
                 << "\": " << write_status;
    }
    close(write_fd);
    if (rename(out_file_name.c_str(), partition.image_file_path.c_str()) < 0) {
      int error_num = errno;
      LOG(FATAL) << "Could not move \"" << out_file_name << "\" to \""
                 << partition.image_file_path << "\": " << strerror(error_num);
    }
    sparse_file_destroy(sparse);
    close(fd);
  }
}

} // namespace

uint64_t AlignToPartitionSize(uint64_t size) {
  return AlignToPowerOf2(size, PARTITION_SIZE_SHIFT);
}

void AggregateImage(const std::vector<ImagePartition>& partitions,
                    const std::string& output_path) {
  DeAndroidSparse(partitions);
  CompositeDiskBuilder builder;
  for (auto& partition : partitions) {
    builder.AppendPartition(partition);
  }
  auto output = SharedFD::Creat(output_path, 0600);
  auto beginning = builder.Beginning();
  if (!WriteBeginning(output, beginning)) {
    LOG(FATAL) << "Could not write GPT beginning to \"" << output_path
               << "\": " << output->StrError();
  }
  for (auto& disk : partitions) {
    auto disk_fd = SharedFD::Open(disk.image_file_path, O_RDONLY);
    auto file_size = FileSize(disk.image_file_path);
    if (!output->CopyFrom(*disk_fd, file_size)) {
      LOG(FATAL) << "Could not copy from \"" << disk.image_file_path
                 << "\" to \"" << output_path << "\": " << output->StrError();
    }
    // Handle disk images that are not aligned to PARTITION_SIZE_SHIFT
    std::uint64_t padding =
        AlignToPowerOf2(file_size, PARTITION_SIZE_SHIFT) - file_size;
    std::string padding_str;
    padding_str.resize(padding, '\0');
    if (WriteAll(output, padding_str) != padding_str.size()) {
      LOG(FATAL) << "Could not write partition padding to \"" << output_path
                 << "\": " << output->StrError();
    }
  }
  std::uint64_t padding =
      builder.DiskSize() - ((beginning.header.backup_lba + 1) * SECTOR_SIZE);
  if (!WriteEnd(output, builder.End(beginning), padding)) {
    LOG(FATAL) << "Could not write GPT end to \"" << output_path
               << "\": " << output->StrError();
  }
};

void CreateCompositeDisk(std::vector<ImagePartition> partitions,
                         const std::string& header_file,
                         const std::string& footer_file,
                         const std::string& output_composite_path) {
  std::vector<MultipleImagePartition> multiple_image_partitions;
  for (const auto& partition : partitions) {
    multiple_image_partitions.push_back(ToMultipleImagePartition(partition));
  }
  return CreateCompositeDisk(std::move(multiple_image_partitions), header_file,
                             footer_file, output_composite_path);
}

void CreateCompositeDisk(std::vector<MultipleImagePartition> partitions,
                         const std::string& header_file,
                         const std::string& footer_file,
                         const std::string& output_composite_path) {
  CompositeDiskBuilder builder;
  for (auto& partition : partitions) {
    builder.AppendPartition(partition);
  }
  auto header = SharedFD::Creat(header_file, 0600);
  auto beginning = builder.Beginning();
  if (!WriteBeginning(header, beginning)) {
    LOG(FATAL) << "Could not write GPT beginning to \"" << header_file
               << "\": " << header->StrError();
  }
  auto footer = SharedFD::Creat(footer_file, 0600);
  std::uint64_t padding =
      builder.DiskSize() - ((beginning.header.backup_lba + 1) * SECTOR_SIZE);
  if (!WriteEnd(footer, builder.End(beginning), padding)) {
    LOG(FATAL) << "Could not write GPT end to \"" << footer_file
               << "\": " << footer->StrError();
  }
  auto composite_proto = builder.MakeCompositeDiskSpec(header_file, footer_file);
  std::ofstream composite(output_composite_path.c_str(),
                          std::ios::binary | std::ios::trunc);
  composite << "composite_disk\x1d";
  composite_proto.SerializeToOstream(&composite);
  composite.flush();
}

void CreateQcowOverlay(const std::string& crosvm_path,
                       const std::string& backing_file,
                       const std::string& output_overlay_path) {
  Command crosvm_qcow2_cmd(crosvm_path);
  crosvm_qcow2_cmd.AddParameter("create_qcow2");
  crosvm_qcow2_cmd.AddParameter("--backing_file=", backing_file);
  crosvm_qcow2_cmd.AddParameter(output_overlay_path);
  int success = crosvm_qcow2_cmd.Start().Wait();
  if (success != 0) {
    LOG(FATAL) << "Unable to run crosvm create_qcow2. Exited with status " << success;
  }
}

} // namespace cuttlefish
