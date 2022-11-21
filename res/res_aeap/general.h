/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

#ifndef RES_AEAP_GENERAL_H
#define RES_AEAP_GENERAL_H

/*!
 * \brief Retrieve the scheduling context
 *
 * \returns The scheduling context
 */
struct ast_sched_context *aeap_sched_context(void);

/*!
 * \brief Initialize general/common AEAP facilities
 *
 * \returns 0 on success, -1 on error
 */
int aeap_general_initialize(void);

/*!
 * \brief Finalize/cleanup general AEAP facilities
 */
void aeap_general_finalize(void);

#endif /* RES_AEAP_GENERAL_H */
