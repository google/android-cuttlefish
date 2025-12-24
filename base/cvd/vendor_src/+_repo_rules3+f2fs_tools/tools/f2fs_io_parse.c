/*
 * f2fs IO tracer
 *
 * Copyright (c) 2014 Motorola Mobility
 * Copyright (c) 2014 Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/queue.h>
#include <assert.h>
#include <locale.h>

#define P_NAMELEN	16

/* For global trace methods */
enum show_type {
	SHOW_PID,
	SHOW_FTYPE,
	SHOW_ALL,
};

enum trace_types {
	TP_PID,
	TP_IOS,
	TP_MAX,
};

struct tps {
	enum trace_types type;
	const char *name;
};

struct tps trace_points[] = {
	{ TP_PID,	"f2fs_trace_pid" },
	{ TP_IOS,	"f2fs_trace_ios" },
};

/* For f2fs_trace_pid and f2fs_trace_ios */
enum rw_type {
	READ,
	WRITE,
	MAX_RW,
};

enum file_type {
	__NORMAL_FILE,
	__DIR_FILE,
	__NODE_FILE,
	__META_FILE,
	__ATOMIC_FILE,
	__VOLATILE_FILE,
	__MISC_FILE,
	__NR_FILES,
};

char *file_type_string[] = {
	"User      ",
	"Dir       ",
	"Node      ",
	"Meta      ",
	"Atomic    ",
	"Voltile   ",
	"Misc      ",
};

struct pid_ent {
	int pid;
	char name[P_NAMELEN];
	unsigned long long io[__NR_FILES][MAX_RW];
	unsigned long long total_io[MAX_RW];
	LIST_ENTRY(pid_ent) ptr;
};

/* global variables */
int major = 0, minor = 0;
int show_option = SHOW_ALL;
unsigned long long total_io[__NR_FILES][MAX_RW];

LIST_HEAD(plist, pid_ent) pid_info;

/* Functions */
static inline int atoh(char *str)
{
	int val;
	sscanf(str, "%x", &val);
	return val;
}

static void do_init()
{
	struct pid_ent *misc;

	misc = calloc(1, sizeof(struct pid_ent));
	assert(misc);

	LIST_INIT(&pid_info);
	LIST_INSERT_HEAD(&pid_info, misc, ptr);
}

void show_usage()
{
	printf("\nUsage: parse.f2fs [options] log_file\n");
	printf("[options]:\n");
	printf("  -a RW sorted by pid & file types\n");
	printf("  -f RW sorted by file types\n");
	printf("  -p RW sorted by pid\n");
	printf("  -m major number\n");
	printf("  -n minor number\n");
	exit(1);
}

static int parse_options(int argc, char *argv[])
{
	const char *option_string = "fm:n:p";
	int option = 0;

	while ((option = getopt(argc, argv, option_string)) != EOF) {
		switch (option) {
		case 'f':
			show_option = SHOW_FTYPE;
			break;
		case 'm':
			major = atoh(optarg);
			break;
		case 'n':
			minor = atoh(optarg);
			break;
		case 'p':
			show_option = SHOW_PID;
			break;
		default:
			printf("\tError: Unknown option %c\n", option);
			show_usage();
			break;
		}
	}
	if ((optind + 1) != argc) {
		printf("\tError: Log file is not specified.\n");
		show_usage();
	}
	return optind;
}

struct pid_ent *get_pid_entry(int pid)
{
	struct pid_ent *entry;

	LIST_FOREACH(entry, &pid_info, ptr) {
		if (entry->pid == pid)
			return entry;
	}
	return LIST_FIRST(&pid_info);
}

static void handle_tp_pid(char *ptr)
{
	struct pid_ent *pent;

	pent = calloc(1, sizeof(struct pid_ent));
	assert(pent);

	ptr = strtok(NULL, " ");
	pent->pid = atoh(ptr);

	ptr = strtok(NULL, " ");
	strcpy(pent->name, ptr);

	LIST_INSERT_HEAD(&pid_info, pent, ptr);
}

static void handle_tp_ios(char *ptr)
{
	int pid, type, rw, len;
	struct pid_ent *p;

	ptr = strtok(NULL, " ");
	pid = atoh(ptr);

	ptr = strtok(NULL, " ");
	ptr = strtok(NULL, " ");
	type = atoh(ptr);

	ptr = strtok(NULL, " ");
	rw = atoh(ptr);

	ptr = strtok(NULL, " ");
	/* int op_flags = atoh(ptr) */
	ptr = strtok(NULL, " ");
	/* unsigned long long blkaddr = atoh(ptr); */

	ptr = strtok(NULL, " ");
	len = atoh(ptr);

	/* update per-pid stat */
	p = get_pid_entry(pid);
	p->io[type][rw & 0x1] += len;
	p->total_io[rw & 0x1] += len;

	/* update total stat */
	total_io[type][rw & 0x1] += len;
}

static void do_parse(FILE *file)
{
	char line[300];
	char *ptr;
	int i;

	while (fgets(line, sizeof(line), file) != NULL) {
		ptr = strtok(line, ":");

		ptr = strtok(NULL, " :");

		for (i = 0; i < TP_MAX; i++) {
			if (!strcmp(ptr, trace_points[i].name))
				break;
		}
		if (i == TP_MAX)
			continue;
		ptr = strtok(NULL, " :");
		if (major && major != atoh(ptr))
			continue;
		ptr = strtok(NULL, " :");
		if (minor && minor != atoh(ptr))
			continue;

		switch (i) {
		case TP_PID:
			handle_tp_pid(ptr);
			break;
		case TP_IOS:
			handle_tp_ios(ptr);
			break;
		}
	}
}

static void __print_pid()
{
	struct pid_ent *entry;
	int i;

	setlocale(LC_ALL, "");
	printf("%8s %16s %17s ||", "PID", "NAME", "R/W in 4KB");
	for (i = 0; i < __NR_FILES; i++)
		printf(" %17s |", file_type_string[i]);
	printf("\n");

	LIST_FOREACH(entry, &pid_info, ptr) {
		printf("%8x %16s %'8lld %'8lld ||",
				entry->pid, entry->name,
				entry->total_io[READ],
				entry->total_io[WRITE]);
		for (i = 0; i < __NR_FILES; i++)
			printf(" %'8lld %'8lld |",
				entry->io[i][READ],
				entry->io[i][WRITE]);
		printf("\n");
	}
}

static void __print_ftype()
{
	int i;

	setlocale(LC_ALL, "");
	printf("\n===== Data R/W in 4KB according to File types =====\n");
	for (i = 0; i < __NR_FILES; i++)
		printf(" %17s |", file_type_string[i]);
	printf("\n");

	for (i = 0; i < __NR_FILES; i++)
		printf(" %'8lld %'8lld |",
				total_io[i][READ],
				total_io[i][WRITE]);
	printf("\n");
}

static void do_print()
{
	switch (show_option) {
	case SHOW_PID:
		__print_pid();
		break;
	case SHOW_FTYPE:
		__print_ftype();
		break;
	case SHOW_ALL:
		__print_pid();
		printf("\n\n");
		__print_ftype();
		break;
	}
}

int main(int argc, char **argv)
{
	FILE *file;
	int opt;

	opt = parse_options(argc, argv);

	file = fopen(argv[opt], "r");
	if (!file) {
		perror("open log file");
		exit(EXIT_FAILURE);
	}

	do_init();

	do_parse(file);

	do_print();

	fclose(file);
	return 0;
}
