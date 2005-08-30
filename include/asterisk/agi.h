/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * AGI Extension interfaces
 */

#ifndef _ASTERISK_AGI_H
#define _ASTERISK_AGI_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct agi_state {
	int fd;		/* FD for general output */
	int audio;	/* FD for audio output */
	int ctrl;	/* FD for input control */
} AGI;

typedef struct agi_command {
	/* Null terminated list of the words of the command */
	char *cmda[AST_MAX_CMD_LEN];
	/* Handler for the command (channel, AGI state, # of arguments, argument list). 
	    Returns RESULT_SHOWUSAGE for improper arguments */
	int (*handler)(struct ast_channel *chan, AGI *agi, int argc, char *argv[]);
	/* Summary of the command (< 60 characters) */
	char *summary;
	/* Detailed usage information */
	char *usage;
	struct agi_command *next;
} agi_command;

int agi_register(agi_command *cmd);
void agi_unregister(agi_command *cmd);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_AGI_H */
