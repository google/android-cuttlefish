/*
 * mkquota.c --- create quota files for a filesystem
 *
 * Aditya Kali <adityakali@google.com>
 * Hyojun Kim <hyojun@google.com> - Ported to f2fs-tools
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#include "quotaio.h"
#include "quotaio_v2.h"
#include "quotaio_tree.h"
#include "common.h"
#include "dict.h"


/* Needed for architectures where sizeof(int) != sizeof(void *) */
#define UINT_TO_VOIDPTR(val)  ((void *)(intptr_t)(val))
#define VOIDPTR_TO_UINT(ptr)  ((unsigned int)(intptr_t)(ptr))

#if DEBUG_QUOTA
static void print_dquot(const char *desc, struct dquot *dq)
{
	if (desc)
		fprintf(stderr, "%s: ", desc);
	fprintf(stderr, "%u %lld:%lld:%lld %lld:%lld:%lld\n",
		dq->dq_id, (long long) dq->dq_dqb.dqb_curspace,
		(long long) dq->dq_dqb.dqb_bsoftlimit,
		(long long) dq->dq_dqb.dqb_bhardlimit,
		(long long) dq->dq_dqb.dqb_curinodes,
		(long long) dq->dq_dqb.dqb_isoftlimit,
		(long long) dq->dq_dqb.dqb_ihardlimit);
}
#else
#define print_dquot(...)
#endif

static int write_dquots(dict_t *dict, struct quota_handle *qh)
{
	dnode_t		*n;
	struct dquot	*dq;
	int retval = 0;

	for (n = dict_first(dict); n; n = dict_next(dict, n)) {
		dq = dnode_get(n);
		if (dq) {
			print_dquot("write", dq);
			dq->dq_h = qh;
			update_grace_times(dq);
			if (qh->qh_ops->commit_dquot(dq)) {
				retval = -1;
				break;
			}
		}
	}
	return retval;
}

errcode_t quota_write_inode(struct f2fs_sb_info *sbi, enum quota_type qtype)
{
	struct f2fs_fsck *fsck = F2FS_FSCK(sbi);
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	quota_ctx_t qctx = fsck->qctx;
	struct quota_handle *h = NULL;
	int retval = 0;
	dict_t *dict;

	if ((!qctx) || (!sb->qf_ino[qtype]))
		return 0;

	retval = quota_get_mem(sizeof(struct quota_handle), &h);
	if (retval) {
		log_debug("Unable to allocate quota handle");
		goto out;
	}

	dict = qctx->quota_dict[qtype];
	if (dict) {
		retval = quota_file_create(sbi, h, qtype);
		if (retval) {
			log_debug("Cannot initialize io on quotafile");
		} else {
			retval = write_dquots(dict, h);
			quota_file_close(sbi, h, 1);
		}
	}
out:
	if (h)
		quota_free_mem(&h);
	return retval;
}

/******************************************************************/
/* Helper functions for computing quota in memory.                */
/******************************************************************/

static int dict_uint_cmp(const void *a, const void *b)
{
	unsigned int	c, d;

	c = VOIDPTR_TO_UINT(a);
	d = VOIDPTR_TO_UINT(b);

	if (c == d)
		return 0;
	else if (c > d)
		return 1;
	else
		return -1;
}

static inline qid_t get_qid(struct f2fs_inode *inode, enum quota_type qtype)
{
	switch (qtype) {
	case USRQUOTA:
		return inode->i_uid;
	case GRPQUOTA:
		return inode->i_gid;
	case PRJQUOTA:
		return inode->i_projid;
	default:
		return 0;
	}

	return 0;
}

static void quota_dnode_free(dnode_t *node, void *UNUSED(context))
{
	void *ptr = node ? dnode_get(node) : 0;

	quota_free_mem(&ptr);
	free(node);
}

/*
 * Set up the quota tracking data structures.
 */
errcode_t quota_init_context(struct f2fs_sb_info *sbi)
{
	struct f2fs_fsck *fsck = F2FS_FSCK(sbi);
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	errcode_t err;
	dict_t	*dict;
	quota_ctx_t ctx;
	enum quota_type	qtype;

	err = quota_get_mem(sizeof(struct quota_ctx), &ctx);
	if (err) {
		log_debug("Failed to allocate quota context");
		return err;
	}

	memset(ctx, 0, sizeof(struct quota_ctx));
	dict_init(&ctx->linked_inode_dict, DICTCOUNT_T_MAX, dict_uint_cmp);
	for (qtype = 0; qtype < MAXQUOTAS; qtype++) {
		ctx->quota_file[qtype] = NULL;
		if (!sb->qf_ino[qtype])
			continue;
		err = quota_get_mem(sizeof(dict_t), &dict);
		if (err) {
			log_debug("Failed to allocate dictionary");
			quota_release_context(&ctx);
			return err;
		}
		ctx->quota_dict[qtype] = dict;
		dict_init(dict, DICTCOUNT_T_MAX, dict_uint_cmp);
		dict_set_allocator(dict, NULL, quota_dnode_free, NULL);
	}
	ctx->sbi = sbi;
	fsck->qctx = ctx;
	return 0;
}

void quota_release_context(quota_ctx_t *qctx)
{
	dict_t	*dict;
	enum quota_type	qtype;
	quota_ctx_t ctx;

	if (!qctx)
		return;

	ctx = *qctx;
	for (qtype = 0; qtype < MAXQUOTAS; qtype++) {
		dict = ctx->quota_dict[qtype];
		ctx->quota_dict[qtype] = 0;
		if (dict) {
			dict_free_nodes(dict);
			free(dict);
		}
	}
	dict_free_nodes(&ctx->linked_inode_dict);
	*qctx = NULL;
	free(ctx);
}

static struct dquot *get_dq(dict_t *dict, __u32 key)
{
	struct dquot	*dq;
	dnode_t		*n;

	n = dict_lookup(dict, UINT_TO_VOIDPTR(key));
	if (n)
		dq = dnode_get(n);
	else {
		if (quota_get_mem(sizeof(struct dquot), &dq)) {
			log_err("Unable to allocate dquot");
			return NULL;
		}
		memset(dq, 0, sizeof(struct dquot));
		dict_alloc_insert(dict, UINT_TO_VOIDPTR(key), dq);
		dq->dq_id = key;
	}
	return dq;
}

/*
 * Called to update the blocks used by a particular inode
 */
void quota_data_add(quota_ctx_t qctx, struct f2fs_inode *inode, qsize_t space)
{
	struct dquot	*dq;
	dict_t		*dict;
	enum quota_type	qtype;

	if (!qctx)
		return;

	for (qtype = 0; qtype < MAXQUOTAS; qtype++) {
		dict = qctx->quota_dict[qtype];
		if (dict) {
			dq = get_dq(dict, get_qid(inode, qtype));
			if (dq)
				dq->dq_dqb.dqb_curspace += space;
		}
	}
}

/*
 * Called to remove some blocks used by a particular inode
 */
void quota_data_sub(quota_ctx_t qctx, struct f2fs_inode *inode, qsize_t space)
{
	struct dquot	*dq;
	dict_t		*dict;
	enum quota_type	qtype;

	if (!qctx)
		return;

	for (qtype = 0; qtype < MAXQUOTAS; qtype++) {
		dict = qctx->quota_dict[qtype];
		if (dict) {
			dq = get_dq(dict, get_qid(inode, qtype));
			dq->dq_dqb.dqb_curspace -= space;
		}
	}
}

/*
 * Called to count the files used by an inode's user/group
 */
void quota_data_inodes(quota_ctx_t qctx, struct f2fs_inode *inode, int adjust)
{
	struct dquot	*dq;
	dict_t		*dict; enum quota_type	qtype;

	if (!qctx)
		return;

	for (qtype = 0; qtype < MAXQUOTAS; qtype++) {
		dict = qctx->quota_dict[qtype];
		if (dict) {
			dq = get_dq(dict, get_qid(inode, qtype));
			dq->dq_dqb.dqb_curinodes += adjust;
		}
	}
}

/*
 * Called from fsck to count quota.
 */
void quota_add_inode_usage(quota_ctx_t qctx, f2fs_ino_t ino,
		struct f2fs_inode* inode)
{
	if (qctx) {
		/* Handle hard linked inodes */
		if (inode->i_links > 1) {
			if (dict_lookup(&qctx->linked_inode_dict,
				UINT_TO_VOIDPTR(ino))) {
				return;
			}
			dict_alloc_insert(&qctx->linked_inode_dict,
					UINT_TO_VOIDPTR(ino), NULL);
		}

		qsize_t space = (inode->i_blocks - 1) * F2FS_BLKSIZE;
		quota_data_add(qctx, inode, space);
		quota_data_inodes(qctx, inode, +1);
	}
}

struct scan_dquots_data {
	dict_t		*quota_dict;
	int             update_limits; /* update limits from disk */
	int		update_usage;
	int		usage_is_inconsistent;
};

static int scan_dquots_callback(struct dquot *dquot, void *cb_data)
{
	struct scan_dquots_data *scan_data = cb_data;
	dict_t *quota_dict = scan_data->quota_dict;
	struct dquot *dq;

	dq = get_dq(quota_dict, dquot->dq_id);
	dq->dq_id = dquot->dq_id;
	dq->dq_flags |= DQF_SEEN;

	print_dquot("mem", dq);
	print_dquot("dsk", dquot);
	/* Check if there is inconsistency */
	if (dq->dq_dqb.dqb_curspace != dquot->dq_dqb.dqb_curspace ||
	    dq->dq_dqb.dqb_curinodes != dquot->dq_dqb.dqb_curinodes) {
		scan_data->usage_is_inconsistent = 1;
		log_debug("[QUOTA WARNING] Usage inconsistent for ID %u:"
			"actual (%lld, %lld) != expected (%lld, %lld)\n",
				dq->dq_id, (long long) dq->dq_dqb.dqb_curspace,
				(long long) dq->dq_dqb.dqb_curinodes,
				(long long) dquot->dq_dqb.dqb_curspace,
				(long long) dquot->dq_dqb.dqb_curinodes);
	}

	if (scan_data->update_limits) {
		dq->dq_dqb.dqb_ihardlimit = dquot->dq_dqb.dqb_ihardlimit;
		dq->dq_dqb.dqb_isoftlimit = dquot->dq_dqb.dqb_isoftlimit;
		dq->dq_dqb.dqb_bhardlimit = dquot->dq_dqb.dqb_bhardlimit;
		dq->dq_dqb.dqb_bsoftlimit = dquot->dq_dqb.dqb_bsoftlimit;
	}

	if (scan_data->update_usage) {
		dq->dq_dqb.dqb_curspace = dquot->dq_dqb.dqb_curspace;
		dq->dq_dqb.dqb_curinodes = dquot->dq_dqb.dqb_curinodes;
	}

	return 0;
}

/*
 * Compares the measured quota in qctx->quota_dict with that in the quota inode
 * on disk and updates the limits in qctx->quota_dict. 'usage_inconsistent' is
 * set to 1 if the supplied and on-disk quota usage values are not identical.
 */
errcode_t quota_compare_and_update(struct f2fs_sb_info *sbi,
		enum quota_type qtype, int *usage_inconsistent,
		int preserve_limits)
{
	struct f2fs_fsck *fsck = F2FS_FSCK(sbi);
	quota_ctx_t qctx = fsck->qctx;
	struct quota_handle qh;
	struct scan_dquots_data scan_data;
	struct dquot *dq;
	dnode_t *n;
	dict_t *dict = qctx->quota_dict[qtype];
	errcode_t err = 0;

	if (time_to_inject(FAULT_QUOTA)) {
		ASSERT_MSG("%s is injected.", f2fs_fault_name[FAULT_QUOTA]);
		return -EINVAL;
	}

	if (!dict)
		goto out;

	err = quota_file_open(sbi, &qh, qtype, 0);
	if (err) {
		log_debug("Open quota file failed");
		*usage_inconsistent = 1;
		goto out;
	}

	scan_data.quota_dict = qctx->quota_dict[qtype];
	scan_data.update_limits = preserve_limits;
	scan_data.update_usage = 0;
	scan_data.usage_is_inconsistent = 0;
	err = qh.qh_ops->scan_dquots(&qh, scan_dquots_callback, &scan_data);
	if (err) {
		log_debug("Error scanning dquots");
		goto out;
	}

	for (n = dict_first(dict); n; n = dict_next(dict, n)) {
		dq = dnode_get(n);
		if (!dq)
			continue;
		if ((dq->dq_flags & DQF_SEEN) == 0) {
			log_debug("[QUOTA WARNING] "
				"Missing quota entry ID %d\n", dq->dq_id);
			scan_data.usage_is_inconsistent = 1;
		}
	}
	*usage_inconsistent = scan_data.usage_is_inconsistent;

out:
	return err;
}

