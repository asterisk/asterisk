/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Russell Bryant
 *
 * Russell Bryant <russell@russellbryant.net>
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

/*!
 * \file
 * \brief Periodic beeps into the audio of a call
 */

#ifndef _ASTERISK_BEEP_H
#define _ASTERISK_BEEP_H

#include "asterisk/optional_api.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

AST_OPTIONAL_API(int, ast_beep_start,
		(struct ast_channel *chan, unsigned int interval, char *beep_id, size_t len),
		{ return -1; });

AST_OPTIONAL_API(int, ast_beep_stop,
		(struct ast_channel *chan, const char *beep_id),
		{ return -1; });

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BEEP_H */
