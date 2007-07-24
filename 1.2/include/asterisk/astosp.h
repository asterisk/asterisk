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
 * \brief OSP support (Open Settlement Protocol)
 */

#ifndef _ASTERISK_OSP_H
#define _ASTERISK_OSP_H

#include "asterisk/channel.h"
#include <netinet/in.h>
#include <time.h>

struct ast_osp_result {
	int handle;
	int numresults;
	char tech[20];
	char dest[256];
	char token[4096];
};

/* Note: Channel will be auto-serviced if specified.  Returns -1 on hangup, 
   0 if nothing found, or 1 if something is found */
int ast_osp_lookup(struct ast_channel *chan, char *provider, char *extension, char *callerid, struct ast_osp_result *result);

int ast_osp_next(struct ast_osp_result *result, int cause);

int ast_osp_terminate(int handle, int cause, time_t start, time_t duration);

int ast_osp_validate(char *provider, char *token, int *handle, unsigned int *timeout, char *callerid, struct in_addr addr, char *extension);

#endif /* _ASTERISK_OSP_H */
