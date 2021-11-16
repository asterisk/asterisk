/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief Internal API for the Stasis bridge subclass.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_STASIS_BRIDGE_H
#define _ASTERISK_STASIS_BRIDGE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* ------------------------------------------------------------------- */

/*! Normal capabilities of mixing bridges */
#define STASIS_BRIDGE_MIXING_CAPABILITIES	\
	(AST_BRIDGE_CAPABILITY_NATIVE \
	| AST_BRIDGE_CAPABILITY_1TO1MIX \
	| AST_BRIDGE_CAPABILITY_MULTIMIX)

/*!
 * \internal
 * \brief Create a new Stasis bridge.
 * \since 12.5.0
 *
 * \param capabilities The capabilities that we require to be used on the bridge
 * \param flags Flags that will alter the behavior of the bridge
 * \param name Name given to the bridge by Stasis (optional)
 * \param id Unique ID given to the bridge by Stasis (optional)
 * \param video_mode Video mode of the bridge
 *
 * \retval a pointer to a new bridge on success
 * \retval NULL on failure
 */
struct ast_bridge *bridge_stasis_new(uint32_t capabilities, unsigned int flags, const char *name, const char *id, enum ast_bridge_video_mode_type video_mode);

/*!
 * \internal
 * \brief Initialize the Stasis bridge subclass.
 * \since 12.5.0
 */
void bridge_stasis_init(void);

/* ------------------------------------------------------------------- */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_STASIS_BRIDGE_H */
