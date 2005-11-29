/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * AGI Extension interfaces
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
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



#endif
