/*
 * Copyright (C) 2016 Fujitsu.  All rights reserved.
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
 * License along with this program.
 */

#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <pthread.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include <mntent.h>
#include <limits.h>
#include <stdlib.h>
#include <asm/types.h>
#include <uuid/uuid.h>
#include "utils.h"
#include "commands.h"
#include "send-utils.h"
#include "send-stream.h"
#include "send-dump.h"

#define path_cat_out_with_error(function_name, out_path, path1, path2, ret) \
ret = path_cat_out(out_path, path1, path2);			\
if (ret < 0) {							\
	error("%s: path invalid: %s\n", function_name, path2);	\
	return ret;						\
}

#define TITLE_WIDTH	16
#define PATH_WIDTH	32

static void print_dump(const char *title, const char *path,
	  	       const char *fmt, ...)
{
	va_list args;
	char real_title[TITLE_WIDTH + 1];

	real_title[0] = '\0';
	/* Append ':' to title */
	strncpy(real_title, title, TITLE_WIDTH - 1);
	strncat(real_title, ":", TITLE_WIDTH);

	/* Unified output */
	printf("%-*s%-*s", TITLE_WIDTH, real_title, PATH_WIDTH, path);
	va_start(args, fmt);
	/* Command specified output */
	vprintf(fmt, args);
	va_end(args);
	printf("\n");
}

static int print_subvol(const char *path, const u8 *uuid, u64 ctransid,
			void *user)
{
	struct btrfs_dump_send_args *r = user;
	char uuid_str[BTRFS_UUID_UNPARSED_SIZE];
	int ret;

	path_cat_out_with_error("subvol", r->full_subvol_path, r->root_path,
				path, ret);
	uuid_unparse(uuid, uuid_str);

	print_dump("subvol", r->full_subvol_path, "uuid: %s, transid: %llu",
		   uuid_str, ctransid);
	return 0;
}

static int print_snapshot(const char *path, const u8 *uuid, u64 ctransid,
			  const u8 *parent_uuid, u64 parent_ctransid,
			  void *user)
{
	struct btrfs_dump_send_args *r = user;
	char uuid_str[BTRFS_UUID_UNPARSED_SIZE];
	char parent_uuid_str[BTRFS_UUID_UNPARSED_SIZE];
	int ret;

	path_cat_out_with_error("snapshot", r->full_subvol_path, r->root_path,
				path, ret);
	uuid_unparse(uuid, uuid_str);
	uuid_unparse(parent_uuid, parent_uuid_str);

	print_dump("snapshot", r->full_subvol_path,
		   "uuid: %s, transid: %llu, parent_uuid: %s, parent_transid: %llu",
		   uuid_str, ctransid, parent_uuid_str, parent_ctransid);
	return 0;
}

static int print_mkfile(const char *path, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("mkfile", full_path, r->full_subvol_path, path,
				ret);
	print_dump("mkfile", full_path, "");
	return 0;
}

static int print_mkdir(const char *path, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("mkdir", full_path, r->full_subvol_path, path,
				ret);
	print_dump("mkdir", full_path, "");
	return 0;
}

static int print_mknod(const char *path, u64 mode, u64 dev, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("mkdir", full_path, r->full_subvol_path, path,
				ret);
	print_dump("mknod", full_path, "mode: %llo, dev: 0x%llx", mode, dev);
	return 0;
}

static int print_mkfifo(const char *path, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("mkfifo", full_path, r->full_subvol_path, path,
				ret);
	print_dump("mkfifo", full_path, "");
	return 0;
}

static int print_mksock(const char *path, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("mksock", full_path, r->full_subvol_path, path,
				ret);
	print_dump("mksock", full_path, "");
	return 0;
}

static int print_symlink(const char *path, const char *lnk, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("symlink", full_path, r->full_subvol_path, path,
				ret);
	print_dump("symlink", full_path, "lnk: %s", lnk);
	return 0;
}

static int print_rename(const char *from, const char *to, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_from[PATH_MAX];
	char full_to[PATH_MAX];
	int ret;

	path_cat_out_with_error("rename", full_from, r->full_subvol_path, from,
				ret);
	path_cat_out_with_error("rename", full_to, r->full_subvol_path, to,
				ret);
	print_dump("rename", full_from, "to %s", full_to);
	return 0;
}

static int print_link(const char *path, const char *lnk, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("link", full_path, r->full_subvol_path, path,
				ret);
	print_dump("link", full_path, "lnk: %s", lnk);
	return 0;
}

static int print_unlink(const char *path, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("unlink", full_path, r->full_subvol_path, path,
				ret);
	print_dump("unlink", full_path, "");
	return 0;
}

static int print_rmdir(const char *path, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("rmdir", full_path, r->full_subvol_path, path,
				ret);
	print_dump("rmdir", full_path, "");
	return 0;
}

static int print_write(const char *path, const void *data, u64 offset,
		       u64 len, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("write", full_path, r->full_subvol_path, path,
				ret);
	print_dump("write", full_path, "offset: %llu, len: %llu", offset, len);
	return 0;
}

static int print_clone(const char *path, u64 offset, u64 len,
		       const u8 *clone_uuid, u64 clone_ctransid,
		       const char *clone_path, u64 clone_offset,
		       void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("clone", full_path, r->full_subvol_path, path,
				ret);
	print_dump("clone", full_path, "offset: %llu, len: %llu from: %s, offset: %llu",
		   offset, len, clone_path, clone_offset);
	return 0;
}

static int print_set_xattr(const char *path, const char *name,
			   const void *data, int len, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("set_xattr", full_path, r->full_subvol_path,
				path, ret);
	print_dump("set_xattr", full_path, "name: %s, len: %s", name, len);
	return 0;
}

static int print_remove_xattr(const char *path, const char *name, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("remove_xattr", full_path, r->full_subvol_path,
				path, ret);
	print_dump("remove_xattr", full_path, name);
	return 0;
}

static int print_truncate(const char *path, u64 size, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("truncate", full_path, r->full_subvol_path,
				path, ret);
	print_dump("truncate", full_path, "size: %llu", size);
	return 0;
}

static int print_chmod(const char *path, u64 mode, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("chmod", full_path, r->full_subvol_path, path,
				ret);
	print_dump("chmod", full_path, "mode: %llo", mode);
	return 0;
}

static int print_chown(const char *path, u64 uid, u64 gid, void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("chown", full_path, r->full_subvol_path, path,
				ret);
	print_dump("chown", full_path, "gid: %llu, uid: %llu", gid, uid);
	return 0;
}

static int print_utimes(const char *path, struct timespec *at,
			struct timespec *mt, struct timespec *ct,
			void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("utimes", full_path, r->full_subvol_path, path,
				ret);
	print_dump("utimes", full_path, "");
	return 0;
}

static int print_update_extent(const char *path, u64 offset, u64 len,
			       void *user)
{
	struct btrfs_dump_send_args *r = user;
	char full_path[PATH_MAX];
	int ret;

	path_cat_out_with_error("update_extent", full_path, r->full_subvol_path,
				path, ret);
	print_dump("update_extent", full_path, "offset: %llu, len: %llu",
		   offset, len);
	return 0;
}

struct btrfs_send_ops btrfs_print_send_ops = {
	.subvol = print_subvol,
	.snapshot = print_snapshot,
	.mkfile = print_mkfile,
	.mkdir = print_mkdir,
	.mknod = print_mknod,
	.mkfifo = print_mkfifo,
	.mksock = print_mksock,
	.symlink = print_symlink,
	.rename = print_rename,
	.link = print_link,
	.unlink = print_unlink,
	.rmdir = print_rmdir,
	.write = print_write,
	.clone = print_clone,
	.set_xattr = print_set_xattr,
	.remove_xattr = print_remove_xattr,
	.truncate = print_truncate,
	.chmod = print_chmod,
	.chown = print_chown,
	.utimes = print_utimes,
	.update_extent = print_update_extent
};
