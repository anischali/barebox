// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: © 2025 Anis Chali <anis.chali@ro-main.com>, Conception Ro-Main

/* dd.c - copy binary blobs */

#include <common.h>
#include <command.h>
#include <xfuncs.h>
#include <linux/stat.h>
#include <libbb.h>
#include <fs.h>
#include <malloc.h>
#include <libgen.h>
#include <getopt.h>
#include <libfile.h>

/**
 * @param[in] argc Argument count from command line
 * @param[in] argv List of input arguments
 */
static int do_dd(int argc, char *argv[])
{
	
	return 0;
}

BAREBOX_CMD_HELP_START(dd)
BAREBOX_CMD_HELP_TEXT("Copy binary block from if to of.")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Options:")
BAREBOX_CMD_HELP_OPT ("if", "recursive")
BAREBOX_CMD_HELP_OPT ("of", "do not overwrite an existing file")
BAREBOX_CMD_HELP_OPT ("bs", "verbose")
BAREBOX_CMD_HELP_OPT ("seek", "verbose")
BAREBOX_CMD_HELP_OPT ("skip", "verbose")
BAREBOX_CMD_HELP_OPT ("count", "verbose")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(dd)
	.cmd		= do_dd,
	BAREBOX_CMD_DESC("copy binary blobs")
	BAREBOX_CMD_GROUP(CMD_GRP_FILE)
	BAREBOX_CMD_HELP(cmd_dd_help)
BAREBOX_CMD_END
