/*
 * Copyright (C) 2015 Fujitsu.  All rights reserved.
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

#ifndef __BTRFS_DEDUPE__
#define __BTRFS_DEDUPE__

/*
 * Dedup storage backend
 * On disk is persist storage but overhead is large
 * In memory is fast but will lose all its hash on umount
 */
#define BTRFS_DEDUPE_BACKEND_INMEMORY		0
#define BTRFS_DEDUPE_BACKEND_LAST		1

/* Dedup block size limit and default value */
#define BTRFS_DEDUPE_BLOCKSIZE_MAX	(8 * 1024 * 1024)
#define BTRFS_DEDUPE_BLOCKSIZE_MIN	(16 * 1024)
#define BTRFS_DEDUPE_BLOCKSIZE_DEFAULT	(128 * 1024)

/* Default dedupe limit on number of hash */
#define BTRFS_DEDUPE_LIMIT_NR_DEFAULT	(32 * 1024)

/* Hash algorithm, only support SHA256 yet */
#define BTRFS_DEDUPE_HASH_SHA256		0

#endif
