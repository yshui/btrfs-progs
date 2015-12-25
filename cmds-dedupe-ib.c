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

#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ctree.h"
#include "ioctl.h"

#include "commands.h"
#include "utils.h"
#include "kerncompat.h"
#include "dedupe-ib.h"

static const char * const dedupe_ib_cmd_group_usage[] = {
	"btrfs dedupe-inband <command> [options] <path>",
	NULL
};

static const char dedupe_ib_cmd_group_info[] =
"manage inband(write time) de-duplication";

const struct cmd_group dedupe_ib_cmd_group = {
	dedupe_ib_cmd_group_usage, dedupe_ib_cmd_group_info, {
		NULL_CMD_STRUCT
	}
};

int cmd_dedupe_ib(int argc, char **argv)
{
	return handle_command_group(&dedupe_ib_cmd_group, argc, argv);
}
