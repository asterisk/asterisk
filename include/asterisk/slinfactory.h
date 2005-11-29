/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * A machine to gather up arbitrary frames and convert them
 * to raw slinear on demand.
 * 
 * Copyright (C) 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_SLINFACTORY_H
#define _ASTERISK_SLINFACTORY_H
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_slinfactory {
	struct ast_frame *queue;
	struct ast_trans_pvt *trans;
	short hold[1280];
	short *offset;
	size_t holdlen;
	int size;
	int format;
};

void ast_slinfactory_init(struct ast_slinfactory *sf);
void ast_slinfactory_destroy(struct ast_slinfactory *sf);
int ast_slinfactory_feed(struct ast_slinfactory *sf, struct ast_frame *f);
int ast_slinfactory_read(struct ast_slinfactory *sf, short *buf, size_t bytes);
		 


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
