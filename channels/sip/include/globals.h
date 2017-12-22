/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 * \brief sip global declaration header file
 */

#include "sip.h"

#ifndef _SIP_GLOBALS_H
#define _SIP_GLOBALS_H

extern struct ast_sockaddr bindaddr;     /*!< UDP: The address we bind to */
extern struct ast_sched_context *sched;     /*!< The scheduling context */

/*! \brief Definition of this channel for PBX channel registration */
extern struct ast_channel_tech sip_tech;

/*! \brief This version of the sip channel tech has no send_digit_begin
 * callback so that the core knows that the channel does not want
 * DTMF BEGIN frames.
 * The struct is initialized just before registering the channel driver,
 * and is for use with channels using SIP INFO DTMF.
 */
extern struct ast_channel_tech sip_tech_info;

#endif /* !defined(SIP_GLOBALS_H) */
