/**
 * xattr.h
 *
 * Many parts of codes are copied from Linux kernel/fs/f2fs.
 *
 * Copyright (C) 2015 Huawei Ltd.
 * Witten by:
 *   Hou Pengyang <houpengyang@huawei.com>
 *   Liu Shuoran <liushuoran@huawei.com>
 *   Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _XATTR_H_
#define _XATTR_H_

#include "f2fs.h"
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

struct f2fs_xattr_header {
	__le32 h_magic;		/* magic number for identification */
	__le32 h_refcount;	/* reference count */
	__u32 h_sloadd[4];	/* zero right now */
};

struct f2fs_xattr_entry {
	__u8 e_name_index;
	__u8 e_name_len;
	__le16 e_value_size;	/* size of attribute value */
	char e_name[0];		/* attribute name */
};

#define FSCRYPT_CONTEXT_V1 1
#define FSCRYPT_CONTEXT_V2 2
#ifndef FSCRYPT_KEY_DESCRIPTOR_SIZE
#define FSCRYPT_KEY_DESCRIPTOR_SIZE 8
#endif
#ifndef FSCRYPT_KEY_IDENTIFIER_SIZE
#define FSCRYPT_KEY_IDENTIFIER_SIZE	16
#endif
#define FSCRYPT_FILE_NONCE_SIZE 16
#define F2FS_XATTR_NAME_ENCRYPTION_CONTEXT    "c"

struct fscrypt_context_v1 {
	u8 version; /* FSCRYPT_CONTEXT_V1 */
	u8 contents_encryption_mode;
	u8 filenames_encryption_mode;
	u8 flags;
	u8 master_key_descriptor[FSCRYPT_KEY_DESCRIPTOR_SIZE];
	u8 nonce[FSCRYPT_FILE_NONCE_SIZE];
};

struct fscrypt_context_v2 {
	u8 version; /* FSCRYPT_CONTEXT_V2 */
	u8 contents_encryption_mode;
	u8 filenames_encryption_mode;
	u8 flags;
	u8 __reserved[4];
	u8 master_key_identifier[FSCRYPT_KEY_IDENTIFIER_SIZE];
	u8 nonce[FSCRYPT_FILE_NONCE_SIZE];
};

union fscrypt_context {
	u8 version;
	struct fscrypt_context_v1 v1;
	struct fscrypt_context_v2 v2;
};

static_assert(sizeof(struct fscrypt_context_v1) == 28, "");
static_assert(sizeof(struct fscrypt_context_v2) == 40, "");

/*
* Return the size expected for the given fscrypt_context based on its version
* number, or 0 if the context version is unrecognized.
*/
static inline int fscrypt_context_size(const union fscrypt_context *ctx)
{
	switch (ctx->version) {
	case FSCRYPT_CONTEXT_V1:
		return sizeof(ctx->v1);
	case FSCRYPT_CONTEXT_V2:
		return sizeof(ctx->v2);
	default:
		MSG(0, "Unsupported fscrypt_context format!\n");
	}
	return 0;
}

struct fsverity_descriptor_location {
	__le32 version;
	__le32 size;
	__le64 pos;
};

static_assert(sizeof(struct fsverity_descriptor_location) == 16, "");

#define F2FS_ACL_VERSION	0x0001

struct f2fs_acl_entry {
	__le16 e_tag;
	__le16 e_perm;
	__le32 e_id;
};

struct f2fs_acl_entry_short {
	__le16 e_tag;
	__le16 e_perm;
};

struct f2fs_acl_header {
	__le32 a_version;
};

static inline int f2fs_acl_count(int size)
{
	ssize_t s;
	size -= sizeof(struct f2fs_acl_header);
	s = size - 4 * sizeof(struct f2fs_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(struct f2fs_acl_entry_short))
			return -1;
		return size / sizeof(struct f2fs_acl_entry_short);
	} else {
		if (s % sizeof(struct f2fs_acl_entry))
			return -1;
		return s / sizeof(struct f2fs_acl_entry) + 4;
	}
}

#ifndef XATTR_USER_PREFIX
#define XATTR_USER_PREFIX	"user."
#endif
#ifndef XATTR_SECURITY_PREFIX
#define XATTR_SECURITY_PREFIX	"security."
#endif
#ifndef XATTR_TRUSTED_PREFIX
#define XATTR_TRUSTED_PREFIX	"trusted."
#endif

#ifndef XATTR_CREATE
#define XATTR_CREATE 0x1
#endif
#ifndef XATTR_REPLACE
#define XATTR_REPLACE 0x2
#endif

#define XATTR_ROUND	(3)

#define XATTR_SELINUX_SUFFIX "selinux"
#define F2FS_XATTR_INDEX_USER			1
#define F2FS_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define F2FS_XATTR_INDEX_TRUSTED		4
#define F2FS_XATTR_INDEX_LUSTRE			5
#define F2FS_XATTR_INDEX_SECURITY		6
#define F2FS_XATTR_INDEX_ENCRYPTION		9
#define F2FS_XATTR_INDEX_VERITY			11

#define F2FS_XATTR_NAME_VERITY			"v"

#define IS_XATTR_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#define XATTR_HDR(ptr)		((struct f2fs_xattr_header *)(ptr))
#define XATTR_ENTRY(ptr) 	((struct f2fs_xattr_entry *)(ptr))
#define F2FS_XATTR_MAGIC	0xF2F52011

#define XATTR_NEXT_ENTRY(entry) ((struct f2fs_xattr_entry *) ((char *)(entry) +\
					ENTRY_SIZE(entry)))
#define XATTR_FIRST_ENTRY(ptr)	(XATTR_ENTRY(XATTR_HDR(ptr) + 1))

#define XATTR_ALIGN(size)	((size + XATTR_ROUND) & ~XATTR_ROUND)

#define ENTRY_SIZE(entry) (XATTR_ALIGN(sizeof(struct f2fs_xattr_entry) + \
			entry->e_name_len + le16_to_cpu(entry->e_value_size)))

#define list_for_each_xattr(entry, addr) \
	for (entry = XATTR_FIRST_ENTRY(addr); \
			!IS_XATTR_LAST_ENTRY(entry); \
			entry = XATTR_NEXT_ENTRY(entry))

#define VALID_XATTR_BLOCK_SIZE	(F2FS_BLKSIZE - sizeof(struct node_footer))

#define XATTR_SIZE(i)		((le32_to_cpu((i)->i_xattr_nid) ?	\
					VALID_XATTR_BLOCK_SIZE : 0) +	\
						(inline_xattr_size(i)))

#define MIN_OFFSET	XATTR_ALIGN(F2FS_BLKSIZE -		\
		sizeof(struct node_footer) - sizeof(__u32))

#define MAX_VALUE_LEN	(MIN_OFFSET -				\
		sizeof(struct f2fs_xattr_header) -		\
		sizeof(struct f2fs_xattr_entry))

#define MAX_INLINE_XATTR_SIZE						\
			(DEF_ADDRS_PER_INODE -				\
			F2FS_TOTAL_EXTRA_ATTR_SIZE / sizeof(__le32) -	\
			DEF_INLINE_RESERVED_SIZE -			\
			MIN_INLINE_DENTRY_SIZE / sizeof(__le32))
#endif
