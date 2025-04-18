/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "avb_utils.h"

#include <android-base/strings.h>
#include <libavb/libavb.h>

#include "fec_private.h"

int parse_vbmeta_from_footer(fec_handle *f, std::vector<uint8_t> *vbmeta) {
    if (f->size <= AVB_FOOTER_SIZE) {
        debug("file size not large enough to be avb images:" PRIu64, f->size);
        return -1;
    }

    AvbFooter footer_read;
    if (!raw_pread(f->fd, &footer_read, AVB_FOOTER_SIZE,
                   f->size - AVB_FOOTER_SIZE)) {
        error("failed to read footer: %s", strerror(errno));
        return -1;
    }

    AvbFooter footer;
    if (!avb_footer_validate_and_byteswap(&footer_read, &footer)) {
        debug("invalid avb footer");
        return -1;
    }
    uint64_t vbmeta_offset = footer.vbmeta_offset;
    uint64_t vbmeta_size = footer.vbmeta_size;
    check(vbmeta_offset <= f->size - sizeof(footer) - vbmeta_size);

    std::vector<uint8_t> vbmeta_data(vbmeta_size, 0);
    // TODO(xunchang) handle the sparse image with libsparse.
    if (!raw_pread(f->fd, vbmeta_data.data(), vbmeta_data.size(),
                   vbmeta_offset)) {
        error("failed to read avb vbmeta: %s", strerror(errno));
        return -1;
    }

    if (auto status = avb_vbmeta_image_verify(
            vbmeta_data.data(), vbmeta_data.size(), nullptr, nullptr);
        status != AVB_VBMETA_VERIFY_RESULT_OK &&
        status != AVB_VBMETA_VERIFY_RESULT_OK_NOT_SIGNED) {
        error("failed to verify avb vbmeta, status: %d", status);
        return -1;
    }
    *vbmeta = std::move(vbmeta_data);
    return 0;
}

int parse_avb_image(fec_handle *f, const std::vector<uint8_t> &vbmeta) {
    // TODO(xunchang) check if avb verification or hashtree is disabled.

    // Look for the hashtree descriptor, we expect exactly one descriptor in
    // vbmeta.
    // TODO(xunchang) handle the image with AvbHashDescriptor.
    auto parse_descriptor = [](const AvbDescriptor *descriptor,
                               void *user_data) {
        if (descriptor &&
            avb_be64toh(descriptor->tag) == AVB_DESCRIPTOR_TAG_HASHTREE) {
            auto desp = static_cast<const AvbDescriptor **>(user_data);
            *desp = descriptor;
            return false;
        }
        return true;
    };

    const AvbHashtreeDescriptor *hashtree_descriptor_ptr = nullptr;
    avb_descriptor_foreach(vbmeta.data(), vbmeta.size(), parse_descriptor,
                           &hashtree_descriptor_ptr);
    if (!hashtree_descriptor_ptr) {
        error("failed to find avb hashtree descriptor");
        return -1;
    }

    AvbHashtreeDescriptor hashtree_descriptor;
    if (!avb_hashtree_descriptor_validate_and_byteswap(hashtree_descriptor_ptr,
                                                       &hashtree_descriptor)) {
        error("failed to verify avb hashtree descriptor");
        return -1;
    }

    // The partition name, salt, root append right after the hashtree
    // descriptor.
    auto read_ptr = reinterpret_cast<const uint8_t *>(hashtree_descriptor_ptr);
    // Calculate the offset with respect to the vbmeta; and check both the
    // salt & root are within the range.
    uint32_t salt_offset =
        sizeof(AvbHashtreeDescriptor) + hashtree_descriptor.partition_name_len;
    uint32_t root_offset = salt_offset + hashtree_descriptor.salt_len;
    check(hashtree_descriptor.salt_len < vbmeta.size());
    check(salt_offset < vbmeta.size() - hashtree_descriptor.salt_len);
    check(hashtree_descriptor.root_digest_len < vbmeta.size());
    check(root_offset < vbmeta.size() - hashtree_descriptor.root_digest_len);
    std::vector<uint8_t> salt(
        read_ptr + salt_offset,
        read_ptr + salt_offset + hashtree_descriptor.salt_len);
    std::vector<uint8_t> root_hash(
        read_ptr + root_offset,
        read_ptr + root_offset + hashtree_descriptor.root_digest_len);

    // Expect the AVB image has the format:
    // 1. hashtree
    // 2. ecc data
    // 3. vbmeta
    // 4. avb footer
    check(hashtree_descriptor.fec_offset ==
          hashtree_descriptor.tree_offset + hashtree_descriptor.tree_size);
    check(hashtree_descriptor.fec_offset <=
          f->size - hashtree_descriptor.fec_size);

    f->data_size = hashtree_descriptor.fec_offset;

    f->ecc.blocks = fec_div_round_up(f->data_size, FEC_BLOCKSIZE);
    f->ecc.rounds = fec_div_round_up(f->ecc.blocks, f->ecc.rsn);
    f->ecc.size = hashtree_descriptor.fec_size;
    f->ecc.start = hashtree_descriptor.fec_offset;
    // TODO(xunchang) verify the integrity of the ecc data.
    f->ecc.valid = true;

    std::string hash_algorithm =
        reinterpret_cast<char *>(hashtree_descriptor.hash_algorithm);
    int nid = -1;
    if (android::base::EqualsIgnoreCase(hash_algorithm, "sha1")) {
        nid = NID_sha1;
    } else if (android::base::EqualsIgnoreCase(hash_algorithm, "sha256")) {
        nid = NID_sha256;
    } else {
        error("unsupported hash algorithm %s", hash_algorithm.c_str());
    }

    hashtree_info hashtree;
    hashtree.initialize(hashtree_descriptor.tree_offset,
                        hashtree_descriptor.tree_offset / FEC_BLOCKSIZE, salt,
                        nid);
    if (hashtree.verify_tree(f, root_hash.data()) != 0) {
        error("failed to verify hashtree");
        return -1;
    }

    // We have validate the hashtree,
    f->data_size = hashtree.hash_start;
    f->avb = {
        .valid = true,
        .vbmeta = vbmeta,
        .hashtree = std::move(hashtree),
    };

    return 0;
}
