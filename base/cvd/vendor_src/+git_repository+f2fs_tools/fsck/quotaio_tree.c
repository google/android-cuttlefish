/*
 * Implementation of new quotafile format
 *
 * Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 * Hyojun Kim <hyojun@google.com> - Ported to f2fs-tools
 */

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "quotaio_tree.h"
#include "quotaio.h"

typedef char *dqbuf_t;

#define freedqbuf(buf)		quota_free_mem(&buf)

static inline dqbuf_t getdqbuf(void)
{
	dqbuf_t buf;
	if (quota_get_memzero(QT_BLKSIZE, &buf)) {
		log_err("Failed to allocate dqbuf");
		return NULL;
	}

	return buf;
}

/* Is given dquot empty? */
int qtree_entry_unused(struct qtree_mem_dqinfo *info, char *disk)
{
	unsigned int i;

	for (i = 0; i < info->dqi_entry_size; i++)
		if (disk[i])
			return 0;
	return 1;
}

int qtree_dqstr_in_blk(struct qtree_mem_dqinfo *info)
{
	return (QT_BLKSIZE - sizeof(struct qt_disk_dqdbheader)) /
		info->dqi_entry_size;
}

static int get_index(qid_t id, int depth)
{
	return (id >> ((QT_TREEDEPTH - depth - 1) * 8)) & 0xff;
}

static inline void mark_quotafile_info_dirty(struct quota_handle *h)
{
	h->qh_io_flags |= IOFL_INFODIRTY;
}

/* Read given block */
static void read_blk(struct quota_handle *h, unsigned int blk, dqbuf_t buf)
{
	int err;

	err = h->read(&h->qh_qf, blk << QT_BLKSIZE_BITS, buf,
			QT_BLKSIZE);
	if (err < 0)
		log_err("Cannot read block %u: %s", blk, strerror(errno));
	else if (err != QT_BLKSIZE)
		memset(buf + err, 0, QT_BLKSIZE - err);
}

/* Write block */
static int write_blk(struct quota_handle *h, unsigned int blk, dqbuf_t buf)
{
	int err;

	err = h->write(&h->qh_qf, blk << QT_BLKSIZE_BITS, buf,
			QT_BLKSIZE);
	if (err < 0 && errno != ENOSPC)
		log_err("Cannot write block (%u): %s", blk, strerror(errno));
	if (err != QT_BLKSIZE)
		return -ENOSPC;
	return 0;
}

/* Get free block in file (either from free list or create new one) */
static int get_free_dqblk(struct quota_handle *h)
{
	dqbuf_t buf = getdqbuf();
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	int blk;

	if (!buf)
		return -ENOMEM;

	if (info->dqi_free_blk) {
		blk = info->dqi_free_blk;
		read_blk(h, blk, buf);
		info->dqi_free_blk = le32_to_cpu(dh->dqdh_next_free);
	} else {
		memset(buf, 0, QT_BLKSIZE);
		/* Assure block allocation... */
		if (write_blk(h, info->dqi_blocks, buf) < 0) {
			freedqbuf(buf);
			log_err("Cannot allocate new quota block "
				"(out of disk space).");
			return -ENOSPC;
		}
		blk = info->dqi_blocks++;
	}
	mark_quotafile_info_dirty(h);
	freedqbuf(buf);
	return blk;
}

/* Put given block to free list */
static void put_free_dqblk(struct quota_handle *h, dqbuf_t buf,
			   unsigned int blk)
{
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;

	dh->dqdh_next_free = cpu_to_le32(info->dqi_free_blk);
	dh->dqdh_prev_free = cpu_to_le32(0);
	dh->dqdh_entries = cpu_to_le16(0);
	info->dqi_free_blk = blk;
	mark_quotafile_info_dirty(h);
	write_blk(h, blk, buf);
}

/* Remove given block from the list of blocks with free entries */
static void remove_free_dqentry(struct quota_handle *h, dqbuf_t buf,
				unsigned int blk)
{
	dqbuf_t tmpbuf = getdqbuf();
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	unsigned int nextblk = le32_to_cpu(dh->dqdh_next_free), prevblk =
		le32_to_cpu(dh->dqdh_prev_free);

	if (!tmpbuf)
		return;

	if (nextblk) {
		read_blk(h, nextblk, tmpbuf);
		((struct qt_disk_dqdbheader *)tmpbuf)->dqdh_prev_free =
				dh->dqdh_prev_free;
		write_blk(h, nextblk, tmpbuf);
	}
	if (prevblk) {
		read_blk(h, prevblk, tmpbuf);
		((struct qt_disk_dqdbheader *)tmpbuf)->dqdh_next_free =
				dh->dqdh_next_free;
		write_blk(h, prevblk, tmpbuf);
	} else {
		h->qh_info.u.v2_mdqi.dqi_qtree.dqi_free_entry = nextblk;
		mark_quotafile_info_dirty(h);
	}
	freedqbuf(tmpbuf);
	dh->dqdh_next_free = dh->dqdh_prev_free = cpu_to_le32(0);
	write_blk(h, blk, buf);	/* No matter whether write succeeds
				 * block is out of list */
}

/* Insert given block to the beginning of list with free entries */
static void insert_free_dqentry(struct quota_handle *h, dqbuf_t buf,
				unsigned int blk)
{
	dqbuf_t tmpbuf = getdqbuf();
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;

	if (!tmpbuf)
		return;

	dh->dqdh_next_free = cpu_to_le32(info->dqi_free_entry);
	dh->dqdh_prev_free = cpu_to_le32(0);
	write_blk(h, blk, buf);
	if (info->dqi_free_entry) {
		read_blk(h, info->dqi_free_entry, tmpbuf);
		((struct qt_disk_dqdbheader *)tmpbuf)->dqdh_prev_free =
				cpu_to_le32(blk);
		write_blk(h, info->dqi_free_entry, tmpbuf);
	}
	freedqbuf(tmpbuf);
	info->dqi_free_entry = blk;
	mark_quotafile_info_dirty(h);
}

/* Find space for dquot */
static unsigned int find_free_dqentry(struct quota_handle *h,
				      struct dquot *dquot, int *err)
{
	int blk, i;
	struct qt_disk_dqdbheader *dh;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	char *ddquot;
	dqbuf_t buf;

	*err = 0;
	buf = getdqbuf();
	if (!buf) {
		*err = -ENOMEM;
		return 0;
	}

	dh = (struct qt_disk_dqdbheader *)buf;
	if (info->dqi_free_entry) {
		blk = info->dqi_free_entry;
		read_blk(h, blk, buf);
	} else {
		blk = get_free_dqblk(h);
		if (blk < 0) {
			freedqbuf(buf);
			*err = blk;
			return 0;
		}
		memset(buf, 0, QT_BLKSIZE);
		info->dqi_free_entry = blk;
		mark_quotafile_info_dirty(h);
	}

	/* Block will be full? */
	if (le16_to_cpu(dh->dqdh_entries) + 1 >=
	    qtree_dqstr_in_blk(info))
		remove_free_dqentry(h, buf, blk);

	dh->dqdh_entries =
		cpu_to_le16(le16_to_cpu(dh->dqdh_entries) + 1);
	/* Find free structure in block */
	ddquot = buf + sizeof(struct qt_disk_dqdbheader);
	for (i = 0;
	     i < qtree_dqstr_in_blk(info) && !qtree_entry_unused(info, ddquot);
	     i++)
		ddquot += info->dqi_entry_size;

	if (i == qtree_dqstr_in_blk(info))
		log_err("find_free_dqentry(): Data block full unexpectedly.");

	write_blk(h, blk, buf);
	dquot->dq_dqb.u.v2_mdqb.dqb_off =
		(blk << QT_BLKSIZE_BITS) + sizeof(struct qt_disk_dqdbheader) +
		i * info->dqi_entry_size;
	freedqbuf(buf);
	return blk;
}

/* Insert reference to structure into the trie */
static int do_insert_tree(struct quota_handle *h, struct dquot *dquot,
			  unsigned int * treeblk, int depth)
{
	dqbuf_t buf;
	int newson = 0, newact = 0;
	__le32 *ref;
	unsigned int newblk;
	int ret = 0;

	log_debug("inserting in tree: treeblk=%u, depth=%d", *treeblk, depth);
	buf = getdqbuf();
	if (!buf)
		return -ENOMEM;

	if (!*treeblk) {
		ret = get_free_dqblk(h);
		if (ret < 0)
			goto out_buf;
		*treeblk = ret;
		memset(buf, 0, QT_BLKSIZE);
		newact = 1;
	} else {
		read_blk(h, *treeblk, buf);
	}

	ref = (__le32 *) buf;
	newblk = le32_to_cpu(ref[get_index(dquot->dq_id, depth)]);
	if (!newblk)
		newson = 1;
	if (depth == QT_TREEDEPTH - 1) {
		if (newblk)
			log_err("Inserting already present quota entry "
				"(block %u).",
				ref[get_index(dquot->dq_id, depth)]);
		newblk = find_free_dqentry(h, dquot, &ret);
	} else {
		ret = do_insert_tree(h, dquot, &newblk, depth + 1);
	}

	if (newson && ret >= 0) {
		ref[get_index(dquot->dq_id, depth)] =
			cpu_to_le32(newblk);
		ret = write_blk(h, *treeblk, buf);
		if (ret)
			goto out_buf;
	} else if (newact && ret < 0) {
		put_free_dqblk(h, buf, *treeblk);
	}

out_buf:
	freedqbuf(buf);
	return ret;
}

/* Wrapper for inserting quota structure into tree */
static int dq_insert_tree(struct quota_handle *h, struct dquot *dquot)
{
	unsigned int tmp = QT_TREEOFF;
	int err;

	err = do_insert_tree(h, dquot, &tmp, 0);
	if (err < 0)
		log_err("Cannot write quota (id %u): %s",
			(unsigned int) dquot->dq_id, strerror(errno));
	return err;
}

/* Write dquot to file */
int qtree_write_dquot(struct dquot *dquot)
{
	errcode_t retval;
	unsigned int ret;
	char *ddquot;
	struct quota_handle *h = dquot->dq_h;
	struct qtree_mem_dqinfo *info =
			&dquot->dq_h->qh_info.u.v2_mdqi.dqi_qtree;

	log_debug("writing ddquot 1: off=%llu, info->dqi_entry_size=%u",
			dquot->dq_dqb.u.v2_mdqb.dqb_off,
			info->dqi_entry_size);
	retval = quota_get_mem(info->dqi_entry_size, &ddquot);
	if (retval) {
		log_err("Quota write failed (id %u): %s",
			(unsigned int)dquot->dq_id, strerror(errno));
		return -ENOMEM;
	}
	memset(ddquot, 0, info->dqi_entry_size);

	if (!dquot->dq_dqb.u.v2_mdqb.dqb_off) {
		if (dq_insert_tree(dquot->dq_h, dquot)) {
			quota_free_mem(&ddquot);
			return -EIO;
		}
	}
	info->dqi_ops->mem2disk_dqblk(ddquot, dquot);
	log_debug("writing ddquot 2: off=%llu, info->dqi_entry_size=%u",
			dquot->dq_dqb.u.v2_mdqb.dqb_off,
			info->dqi_entry_size);
	ret = h->write(&h->qh_qf, dquot->dq_dqb.u.v2_mdqb.dqb_off, ddquot,
			info->dqi_entry_size);

	if (ret != info->dqi_entry_size) {
		log_err("Quota write failed (id %u): %s",
			(unsigned int)dquot->dq_id, strerror(errno));
		quota_free_mem(&ddquot);
		return ret;
	}
	quota_free_mem(&ddquot);
	return 0;
}

/* Free dquot entry in data block */
static void free_dqentry(struct quota_handle *h, struct dquot *dquot,
			 unsigned int blk)
{
	struct qt_disk_dqdbheader *dh;
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	dqbuf_t buf = getdqbuf();

	if (!buf)
		return;

	if (dquot->dq_dqb.u.v2_mdqb.dqb_off >> QT_BLKSIZE_BITS != blk)
		log_err("Quota structure has offset to other block (%u) "
			"than it should (%u).", blk,
			  (unsigned int) (dquot->dq_dqb.u.v2_mdqb.dqb_off >>
				  QT_BLKSIZE_BITS));

	read_blk(h, blk, buf);
	dh = (struct qt_disk_dqdbheader *)buf;
	dh->dqdh_entries =
		cpu_to_le16(le16_to_cpu(dh->dqdh_entries) - 1);

	if (!le16_to_cpu(dh->dqdh_entries)) {	/* Block got free? */
		remove_free_dqentry(h, buf, blk);
		put_free_dqblk(h, buf, blk);
	} else {
		memset(buf + (dquot->dq_dqb.u.v2_mdqb.dqb_off &
			      ((1 << QT_BLKSIZE_BITS) - 1)),
		       0, info->dqi_entry_size);

		/* First free entry? */
		if (le16_to_cpu(dh->dqdh_entries) ==
				qtree_dqstr_in_blk(info) - 1)
			/* This will also write data block */
			insert_free_dqentry(h, buf, blk);
		else
			write_blk(h, blk, buf);
	}
	dquot->dq_dqb.u.v2_mdqb.dqb_off = 0;
	freedqbuf(buf);
}

/* Remove reference to dquot from tree */
static void remove_tree(struct quota_handle *h, struct dquot *dquot,
			unsigned int * blk, int depth)
{
	dqbuf_t buf = getdqbuf();
	unsigned int newblk;
	__le32 *ref = (__le32 *) buf;

	if (!buf)
		return;

	read_blk(h, *blk, buf);
	newblk = le32_to_cpu(ref[get_index(dquot->dq_id, depth)]);
	if (depth == QT_TREEDEPTH - 1) {
		free_dqentry(h, dquot, newblk);
		newblk = 0;
	} else {
		remove_tree(h, dquot, &newblk, depth + 1);
	}

	if (!newblk) {
		int i;

		ref[get_index(dquot->dq_id, depth)] = cpu_to_le32(0);

		/* Block got empty? */
		for (i = 0; i < QT_BLKSIZE && !buf[i]; i++);

		/* Don't put the root block into the free block list */
		if (i == QT_BLKSIZE && *blk != QT_TREEOFF) {
			put_free_dqblk(h, buf, *blk);
			*blk = 0;
		} else {
			write_blk(h, *blk, buf);
		}
	}
	freedqbuf(buf);
}

/* Delete dquot from tree */
void qtree_delete_dquot(struct dquot *dquot)
{
	unsigned int tmp = QT_TREEOFF;

	if (!dquot->dq_dqb.u.v2_mdqb.dqb_off)	/* Even not allocated? */
		return;
	remove_tree(dquot->dq_h, dquot, &tmp, 0);
}

/* Find entry in block */
static long find_block_dqentry(struct quota_handle *h,
				      struct dquot *dquot, unsigned int blk)
{
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	dqbuf_t buf = getdqbuf();
	int i;
	char *ddquot = buf + sizeof(struct qt_disk_dqdbheader);

	if (!buf)
		return -ENOMEM;

	read_blk(h, blk, buf);
	for (i = 0;
	     i < qtree_dqstr_in_blk(info) && !info->dqi_ops->is_id(ddquot, dquot);
	     i++)
		ddquot += info->dqi_entry_size;

	if (i == qtree_dqstr_in_blk(info))
		log_err("Quota for id %u referenced but not present.",
			dquot->dq_id);
	freedqbuf(buf);
	return (blk << QT_BLKSIZE_BITS) + sizeof(struct qt_disk_dqdbheader) +
		i * info->dqi_entry_size;
}

/* Find entry for given id in the tree */
static long find_tree_dqentry(struct quota_handle *h,
				     struct dquot *dquot,
				     unsigned int blk, int depth)
{
	dqbuf_t buf = getdqbuf();
	long ret = 0;
	__le32 *ref = (__le32 *) buf;

	if (!buf)
		return -ENOMEM;

	read_blk(h, blk, buf);
	ret = 0;
	blk = le32_to_cpu(ref[get_index(dquot->dq_id, depth)]);
	if (!blk)	/* No reference? */
		goto out_buf;
	if (depth < QT_TREEDEPTH - 1)
		ret = find_tree_dqentry(h, dquot, blk, depth + 1);
	else
		ret = find_block_dqentry(h, dquot, blk);
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Find entry for given id in the tree - wrapper function */
static inline long find_dqentry(struct quota_handle *h,
				       struct dquot *dquot)
{
	return find_tree_dqentry(h, dquot, QT_TREEOFF, 0);
}

/*
 *  Read dquot from disk.
 */
struct dquot *qtree_read_dquot(struct quota_handle *h, qid_t id)
{
	struct qtree_mem_dqinfo *info = &h->qh_info.u.v2_mdqi.dqi_qtree;
	long offset;
	unsigned int ret;
	char *ddquot;
	struct dquot *dquot = get_empty_dquot();

	if (!dquot)
		return NULL;
	if (quota_get_mem(info->dqi_entry_size, &ddquot)) {
		quota_free_mem(&dquot);
		return NULL;
	}

	dquot->dq_id = id;
	dquot->dq_h = h;
	dquot->dq_dqb.u.v2_mdqb.dqb_off = 0;
	memset(&dquot->dq_dqb, 0, sizeof(struct util_dqblk));

	offset = find_dqentry(h, dquot);
	if (offset > 0) {
		dquot->dq_dqb.u.v2_mdqb.dqb_off = offset;
		ret = h->read(&h->qh_qf, offset, ddquot,
			info->dqi_entry_size);
		if (ret != info->dqi_entry_size) {
			if (ret > 0)
				errno = EIO;
			log_err("Cannot read quota structure for id %u: %s",
				dquot->dq_id, strerror(errno));
		}
		info->dqi_ops->disk2mem_dqblk(dquot, ddquot);
	}
	quota_free_mem(&ddquot);
	return dquot;
}

/*
 * Scan all dquots in file and call callback on each
 */
#define set_bit(bmp, ind) ((bmp)[(ind) >> 3] |= (1 << ((ind) & 7)))
#define get_bit(bmp, ind) ((bmp)[(ind) >> 3] & (1 << ((ind) & 7)))

static int report_block(struct dquot *dquot, unsigned int blk, char *bitmap,
			int (*process_dquot) (struct dquot *, void *),
			void *data)
{
	struct qtree_mem_dqinfo *info =
			&dquot->dq_h->qh_info.u.v2_mdqi.dqi_qtree;
	dqbuf_t buf = getdqbuf();
	struct qt_disk_dqdbheader *dh;
	char *ddata;
	int entries, i;

	if (!buf)
		return -1;

	set_bit(bitmap, blk);
	read_blk(dquot->dq_h, blk, buf);
	dh = (struct qt_disk_dqdbheader *)buf;
	ddata = buf + sizeof(struct qt_disk_dqdbheader);
	entries = le16_to_cpu(dh->dqdh_entries);
	for (i = 0; i < qtree_dqstr_in_blk(info);
			i++, ddata += info->dqi_entry_size)
		if (!qtree_entry_unused(info, ddata)) {
			dquot->dq_dqb.u.v2_mdqb.dqb_off =
				(blk << QT_BLKSIZE_BITS) +
				sizeof(struct qt_disk_dqdbheader) +
				i * info->dqi_entry_size;
			info->dqi_ops->disk2mem_dqblk(dquot, ddata);
			if (process_dquot(dquot, data) < 0)
				break;
		}
	freedqbuf(buf);
	return entries;
}

static int check_reference(struct quota_handle *h, unsigned int blk)
{
	if (blk >= h->qh_info.u.v2_mdqi.dqi_qtree.dqi_blocks) {
		log_err("Illegal reference (%u >= %u) in %s quota file",
			blk,
			h->qh_info.u.v2_mdqi.dqi_qtree.dqi_blocks,
			quota_type2name(h->qh_type));
		return -1;
	}
	return 0;
}

/* Return 0 for successful run */
static int report_tree(struct dquot *dquot, unsigned int blk, int depth,
		       char *bitmap, int *entries,
		       int (*process_dquot) (struct dquot *, void *),
		       void *data)
{
	int i;
	dqbuf_t buf = getdqbuf();
	__le32 *ref = (__le32 *) buf;

	if (!buf)
		return -1;

	read_blk(dquot->dq_h, blk, buf);
	for (i = 0; i < QT_BLKSIZE >> 2; i++) {
		blk = le32_to_cpu(ref[i]);
		if (blk == 0)
			continue;

		if (check_reference(dquot->dq_h, blk))
			break;

		if (depth == QT_TREEDEPTH - 1) {
			if (!get_bit(bitmap, blk)) {
				int num_entry = report_block(dquot, blk, bitmap,
							process_dquot, data);
				if (num_entry < 0)
					break;
				*entries += num_entry;
			}
		} else {
			if (report_tree(dquot, blk, depth + 1, bitmap, entries,
						process_dquot, data))
				break;
		}
	}
	freedqbuf(buf);
	return (i < QT_BLKSIZE >> 2) ? -1 : 0;
}

static unsigned int find_set_bits(char *bmp, int blocks)
{
	unsigned int	used = 0;
	int		i;

	for (i = 0; i < blocks; i++)
		if (get_bit(bmp, i))
			used++;
	return used;
}

int qtree_scan_dquots(struct quota_handle *h,
		      int (*process_dquot) (struct dquot *, void *),
		      void *data)
{
	struct v2_mem_dqinfo *v2info = &h->qh_info.u.v2_mdqi;
	struct qtree_mem_dqinfo *info = &v2info->dqi_qtree;
	struct dquot *dquot = get_empty_dquot();
	char *bitmap = NULL;
	int ret = -1;
	int entries = 0;

	if (!dquot)
		return -1;

	dquot->dq_h = h;
	if (quota_get_memzero((info->dqi_blocks + 7) >> 3, &bitmap))
		goto out;
	if (report_tree(dquot, QT_TREEOFF, 0, bitmap, &entries, process_dquot,
				data))
		goto out;

	v2info->dqi_used_entries = entries;
	v2info->dqi_data_blocks = find_set_bits(bitmap, info->dqi_blocks);
	ret = 0;

out:
	if (bitmap)
		quota_free_mem(&bitmap);
	if (dquot)
		quota_free_mem(&dquot);

	return ret;
}
