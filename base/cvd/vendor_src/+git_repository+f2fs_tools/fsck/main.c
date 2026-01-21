/**
 * main.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2015 Jaegeuk Kim <jaegeuk@kernel.org>
 *  : implement defrag.f2fs
 * Copyright (C) 2015 Huawei Ltd.
 *   Hou Pengyang <houpengyang@huawei.com>
 *   Liu Shuoran <liushuoran@huawei.com>
 *   Jaegeuk Kim <jaegeuk@kernel.org>
 *  : add sload.f2fs
 * Copyright (c) 2019 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add cache layer
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add sload compression support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "fsck.h"
#include <libgen.h>
#include <ctype.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include "quotaio.h"
#include "compress.h"
#ifdef WITH_INJECT
#include "inject.h"
#else
static void inject_usage(void)
{
	MSG(0, "\ninject.f2fs not supported\n");
	exit(1);
}
#endif

struct f2fs_fsck gfsck;

INIT_FEATURE_TABLE;

#if defined(WITH_SLOAD) || defined(WITH_DUMP)
static char *absolute_path(const char *file)
{
	char *ret;
	char cwd[PATH_MAX];

	if (file[0] != '/') {
		if (getcwd(cwd, PATH_MAX) == NULL) {
			fprintf(stderr, "Failed to getcwd\n");
			exit(EXIT_FAILURE);
		}
		ret = malloc(strlen(cwd) + 1 + strlen(file) + 1);
		if (ret)
			sprintf(ret, "%s/%s", cwd, file);
	} else
		ret = strdup(file);
	return ret;
}
#endif

void fsck_usage()
{
	MSG(0, "\nUsage: fsck.f2fs [options] device\n");
	MSG(0, "[options]:\n");
	MSG(0, "  -a check/fix potential corruption, reported by f2fs\n");
	MSG(0, "  -c <num-cache-entry>  set number of cache entries"
			" (default 0)\n");
	MSG(0, "  -m <max-hash-collision>  set max cache hash collision"
			" (default 16)\n");
	MSG(0, "  -C encoding[:flag1,flag2] Set options for enabling"
			" casefolding\n");
	MSG(0, "  -d debug level [default:0]\n");
	MSG(0, "  -f check/fix entire partition\n");
	MSG(0, "  -g add default options\n");
	MSG(0, "  -H support write hint\n");
	MSG(0, "  -l show superblock/checkpoint\n");
	MSG(0, "  -M show a file map\n");
	MSG(0, "  -O feature1[feature2,feature3,...] e.g. \"encrypt\"\n");
	MSG(0, "  -p preen mode [default:0 the same as -a [0|1|2]]\n");
	MSG(0, "  -S sparse_mode\n");
	MSG(0, "  -t show directory tree\n");
	MSG(0, "  -q preserve quota limits\n");
	MSG(0, "  -y fix all the time\n");
	MSG(0, "  -V print the version number and exit\n");
	MSG(0, "  --dry-run do not really fix corruptions\n");
	MSG(0, "  --no-kernel-check skips detecting kernel change\n");
	MSG(0, "  --kernel-check checks kernel change\n");
	MSG(0, "  --debug-cache to debug cache when -c is used\n");
	MSG(0, "  --nolinear-lookup=X X=1: disable linear lookup, X=0: enable linear lookup\n");
	MSG(0, "  --fault_injection=%%d to enable fault injection with specified injection rate\n");
	MSG(0, "  --fault_type=%%d to configure enabled fault injection type\n");
	exit(1);
}

void dump_usage()
{
	MSG(0, "\nUsage: dump.f2fs [options] device\n");
	MSG(0, "[options]:\n");
	MSG(0, "  -d debug level [default:0]\n");
	MSG(0, "  -i inode no (hex)\n");
	MSG(0, "  -I inode no (hex) scan full disk\n");
	MSG(0, "  -n [NAT dump nid from #1~#2 (decimal), for all 0~-1]\n");
	MSG(0, "  -M show a block map\n");
	MSG(0, "  -s [SIT dump segno from #1~#2 (decimal), for all 0~-1]\n");
	MSG(0, "  -S sparse_mode\n");
	MSG(0, "  -a [SSA dump segno from #1~#2 (decimal), for all 0~-1]\n");
	MSG(0, "  -b blk_addr (in 4KB)\n");
	MSG(0, "  -r dump out from the root inode\n");
	MSG(0, "  -f do not prompt before dumping\n");
	MSG(0, "  -H support write hint\n");
	MSG(0, "  -y alias for -f\n");
	MSG(0, "  -o dump inodes to the given path\n");
	MSG(0, "  -P preserve mode/owner/group for dumped inode\n");
	MSG(0, "  -L Preserves symlinks. Otherwise symlinks are dumped as regular files.\n");
	MSG(0, "  -V print the version number and exit\n");

	exit(1);
}

void defrag_usage()
{
	MSG(0, "\nUsage: defrag.f2fs [options] device\n");
	MSG(0, "[options]:\n");
	MSG(0, "  -d debug level [default:0]\n");
	MSG(0, "  -H support write hint\n");
	MSG(0, "  -s start block address [default: main_blkaddr]\n");
	MSG(0, "  -S sparse_mode\n");
	MSG(0, "  -l length [default:512 (2MB)]\n");
	MSG(0, "  -t target block address [default: main_blkaddr + 2MB]\n");
	MSG(0, "  -i set direction as shrink [default: expand]\n");
	MSG(0, "  -V print the version number and exit\n");
	exit(1);
}

void resize_usage()
{
	MSG(0, "\nUsage: resize.f2fs [options] device\n");
	MSG(0, "[options]:\n");
	MSG(0, "  -d debug level [default:0]\n");
	MSG(0, "  -H support write hint\n");
	MSG(0, "  -o overprovision percentage [default:auto]\n");
	MSG(0, "  -s safe resize (Does not resize metadata)\n");
	MSG(0, "  -t target sectors [default: device size]\n");
	MSG(0, "  -V print the version number and exit\n");
	exit(1);
}

void sload_usage()
{
	MSG(0, "\nUsage: sload.f2fs [options] device\n");
	MSG(0, "[options]:\n");
	MSG(0, "  -C fs_config\n");
	MSG(0, "  -f source directory [path of the source directory]\n");
	MSG(0, "  -p product out directory\n");
	MSG(0, "  -s file_contexts\n");
	MSG(0, "  -S sparse_mode\n");
	MSG(0, "  -t mount point [prefix of target fs path, default:/]\n");
	MSG(0, "  -T timestamp\n");
	MSG(0, "  -P preserve owner: user and group\n");
	MSG(0, "  -c enable compression (default allow policy)\n");
	MSG(0, "    ------------ Compression sub-options -----------------\n");
	MSG(0, "    -L <log-of-blocks-per-cluster>, default 2\n");
	MSG(0, "    -a <algorithm> compression algorithm, default LZ4\n");
	MSG(0, "    -x <ext> compress files except for these extensions.\n");
	MSG(0, "    -i <ext> compress files with these extensions only.\n");
	MSG(0, "    * -i or -x: use it many times for multiple extensions.\n");
	MSG(0, "    * -i and -x cannot be used together..\n");
	MSG(0, "    -m <num> min compressed blocks per cluster\n");
	MSG(0, "    -r read only (to release unused blocks) for compressed "
			"files\n");
	MSG(0, "    ------------------------------------------------------\n");
	MSG(0, "  -d debug level [default:0]\n");
	MSG(0, "  -V print the version number and exit\n");
	exit(1);
}

void label_usage()
{
	MSG(0, "\nUsage: f2fslabel [options] device [volume-label]\n");
	MSG(0, "[options]:\n");
	MSG(0, "  -V print the version number and exit\n");
	exit(1);
}

int is_digits(char *optarg)
{
	unsigned int i;

	for (i = 0; i < strlen(optarg); i++)
		if (!isdigit(optarg[i]))
			break;
	return i == strlen(optarg);
}

static void error_out(char *prog)
{
	if (!strcmp("fsck.f2fs", prog))
		fsck_usage();
	else if (!strcmp("dump.f2fs", prog))
		dump_usage();
	else if (!strcmp("defrag.f2fs", prog))
		defrag_usage();
	else if (!strcmp("resize.f2fs", prog))
		resize_usage();
	else if (!strcmp("sload.f2fs", prog))
		sload_usage();
	else if (!strcmp("f2fslabel", prog))
		label_usage();
	else if (!strcmp("inject.f2fs", prog))
		inject_usage();
	else
		MSG(0, "\nWrong program.\n");
}

static void add_default_options(void)
{
	switch (c.defset) {
	case CONF_ANDROID:
		if (c.func == FSCK) {
			/* -a */
			c.auto_fix = 1;
		} else if (c.func == RESIZE) {
			c.force = 1;
		}

		/*
		 * global config for fsck family tools, including dump,
		 * defrag, resize, sload, label and inject.
		 */

		/* disable nat_bits feature by default */
		c.disabled_feature |= F2FS_FEATURE_NAT_BITS;

		/* enable write hitn by default */
		c.need_whint = true;
		c.whint = WRITE_LIFE_NOT_SET;
	}
	c.quota_fix = 1;
}

void f2fs_parse_options(int argc, char *argv[])
{
	int option = 0;
	char *prog = basename(argv[0]);
	int err = NOERROR;
#ifdef WITH_ANDROID
	int i;

	/* Allow prog names (e.g, sload_f2fs, fsck_f2fs, etc) */
	for (i = 0; i < strlen(prog); i++) {
		if (prog[i] == '_')
			prog[i] = '.';
	}
#endif
	if (argc < 2) {
		MSG(0, "\tError: Device not specified\n");
		error_out(prog);
	}

	if (!strcmp("fsck.f2fs", prog)) {
		const char *option_string = ":aC:c:m:Md:fg:HlO:p:q:StyV";
		int opt = 0, val;
		char *token;
		struct option long_opt[] = {
			{"dry-run", no_argument, 0, 1},
			{"no-kernel-check", no_argument, 0, 2},
			{"kernel-check", no_argument, 0, 3},
			{"debug-cache", no_argument, 0, 4},
			{"nolinear-lookup", required_argument, 0, 5},
			{"fault_injection", required_argument, 0, 6},
			{"fault_type", required_argument, 0, 7},
			{0, 0, 0, 0}
		};

		c.func = FSCK;
		c.cache_config.max_hash_collision = 16;
		c.cache_config.dbg_en = false;
		while ((option = getopt_long(argc, argv, option_string,
						long_opt, &opt)) != EOF) {
			switch (option) {
			case 1:
				c.dry_run = 1;
				MSG(0, "Info: Dry run\n");
				break;
			case 2:
				c.no_kernel_check = 1;
				MSG(0, "Info: No Kernel Check\n");
				break;
			case 3:
				c.no_kernel_check = 0;
				MSG(0, "Info: Do Kernel Check\n");
				break;
			case 4:
				c.cache_config.dbg_en = true;
				break;
			case 5:
				if (!optarg || !strcmp(optarg, "0"))
					c.nolinear_lookup = LINEAR_LOOKUP_ENABLE;
				else
					c.nolinear_lookup = LINEAR_LOOKUP_DISABLE;
				break;
			case 6:
				val = atoi(optarg);
				if ((unsigned int)val <= 1) {
					MSG(0, "\tError: injection rate must be larger "
							"than 1: %d\n", val);
					fsck_usage();
				}
				c.fault_info.inject_rate = val;
				c.fault_info.inject_type = F2FS_ALL_FAULT_TYPE;
				break;
			case 7:
				val = atoi(optarg);
				if (val >= (1UL << (FAULT_MAX))) {
					MSG(0, "\tError: Invalid inject type: %x\n", val);
					fsck_usage();
				}
				c.fault_info.inject_type = val;
				break;
			case 'a':
				c.auto_fix = 1;
				MSG(0, "Info: Automatic fix mode enabled.\n");
				break;
			case 'c':
				c.cache_config.num_cache_entry = atoi(optarg);
				break;
			case 'm':
				c.cache_config.max_hash_collision =
						atoi(optarg);
				break;
			case 'g':
				if (!strcmp(optarg, "android")) {
					c.defset = CONF_ANDROID;
					MSG(0, "Info: Set conf for android\n");
				}
				break;
			case 'H':
				c.need_whint = true;
				c.whint = WRITE_LIFE_NOT_SET;
				break;
			case 'l':
				c.layout = 1;
				break;
			case 'M':
				c.show_file_map = 1;
				break;
			case 'O':
				if (parse_feature(feature_table, optarg))
					fsck_usage();
				break;
			case 'p':
				/* preen mode has different levels:
				 *  0: default level, the same as -a
				 *  1: check meta
				 *  2: same as 0, but will skip some
				 *     check for old kernel
				 */
				if (optarg[0] == '-' || !is_digits(optarg) ||
							optind == argc) {
					MSG(0, "Info: Use default preen mode\n");
					c.preen_mode = PREEN_MODE_0;
					c.auto_fix = 1;
					optind--;
					break;
				}
				c.preen_mode = atoi(optarg);
				if (c.preen_mode < 0)
					c.preen_mode = PREEN_MODE_0;
				else if (c.preen_mode >= PREEN_MODE_MAX)
					c.preen_mode = PREEN_MODE_MAX - 1;
				if (c.preen_mode == PREEN_MODE_0 ||
					c.preen_mode == PREEN_MODE_2)
					c.auto_fix = 1;
				MSG(0, "Info: Fix the reported corruption in "
					"preen mode %d\n", c.preen_mode);
				break;
			case 'd':
				if (optarg[0] == '-') {
					err = ENEED_ARG;
					break;
				} else if (!is_digits(optarg)) {
					err = EWRONG_OPT;
					break;
				}
				c.dbg_lv = atoi(optarg);
				MSG(0, "Info: Debug level = %d\n", c.dbg_lv);
				break;
			case 'f':
			case 'y':
				c.fix_on = 1;
				c.force = 1;
				MSG(0, "Info: Force to fix corruption\n");
				break;
			case 'q':
				c.preserve_limits = atoi(optarg);
				MSG(0, "Info: Preserve quota limits = %d\n",
					c.preserve_limits);
				break;
			case 'S':
				c.sparse_mode = 1;
				break;
			case 't':
				c.show_dentry = 1;
				break;
			case ':':
				if (optopt == 'p') {
					MSG(0, "Info: Use default preen mode\n");
					c.preen_mode = PREEN_MODE_0;
					c.auto_fix = 1;
				} else {
					option = optopt;
					err = ENEED_ARG;
					break;
				}
				break;
			case 'C':
				token = strtok(optarg, ":");
				val = f2fs_str2encoding(token);
				if (val < 0) {
					MSG(0, "\tError: Unknown encoding %s\n", token);
					fsck_usage();
				}
				c.s_encoding = val;
				token = strtok(NULL, "");
				val = f2fs_str2encoding_flags(&token, &c.s_encoding_flags);
				if (val) {
					MSG(0, "\tError: Unknown flag %s\n", token);
					fsck_usage();
				}
				c.feature |= F2FS_FEATURE_CASEFOLD;
				break;
			case 'V':
				show_version(prog);
				exit(0);
			case '?':
				option = optopt;
				fallthrough;
			default:
				err = EUNKNOWN_OPT;
				break;
			}
			if (err != NOERROR)
				break;
		}
	} else if (!strcmp("dump.f2fs", prog)) {
#ifdef WITH_DUMP
		const char *option_string = "d:fi:I:n:LMo:Prs:Sa:b:Vy";
		static struct dump_option dump_opt = {
			.nid = 0,	/* default root ino */
			.start_nat = -1,
			.end_nat = -1,
			.start_sit = -1,
			.end_sit = -1,
			.start_ssa = -1,
			.end_ssa = -1,
			.blk_addr = -1,
			.scan_nid = 0,
			.use_root_nid = 0,
			.base_path = NULL,
		};

		c.func = DUMP;
		while ((option = getopt(argc, argv, option_string)) != EOF) {
			int ret = 0;

			switch (option) {
			case 'd':
				if (!is_digits(optarg)) {
					err = EWRONG_OPT;
					break;
				}
				c.dbg_lv = atoi(optarg);
				MSG(0, "Info: Debug level = %d\n",
							c.dbg_lv);
				break;
			case 'i':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%d",
							&dump_opt.nid);
				else
					ret = sscanf(optarg, "%x",
							&dump_opt.nid);
				break;
			case 'I':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%d",
							&dump_opt.scan_nid);
				else
					ret = sscanf(optarg, "%x",
							&dump_opt.scan_nid);
				break;
			case 'n':
				ret = sscanf(optarg, "%d~%d",
							&dump_opt.start_nat,
							&dump_opt.end_nat);
				break;
			case 'M':
				c.show_file_map = 1;
				break;
			case 's':
				ret = sscanf(optarg, "%d~%d",
							&dump_opt.start_sit,
							&dump_opt.end_sit);
				break;
			case 'S':
				c.sparse_mode = 1;
				break;
			case 'a':
				ret = sscanf(optarg, "%d~%d",
							&dump_opt.start_ssa,
							&dump_opt.end_ssa);
				break;
			case 'b':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%d",
							&dump_opt.blk_addr);
				else
					ret = sscanf(optarg, "%x",
							&dump_opt.blk_addr);
				break;
			case 'y':
			case 'f':
				c.force = 1;
				break;
			case 'r':
				dump_opt.use_root_nid = 1;
				break;
			case 'o':
				dump_opt.base_path = absolute_path(optarg);
				break;
			case 'P':
#if defined(__MINGW32__)
				MSG(0, "-P not supported for Windows\n");
				err = EWRONG_OPT;
#else
				c.preserve_perms = 1;
#endif
				break;
			case 'L':
#if defined(__MINGW32__)
				MSG(0, "-L not supported for Windows\n");
				err = EWRONG_OPT;
#else
				c.preserve_symlinks = 1;
#endif
				break;
			case 'V':
				show_version(prog);
				exit(0);
			default:
				err = EUNKNOWN_OPT;
				break;
			}
			ASSERT(ret >= 0);
			if (err != NOERROR)
				break;
		}

		c.private = &dump_opt;
#endif
	} else if (!strcmp("defrag.f2fs", prog)) {
#ifdef WITH_DEFRAG
		const char *option_string = "d:Hs:Sl:t:iV";

		c.func = DEFRAG;
		while ((option = getopt(argc, argv, option_string)) != EOF) {
			int ret = 0;

			switch (option) {
			case 'd':
				if (!is_digits(optarg)) {
					err = EWRONG_OPT;
					break;
				}
				c.dbg_lv = atoi(optarg);
				MSG(0, "Info: Debug level = %d\n",
							c.dbg_lv);
				break;
			case 'H':
				c.need_whint = true;
				c.whint = WRITE_LIFE_NOT_SET;
				break;
			case 's':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%"PRIu64"",
							&c.defrag_start);
				else
					ret = sscanf(optarg, "%"PRIx64"",
							&c.defrag_start);
				break;
			case 'S':
				c.sparse_mode = 1;
				break;
			case 'l':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%"PRIu64"",
							&c.defrag_len);
				else
					ret = sscanf(optarg, "%"PRIx64"",
							&c.defrag_len);
				break;
			case 't':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%"PRIu64"",
							&c.defrag_target);
				else
					ret = sscanf(optarg, "%"PRIx64"",
							&c.defrag_target);
				break;
			case 'i':
				c.defrag_shrink = 1;
				break;
			case 'V':
				show_version(prog);
				exit(0);
			default:
				err = EUNKNOWN_OPT;
				break;
			}
			ASSERT(ret >= 0);
			if (err != NOERROR)
				break;
		}
#endif
	} else if (!strcmp("resize.f2fs", prog)) {
#ifdef WITH_RESIZE
		const char *option_string = "d:fFHst:o:V";

		c.func = RESIZE;
		while ((option = getopt(argc, argv, option_string)) != EOF) {
			int ret = 0;

			switch (option) {
			case 'd':
				if (!is_digits(optarg)) {
					err = EWRONG_OPT;
					break;
				}
				c.dbg_lv = atoi(optarg);
				MSG(0, "Info: Debug level = %d\n",
							c.dbg_lv);
				break;
			case 'f':
				c.ignore_error = 1;
				MSG(0, "Info: Ignore errors during resize\n");
				break;
			case 'F':
				c.force = 1;
				MSG(0, "Info: Force to resize\n");
				break;
			case 'H':
				c.need_whint = true;
				c.whint = WRITE_LIFE_NOT_SET;
				break;
			case 's':
				c.safe_resize = 1;
				break;
			case 't':
				if (strncmp(optarg, "0x", 2))
					ret = sscanf(optarg, "%"PRIu64"",
							&c.target_sectors);
				else
					ret = sscanf(optarg, "%"PRIx64"",
							&c.target_sectors);
				break;
			case 'o':
				c.new_overprovision = atof(optarg);
				break;
			case 'V':
				show_version(prog);
				exit(0);
			default:
				err = EUNKNOWN_OPT;
				break;
			}
			ASSERT(ret >= 0);
			if (err != NOERROR)
				break;
		}
#endif
	} else if (!strcmp("sload.f2fs", prog)) {
#ifdef WITH_SLOAD
		const char *option_string = "cL:a:i:x:m:rC:d:f:p:s:St:T:VP";
#ifdef HAVE_LIBSELINUX
		int max_nr_opt = (int)sizeof(c.seopt_file) /
			sizeof(c.seopt_file[0]);
		char *token;
#endif
		char *p;

		c.func = SLOAD;
		c.compress.cc.log_cluster_size = 2;
		c.compress.alg = COMPR_LZ4;
		c.compress.min_blocks = 1;
		c.compress.filter_ops = &ext_filter;
		while ((option = getopt(argc, argv, option_string)) != EOF) {
			unsigned int i;
			int val;

			switch (option) {
			case 'c': /* compression support */
				c.compress.enabled = true;
				break;
			case 'L': /* compression: log of blocks-per-cluster */
				c.compress.required = true;
				val = atoi(optarg);
				if (val < MIN_COMPRESS_LOG_SIZE ||
						val > MAX_COMPRESS_LOG_SIZE) {
					MSG(0, "\tError: log of blocks per"
						" cluster must be in the range"
						" of %d .. %d.\n",
						MIN_COMPRESS_LOG_SIZE,
						MAX_COMPRESS_LOG_SIZE);
					error_out(prog);
				}
				c.compress.cc.log_cluster_size = val;
				break;
			case 'a': /* compression: choose algorithm */
				c.compress.required = true;
				c.compress.alg = MAX_COMPRESS_ALGS;
				for (i = 0; i < MAX_COMPRESS_ALGS; i++) {
					if (!strcmp(supported_comp_names[i],
								optarg)) {
						c.compress.alg = i;
						break;
					}
				}
				if (c.compress.alg == MAX_COMPRESS_ALGS) {
					MSG(0, "\tError: Unknown compression"
						" algorithm %s\n", optarg);
					error_out(prog);
				}
				break;
			case 'i': /* compress only these extensions */
				c.compress.required = true;
				if (c.compress.filter == COMPR_FILTER_ALLOW) {
					MSG(0, "\tError: could not mix option"
							" -i and -x\n");
					error_out(prog);
				}
				c.compress.filter = COMPR_FILTER_DENY;
				c.compress.filter_ops->add(optarg);
				break;
			case 'x': /* compress except for these extensions */
				c.compress.required = true;
				if (c.compress.filter == COMPR_FILTER_DENY) {
					MSG(0, "\tError: could not mix option"
							" -i and -x\n");
					error_out(prog);
				}
				c.compress.filter = COMPR_FILTER_ALLOW;
				c.compress.filter_ops->add(optarg);
				break;
			case 'm': /* minimum compressed blocks per cluster */
				c.compress.required = true;
				val = atoi(optarg);
				if (val <= 0) {
					MSG(0, "\tError: minimum compressed"
						" blocks per cluster must be"
						" positive.\n");
					error_out(prog);
				}
				c.compress.min_blocks = val;
				break;
			case 'r': /* for setting FI_COMPRESS_RELEASED */
				c.compress.required = true;
				c.compress.readonly = true;
				break;
			case 'C':
				c.fs_config_file = absolute_path(optarg);
				break;
			case 'd':
				if (!is_digits(optarg)) {
					err = EWRONG_OPT;
					break;
				}
				c.dbg_lv = atoi(optarg);
				MSG(0, "Info: Debug level = %d\n",
						c.dbg_lv);
				break;
			case 'f':
				c.from_dir = absolute_path(optarg);
				break;
			case 'p':
				c.target_out_dir = absolute_path(optarg);
				break;
			case 's':
#ifdef HAVE_LIBSELINUX
				token = strtok(optarg, ",");
				while (token) {
					if (c.nr_opt == max_nr_opt) {
						MSG(0, "\tError: Expected at most %d selinux opts\n",
										max_nr_opt);
						error_out(prog);
					}
					c.seopt_file[c.nr_opt].type =
								SELABEL_OPT_PATH;
					c.seopt_file[c.nr_opt].value =
								absolute_path(token);
					c.nr_opt++;
					token = strtok(NULL, ",");
				}
#else
				MSG(0, "Info: Not support selinux opts\n");
#endif
				break;
			case 'S':
				c.sparse_mode = 1;
				break;
			case 't':
				c.mount_point = (char *)optarg;
				break;
			case 'T':
				c.fixed_time = strtoul(optarg, &p, 0);
				break;
			case 'V':
				show_version(prog);
				exit(0);
			case 'P':
				c.preserve_perms = 1;
				break;
			default:
				err = EUNKNOWN_OPT;
				break;
			}
			if (err != NOERROR)
				break;
		}
		if (c.compress.required && !c.compress.enabled) {
			MSG(0, "\tError: compression sub-options are used"
				" without the compression enable (-c) option\n"
			);
			error_out(prog);
		}
		if (err == NOERROR && c.compress.enabled) {
			c.compress.cc.cluster_size = 1
				<< c.compress.cc.log_cluster_size;
			if (c.compress.filter == COMPR_FILTER_UNASSIGNED)
				c.compress.filter = COMPR_FILTER_ALLOW;
			if (c.compress.min_blocks >=
					c.compress.cc.cluster_size) {
				MSG(0, "\tError: minimum reduced blocks by"
					" compression per cluster must be at"
					" most one less than blocks per"
					" cluster, i.e. %d\n",
					c.compress.cc.cluster_size - 1);
				error_out(prog);
			}
		}
#endif /* WITH_SLOAD */
	} else if (!strcmp("f2fslabel", prog)) {
#ifdef WITH_LABEL
		const char *option_string = "V";

		c.func = LABEL;
		while ((option = getopt(argc, argv, option_string)) != EOF) {
			switch (option) {
			case 'V':
				show_version(prog);
				exit(0);
			default:
				err = EUNKNOWN_OPT;
				break;
			}
			if (err != NOERROR)
				break;
		}

		if (argc > (optind + 2)) { /* unknown argument(s) is(are) passed */
			optind += 2;
			err = EUNKNOWN_ARG;
		} else if (argc == (optind + 2)) { /* change label */
			c.vol_label = argv[optind + 1];
			argc--;
		} else { /* print label */
			/*
			 * Since vol_label was initialized as "", in order to
			 * distinguish between clear label and print, set
			 * vol_label as NULL for print case
			 */
			c.vol_label = NULL;
		}
#endif /* WITH_LABEL */
	} else if (!strcmp("inject.f2fs", prog)) {
#ifdef WITH_INJECT
		static struct inject_option inject_opt = {
			.sb = -1,
			.cp = -1,
			.nat = -1,
			.sit = -1,
			.idx = -1,
			.nid = -1,
		};

		err = inject_parse_options(argc, argv, &inject_opt);
		if (err < 0) {
			err = EWRONG_OPT;
		}

		c.func = INJECT;
		c.private = &inject_opt;
#endif /* WITH_INJECT */
	}

#if defined(__MINGW32__)
	if (c.need_whint) {
		MSG(0, "-H not supported for Windows\n");
		err = EWRONG_OPT;
	}
#endif
	if (err == NOERROR) {
		add_default_options();

		if (optind >= argc) {
			MSG(0, "\tError: Device not specified\n");
			error_out(prog);
		}

		c.devices[0].path = strdup(argv[optind]);
		if (argc > (optind + 1)) {
			c.dbg_lv = 0;
			err = EUNKNOWN_ARG;
		}
		if (err == NOERROR)
			return;
	}

	check_block_struct_sizes();
	/* print out error */
	switch (err) {
	case EWRONG_OPT:
		MSG(0, "\tError: Wrong option -%c %s\n", option, optarg);
		break;
	case ENEED_ARG:
		MSG(0, "\tError: Need argument for -%c\n", option);
		break;
	case EUNKNOWN_OPT:
		MSG(0, "\tError: Unknown option %c\n", option);
		break;
	case EUNKNOWN_ARG:
		MSG(0, "\tError: Unknown argument %s\n", argv[optind]);
		break;
	}
	error_out(prog);
}

static int do_fsck(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	u32 flag = le32_to_cpu(ckpt->ckpt_flags);
	u32 blk_cnt;
	struct f2fs_compr_blk_cnt cbc;
	struct child_info child = { 0 };
	errcode_t ret;

	fsck_init(sbi);

	print_cp_state(flag);

	if (c.roll_forward && c.zoned_model == F2FS_ZONED_HM)
		save_curseg_warm_node_info(sbi);

	fsck_chk_and_fix_write_pointers(sbi);

	fsck_chk_curseg_info(sbi);

	if (!c.fix_on && !c.bug_on) {
		switch (c.preen_mode) {
		case PREEN_MODE_1:
			if (fsck_chk_meta(sbi)) {
				MSG(0, "[FSCK] F2FS metadata   [Fail]");
				MSG(0, "\tError: meta does not match, "
					"force check all\n");
			} else {
				MSG(0, "[FSCK] F2FS metadata   [Ok..]");
				fsck_free(sbi);
				return FSCK_SUCCESS;
			}

			if (!c.ro)
				c.fix_on = 1;
			break;
		}
	} else if (c.preen_mode) {
		/*
		 * we can hit this in 3 situations:
		 *  1. fsck -f, fix_on has already been set to 1 when
		 *     parsing options;
		 *  2. fsck -a && CP_FSCK_FLAG is set, fix_on has already
		 *     been set to 1 when checking CP_FSCK_FLAG;
		 *  3. fsck -p 1 && error is detected, then bug_on is set,
		 *     we set fix_on = 1 here, so that fsck can fix errors
		 *     automatically
		*/
		c.fix_on = 1;
	}

	fsck_chk_checkpoint(sbi);

	fsck_chk_quota_node(sbi);

	/* Traverse all block recursively from root inode */
	blk_cnt = 1;
	cbc.cnt = 0;
	cbc.cheader_pgofs = CHEADER_PGOFS_NONE;

	if (c.feature & F2FS_FEATURE_QUOTA_INO) {
		ret = quota_init_context(sbi);
		if (ret) {
			ASSERT_MSG("quota_init_context failure: %d", ret);
			return FSCK_OPERATIONAL_ERROR;
		}
	}
	fsck_chk_orphan_node(sbi);

	if (fsck_sanity_check_nat(sbi, sbi->root_ino_num))
		fsck_chk_root_inode(sbi);

	child.p_ino = sbi->root_ino_num;
	fsck_chk_node_blk(sbi, NULL, sbi->root_ino_num,
			F2FS_FT_DIR, TYPE_INODE, &blk_cnt, &cbc, &child);
	fsck_chk_quota_files(sbi);

	ret = fsck_verify(sbi);
	fsck_free(sbi);

	if (!c.bug_on)
		return FSCK_SUCCESS;
	if (!ret)
		return FSCK_ERROR_CORRECTED;
	return FSCK_ERRORS_LEFT_UNCORRECTED;
}

#ifdef WITH_DUMP
static void do_dump(struct f2fs_sb_info *sbi)
{
	struct dump_option *opt = (struct dump_option *)c.private;
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	u32 flag = le32_to_cpu(ckpt->ckpt_flags);

	if (opt->use_root_nid)
		opt->nid = sbi->root_ino_num;

	if (opt->end_nat == -1)
		opt->end_nat = NM_I(sbi)->max_nid;
	if (opt->end_sit == -1)
		opt->end_sit = SM_I(sbi)->main_segments;
	if (opt->end_ssa == -1)
		opt->end_ssa = SM_I(sbi)->main_segments;
	if (opt->start_nat != -1)
		nat_dump(sbi, opt->start_nat, opt->end_nat);
	if (opt->start_sit != -1)
		sit_dump(sbi, opt->start_sit, opt->end_sit);
	if (opt->start_ssa != -1)
		ssa_dump(sbi, opt->start_ssa, opt->end_ssa);
	if (opt->blk_addr != -1)
		dump_info_from_blkaddr(sbi, opt->blk_addr);
	if (opt->nid)
		dump_node(sbi, opt->nid, c.force, opt->base_path, 1, 1, NULL);
	if (opt->scan_nid)
		dump_node_scan_disk(sbi, opt->scan_nid);

	print_cp_state(flag);

}
#endif

#ifdef WITH_DEFRAG
static int do_defrag(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);

	if (get_sb(feature) & F2FS_FEATURE_RO) {
		MSG(0, "Not support on readonly image.\n");
		return -1;
	}

	if (get_sb(feature) & F2FS_FEATURE_DEVICE_ALIAS) {
		MSG(0, "Not support on image with device aliasing feature.\n");
		return -1;
	}

	if (c.defrag_start > get_sb(block_count))
		goto out_range;
	if (c.defrag_start < SM_I(sbi)->main_blkaddr)
		c.defrag_start = SM_I(sbi)->main_blkaddr;

	if (c.defrag_len == 0)
		c.defrag_len = sbi->blocks_per_seg;

	if (c.defrag_start + c.defrag_len > get_sb(block_count))
		c.defrag_len = get_sb(block_count) - c.defrag_start;

	if (c.defrag_target == 0) {
		c.defrag_target = c.defrag_start - 1;
		if (!c.defrag_shrink)
			c.defrag_target += c.defrag_len + 1;
	}

	if (c.defrag_target < SM_I(sbi)->main_blkaddr ||
			c.defrag_target > get_sb(block_count))
		goto out_range;
	if (c.defrag_target >= c.defrag_start &&
		c.defrag_target < c.defrag_start + c.defrag_len)
		goto out_range;

	if (c.defrag_start > c.defrag_target)
		MSG(0, "Info: Move 0x%"PRIx64" <- [0x%"PRIx64"-0x%"PRIx64"]\n",
				c.defrag_target,
				c.defrag_start,
				c.defrag_start + c.defrag_len - 1);
	else
		MSG(0, "Info: Move [0x%"PRIx64"-0x%"PRIx64"] -> 0x%"PRIx64"\n",
				c.defrag_start,
				c.defrag_start + c.defrag_len - 1,
				c.defrag_target);

	return f2fs_defragment(sbi, c.defrag_start, c.defrag_len,
			c.defrag_target, c.defrag_shrink);
out_range:
	ASSERT_MSG("Out-of-range [0x%"PRIx64" ~ 0x%"PRIx64"] to 0x%"PRIx64"",
				c.defrag_start,
				c.defrag_start + c.defrag_len - 1,
				c.defrag_target);
	return -1;
}
#endif

#ifdef WITH_RESIZE
static int do_resize(struct f2fs_sb_info *sbi)
{
	char answer[255] = {0};
	int ret;

	if (!c.force) {
retry:
		printf("\nResize operation is currently experimental, please "
			"backup your data.\nDo you want to continue? [y/n]");
		ret = scanf("%s", answer);
		ASSERT(ret >= 0);
		if (!strcasecmp(answer, "y"))
			printf("Proceeding to resize\n");
		else if (!strcasecmp(answer, "n"))
			return 0;
		else
			goto retry;
	}

	if (!c.target_sectors)
		c.target_sectors = c.total_sectors;

	if (c.target_sectors > c.total_sectors) {
		ASSERT_MSG("Out-of-range Target=0x%"PRIx64" / 0x%"PRIx64"",
				c.target_sectors, c.total_sectors);
		return -1;
	}

	return f2fs_resize(sbi);
}
#endif

#ifdef WITH_SLOAD
static int init_compr(struct f2fs_sb_info *sbi)
{
	if (!c.compress.enabled)
		return 0;

	if (!(sbi->raw_super->feature
			& cpu_to_le32(F2FS_FEATURE_COMPRESSION))) {
		MSG(0, "Error: Compression (-c) was requested "
			"but the file system is not created "
			"with such feature.\n");
		return -1;
	}
	if (!supported_comp_ops[c.compress.alg].init) {
		MSG(0, "Error: The selected compression algorithm is not"
				" supported\n");
		return -1;
	}
	c.compress.ops = supported_comp_ops + c.compress.alg;
	c.compress.ops->init(&c.compress.cc);
	c.compress.ops->reset(&c.compress.cc);
	c.compress.cc.rlen = c.compress.cc.cluster_size * F2FS_BLKSIZE;
	return 0;
}

static int do_sload(struct f2fs_sb_info *sbi)
{
	if (!c.from_dir) {
		MSG(0, "Info: No source directory, but it's okay.\n");
		return 0;
	}
	if (!c.mount_point)
		c.mount_point = "/";

	if (init_compr(sbi))
		return -1;

	return f2fs_sload(sbi);
}
#endif

#ifdef WITH_LABEL
static int do_label(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);

	if (!c.vol_label) {
		char label[MAX_VOLUME_NAME];

		utf16_to_utf8(label, (const char *)sb->volume_name,
			      MAX_VOLUME_NAME, MAX_VOLUME_NAME);
		MSG(0, "Info: volume label = %s\n", label);
		return 0;
	}

	if (strlen(c.vol_label) > MAX_VOLUME_NAME) {
		ERR_MSG("Label should not exceed %d characters\n", MAX_VOLUME_NAME);
		return -1;
	}

	utf8_to_utf16((char *)sb->volume_name, (const char *)c.vol_label,
		      MAX_VOLUME_NAME, strlen(c.vol_label));

	update_superblock(sb, SB_MASK_ALL);

	MSG(0, "Info: volume label is changed to %s\n", c.vol_label);

	return 0;
}
#endif

#ifdef HAVE_MACH_TIME_H
static u64 get_boottime_ns()
{
	return mach_absolute_time();
}
#elif defined(HAVE_CLOCK_GETTIME) && defined(HAVE_CLOCK_BOOTTIME)
static u64 get_boottime_ns()
{
	struct timespec t;
	t.tv_sec = t.tv_nsec = 0;
	clock_gettime(CLOCK_BOOTTIME, &t);
	return (u64)t.tv_sec * 1000000000LL + t.tv_nsec;
}
#else
static u64 get_boottime_ns()
{
	return 0;
}
#endif

int main(int argc, char **argv)
{
	struct f2fs_sb_info *sbi;
	int ret = 0, ret2;
	u64 start = get_boottime_ns();

	f2fs_init_configuration();

	f2fs_parse_options(argc, argv);

	if (c.func != DUMP && (ret = f2fs_devs_are_umounted()) < 0) {
		if (ret == -EBUSY) {
			ret = -1;
			if (c.func == FSCK)
				ret = FSCK_OPERATIONAL_ERROR;
			goto quick_err;
		}
		if (!c.ro || c.func == DEFRAG) {
			MSG(0, "\tError: Not available on mounted device!\n");
			ret = -1;
			if (c.func == FSCK)
				ret = FSCK_OPERATIONAL_ERROR;
			goto quick_err;
		}

		/* allow ro-mounted partition */
		if (c.force) {
			MSG(0, "Info: Force to check/repair FS on RO mounted device\n");
		} else {
			MSG(0, "Info: Check FS only on RO mounted device\n");
			c.fix_on = 0;
			c.auto_fix = 0;
		}
	}

	/* Get device */
	if (f2fs_get_device_info() < 0 || f2fs_get_f2fs_info() < 0) {
		ret = -1;
		if (c.func == FSCK)
			ret = FSCK_OPERATIONAL_ERROR;
		goto quick_err;
	}

fsck_again:
	memset(&gfsck, 0, sizeof(gfsck));
	gfsck.sbi.fsck = &gfsck;
	sbi = &gfsck.sbi;

	ret = f2fs_do_mount(sbi);
	if (ret != 0) {
		if (ret == 1) {
			MSG(0, "Info: No error was reported\n");
			ret = 0;
		}
		goto out_err;
	}

	switch (c.func) {
	case FSCK:
		ret = do_fsck(sbi);
		break;
#ifdef WITH_DUMP
	case DUMP:
		do_dump(sbi);
		break;
#endif
#ifdef WITH_DEFRAG
	case DEFRAG:
		ret = do_defrag(sbi);
		if (ret)
			goto out_err;
		break;
#endif
#ifdef WITH_RESIZE
	case RESIZE:
		if (do_resize(sbi))
			goto out_err;
		break;
#endif
#ifdef WITH_SLOAD
	case SLOAD:
		if (do_sload(sbi))
			goto out_err;

		ret = f2fs_sparse_initialize_meta(sbi);
		if (ret < 0)
			goto out_err;

		f2fs_do_umount(sbi);

		/* fsck to fix missing quota */
		c.func = FSCK;
		c.fix_on = 1;
		goto fsck_again;
#endif
#ifdef WITH_LABEL
	case LABEL:
		if (do_label(sbi))
			goto out_err;
		break;
#endif
#ifdef WITH_INJECT
	case INJECT:
		if (do_inject(sbi))
			goto out_err;
		break;
#endif
	default:
		ERR_MSG("Wrong program name\n");
		ASSERT(0);
	}

	f2fs_do_umount(sbi);

	if (c.func == FSCK && c.bug_on) {
		if (!c.ro && c.fix_on == 0 && c.auto_fix == 0 && !c.dry_run) {
			char ans[255] = {0};
retry:
			printf("Do you want to fix this partition? [Y/N] ");
			ret2 = scanf("%s", ans);
			ASSERT(ret2 >= 0);
			if (!strcasecmp(ans, "y"))
				c.fix_on = 1;
			else if (!strcasecmp(ans, "n"))
				c.fix_on = 0;
			else
				goto retry;

			if (c.fix_on)
				goto fsck_again;
		}
	}
	ret2 = f2fs_finalize_device();
	if (ret2) {
		if (c.func == FSCK)
			return FSCK_OPERATIONAL_ERROR;
		return ret2;
	}

	if (c.func == SLOAD)
		c.compress.filter_ops->destroy();

	if (!c.show_file_map)
		printf("\nDone: %lf secs\n", (get_boottime_ns() - start) / 1000000000.0);
	return ret;

out_err:
	if (sbi->ckpt)
		free(sbi->ckpt);
	if (sbi->raw_super)
		free(sbi->raw_super);
quick_err:
	f2fs_release_sparse_resource();
	return ret;
}
