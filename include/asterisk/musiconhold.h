/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Options provided by main asterisk program
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_MOH_H
#define _ASTERISK_MOH_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Turn on music on hold on a given channel */
extern int ast_moh_start(struct ast_channel *chan, char *mclass);

/*! Turn off music on hold on a given channel */
extern void ast_moh_stop(struct ast_channel *chan);

extern void ast_install_music_functions(int (*start_ptr)(struct ast_channel *, char *),
										void (*stop_ptr)(struct ast_channel *),
										void (*cleanup_ptr)(struct ast_channel *));
	
extern void ast_uninstall_music_functions(void);
void ast_moh_cleanup(struct ast_channel *chan);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
