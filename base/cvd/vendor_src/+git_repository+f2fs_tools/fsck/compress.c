/**
 * compress.c
 *
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add sload compression support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* for config.h for general environment (non-Android) */
#include "f2fs.h"

#include "compress.h"
#ifdef HAVE_LIBLZO2
#include <lzo/lzo1x.h>	/* for lzo1x_1_15_compress() */
#endif
#ifdef HAVE_LIBLZ4
#include <lz4.h>	/* for LZ4_compress_fast_extState() */
#endif

/*
 * macro/constants borrowed from kernel header (GPL-2.0):
 * include/linux/lzo.h, and include/linux/lz4.h
 */
#ifdef HAVE_LIBLZO2
#define lzo1x_worst_compress(x)		((x) + (x) / 16 + 64 + 3 + 2)
#define LZO_WORK_SIZE			ALIGN_UP(LZO1X_1_15_MEM_COMPRESS, 8)
#endif
#ifdef HAVE_LIBLZ4
#define LZ4_MEMORY_USAGE		14
#define LZ4_MAX_INPUT_SIZE		0x7E000000 /* 2 113 929 216 bytes */
#define LZ4_MEM_COMPRESS		sizeof(LZ4_stream_t)
#define LZ4_ACCELERATION_DEFAULT	1
#define LZ4_WORK_SIZE			ALIGN_UP(LZ4_MEM_COMPRESS, 8)
#endif

#if defined(HAVE_LIBLZO2) || defined(HAVE_LIBLZ4)
static void reset_cc(struct compress_ctx *cc)
{
	memset(cc->rbuf, 0, cc->cluster_size * F2FS_BLKSIZE);
	memset(cc->cbuf->cdata, 0, cc->cluster_size * F2FS_BLKSIZE
			- F2FS_BLKSIZE);
}
#endif

#ifdef HAVE_LIBLZO2
static void lzo_compress_init(struct compress_ctx *cc)
{
	size_t size = cc->cluster_size * F2FS_BLKSIZE;
	size_t alloc = size + lzo1x_worst_compress(size)
			+ COMPRESS_HEADER_SIZE + LZO_WORK_SIZE;
	cc->private = malloc(alloc);
	ASSERT(cc->private);
	cc->rbuf = (char *) cc->private + LZO_WORK_SIZE;
	cc->cbuf = (struct compress_data *)((char *) cc->rbuf + size);
}

static int lzo_compress(struct compress_ctx *cc)
{
	int ret = lzo1x_1_15_compress(cc->rbuf, cc->rlen, cc->cbuf->cdata,
			(lzo_uintp)(&cc->clen), cc->private);
	cc->cbuf->clen = cpu_to_le32(cc->clen);
	return ret;
}
#endif

#ifdef HAVE_LIBLZ4
static void lz4_compress_init(struct compress_ctx *cc)
{
	size_t size = cc->cluster_size * F2FS_BLKSIZE;
	size_t alloc = size + LZ4_COMPRESSBOUND(size)
			+ COMPRESS_HEADER_SIZE + LZ4_WORK_SIZE;
	cc->private = malloc(alloc);
	ASSERT(cc->private);
	cc->rbuf = (char *) cc->private + LZ4_WORK_SIZE;
	cc->cbuf = (struct compress_data *)((char *) cc->rbuf + size);
}

static int lz4_compress(struct compress_ctx *cc)
{
	cc->clen = LZ4_compress_fast_extState(cc->private, cc->rbuf,
			(char *)cc->cbuf->cdata, cc->rlen,
			cc->rlen - F2FS_BLKSIZE * c.compress.min_blocks -
			COMPRESS_HEADER_SIZE,
			LZ4_ACCELERATION_DEFAULT);

	if (!cc->clen)
		return 1;

	cc->cbuf->clen = cpu_to_le32(cc->clen);
	return 0;
}
#endif

const char *supported_comp_names[] = {
	"lzo",
	"lz4",
	"",
};

compress_ops supported_comp_ops[] = {
#ifdef HAVE_LIBLZO2
	{lzo_compress_init, lzo_compress, reset_cc},
#else
	{NULL, NULL, NULL},
#endif
#ifdef HAVE_LIBLZ4
	{lz4_compress_init, lz4_compress, reset_cc},
#else
	{NULL, NULL, NULL},
#endif
};

/* linked list */
typedef struct _ext_t {
	const char *ext;
	struct _ext_t *next;
} ext_t;

static ext_t *extension_list;

static bool ext_found(const char *ext)
{
	ext_t *p = extension_list;

	while (p != NULL && strcmp(ext, p->ext))
		p = p->next;
	return (p != NULL);
}

static const char *get_ext(const char *path)
{
	char *p = strrchr(path, '.');

	return p == NULL ? path + strlen(path) : p + 1;
}

static bool ext_do_filter(const char *path)
{
	return (ext_found(get_ext(path)) == true) ^
		(c.compress.filter == COMPR_FILTER_ALLOW);
}

static void ext_filter_add(const char *ext)
{
	ext_t *node;

	ASSERT(ext != NULL);
	if (ext_found(ext))
		return; /* ext was already registered */
	node = malloc(sizeof(ext_t));
	ASSERT(node != NULL);
	node->ext = ext;
	node->next = extension_list;
	extension_list = node;
}

static void ext_filter_destroy(void)
{
	ext_t *p;

	while (extension_list != NULL) {
		p = extension_list;
		extension_list = p->next;
		free(p);
	}
}

filter_ops ext_filter = {
	.add = ext_filter_add,
	.destroy = ext_filter_destroy,
	.filter = ext_do_filter,
};
