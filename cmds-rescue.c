/*
 * Copyright (C) 2013 SUSE.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "kerncompat.h"

#include <getopt.h>
#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "commands.h"
#include "utils.h"

static const char * const rescue_cmd_group_usage[] = {
	"btrfs rescue <command> [options] <path>",
	NULL
};

int btrfs_recover_chunk_tree(char *path, int verbose, int yes);
int btrfs_recover_superblocks(char *path, int verbose, int yes);

static const char * const cmd_rescue_chunk_recover_usage[] = {
	"btrfs rescue chunk-recover [options] <device>",
	"Recover the chunk tree by scanning the devices one by one.",
	"",
	"-y	Assume an answer of `yes' to all questions",
	"-v	Verbose mode",
	"-h	Help",
	NULL
};

static const char * const cmd_rescue_super_recover_usage[] = {
	"btrfs rescue super-recover [options] <device>",
	"Recover bad superblocks from good copies",
	"",
	"-y	Assume an answer of `yes' to all questions",
	"-v	Verbose mode",
	NULL
};

static int cmd_rescue_chunk_recover(int argc, char *argv[])
{
	int ret = 0;
	char *file;
	int yes = 0;
	int verbose = 0;

	while (1) {
		int c = getopt(argc, argv, "yvh");
		if (c < 0)
			break;
		switch (c) {
		case 'y':
			yes = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
		default:
			usage(cmd_rescue_chunk_recover_usage);
		}
	}

	argc = argc - optind;
	if (check_argc_exact(argc, 1))
		usage(cmd_rescue_chunk_recover_usage);

	file = argv[optind];

	ret = check_mounted(file);
	if (ret < 0) {
		fprintf(stderr, "Could not check mount status: %s\n",
			strerror(-ret));
		return 1;
	} else if (ret) {
		fprintf(stderr, "the device is busy\n");
		return 1;
	}

	ret = btrfs_recover_chunk_tree(file, verbose, yes);
	if (!ret) {
		fprintf(stdout, "Recover the chunk tree successfully.\n");
	} else if (ret > 0) {
		ret = 0;
		fprintf(stdout, "Abort to rebuild the on-disk chunk tree.\n");
	} else {
		fprintf(stdout, "Fail to recover the chunk tree.\n");
	}
	return ret;
}

/*
 * return codes:
 *   0 : All superblocks are valid, no need to recover
 *   1 : Usage or syntax error
 *   2 : Recover all bad superblocks successfully
 *   3 : Fail to Recover bad supeblocks
 *   4 : Abort to recover bad superblocks
 */
static int cmd_rescue_super_recover(int argc, char **argv)
{
	int ret;
	int verbose = 0;
	int yes = 0;
	char *dname;

	while (1) {
		int c = getopt(argc, argv, "vy");
		if (c < 0)
			break;
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'y':
			yes = 1;
			break;
		default:
			usage(cmd_rescue_super_recover_usage);
		}
	}
	argc = argc - optind;
	if (check_argc_exact(argc, 1))
		usage(cmd_rescue_super_recover_usage);

	dname = argv[optind];
	ret = check_mounted(dname);
	if (ret < 0) {
		fprintf(stderr, "Could not check mount status: %s\n",
			strerror(-ret));
		return 1;
	} else if (ret) {
		fprintf(stderr, "the device is busy\n");
		return 1;
	}
	ret = btrfs_recover_superblocks(dname, verbose, yes);
	return ret;
}

static const char * const cmd_rescue_zero_log_usage[] = {
	"btrfs rescue zero-log <device>",
	"Clear the tree log. Usable if it's corrupted and prevents mount.",
	"",
	NULL
};

static int cmd_rescue_zero_log(int argc, char **argv)
{
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans;
	struct btrfs_super_block *sb;
	char *devname;
	int ret;

	if (check_argc_exact(argc, 2))
		usage(cmd_rescue_zero_log_usage);

	devname = argv[optind];
	ret = check_mounted(devname);
	if (ret < 0) {
		fprintf(stderr, "Could not check mount status: %s\n", strerror(-ret));
		goto out;
	} else if (ret) {
		fprintf(stderr, "%s is currently mounted. Aborting.\n", devname);
		ret = -EBUSY;
	}

	root = open_ctree(devname, 0, OPEN_CTREE_WRITES | OPEN_CTREE_PARTIAL);
	if (!root) {
		fprintf(stderr, "Could not open ctree\n");
		return 1;
	}

	sb = root->fs_info->super_copy;
	printf("Clearing log on %s, previous log_root %llu, level %u\n",
			devname,
			(unsigned long long)btrfs_super_log_root(sb),
			(unsigned)btrfs_super_log_root_level(sb));
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_super_log_root(sb, 0);
	btrfs_set_super_log_root_level(sb, 0);
	btrfs_commit_transaction(trans, root);
	close_ctree(root);

out:
	return !!ret;
}

const char * const cmd_rescue_select_super_usage[] = {
	"btrfs rescue select-super [options] <device>",
	"Select backup superblock as primary and replace it. (dangerous)",
	"",
	"-s super	 number of superblock backup to be selected (1,2)",
	NULL
};

int cmd_rescue_select_super(int argc, char *argv[])
{
	struct btrfs_root *root;
	int ret;
	u64 num = 0;
	u64 bytenr = 0;

	optind = 1;
	while (1) {
		int c = getopt(argc, argv, "s:");

		if (c < 0)
			break;

		switch (c) {
		case 's':
			num = arg_strtou64(optarg);
			if (num >= BTRFS_SUPER_MIRROR_MAX) {
				fprintf(stderr,
					"ERROR: super mirror should be less than: %d\n",
					BTRFS_SUPER_MIRROR_MAX);
				exit(1);
			}
			bytenr = btrfs_sb_offset(((int)num));
			break;
		default:
			usage(cmd_rescue_select_super_usage);
		}
	}
	argc = argc - optind;

	if (check_argc_exact(argc, 1))
		usage(cmd_rescue_select_super_usage);

	if (bytenr == 0) {
		fprintf(stderr, "Please select the super copy with -s\n");
		usage(cmd_rescue_select_super_usage);
	}

	ret = check_mounted(argv[optind]);
	if (ret < 0) {
		fprintf(stderr, "Could not check mount status: %s\n", strerror(-ret));
		return ret;
	} else if(ret) {
		fprintf(stderr, "%s is currently mounted. Aborting.\n", argv[optind]);
		return -EBUSY;
	}

	root = open_ctree(argv[optind], bytenr, 1);
	if (!root) {
		fprintf(stderr, "Open ctree failed\n");
		return 1;
	}

	/* make the super writing code think we've read the first super */
	root->fs_info->super_bytenr = BTRFS_SUPER_INFO_OFFSET;
	ret = write_all_supers(root);

	if (ret) {
		fprintf(stderr, "ERROR: cannot write superblocks\n");
		return 1;
	}

	/*
	 * We don't close the ctree or anything, because we don't want a real
	 * transaction commit.  We just want the super copy we pulled off the
	 * disk to overwrite all the other copies
	 */
	printf("Using SB copy %llu, bytenr %llu and writing to primary\n",
			(unsigned long long)num, (unsigned long long)bytenr);
	return ret;
}

static const char rescue_cmd_group_info[] =
"toolbox for specific rescue operations";

const struct cmd_group rescue_cmd_group = {
	rescue_cmd_group_usage, rescue_cmd_group_info, {
		{ "chunk-recover", cmd_rescue_chunk_recover,
			cmd_rescue_chunk_recover_usage, NULL, 0},
		{ "super-recover", cmd_rescue_super_recover,
			cmd_rescue_super_recover_usage, NULL, 0},
		{ "select-super", cmd_rescue_select_super,
			cmd_rescue_select_super_usage, NULL, 0},
		{ "zero-log", cmd_rescue_zero_log,
			cmd_rescue_zero_log_usage, NULL, 0},
		NULL_CMD_STRUCT
	}
};

int cmd_rescue(int argc, char **argv)
{
	return handle_command_group(&rescue_cmd_group, argc, argv);
}
