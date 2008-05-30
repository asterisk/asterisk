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

/*! \file
 * \brief AGI Extension interfaces - Asterisk Gateway Interface
 */

#ifndef _ASTERISK_AGI_H
#define _ASTERISK_AGI_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/cli.h"

typedef struct agi_state {
	int fd;		        /*!< FD for general output */
	int audio;	        /*!< FD for audio output */
	int ctrl;		/*!< FD for input control */
	unsigned int fast:1;    /*!< flag for fast agi or not */
	struct ast_speech *speech; /*!< Speech structure for speech recognition */
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
	/* Does this application run dead */
	int dead;
	/* Pointer to module that registered the agi command */
	struct ast_module *mod;
	/* Linked list pointer */
	AST_LIST_ENTRY(agi_command) list;
} agi_command;

int ast_agi_fdprintf(struct ast_channel *chan, int fd, char *fmt, ...);
int ast_agi_register(struct ast_module *mod, agi_command *cmd);
int ast_agi_unregister(struct ast_module *mod, agi_command *cmd);
void ast_agi_register_multiple(struct ast_module *mod, agi_command *cmd, int len);
void ast_agi_unregister_multiple(struct ast_module *mod, agi_command *cmd, int len);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_AGI_H */
