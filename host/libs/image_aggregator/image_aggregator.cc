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
#include <android-base/strings.h>
#include <cdisk_spec.pb.h>
#include <google/protobuf/text_format.h>
#include <sparse/sparse.h>
#include <uuid.h>
#include <zlib.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/cf_endian.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/mbr.h"
#include "host/libs/image_aggregator/sparse_image_utils.h"

namespace cuttlefish {
namespace {

constexpr int GPT_NUM_PARTITIONS = 128;
static const std::string CDISK_MAGIC = "composite_disk\x1d";
static const std::string QCOW2_MAGIC = "QFI\xfb";

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
  std::uint8_t header_padding[SECTOR_SIZE - sizeof(GptHeader)];
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  std::uint8_t partition_alignment[3072];
};

static_assert(AlignToPowerOf2(sizeof(GptBeginning), PARTITION_SIZE_SHIFT) ==
              sizeof(GptBeginning));

struct __attribute__((packed)) GptEnd {
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  GptHeader footer;
  std::uint8_t footer_padding[SECTOR_SIZE - sizeof(GptHeader)];
};

static_assert(sizeof(GptEnd) % SECTOR_SIZE == 0);

struct PartitionInfo {
  MultipleImagePartition source;
  std::uint64_t size;
  std::uint64_t offset;

  std::uint64_t AlignedSize() const { return AlignToPartitionSize(size); }
};

struct __attribute__((packed)) QCowHeader {
  Be32 magic;
  Be32 version;
  Be64 backing_file_offset;
  Be32 backing_file_size;
  Be32 cluster_bits;
  Be64 size;
  Be32 crypt_method;
  Be32 l1_size;
  Be64 l1_table_offset;
  Be64 refcount_table_offset;
  Be32 refcount_table_clusters;
  Be32 nb_snapshots;
  Be64 snapshots_offset;
};

static_assert(sizeof(QCowHeader) == 72);

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
std::uint64_t ExpandedStorageSize(const std::string& file_path) {
  android::base::unique_fd fd(open(file_path.c_str(), O_RDONLY));
  CHECK(fd.get() >= 0) << "Could not open \"" << file_path << "\""
                       << strerror(errno);

  std::uint64_t file_size = FileSize(file_path);

  // Try to read the disk in a nicely-aligned block size unless the whole file
  // is smaller.
  constexpr uint64_t MAGIC_BLOCK_SIZE = 4096;
  std::string magic(std::min(file_size, MAGIC_BLOCK_SIZE), '\0');
  if (!android::base::ReadFully(fd, magic.data(), magic.size())) {
    PLOG(FATAL) << "Fail to read: " << file_path;
    return 0;
  }
  CHECK(lseek(fd, 0, SEEK_SET) != -1)
      << "Fail to seek(\"" << file_path << "\")" << strerror(errno);

  // Composite disk image
  if (android::base::StartsWith(magic, CDISK_MAGIC)) {
    // seek to the beginning of proto message
    CHECK(lseek(fd, CDISK_MAGIC.size(), SEEK_SET) != -1)
        << "Fail to seek(\"" << file_path << "\")" << strerror(errno);
    std::string message;
    if (!android::base::ReadFdToString(fd, &message)) {
      PLOG(FATAL) << "Fail to read(cdisk): " << file_path;
      return 0;
    }
    CompositeDisk cdisk;
    if (!cdisk.ParseFromString(message)) {
      PLOG(FATAL) << "Fail to parse(cdisk): " << file_path;
      return 0;
    }
    return cdisk.length();
  }

  // Qcow2 image
  if (android::base::StartsWith(magic, QCOW2_MAGIC)) {
    QCowHeader header;
    if (!android::base::ReadFully(fd, &header, sizeof(QCowHeader))) {
      PLOG(FATAL) << "Fail to read(qcow2 header): " << file_path;
      return 0;
    }
    return header.size.as_uint64_t();
  }

  // Android-Sparse
  if (auto sparse =
          sparse_file_import(fd, /* verbose */ false, /* crc */ false);
      sparse) {
    auto size = sparse_file_len(sparse, false, true);
    sparse_file_destroy(sparse);
    return size;
  }

  // raw image file
  return file_size;
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
    uint64_t size = 0;
    for (const auto& path : source.image_file_paths) {
      size += ExpandedStorageSize(path);
    }
    auto aligned_size = AlignToPartitionSize(size);
    CHECK(size == aligned_size || source.read_only)
        << "read-write partition " << source.label
        << " is not aligned to the size of " << (1 << PARTITION_SIZE_SHIFT);
    partitions_.push_back(PartitionInfo{
        .source = source,
        .size = size,
        .offset = next_disk_offset_,
    });
    next_disk_offset_ = next_disk_offset_ + aligned_size;
  }

  std::uint64_t DiskSize() const {
    return AlignToPowerOf2(next_disk_offset_ + sizeof(GptEnd), DISK_SIZE_SHIFT);
  }

  /**
   * Generates a composite disk specification file, assuming that `header_file`
   * and `footer_file` will be populated with the contents of `Beginning()` and
   * `End()`.
   */
  CompositeDisk MakeCompositeDiskSpec(const std::string& header_file,
                                      const std::string& footer_file) const {
    CompositeDisk disk;
    disk.set_version(2);
    disk.set_length(DiskSize());

    ComponentDisk* header = disk.add_component_disks();
    header->set_file_path(header_file);
    header->set_offset(0);

    for (auto& partition : partitions_) {
      uint64_t size = 0;
      for (const auto& path : partition.source.image_file_paths) {
        ComponentDisk* component = disk.add_component_disks();
        component->set_file_path(path);
        component->set_offset(partition.offset + size);
        component->set_read_write_capability(
            partition.source.read_only ? ReadWriteCapability::READ_ONLY
                                       : ReadWriteCapability::READ_WRITE);
        size += ExpandedStorageSize(path);
      }
      CHECK(partition.size == size);
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
  GptBeginning Beginning() const {
    if (partitions_.size() > GPT_NUM_PARTITIONS) {
      LOG(FATAL) << "Too many partitions: " << partitions_.size();
      return {};
    }
    GptBeginning gpt = {
        .protective_mbr = ProtectiveMbr(DiskSize()),
        .header =
            {
                .signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
                .revision = {0, 0, 1, 0},
                .header_size = sizeof(GptHeader),
                .current_lba = 1,
                .backup_lba = (DiskSize() / SECTOR_SIZE) - 1,
                .first_usable_lba = sizeof(GptBeginning) / SECTOR_SIZE,
                .last_usable_lba = (next_disk_offset_ / SECTOR_SIZE) - 1,
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
              (partition.offset + partition.AlignedSize()) / SECTOR_SIZE - 1,
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
    std::memcpy((void*)gpt.entries, (void*)head.entries, sizeof(gpt.entries));
    gpt.footer = head.header;
    gpt.footer.partition_entries_lba =
        (DiskSize() - sizeof(gpt.entries)) / SECTOR_SIZE - 1;
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

bool WriteEnd(SharedFD out, const GptEnd& end) {
  auto disk_size = (end.footer.current_lba + 1) * SECTOR_SIZE;
  auto footer_start = (end.footer.last_usable_lba + 1) * SECTOR_SIZE;
  auto padding = disk_size - footer_start - sizeof(GptEnd);
  std::string padding_str(padding, '\0');
  if (WriteAll(out, padding_str) != padding_str.size()) {
    LOG(ERROR) << "Could not write GPT end padding: " << out->StrError();
    return false;
  }
  if (WriteAllBinary(out, &end) != sizeof(end)) {
    LOG(ERROR) << "Could not write GPT end contents: " << out->StrError();
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
    if (!ConvertToRawImage(partition.image_file_path)) {
      LOG(FATAL) << "Failed to desparse " << partition.image_file_path;
    }
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
    std::uint64_t padding = AlignToPartitionSize(file_size) - file_size;
    std::string padding_str;
    padding_str.resize(padding, '\0');
    if (WriteAll(output, padding_str) != padding_str.size()) {
      LOG(FATAL) << "Could not write partition padding to \"" << output_path
                 << "\": " << output->StrError();
    }
  }
  if (!WriteEnd(output, builder.End(beginning))) {
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
  if (!WriteEnd(footer, builder.End(beginning))) {
    LOG(FATAL) << "Could not write GPT end to \"" << footer_file
               << "\": " << footer->StrError();
  }
  auto composite_proto = builder.MakeCompositeDiskSpec(header_file, footer_file);
  std::ofstream composite(output_composite_path.c_str(),
                          std::ios::binary | std::ios::trunc);
  composite << CDISK_MAGIC;
  composite_proto.SerializeToOstream(&composite);
  composite.flush();
}

void CreateQcowOverlay(const std::string& crosvm_path,
                       const std::string& backing_file,
                       const std::string& output_overlay_path) {
  Command cmd(crosvm_path);
  cmd.AddParameter("create_qcow2");
  cmd.AddParameter("--backing-file");
  cmd.AddParameter(backing_file);
  cmd.AddParameter(output_overlay_path);

  std::string stdout_str;
  std::string stderr_str;
  int success =
      RunWithManagedStdio(std::move(cmd), nullptr, &stdout_str, &stderr_str);

  if (success != 0) {
    LOG(ERROR) << "Failed to run `" << crosvm_path
               << " create_qcow2 --backing-file " << backing_file << " "
               << output_overlay_path << "`";
    LOG(ERROR) << "stdout:\n###\n" << stdout_str << "\n###";
    LOG(ERROR) << "stderr:\n###\n" << stderr_str << "\n###";
    LOG(FATAL) << "Return code: \"" << success << "\"";
  }
}

} // namespace cuttlefish
