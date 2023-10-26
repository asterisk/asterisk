/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#ifndef PJSIP_SESSION_H_
#define PJSIP_SESSION_H_

/*!
 * \internal
 * \brief Unregisters the session supplement
 */
void pjsip_reason_header_unload(void);

/*!
 * \internal
 * \brief Registers the session supplement
 */
void pjsip_reason_header_load(void);

#endif /* PJSIP_SESSION_H_ */
