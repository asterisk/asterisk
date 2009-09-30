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
#include "asterisk/xmldoc.h"

typedef struct agi_state {
	int fd;		        /*!< FD for general output */
	int audio;	        /*!< FD for audio output */
	int ctrl;		/*!< FD for input control */
	unsigned int fast:1;    /*!< flag for fast agi or not */
	struct ast_speech *speech; /*!< Speech structure for speech recognition */
} AGI;

typedef struct agi_command {
	char *cmda[AST_MAX_CMD_LEN];		/*!< Null terminated list of the words of the command */
	/*! Handler for the command (channel, AGI state, # of arguments, argument list). 
	    Returns RESULT_SHOWUSAGE for improper arguments */
	int (*handler)(struct ast_channel *chan, AGI *agi, int argc, char *argv[]);
	/*! Summary of the command (< 60 characters) */
	char *summary;
	/*! Detailed usage information */
	char *usage;
	/*! Does this application run dead */
	int dead;
	/*! AGI command syntax description */
	char *syntax;
	/*! See also content */
	char *seealso;
	/*! Where the documentation come from. */
	enum ast_doc_src docsrc;
	/*! Pointer to module that registered the agi command */
	struct ast_module *mod;
	/*! Linked list pointer */
	AST_LIST_ENTRY(agi_command) list;
} agi_command;

/*!
 * \brief
 *
 * Registers an AGI command.
 *
 * \param mod Pointer to the module_info structure for the module that is registering the command
 * \param cmd Pointer to the descriptor for the command
 * \return 1 on success, 0 if the command is already registered
 *
 */
int ast_agi_register(struct ast_module *mod, agi_command *cmd) attribute_weak;

/*!
 * \brief
 *
 * Unregisters an AGI command.
 *
 * \param mod Pointer to the module_info structure for the module that is unregistering the command
 * \param cmd Pointer to the descriptor for the command
 * \return 1 on success, 0 if the command was not already registered
 *
 */
int ast_agi_unregister(struct ast_module *mod, agi_command *cmd) attribute_weak;

/*!
 * \brief
 *
 * Registers a group of AGI commands, provided as an array of struct agi_command
 * entries.
 *
 * \param mod Pointer to the module_info structure for the module that is registering the commands
 * \param cmd Pointer to the first entry in the array of command descriptors
 * \param len Length of the array (use the ARRAY_LEN macro to determine this easily)
 * \return 0 on success, -1 on failure
 *
 * \note If any command fails to register, all commands previously registered during the operation
 * will be unregistered. In other words, this function registers all the provided commands, or none
 * of them.
 */
int ast_agi_register_multiple(struct ast_module *mod, struct agi_command *cmd, unsigned int len) attribute_weak;

/*!
 * \brief
 *
 * Unregisters a group of AGI commands, provided as an array of struct agi_command
 * entries.
 *
 * \param mod Pointer to the module_info structure for the module that is unregistering the commands
 * \param cmd Pointer to the first entry in the array of command descriptors
 * \param len Length of the array (use the ARRAY_LEN macro to determine this easily)
 * \return 0 on success, -1 on failure
 *
 * \note If any command fails to unregister, this function will continue to unregister the
 * remaining commands in the array; it will not reregister the already-unregistered commands.
 */
int ast_agi_unregister_multiple(struct ast_module *mod, struct agi_command *cmd, unsigned int len) attribute_weak;

/*!
 * \brief
 *
 * Sends a string of text to an application connected via AGI.
 *
 * \param fd The file descriptor for the AGI session (from struct agi_state)
 * \param chan Pointer to an associated Asterisk channel, if any
 * \param fmt printf-style format string
 * \return 0 for success, -1 for failure
 *
 */
int ast_agi_send(int fd, struct ast_channel *chan, char *fmt, ...) attribute_weak __attribute__((format(printf, 3, 4)));

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_AGI_H */
