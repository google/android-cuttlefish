/**
 * sload.c
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
#define _GNU_SOURCE
#include "fsck.h"
#include <libgen.h>
#include <dirent.h>
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

#ifdef HAVE_LIBSELINUX
static struct selabel_handle *sehnd = NULL;
#endif

typedef void (*fs_config_f)(const char *path, int dir,
			    const char *target_out_path,
			    unsigned *uid, unsigned *gid,
			    unsigned *mode, uint64_t *capabilities);

#ifndef _WIN32
static fs_config_f fs_config_func = NULL;

#ifdef HAVE_SELINUX_ANDROID_H
#include <selinux/android.h>
#include <private/android_filesystem_config.h>
#include <private/canned_fs_config.h>
#include <private/fs_config.h>
#endif

static int filter_dot(const struct dirent *d)
{
	return (strcmp(d->d_name, "..") && strcmp(d->d_name, "."));
}

static int f2fs_make_directory(struct f2fs_sb_info *sbi,
				int entries, struct dentry *de)
{
	int ret = 0;
	int i = 0;

	for (i = 0; i < entries; i++) {
		if (de[i].file_type == F2FS_FT_DIR)
			ret = f2fs_mkdir(sbi, de + i);
		else if (de[i].file_type == F2FS_FT_REG_FILE)
			ret = f2fs_create(sbi, de + i);
		else if (de[i].file_type == F2FS_FT_SYMLINK)
			ret = f2fs_symlink(sbi, de + i);

		if (ret)
			break;
	}

	return ret;
}
#endif

#ifdef HAVE_LIBSELINUX
static int set_selinux_xattr(struct f2fs_sb_info *sbi, const char *path,
							nid_t ino, int mode)
{
	char *secontext = NULL;
	char *mnt_path = NULL;

	if (!sehnd)
		return 0;

	if (asprintf(&mnt_path, "%s%s", c.mount_point, path) <= 0) {
		ERR_MSG("cannot allocate security path for %s%s\n",
						c.mount_point, path);
		return -ENOMEM;
	}

	/* set root inode selinux context */
	if (selabel_lookup(sehnd, &secontext, mnt_path, mode) < 0) {
		ERR_MSG("cannot lookup security context for %s\n", mnt_path);
		free(mnt_path);
		return -EINVAL;
	}

	if (secontext) {
		MSG(2, "%s (%d) -> SELinux context = %s\n",
						mnt_path, ino, secontext);
		inode_set_selinux(sbi, ino, secontext);
	}
	freecon(secontext);
	free(mnt_path);
	return 0;
}
#else
#define set_selinux_xattr(...)	0
#endif

#ifndef _WIN32
static int set_perms_and_caps(struct dentry *de)
{
	uint64_t capabilities = 0;
	unsigned int uid = 0, gid = 0, imode = 0;
	char *mnt_path = NULL;
	char *mount_path = c.mount_point;

	/*
	 * de->path already has "/" in the beginning of it.
	 * Need to remove "/" when c.mount_point is "/", not to add it twice.
	 */
	if (strlen(c.mount_point) == 1 && c.mount_point[0] == '/')
		mount_path = "";

	if (asprintf(&mnt_path, "%s%s", mount_path, de->path) <= 0) {
		ERR_MSG("cannot allocate mount path for %s%s\n",
				mount_path, de->path);
		return -ENOMEM;
	}

	/* Permissions */
	if (fs_config_func != NULL) {
		fs_config_func(mnt_path, de->file_type == F2FS_FT_DIR,
				c.target_out_dir, &uid, &gid, &imode,
				&capabilities);
		de->uid = uid & 0xffff;
		de->gid = gid & 0xffff;
		de->mode = (de->mode & S_IFMT) | (imode & 0xffff);
		de->capabilities = capabilities;
	}
	MSG(2, "%s -> mode = 0x%x, uid = 0x%x, gid = 0x%x, "
			"capabilities = 0x%"PRIx64"\n",
		mnt_path, de->mode, de->uid, de->gid, de->capabilities);
	free(mnt_path);
	return 0;
}

static void set_inode_metadata(struct dentry *de)
{
	struct stat stat;
	int ret;

	ret = lstat(de->full_path, &stat);
	if (ret < 0) {
		ERR_MSG("lstat failure\n");
		ASSERT(0);
	}

	if (S_ISREG(stat.st_mode)) {
		if (stat.st_nlink > 1) {
			/*
			 * This file might have multiple links to it, so remember
			 * device and inode.
			 */
			de->from_devino = stat.st_dev;
			de->from_devino <<= 32;
			de->from_devino |= stat.st_ino;
		}
		de->file_type = F2FS_FT_REG_FILE;
	} else if (S_ISDIR(stat.st_mode)) {
		de->file_type = F2FS_FT_DIR;
	} else if (S_ISCHR(stat.st_mode)) {
		de->file_type = F2FS_FT_CHRDEV;
	} else if (S_ISBLK(stat.st_mode)) {
		de->file_type = F2FS_FT_BLKDEV;
	} else if (S_ISFIFO(stat.st_mode)) {
		de->file_type = F2FS_FT_FIFO;
	} else if (S_ISSOCK(stat.st_mode)) {
		de->file_type = F2FS_FT_SOCK;
	} else if (S_ISLNK(stat.st_mode)) {
		de->file_type = F2FS_FT_SYMLINK;
		de->link = calloc(F2FS_BLKSIZE, 1);
		ASSERT(de->link);
		ret = readlink(de->full_path, de->link, F2FS_BLKSIZE - 1);
		ASSERT(ret >= 0);
	} else {
		ERR_MSG("unknown file type on %s", de->path);
		ASSERT(0);
	}

	de->size = stat.st_size;
	de->mode = stat.st_mode &
			(S_IFMT|S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
	if (c.fixed_time == -1 && c.from_dir)
		de->mtime = stat.st_mtime;
	else
		de->mtime = c.fixed_time;

	if (c.preserve_perms) {
		de->uid = stat.st_uid;
		de->gid = stat.st_gid;
	}

	set_perms_and_caps(de);
}

static int build_directory(struct f2fs_sb_info *sbi, const char *full_path,
			const char *dir_path, const char *target_out_dir,
			nid_t dir_ino)
{
	int entries = 0;
	struct dentry *dentries;
	struct dirent **namelist = NULL;
	int i = 0, ret = 0;

	entries = scandir(full_path, &namelist, filter_dot, (void *)alphasort);
	if (entries < 0) {
		ERR_MSG("No entries in %s\n", full_path);
		return -ENOENT;
	}

	dentries = calloc(entries, sizeof(struct dentry));
	ASSERT(dentries);

	for (i = 0; i < entries; i++) {
		dentries[i].name = (unsigned char *)strdup(namelist[i]->d_name);
		if (dentries[i].name == NULL) {
			ERR_MSG("Skip: ENOMEM\n");
			continue;
		}
		dentries[i].len = strlen((char *)dentries[i].name);

		ret = asprintf(&dentries[i].path, "%s%s",
					dir_path, namelist[i]->d_name);
		ASSERT(ret > 0);
		ret = asprintf(&dentries[i].full_path, "%s/%s",
					full_path, namelist[i]->d_name);
		ASSERT(ret > 0);
		free(namelist[i]);

		set_inode_metadata(dentries + i);

		dentries[i].pino = dir_ino;
	}

	free(namelist);

	ret = f2fs_make_directory(sbi, entries, dentries);
	if (ret)
		goto out_free;

	for (i = 0; i < entries; i++) {
		if (dentries[i].file_type == F2FS_FT_REG_FILE) {
			f2fs_build_file(sbi, dentries + i);
		} else if (dentries[i].file_type == F2FS_FT_DIR) {
			char *subdir_full_path = NULL;
			char *subdir_dir_path = NULL;

			ret = asprintf(&subdir_full_path, "%s",
							dentries[i].full_path);
			ASSERT(ret > 0);
			ret = asprintf(&subdir_dir_path, "%s/",
							dentries[i].path);
			ASSERT(ret > 0);

			ret = build_directory(sbi, subdir_full_path,
						subdir_dir_path,
						target_out_dir,
						dentries[i].ino);
			free(subdir_full_path);
			free(subdir_dir_path);

			if (ret)
				goto out_free;
		} else if (dentries[i].file_type == F2FS_FT_SYMLINK) {
			/*
			 * It is already done in f2fs_make_directory
			 * f2fs_make_symlink(sbi, dir_ino, &dentries[i]);
			 */
		} else {
			MSG(1, "Error unknown file type\n");
		}

		ret = set_selinux_xattr(sbi, dentries[i].path,
					dentries[i].ino, dentries[i].mode);
		if (ret)
			goto out_free;

		free(dentries[i].path);
		free(dentries[i].full_path);
		free((void *)dentries[i].name);
	}
out_free:
	for (; i < entries; i++) {
		free(dentries[i].path);
		free(dentries[i].full_path);
		free((void *)dentries[i].name);
	}

	free(dentries);
	return 0;
}
#else
static int build_directory(struct f2fs_sb_info *sbi, const char *full_path,
			const char *dir_path, const char *target_out_dir,
			nid_t dir_ino)
{
	return -1;
}
#endif

static int configure_files(void)
{
#ifdef HAVE_LIBSELINUX
	if (!c.nr_opt)
		goto skip;
#if !defined(__ANDROID__)
	sehnd = selabel_open(SELABEL_CTX_FILE, c.seopt_file, c.nr_opt);
	if (!sehnd) {
		ERR_MSG("Failed to open file contexts \"%s\"",
					c.seopt_file[0].value);
			return -EINVAL;
	}
#else
	sehnd = selinux_android_file_context_handle();
	if (!sehnd) {
		ERR_MSG("Failed to get android file_contexts\n");
		return -EINVAL;
	}
#endif
skip:
#endif
#ifdef HAVE_SELINUX_ANDROID_H
	/* Load the FS config */
	if (c.fs_config_file) {
		int ret = load_canned_fs_config(c.fs_config_file);

		if (ret < 0) {
			ERR_MSG("Failed to load fs_config \"%s\"",
						c.fs_config_file);
			return ret;
		}
		fs_config_func = canned_fs_config;
	} else {
		fs_config_func = fs_config;
	}
#endif
	return 0;
}

int f2fs_sload(struct f2fs_sb_info *sbi)
{
	int ret = 0;

	/* this requires for the below sanity checks */
	fsck_init(sbi);

	ret = configure_files();
	if (ret) {
		ERR_MSG("Failed to configure files\n");
		return ret;
	}

	/* flush NAT/SIT journal entries */
	flush_journal_entries(sbi);

	/* initialize empty hardlink cache */
	sbi->hardlink_cache = 0;

	ret = build_directory(sbi, c.from_dir, "/",
					c.target_out_dir, F2FS_ROOT_INO(sbi));
	if (ret) {
		ERR_MSG("Failed to build due to %d\n", ret);
		return ret;
	}

	ret = set_selinux_xattr(sbi, c.mount_point,
					F2FS_ROOT_INO(sbi), S_IFDIR);
	if (ret) {
		ERR_MSG("Failed to set selinux for root: %d\n", ret);
		return ret;
	}

	/* update curseg info; can update sit->types */
	move_curseg_info(sbi, SM_I(sbi)->main_blkaddr, 0);
	zero_journal_entries(sbi);
	write_curseg_info(sbi);

	/* flush dirty sit entries */
	flush_sit_entries(sbi);

	write_checkpoint(sbi);
	return 0;
}
