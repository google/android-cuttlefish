/** quotaio.c
 *
 * Generic IO operations on quotafiles
 * Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 * Aditya Kali <adityakali@google.com> - Ported to e2fsprogs
 * Hyojun Kim <hyojun@google.com> - Ported to f2fs-tools
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <assert.h>

#include "common.h"
#include "quotaio.h"

static const char * const extensions[MAXQUOTAS] = {
	[USRQUOTA] = "user",
	[GRPQUOTA] = "group",
	[PRJQUOTA] = "project",
};

/* Header in all newer quotafiles */
struct disk_dqheader {
	__le32 dqh_magic;
	__le32 dqh_version;
};

static_assert(sizeof(struct disk_dqheader) == 8, "");

int cur_qtype = -1;
u32 qf_last_blkofs[MAXQUOTAS] = {0, 0, 0};
enum qf_szchk_type_t qf_szchk_type[MAXQUOTAS] =
{
	QF_SZCHK_NONE, QF_SZCHK_NONE, QF_SZCHK_NONE
};
u64 qf_maxsize[MAXQUOTAS];

/**
 * Convert type of quota to written representation
 */
const char *quota_type2name(enum quota_type qtype)
{
	if (qtype >= MAXQUOTAS)
		return "unknown";
	return extensions[qtype];
}

/*
 * Set grace time if needed
 */
void update_grace_times(struct dquot *q)
{
	time_t now;

	time(&now);
	if (q->dq_dqb.dqb_bsoftlimit && toqb(q->dq_dqb.dqb_curspace) >
			q->dq_dqb.dqb_bsoftlimit) {
		if (!q->dq_dqb.dqb_btime)
			q->dq_dqb.dqb_btime =
				now + q->dq_h->qh_info.dqi_bgrace;
	} else {
		q->dq_dqb.dqb_btime = 0;
	}

	if (q->dq_dqb.dqb_isoftlimit && q->dq_dqb.dqb_curinodes >
			q->dq_dqb.dqb_isoftlimit) {
		if (!q->dq_dqb.dqb_itime)
				q->dq_dqb.dqb_itime =
					now + q->dq_h->qh_info.dqi_igrace;
	} else {
		q->dq_dqb.dqb_itime = 0;
	}
}

/* Functions to read/write quota file. */
static unsigned int quota_write_nomount(struct quota_file *qf,
					long offset,
					void *buf, unsigned int size)
{
	unsigned int written;

	written = f2fs_write(qf->sbi, qf->ino, buf, size, offset);
	if (qf->filesize < offset + written)
		qf->filesize = offset + written;
	if (written != size)
		return -EIO;
	return written;
}

static unsigned int quota_read_nomount(struct quota_file *qf, long offset,
		void *buf, unsigned int size)
{
	return f2fs_read(qf->sbi, qf->ino, buf, size, offset);
}

/*
 * Detect quota format and initialize quota IO
 */
errcode_t quota_file_open(struct f2fs_sb_info *sbi, struct quota_handle *h,
			  enum quota_type qtype, int flags)
{
	struct f2fs_fsck *fsck = F2FS_FSCK(sbi);
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	quota_ctx_t qctx = fsck->qctx;
	f2fs_ino_t qf_ino;
	errcode_t err = 0;
	int allocated_handle = 0;

	if (qtype >= MAXQUOTAS)
		return EINVAL;

	qf_ino = sb->qf_ino[qtype];

	if (!h) {
		if (qctx->quota_file[qtype]) {
			h = qctx->quota_file[qtype];
			(void) quota_file_close(sbi, h, 0);
		}
		err = quota_get_mem(sizeof(struct quota_handle), &h);
		if (err) {
			log_err("Unable to allocate quota handle");
			return err;
		}
		allocated_handle = 1;
	}

	h->qh_qf.sbi = sbi;
	h->qh_qf.ino = qf_ino;
	h->write = quota_write_nomount;
	h->read = quota_read_nomount;
	h->qh_file_flags = flags;
	h->qh_io_flags = 0;
	h->qh_type = qtype;
	h->qh_fmt = QFMT_VFS_V1;
	memset(&h->qh_info, 0, sizeof(h->qh_info));
	h->qh_ops = &quotafile_ops_2;

	if (h->qh_ops->check_file &&
	    (h->qh_ops->check_file(h, qtype) == 0)) {
		log_err("qh_ops->check_file failed");
		err = EIO;
		goto errout;
	}

	if (h->qh_ops->init_io && (h->qh_ops->init_io(h, qtype) < 0)) {
		log_err("qh_ops->init_io failed");
		err = EIO;
		goto errout;
	}
	if (allocated_handle)
		qctx->quota_file[qtype] = h;
errout:
	return err;
}

/*
 * Create new quotafile of specified format on given filesystem
 */
errcode_t quota_file_create(struct f2fs_sb_info *sbi, struct quota_handle *h,
		enum quota_type qtype)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	f2fs_ino_t qf_inum = sb->qf_ino[qtype];
	errcode_t err = 0;

	memset(&h->qh_qf, 0, sizeof(h->qh_qf));
	h->qh_qf.sbi = sbi;
	h->qh_qf.ino = qf_inum;
	h->write = quota_write_nomount;
	h->read = quota_read_nomount;

	log_debug("Creating quota ino=%u, type=%d", qf_inum, qtype);
	h->qh_io_flags = 0;
	h->qh_type = qtype;
	h->qh_fmt = QFMT_VFS_V1;
	memset(&h->qh_info, 0, sizeof(h->qh_info));
	h->qh_ops = &quotafile_ops_2;

	if (h->qh_ops->new_io && (h->qh_ops->new_io(h) < 0)) {
		log_err("qh_ops->new_io failed");
		err = EIO;
	}

	return err;
}

/*
 * Close quotafile and release handle
 */
errcode_t quota_file_close(struct f2fs_sb_info *sbi, struct quota_handle *h,
		int update_filesize)
{
	struct f2fs_fsck *fsck = F2FS_FSCK(sbi);
	quota_ctx_t qctx = fsck->qctx;

        if (h->qh_io_flags & IOFL_INFODIRTY) {
		if (h->qh_ops->write_info && h->qh_ops->write_info(h) < 0)
			return EIO;
		h->qh_io_flags &= ~IOFL_INFODIRTY;
	}
	if (h->qh_ops->end_io && h->qh_ops->end_io(h) < 0)
		return EIO;
	if (update_filesize) {
		f2fs_filesize_update(sbi, h->qh_qf.ino, h->qh_qf.filesize);
	}
	if (qctx->quota_file[h->qh_type] == h)
		quota_free_mem(&qctx->quota_file[h->qh_type]);
	return 0;
}

/*
 * Create empty quota structure
 */
struct dquot *get_empty_dquot(void)
{
	struct dquot *dquot;

	if (quota_get_memzero(sizeof(struct dquot), &dquot)) {
		log_err("Failed to allocate dquot");
		return NULL;
	}

	dquot->dq_id = -1;
	return dquot;
}
