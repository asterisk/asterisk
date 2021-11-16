/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

#ifndef _ASTERISK_STASIS_APP_SNOOP_H
#define _ASTERISK_STASIS_APP_SNOOP_H

/*! \file
 *
 * \brief Stasis Application Snoop API. See \ref res_stasis "Stasis
 * Application API" for detailed documentation.
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \since 12
 */

#include "asterisk/stasis_app.h"

/*! \brief Directions for audio stream flow */
enum stasis_app_snoop_direction {
	/*! \brief No direction */
	STASIS_SNOOP_DIRECTION_NONE = 0,
	/*! \brief Audio stream out to the channel */
	STASIS_SNOOP_DIRECTION_OUT,
	/*! \brief Audio stream in from the channel */
	STASIS_SNOOP_DIRECTION_IN,
	/*! \brief Audio stream to AND from the channel */
	STASIS_SNOOP_DIRECTION_BOTH,
};

/*!
 * \brief Create a snoop on the provided channel.
 *
 * \param chan Channel to snoop on.
 * \param spy Direction of media that should be spied on.
 * \param whisper Direction of media that should be whispered into.
 * \param app Stasis application to execute on the snoop channel.
 * \param app_args Stasis application arguments.
 * \param snoop_id
 * \return ast_channel ast_channel_unref() when done.
 * \retval NULL if snoop channel couldn't be created.
 */
struct ast_channel *stasis_app_control_snoop(struct ast_channel *chan,
	enum stasis_app_snoop_direction spy, enum stasis_app_snoop_direction whisper,
	const char *app, const char *app_args, const char *snoop_id);

#endif /* _ASTERISK_STASIS_APP_SNOOP_H */
