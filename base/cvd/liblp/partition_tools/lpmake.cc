/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef WIN32
#include <sysexits.h>
#endif
#include <unistd.h>

#include <algorithm>
#include <memory>

#include <android-base/parseint.h>
#include <android-base/result.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <liblp/builder.h>
#include <liblp/liblp.h>

using namespace android;
using namespace android::fs_mgr;

using android::base::Error;
using android::base::Result;
using android::base::unique_fd;

#ifdef WIN32
static constexpr int EX_OK = 0;
static constexpr int EX_USAGE = 1;
static constexpr int EX_SOFTWARE = 2;
static constexpr int EX_CANTCREAT = 3;
#else
static constexpr int O_BINARY = 0;
#endif

/* Prints program usage to |where|. */
static int usage(int /* argc */, char* argv[]) {
    fprintf(stderr,
            "%s - command-line tool for creating Android Logical Partition images.\n"
            "\n"
            "Usage:\n"
            "  %s [options]\n"
            "\n"
            "Required options:\n"
            "  -d,--device-size=[SIZE|auto]  Size of the block device for logical partitions.\n"
            "                                Can be set to auto to automatically calculate the\n"
            "                                minimum size, the sum of partition sizes plus\n"
            "                                metadata-size times the number of partitions.\n"
            "  -m,--metadata-size=SIZE       Maximum size to reserve for partition metadata.\n"
            "  -s,--metadata-slots=COUNT     Number of slots to store metadata copies.\n"
            "  -p,--partition=DATA           Add a partition given the data, see below.\n"
            "  -o,--output=FILE              Output file.\n"
            "\n"
            "Optional:\n"
            "  -b,--block-size=SIZE          Physical block size, defaults to 4096.\n"
            "  -a,--alignment=N              Optimal partition alignment in bytes.\n"
            "  -O,--alignment-offset=N       Alignment offset in bytes to device parent.\n"
            "  -S,--sparse                   Output a sparse image for fastboot.\n"
            "  -i,--image=PARTITION=FILE     If building a sparse image for fastboot, include\n"
            "                                the given file (or sparse file) as initial data for\n"
            "                                the named partition.\n"
            "  -g,--group=GROUP:SIZE         Define a named partition group with the given\n"
            "                                maximum size.\n"
            "  -D,--device=DATA              Add a block device that the super partition\n"
            "                                spans over. If specified, then -d/--device-size\n"
            "                                and alignments must not be specified. The format\n"
            "                                for DATA is listed below.\n"
            "  -n,--super-name=NAME          Specify the name of the block device that will\n"
            "                                house the super partition.\n"
            "  -x,--auto-slot-suffixing      Mark the block device and partition names needing\n"
            "                                slot suffixes before being used.\n"
            "  -F,--force-full-image         Force a full image to be written even if no\n"
            "                                partition images were specified. Normally, this\n"
            "                                would produce a minimal super_empty.img which\n"
            "                                cannot be flashed; force-full-image will produce\n"
            "                                a flashable image.\n"
            "  --virtual-ab                  Add the VIRTUAL_AB_DEVICE flag to the metadata\n"
            "                                header. Note that the resulting super.img will\n"
            "                                require a liblp capable of parsing a v1.2 header.\n"
            "\n"
            "Partition data format:\n"
            "  <name>:<attributes>:<size>[:group]\n"
            "  Attrs must be 'none' or 'readonly'.\n"
            "\n"
            "Device data format:\n"
            "  <partition_name>:<size>[:<alignment>:<alignment_offset>]\n"
            "  The partition name is the basename of the /dev/block/by-name/ path of the\n"
            "  block device. The size is the device size in bytes. The alignment and\n"
            "  alignment offset parameters are the same as -a/--alignment and \n"
            "  -O/--alignment-offset.\n",
            argv[0], argv[0]);
    return EX_USAGE;
}

enum class Option : int {
    // Long-only options.
    kVirtualAB = 1,

    // Short character codes.
    kDeviceSize = 'd',
    kMetadataSize = 'm',
    kMetadataSlots = 's',
    kPartition = 'p',
    kOutput = 'o',
    kHelp = 'h',
    kAlignmentOffset = 'O',
    kAlignment = 'a',
    kSparse = 'S',
    kBlockSize = 'b',
    kImage = 'i',
    kGroup = 'g',
    kDevice = 'D',
    kSuperName = 'n',
    kAutoSlotSuffixing = 'x',
    kForceFullImage = 'F',
};

struct PartitionInfo {
    std::string name;
    uint64_t size;
    uint32_t attribute_flags;
    std::string group_name;

    static Result<PartitionInfo> Parse(const char* arg) {
        std::vector<std::string> parts = android::base::Split(arg, ":");
        if (parts.size() > 4) {
            return Error() << "Partition info has invalid formatting.";
        }

        std::string name = parts[0];
        if (name.empty()) {
            return Error() << "Partition must have a valid name.";
        }

        uint64_t size;
        if (!android::base::ParseUint(parts[2].c_str(), &size)) {
            return Error() << "Partition must have a valid size.";
        }

        uint32_t attribute_flags = 0;
        std::string attributes = parts[1];
        if (attributes == "readonly") {
            attribute_flags |= LP_PARTITION_ATTR_READONLY;
        } else if (attributes != "none") {
            return Error() << "Attribute not recognized: " << attributes;
        }

        std::string group_name = "default";
        if (parts.size() >= 4) {
            group_name = parts[3];
        }

        return PartitionInfo{name, size, attribute_flags, group_name};
    }
};

static uint64_t CalculateBlockDeviceSize(uint32_t alignment, uint32_t metadata_size,
                                         uint32_t metadata_slots,
                                         const std::vector<PartitionInfo>& partitions) {
    uint64_t ret = LP_PARTITION_RESERVED_BYTES;
    ret += LP_METADATA_GEOMETRY_SIZE * 2;

    // Each metadata slot has a primary and backup copy.
    ret += metadata_slots * metadata_size * 2;

    if (alignment) {
        uint64_t remainder = ret % alignment;
        uint64_t to_add = alignment - remainder;
        if (to_add > std::numeric_limits<uint64_t>::max() - ret) {
            return 0;
        }
        ret += to_add;
    }

    ret += partitions.size() * alignment;
    for (const auto& partition_info : partitions) {
        ret += partition_info.size;
    }
    return ret;
}

static bool GetFileSize(const std::string& path, uint64_t* size) {
    unique_fd fd(open(path.c_str(), O_RDONLY | O_BINARY));
    if (fd < 0) {
        fprintf(stderr, "Could not open file: %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }

    auto offs = lseek(fd.get(), 0, SEEK_END);
    if (offs < 0) {
        fprintf(stderr, "Failed to seek file: %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }

    *size = offs;
    return true;
}

int main(int argc, char* argv[]) {
    struct option options[] = {
        { "device-size", required_argument, nullptr, (int)Option::kDeviceSize },
        { "metadata-size", required_argument, nullptr, (int)Option::kMetadataSize },
        { "metadata-slots", required_argument, nullptr, (int)Option::kMetadataSlots },
        { "partition", required_argument, nullptr, (int)Option::kPartition },
        { "output", required_argument, nullptr, (int)Option::kOutput },
        { "help", no_argument, nullptr, (int)Option::kHelp },
        { "alignment-offset", required_argument, nullptr, (int)Option::kAlignmentOffset },
        { "alignment", required_argument, nullptr, (int)Option::kAlignment },
        { "sparse", no_argument, nullptr, (int)Option::kSparse },
        { "block-size", required_argument, nullptr, (int)Option::kBlockSize },
        { "image", required_argument, nullptr, (int)Option::kImage },
        { "group", required_argument, nullptr, (int)Option::kGroup },
        { "device", required_argument, nullptr, (int)Option::kDevice },
        { "super-name", required_argument, nullptr, (int)Option::kSuperName },
        { "auto-slot-suffixing", no_argument, nullptr, (int)Option::kAutoSlotSuffixing },
        { "force-full-image", no_argument, nullptr, (int)Option::kForceFullImage },
        { "virtual-ab", no_argument, nullptr, (int)Option::kVirtualAB },
        { nullptr, 0, nullptr, 0 },
    };

    uint64_t blockdevice_size = 0;
    uint32_t metadata_size = 0;
    uint32_t metadata_slots = 0;
    uint32_t alignment_offset = 0;
    uint32_t alignment = kDefaultPartitionAlignment;
    uint32_t block_size = 4096;
    std::string super_name = "super";
    std::string output_path;
    std::vector<PartitionInfo> partitions;
    std::vector<std::string> groups;
    std::vector<BlockDeviceInfo> block_devices;
    std::map<std::string, std::string> images;
    bool output_sparse = false;
    bool has_implied_super = false;
    bool force_full_image = false;
    bool virtual_ab = false;
    bool auto_blockdevice_size = false;

    int rv;
    int index;
    while ((rv = getopt_long_only(argc, argv, "d:m:s:p:o:h:FSx", options, &index)) != -1) {
        switch ((Option)rv) {
            case Option::kHelp:
                return usage(argc, argv);
            case Option::kDeviceSize:
                if (strcmp(optarg, "auto") == 0) {
                    auto_blockdevice_size = true;
                } else if (!android::base::ParseUint(optarg, &blockdevice_size) ||
                           !blockdevice_size) {
                    fprintf(stderr, "Invalid argument to --device-size.\n");
                    return EX_USAGE;
                }
                has_implied_super = true;
                break;
            case Option::kMetadataSize:
                if (!android::base::ParseUint(optarg, &metadata_size)) {
                    fprintf(stderr, "Invalid argument to --metadata-size.\n");
                    return EX_USAGE;
                }
                break;
            case Option::kMetadataSlots:
                if (!android::base::ParseUint(optarg, &metadata_slots)) {
                    fprintf(stderr, "Invalid argument to --metadata-slots.\n");
                    return EX_USAGE;
                }
                break;
            case Option::kPartition:
                if (auto res = PartitionInfo::Parse(optarg); !res.ok()) {
                    fprintf(stderr, "%s\n", res.error().message().c_str());
                    return EX_USAGE;
                } else {
                    partitions.push_back(std::move(*res));
                }
                break;
            case Option::kGroup:
                groups.push_back(optarg);
                break;
            case Option::kOutput:
                output_path = optarg;
                break;
            case Option::kAlignmentOffset:
                if (!android::base::ParseUint(optarg, &alignment_offset)) {
                    fprintf(stderr, "Invalid argument to --alignment-offset.\n");
                    return EX_USAGE;
                }
                has_implied_super = true;
                break;
            case Option::kAlignment:
                if (!android::base::ParseUint(optarg, &alignment)) {
                    fprintf(stderr, "Invalid argument to --alignment.\n");
                    return EX_USAGE;
                }
                has_implied_super = true;
                break;
            case Option::kSparse:
                output_sparse = true;
                break;
            case Option::kBlockSize:
                if (!android::base::ParseUint(optarg, &block_size) || !block_size) {
                    fprintf(stderr, "Invalid argument to --block-size.\n");
                    return EX_USAGE;
                }
                break;
            case Option::kImage:
            {
                char* separator = strchr(optarg, '=');
                if (!separator || separator == optarg || !strlen(separator + 1)) {
                    fprintf(stderr, "Expected PARTITION=FILE.\n");
                    return EX_USAGE;
                }
                *separator = '\0';

                std::string partition_name(optarg);
                std::string file(separator + 1);
                images[partition_name] = file;
                break;
            }
            case Option::kSuperName:
                super_name = optarg;
                break;
            case Option::kDevice:
            {
                std::vector<std::string> parts = android::base::Split(optarg, ":");
                if (parts.size() < 2) {
                    fprintf(stderr, "Block device info has invalid formatting.\n");
                    return EX_USAGE;
                }

                BlockDeviceInfo info;
                info.partition_name = parts[0];
                if (!android::base::ParseUint(parts[1].c_str(), &info.size) || !info.size) {
                    fprintf(stderr, "Block device must have a valid size.\n");
                    return EX_USAGE;
                }
                info.alignment = kDefaultPartitionAlignment;
                if (parts.size() >= 3 &&
                    !android::base::ParseUint(parts[2].c_str(), &info.alignment)) {
                    fprintf(stderr, "Block device must have a valid alignment.\n");
                    return EX_USAGE;
                }
                if (parts.size() >= 4 &&
                    !android::base::ParseUint(parts[3].c_str(), &info.alignment_offset)) {
                    fprintf(stderr, "Block device must have a valid alignment offset.\n");
                    return EX_USAGE;
                }
                block_devices.emplace_back(info);
                break;
            }
            case Option::kAutoSlotSuffixing:
                fprintf(stderr, "Auto slot suffixing is no longer supported.\n");
                return EX_USAGE;
            case Option::kForceFullImage:
                force_full_image = true;
                break;
            case Option::kVirtualAB:
                virtual_ab = true;
                break;
            default:
                break;
        }
    }

    // Check for empty arguments so we can print a more helpful message rather
    // than error on each individual missing argument.
    if (optind == 1) {
        return usage(argc, argv);
    }

    if (auto_blockdevice_size) {
        blockdevice_size =
                CalculateBlockDeviceSize(alignment, metadata_size, metadata_slots, partitions);
        if (!blockdevice_size) {
            fprintf(stderr, "Invalid block device parameters.\n");
            return EX_USAGE;
        }
    }

    // Must specify a block device via the old method (--device-size etc) or
    // via --device, but not both.
    if ((has_implied_super && (!block_devices.empty() || !blockdevice_size)) ||
        (!has_implied_super && block_devices.empty()) ||
        (block_devices.empty() && !blockdevice_size)) {
        fprintf(stderr, "Must specify --device OR --device-size.\n");
        return EX_USAGE;
    }
    if (!metadata_size) {
        fprintf(stderr, "--metadata-size must be more than 0 bytes.\n");
        return EX_USAGE;
    }
    if (!metadata_slots) {
        fprintf(stderr, "--metadata-slots must be more than 0.\n");
        return EX_USAGE;
    }
    if (output_path.empty()) {
        fprintf(stderr, "--output must specify a valid path.\n");
        return EX_USAGE;
    }
    if (partitions.empty()) {
        fprintf(stderr, "Partition table must have at least one entry.\n");
        return EX_USAGE;
    }

    // Note that we take the block_size to mean both the logical block size and
    // the block size for libsparse.
    if (has_implied_super) {
        block_devices.emplace_back(super_name, blockdevice_size, alignment, alignment_offset, block_size);
    } else {
        // Apply the block size to each device.
        for (auto& block_device : block_devices) {
            block_device.logical_block_size = block_size;
        }
    }

    std::unique_ptr<MetadataBuilder> builder =
            MetadataBuilder::New(block_devices, super_name, metadata_size, metadata_slots);
    if (!builder) {
        fprintf(stderr, "Invalid metadata parameters.\n");
        return EX_USAGE;
    }

    if (virtual_ab) {
        builder->SetVirtualABDeviceFlag();
    }

    for (const auto& group_info : groups) {
        std::vector<std::string> parts = android::base::Split(group_info, ":");
        if (parts.size() != 2) {
            fprintf(stderr, "Partition info has invalid formatting.\n");
            return EX_USAGE;
        }

        std::string name = parts[0];
        if (name.empty()) {
            fprintf(stderr, "Partition group must have a valid name.\n");
            return EX_USAGE;
        }

        uint64_t size;
        if (!android::base::ParseUint(parts[1].c_str(), &size)) {
            fprintf(stderr, "Partition group must have a valid maximum size.\n");
            return EX_USAGE;
        }

        if (!builder->AddGroup(name, size)) {
            fprintf(stderr, "Group name %s already exists.\n", name.c_str());
            return EX_SOFTWARE;
        }
    }

    for (auto& partition_info : partitions) {
        Partition* partition = builder->AddPartition(partition_info.name, partition_info.group_name,
                                                     partition_info.attribute_flags);
        if (!partition) {
            fprintf(stderr, "Could not add partition: %s\n", partition_info.name.c_str());
            return EX_SOFTWARE;
        }
        if (!partition_info.size) {
            // Deduce the size automatically.
            if (auto iter = images.find(partition_info.name); iter != images.end()) {
                if (!GetFileSize(iter->second, &partition_info.size)) {
                    return EX_SOFTWARE;
                }
            }
        }
        if (!builder->ResizePartition(partition, partition_info.size)) {
            fprintf(stderr, "Not enough space on device for partition %s with size %" PRIu64 "\n",
                    partition_info.name.c_str(), partition_info.size);
            return EX_SOFTWARE;
        }
    }

    // unlink before writing, in case it is being used by an emulator or other program,
    // we don't want to break that program by changing the data it is accessing.
    unlink(output_path.c_str());

    std::unique_ptr<LpMetadata> metadata = builder->Export();
    if (!images.empty() || force_full_image) {
        if (block_devices.size() == 1) {
            if (!WriteToImageFile(output_path.c_str(), *metadata.get(), block_size, images,
                                  output_sparse)) {
                return EX_CANTCREAT;
            }
        } else {
            if (!WriteSplitImageFiles(output_path, *metadata.get(), block_size, images,
                                      output_sparse)) {
                return EX_CANTCREAT;
            }
        }
    } else if (!WriteToImageFile(output_path.c_str(), *metadata.get())) {
        return EX_CANTCREAT;
    }
    return EX_OK;
}
