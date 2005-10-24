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
 * \brief Music on hold handling
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

#endif /* _ASTERISK_MOH_H */
