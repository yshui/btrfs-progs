/*
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>

#include "kerncompat.h"
#include "ioctl.h"
#include "utils.h"
#include "ctree.h"
#include "send-utils.h"
#include "disk-io.h"
#include "commands.h"
#include "btrfs-list.h"
#include "cmds-inspect-dump-tree.h"
#include "cmds-inspect-dump-super.h"
#include "cmds-inspect-tree-stats.h"

static const char * const inspect_cmd_group_usage[] = {
	"btrfs inspect-internal <command> <args>",
	NULL
};

static int __ino_to_path_fd(u64 inum, int fd, int verbose, const char *prepend)
{
	int ret;
	int i;
	struct btrfs_ioctl_ino_path_args ipa;
	struct btrfs_data_container fspath[PATH_MAX];

	memset(fspath, 0, sizeof(*fspath));
	ipa.inum = inum;
	ipa.size = PATH_MAX;
	ipa.fspath = ptr_to_u64(fspath);

	ret = ioctl(fd, BTRFS_IOC_INO_PATHS, &ipa);
	if (ret < 0) {
		error("ino paths ioctl: %s", strerror(errno));
		goto out;
	}

	if (verbose)
		printf("ioctl ret=%d, bytes_left=%lu, bytes_missing=%lu, "
			"cnt=%d, missed=%d\n", ret,
			(unsigned long)fspath->bytes_left,
			(unsigned long)fspath->bytes_missing,
			fspath->elem_cnt, fspath->elem_missed);

	for (i = 0; i < fspath->elem_cnt; ++i) {
		u64 ptr;
		char *str;
		ptr = (u64)(unsigned long)fspath->val;
		ptr += fspath->val[i];
		str = (char *)(unsigned long)ptr;
		if (prepend)
			printf("%s/%s\n", prepend, str);
		else
			printf("%s\n", str);
	}

out:
	return !!ret;
}

static const char * const cmd_inspect_inode_resolve_usage[] = {
	"btrfs inspect-internal inode-resolve [-v] <inode> <path>",
	"Get file system paths for the given inode",
	"",
	"-v   verbose mode",
	NULL
};

static int cmd_inspect_inode_resolve(int argc, char **argv)
{
	int fd;
	int verbose = 0;
	int ret;
	DIR *dirstream = NULL;

	optind = 1;
	while (1) {
		int c = getopt(argc, argv, "v");
		if (c < 0)
			break;

		switch (c) {
		case 'v':
			verbose = 1;
			break;
		default:
			usage(cmd_inspect_inode_resolve_usage);
		}
	}

	if (check_argc_exact(argc - optind, 2))
		usage(cmd_inspect_inode_resolve_usage);

	fd = btrfs_open_dir(argv[optind + 1], &dirstream, 1);
	if (fd < 0)
		return 1;

	ret = __ino_to_path_fd(arg_strtou64(argv[optind]), fd, verbose,
			       argv[optind+1]);
	close_file_or_dir(fd, dirstream);
	return !!ret;

}

static const char * const cmd_inspect_logical_resolve_usage[] = {
	"btrfs inspect-internal logical-resolve [-Pv] [-s bufsize] <logical> <path>",
	"Get file system paths for the given logical address",
	"-P          skip the path resolving and print the inodes instead",
	"-v          verbose mode",
	"-s bufsize  set inode container's size. This is used to increase inode",
	"            container's size in case it is not enough to read all the ",
	"            resolved results. The max value one can set is 64k",
	NULL
};

static int cmd_inspect_logical_resolve(int argc, char **argv)
{
	int ret;
	int fd;
	int i;
	int verbose = 0;
	int getpath = 1;
	int bytes_left;
	struct btrfs_ioctl_logical_ino_args loi;
	struct btrfs_data_container *inodes;
	u64 size = 4096;
	char full_path[4096];
	char *path_ptr;
	DIR *dirstream = NULL;

	optind = 1;
	while (1) {
		int c = getopt(argc, argv, "Pvs:");
		if (c < 0)
			break;

		switch (c) {
		case 'P':
			getpath = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		case 's':
			size = arg_strtou64(optarg);
			break;
		default:
			usage(cmd_inspect_logical_resolve_usage);
		}
	}

	if (check_argc_exact(argc - optind, 2))
		usage(cmd_inspect_logical_resolve_usage);

	size = min(size, (u64)64 * 1024);
	inodes = malloc(size);
	if (!inodes)
		return 1;

	memset(inodes, 0, sizeof(*inodes));
	loi.logical = arg_strtou64(argv[optind]);
	loi.size = size;
	loi.inodes = ptr_to_u64(inodes);

	fd = btrfs_open_dir(argv[optind + 1], &dirstream, 1);
	if (fd < 0) {
		ret = 12;
		goto out;
	}

	ret = ioctl(fd, BTRFS_IOC_LOGICAL_INO, &loi);
	if (ret < 0) {
		error("logical ino ioctl: %s", strerror(errno));
		goto out;
	}

	if (verbose)
		printf("ioctl ret=%d, total_size=%llu, bytes_left=%lu, "
			"bytes_missing=%lu, cnt=%d, missed=%d\n",
			ret, size,
			(unsigned long)inodes->bytes_left,
			(unsigned long)inodes->bytes_missing,
			inodes->elem_cnt, inodes->elem_missed);

	bytes_left = sizeof(full_path);
	ret = snprintf(full_path, bytes_left, "%s/", argv[optind+1]);
	path_ptr = full_path + ret;
	bytes_left -= ret + 1;
	BUG_ON(bytes_left < 0);

	for (i = 0; i < inodes->elem_cnt; i += 3) {
		u64 inum = inodes->val[i];
		u64 offset = inodes->val[i+1];
		u64 root = inodes->val[i+2];
		int path_fd;
		char *name;
		DIR *dirs = NULL;

		if (getpath) {
			name = btrfs_list_path_for_root(fd, root);
			if (IS_ERR(name)) {
				ret = PTR_ERR(name);
				goto out;
			}
			if (!name) {
				path_ptr[-1] = '\0';
				path_fd = fd;
			} else {
				path_ptr[-1] = '/';
				ret = snprintf(path_ptr, bytes_left, "%s",
						name);
				BUG_ON(ret >= bytes_left);
				free(name);
				path_fd = btrfs_open_dir(full_path, &dirs, 1);
				if (path_fd < 0) {
					ret = -ENOENT;
					goto out;
				}
			}
			__ino_to_path_fd(inum, path_fd, verbose, full_path);
			if (path_fd != fd)
				close_file_or_dir(path_fd, dirs);
		} else {
			printf("inode %llu offset %llu root %llu\n", inum,
				offset, root);
		}
	}

out:
	close_file_or_dir(fd, dirstream);
	free(inodes);
	return !!ret;
}

static const char * const cmd_inspect_subvolid_resolve_usage[] = {
	"btrfs inspect-internal subvolid-resolve <subvolid> <path>",
	"Get file system paths for the given subvolume ID.",
	NULL
};

static int cmd_inspect_subvolid_resolve(int argc, char **argv)
{
	int ret;
	int fd = -1;
	u64 subvol_id;
	char path[PATH_MAX];
	DIR *dirstream = NULL;

	clean_args_no_options(argc, argv, cmd_inspect_subvolid_resolve_usage);

	if (check_argc_exact(argc - optind, 2))
		usage(cmd_inspect_subvolid_resolve_usage);

	fd = btrfs_open_dir(argv[optind + 1], &dirstream, 1);
	if (fd < 0) {
		ret = -ENOENT;
		goto out;
	}

	subvol_id = arg_strtou64(argv[optind]);
	ret = btrfs_subvolid_resolve(fd, path, sizeof(path), subvol_id);

	if (ret) {
		error("resolving subvolid %llu error %d",
			(unsigned long long)subvol_id, ret);
		goto out;
	}

	path[PATH_MAX - 1] = '\0';
	printf("%s\n", path);

out:
	close_file_or_dir(fd, dirstream);
	return !!ret;
}

static const char* const cmd_inspect_rootid_usage[] = {
	"btrfs inspect-internal rootid <path>",
	"Get tree ID of the containing subvolume of path.",
	NULL
};

static int cmd_inspect_rootid(int argc, char **argv)
{
	int ret;
	int fd = -1;
	u64 rootid;
	DIR *dirstream = NULL;

	clean_args_no_options(argc, argv, cmd_inspect_rootid_usage);

	if (check_argc_exact(argc - optind, 1))
		usage(cmd_inspect_rootid_usage);

	fd = btrfs_open_dir(argv[optind], &dirstream, 1);
	if (fd < 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = lookup_ino_rootid(fd, &rootid);
	if (ret) {
		error("rootid failed with ret=%d", ret);
		goto out;
	}

	printf("%llu\n", (unsigned long long)rootid);
out:
	close_file_or_dir(fd, dirstream);

	return !!ret;
}

static const char* const cmd_inspect_min_dev_size_usage[] = {
	"btrfs inspect-internal min-dev-size [options] <path>",
	"Get the minimum size the device can be shrunk to. The",
	"device id 1 is used by default.",
	"--id DEVID   specify the device id to query",
	NULL
};

struct dev_extent_elem {
	u64 start;
	/* inclusive end */
	u64 end;
	struct list_head list;
};

static int add_dev_extent(struct list_head *list,
			  const u64 start, const u64 end,
			  const int append)
{
	struct dev_extent_elem *e;

	e = malloc(sizeof(*e));
	if (!e)
		return -ENOMEM;

	e->start = start;
	e->end = end;

	if (append)
		list_add_tail(&e->list, list);
	else
		list_add(&e->list, list);

	return 0;
}

static void free_dev_extent_list(struct list_head *list)
{
	while (!list_empty(list)) {
		struct dev_extent_elem *e;

		e = list_first_entry(list, struct dev_extent_elem, list);
		list_del(&e->list);
		free(e);
	}
}

static int hole_includes_sb_mirror(const u64 start, const u64 end)
{
	int i;
	int ret = 0;

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		u64 bytenr = btrfs_sb_offset(i);

		if (bytenr >= start && bytenr <= end) {
			ret = 1;
			break;
		}
	}

	return ret;
}

static void adjust_dev_min_size(struct list_head *extents,
				struct list_head *holes,
				u64 *min_size)
{
	/*
	 * If relocation of the block group of a device extent must happen (see
	 * below) scratch space is used for the relocation. So track here the
	 * size of the largest device extent that has to be relocated. We track
	 * only the largest and not the sum of the sizes of all relocated block
	 * groups because after each block group is relocated the running
	 * transaction is committed so that pinned space is released.
	 */
	u64 scratch_space = 0;

	/*
	 * List of device extents is sorted by descending order of the extent's
	 * end offset. If some extent goes beyond the computed minimum size,
	 * which initially matches the sum of the lengths of all extents,
	 * we need to check if the extent can be relocated to an hole in the
	 * device between [0, *min_size[ (which is what the resize ioctl does).
	 */
	while (!list_empty(extents)) {
		struct dev_extent_elem *e;
		struct dev_extent_elem *h;
		int found = 0;
		u64 extent_len;
		u64 hole_len = 0;

		e = list_first_entry(extents, struct dev_extent_elem, list);
		if (e->end <= *min_size)
			break;

		/*
		 * Our extent goes beyond the computed *min_size. See if we can
		 * find a hole large enough to relocate it to. If not we must stop
		 * and set *min_size to the end of the extent.
		 */
		extent_len = e->end - e->start + 1;
		list_for_each_entry(h, holes, list) {
			hole_len = h->end - h->start + 1;
			if (hole_len >= extent_len) {
				found = 1;
				break;
			}
		}

		if (!found) {
			*min_size = e->end + 1;
			break;
		}

		/*
		 * If the hole found contains the location for a superblock
		 * mirror, we are pessimistic and require allocating one
		 * more extent of the same size. This is because the block
		 * group could be in the worst case used by a single extent
		 * with a size >= (block_group.length - superblock.size).
		 */
		if (hole_includes_sb_mirror(h->start,
					    h->start + extent_len - 1))
			*min_size += extent_len;

		if (hole_len > extent_len) {
			h->start += extent_len;
		} else {
			list_del(&h->list);
			free(h);
		}

		list_del(&e->list);
		free(e);

		if (extent_len > scratch_space)
			scratch_space = extent_len;
	}

	if (scratch_space) {
		*min_size += scratch_space;
		/*
		 * Chunk allocation requires inserting/updating items in the
		 * chunk tree, so often this can lead to the need of allocating
		 * a new system chunk too, which has a maximum size of 32Mb.
		 */
		*min_size += 32 * 1024 * 1024;
	}
}

static int print_min_dev_size(int fd, u64 devid)
{
	int ret = 1;
	/*
	 * Device allocations starts at 1Mb or at the value passed through the
	 * mount option alloc_start if it's bigger than 1Mb. The alloc_start
	 * option is used for debugging and testing only, and recently the
	 * possibility of deprecating/removing it has been discussed, so we
	 * ignore it here.
	 */
	u64 min_size = 1 * 1024 * 1024ull;
	struct btrfs_ioctl_search_args args;
	struct btrfs_ioctl_search_key *sk = &args.key;
	u64 last_pos = (u64)-1;
	LIST_HEAD(extents);
	LIST_HEAD(holes);

	memset(&args, 0, sizeof(args));
	sk->tree_id = BTRFS_DEV_TREE_OBJECTID;
	sk->min_objectid = devid;
	sk->max_objectid = devid;
	sk->max_type = BTRFS_DEV_EXTENT_KEY;
	sk->min_type = BTRFS_DEV_EXTENT_KEY;
	sk->min_offset = 0;
	sk->max_offset = (u64)-1;
	sk->min_transid = 0;
	sk->max_transid = (u64)-1;
	sk->nr_items = 4096;

	while (1) {
		int i;
		struct btrfs_ioctl_search_header *sh;
		unsigned long off = 0;

		ret = ioctl(fd, BTRFS_IOC_TREE_SEARCH, &args);
		if (ret < 0) {
			error("tree search ioctl: %s", strerror(errno));
			ret = 1;
			goto out;
		}

		if (sk->nr_items == 0)
			break;

		for (i = 0; i < sk->nr_items; i++) {
			struct btrfs_dev_extent *extent;
			u64 len;

			sh = (struct btrfs_ioctl_search_header *)(args.buf +
								  off);
			off += sizeof(*sh);
			extent = (struct btrfs_dev_extent *)(args.buf + off);
			off += btrfs_search_header_len(sh);

			sk->min_objectid = btrfs_search_header_objectid(sh);
			sk->min_type = btrfs_search_header_type(sh);
			sk->min_offset = btrfs_search_header_offset(sh) + 1;

			if (btrfs_search_header_objectid(sh) != devid ||
			    btrfs_search_header_type(sh) != BTRFS_DEV_EXTENT_KEY)
				continue;

			len = btrfs_stack_dev_extent_length(extent);
			min_size += len;
			ret = add_dev_extent(&extents,
				btrfs_search_header_offset(sh),
				btrfs_search_header_offset(sh) + len - 1, 0);

			if (!ret && last_pos != (u64)-1 &&
			    last_pos != btrfs_search_header_offset(sh))
				ret = add_dev_extent(&holes, last_pos,
					btrfs_search_header_offset(sh) - 1, 1);
			if (ret) {
				error("add device extent: %s", strerror(-ret));
				ret = 1;
				goto out;
			}

			last_pos = btrfs_search_header_offset(sh) + len;
		}

		if (sk->min_type != BTRFS_DEV_EXTENT_KEY ||
		    sk->min_objectid != devid)
			break;
	}

	adjust_dev_min_size(&extents, &holes, &min_size);
	printf("%llu bytes (%s)\n", min_size, pretty_size(min_size));
	ret = 0;
out:
	free_dev_extent_list(&extents);
	free_dev_extent_list(&holes);

	return ret;
}

static int cmd_inspect_min_dev_size(int argc, char **argv)
{
	int ret;
	int fd = -1;
	DIR *dirstream = NULL;
	u64 devid = 1;

	while (1) {
		int c;
		enum { GETOPT_VAL_DEVID = 256 };
		static const struct option long_options[] = {
			{ "id", required_argument, NULL, GETOPT_VAL_DEVID },
			{NULL, 0, NULL, 0}
		};

		c = getopt_long(argc, argv, "", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case GETOPT_VAL_DEVID:
			devid = arg_strtou64(optarg);
			break;
		default:
			usage(cmd_inspect_min_dev_size_usage);
		}
	}
	if (check_argc_exact(argc - optind, 1))
		usage(cmd_inspect_min_dev_size_usage);

	fd = btrfs_open_dir(argv[optind], &dirstream, 1);
	if (fd < 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = print_min_dev_size(fd, devid);
	close_file_or_dir(fd, dirstream);
out:
	return !!ret;
}

static const char * const cmd_dump_chunks_usage[] = {
	"btrfs inspect-internal chunk-stats [options] <path>",
	"Show chunks (block groups) layout",
	"Show chunks (block groups) layout for all devices",
	"",
	HELPINFO_UNITS_LONG,
	"--sort=MODE        sort by the physical or logical chunk start",
	"                   MODE is one of pstart or lstart (default: pstart)",
	"--usage            show usage per block group, note this can be slow",
	NULL
};

enum {
	CHUNK_SORT_PSTART,
	CHUNK_SORT_LSTART,
	CHUNK_SORT_DEFAULT = CHUNK_SORT_PSTART
};

struct dump_chunks_entry {
	u64 devid;
	u64 start;
	u64 lstart;
	u64 length;
	u64 flags;
	u64 age;
	u64 used;
	u32 pnumber;
};

struct dump_chunks_ctx {
	unsigned length;
	unsigned size;
	struct dump_chunks_entry *stats;
};

int cmp_cse_devid_start(const void *va, const void *vb)
{
	const struct dump_chunks_entry *a = va;
	const struct dump_chunks_entry *b = vb;

	if (a->devid < b->devid)
		return -1;
	if (a->devid > b->devid)
		return 1;

	if (a->start < b->start)
		return -1;
	if (a->start == b->start) {
		error(
	"chunks start on same offset in the same device: devid %llu start %llu",
		    (unsigned long long)a->devid, (unsigned long long)a->start);
		return 0;
	}
	return 1;
}

int cmp_cse_devid_lstart(const void *va, const void *vb)
{
	const struct dump_chunks_entry *a = va;
	const struct dump_chunks_entry *b = vb;

	if (a->devid < b->devid)
		return -1;
	if (a->devid > b->devid)
		return 1;

	if (a->lstart < b->lstart)
		return -1;
	if (a->lstart == b->lstart) {
		error(
"chunks logically start on same offset in the same device: devid %llu start %llu",
		    (unsigned long long)a->devid, (unsigned long long)a->lstart);
		return 0;
	}
	return 1;
}

void print_dump_chunks(struct dump_chunks_ctx *ctx, unsigned sort_mode,
		unsigned unit_mode, int with_usage)
{
	u64 devid;
	struct dump_chunks_entry e;
	int i;
	int chidx;
	u64 lastend = 0;
	u64 age;

	/*
	 * Chunks are sorted logically as found by the ioctl, we need to sort
	 * them once to find the physical ordering. This is the default mode.
	 */
	qsort(ctx->stats, ctx->length, sizeof(ctx->stats[0]),
			cmp_cse_devid_start);
	devid = 0;
	age = 0;
	for (i = 0; i < ctx->length; i++) {
		e = ctx->stats[i];
		if (e.devid != devid) {
			devid = e.devid;
			age = 0;
		}
		ctx->stats[i].pnumber = age;
		age++;
	}

	if (sort_mode == CHUNK_SORT_LSTART)
		qsort(ctx->stats, ctx->length, sizeof(ctx->stats[0]),
				cmp_cse_devid_lstart);

	devid = 0;
	for (i = 0; i < ctx->length; i++) {
		e = ctx->stats[i];
		if (e.devid != devid) {
			devid = e.devid;
			if (i != 0)
				putchar('\n');
			printf("Chunks on device id: %llu\n", devid);
			printf("PNumber            Type        PStart        Length          PEnd     Age         LStart%s\n",
					with_usage ? "  Usage" : "");
			printf("----------------------------------------------------------------------------------------%s\n",
					with_usage ? "-------" : "");
			chidx = 0;
			lastend = 0;
		}
		if (sort_mode == CHUNK_SORT_PSTART && lastend > 0
		    && e.start != lastend) {
			printf("      .           empty             .  ");
			printf("%12s  ",
				pretty_size_mode(e.start - lastend, unit_mode));
			printf("           .       .              .\n");
		}

		printf("%7u ", e.pnumber);
		printf("%8s/%-6s  ", btrfs_group_type_str(e.flags),
				btrfs_group_profile_str(e.flags));
		printf("%12s  ", pretty_size_mode(e.start, unit_mode));
		printf("%12s  ", pretty_size_mode(e.length, unit_mode));
		printf("%12s  ",
			pretty_size_mode(e.start + e.length - 1, unit_mode));
		printf("%6llu ", e.age);
		printf("%14s", pretty_size_mode(e.lstart, unit_mode));
		if (with_usage)
			printf("  %5.2f", (float)e.used / e.length * 100);
		printf("\n");

		lastend = e.start + e.length;
		chidx++;
	}
}

static u64 fill_usage(int fd, u64 lstart)
{
	struct btrfs_ioctl_search_args args;
	struct btrfs_ioctl_search_key *sk = &args.key;
	struct btrfs_ioctl_search_header sh;
	struct btrfs_block_group_item *item;
	int ret;

	memset(&args, 0, sizeof(args));
	sk->tree_id = BTRFS_EXTENT_TREE_OBJECTID;
	sk->min_objectid = lstart;
	sk->max_objectid = lstart;
	sk->min_type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	sk->max_type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	sk->min_offset = 0;
	sk->max_offset = (u64)-1;
	sk->max_transid = (u64)-1;
	sk->nr_items = 1;

	ret = ioctl(fd, BTRFS_IOC_TREE_SEARCH, &args);
	if (ret < 0) {
		error("cannot perform the search: %s", strerror(errno));
		return 1;
	}
	if (sk->nr_items == 0) {
		warning("blockgroup %llu not found",
				(unsigned long long)lstart);
		return 0;
	}
	if (sk->nr_items > 1) {
		warning("found more than one blockgroup %llu",
				(unsigned long long)lstart);
	}

	memcpy(&sh, args.buf, sizeof(sh));
	item = (struct btrfs_block_group_item*)(args.buf + sizeof(sh));

	return item->used;
}

static int cmd_dump_chunks(int argc, char **argv)
{
	struct btrfs_ioctl_search_args args;
	struct btrfs_ioctl_search_key *sk = &args.key;
	struct btrfs_ioctl_search_header sh;
	unsigned long off = 0;
	u64 *age = 0;
	unsigned age_size = 128;
	int ret;
	int fd;
	int i;
	int e;
	DIR *dirstream = NULL;
	unsigned unit_mode;
	unsigned sort_mode = 0;
	int with_usage = 0;
	const char *path;
	struct dump_chunks_ctx ctx = {
		.length = 0,
		.size = 1024,
		.stats = NULL
	};

	unit_mode = get_unit_mode_from_arg(&argc, argv, 0);

	while (1) {
		int c;
		enum { GETOPT_VAL_SORT = 256, GETOPT_VAL_USAGE };
		static const struct option long_options[] = {
			{"sort", required_argument, NULL, GETOPT_VAL_SORT },
			{"usage", no_argument, NULL, GETOPT_VAL_USAGE },
			{NULL, 0, NULL, 0}
		};

		c = getopt_long(argc, argv, "", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case GETOPT_VAL_SORT:
			if (strcmp(optarg, "pstart") == 0) {
				sort_mode = CHUNK_SORT_PSTART;
			} else if (strcmp(optarg, "lstart") == 0) {
				sort_mode = CHUNK_SORT_LSTART;
			} else {
				error("unknown sort mode: %s", optarg);
				exit(1);
			}
			break;
		case GETOPT_VAL_USAGE:
			with_usage = 1;
			break;
		default:
			usage(cmd_dump_chunks_usage);
		}
	}

	if (check_argc_exact(argc - optind, 1))
		usage(cmd_dump_chunks_usage);

	ctx.stats = calloc(ctx.size, sizeof(ctx.stats[0]));
	if (!ctx.stats)
		goto out_nomem;

	path = argv[optind];

	fd = open_file_or_dir(path, &dirstream);
	if (fd < 0) {
	        error("cannot access '%s': %s", path, strerror(errno));
		return 1;
	}

	memset(&args, 0, sizeof(args));
	sk->tree_id = BTRFS_CHUNK_TREE_OBJECTID;
	sk->min_objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	sk->max_objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	sk->min_type = BTRFS_CHUNK_ITEM_KEY;
	sk->max_type = BTRFS_CHUNK_ITEM_KEY;
	sk->max_offset = (u64)-1;
	sk->max_transid = (u64)-1;
	age = calloc(age_size, sizeof(u64));
	if (!age)
		goto out_nomem;

	while (1) {
		sk->nr_items = 1;
		ret = ioctl(fd, BTRFS_IOC_TREE_SEARCH, &args);
		e = errno;
		if (ret < 0) {
			error("cannot perform the search: %s", strerror(e));
			return 1;
		}
		if (sk->nr_items == 0)
			break;

		off = 0;
		for (i = 0; i < sk->nr_items; i++) {
			struct btrfs_chunk *item;
			struct btrfs_stripe *stripes;
			int sidx;
			u64 used = (u64)-1;

			memcpy(&sh, args.buf + off, sizeof(sh));
			off += sizeof(sh);
			item = (struct btrfs_chunk*)(args.buf + off);
			off += sh.len;

			stripes = &item->stripe;
			for (sidx = 0; sidx < item->num_stripes; sidx++) {
				struct dump_chunks_entry *e;
				u64 devid;

				e = &ctx.stats[ctx.length];
				devid = stripes[sidx].devid;
				e->devid = devid;
				e->start = stripes[sidx].offset;
				e->lstart = sh.offset;
				e->length = item->length;
				e->flags = item->type;
				e->pnumber = -1;
				while (devid > age_size) {
					u64 *tmp;
					unsigned old_size = age_size;

					age_size += 128;
					tmp = calloc(age_size, sizeof(u64));
					if (!tmp) {
						free(age);
						goto out_nomem;
					}
					memcpy(tmp, age, sizeof(u64) * old_size);
					age = tmp;
				}
				e->age = age[devid]++;
				if (with_usage) {
					if (used == (u64)-1)
						used = fill_usage(fd, sh.offset);
					e->used = used;
				} else {
					e->used = 0;
				}

				ctx.length++;

				if (ctx.length == ctx.size) {
					ctx.size += 1024;
					ctx.stats = realloc(ctx.stats, ctx.size
						* sizeof(ctx.stats[0]));
					if (!ctx.stats)
						goto out_nomem;
				}
			}

			sk->min_objectid = sh.objectid;
			sk->min_type = sh.type;
			sk->min_offset = sh.offset;
		}
		if (sk->min_offset < (u64)-1)
			sk->min_offset++;
		else
			break;
	}

	print_dump_chunks(&ctx, sort_mode, unit_mode, with_usage);
	free(ctx.stats);

	close_file_or_dir(fd, dirstream);
	return 0;

out_nomem:
	error("not enough memory");
	return 1;
}

static const char inspect_cmd_group_info[] =
"query various internal information";

const struct cmd_group inspect_cmd_group = {
	inspect_cmd_group_usage, inspect_cmd_group_info, {
		{ "inode-resolve", cmd_inspect_inode_resolve,
			cmd_inspect_inode_resolve_usage, NULL, 0 },
		{ "logical-resolve", cmd_inspect_logical_resolve,
			cmd_inspect_logical_resolve_usage, NULL, 0 },
		{ "subvolid-resolve", cmd_inspect_subvolid_resolve,
			cmd_inspect_subvolid_resolve_usage, NULL, 0 },
		{ "rootid", cmd_inspect_rootid, cmd_inspect_rootid_usage, NULL,
			0 },
		{ "min-dev-size", cmd_inspect_min_dev_size,
			cmd_inspect_min_dev_size_usage, NULL, 0 },
		{ "dump-tree", cmd_inspect_dump_tree,
				cmd_inspect_dump_tree_usage, NULL, 0 },
		{ "dump-super", cmd_inspect_dump_super,
				cmd_inspect_dump_super_usage, NULL, 0 },
		{ "tree-stats", cmd_inspect_tree_stats,
				cmd_inspect_tree_stats_usage, NULL, 0 },
		{ "dump-chunks", cmd_dump_chunks, cmd_dump_chunks_usage, NULL,
			0 },
		NULL_CMD_STRUCT
	}
};

int cmd_inspect(int argc, char **argv)
{
	return handle_command_group(&inspect_cmd_group, argc, argv);
}
