/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#ifndef MULTICAST_RTP_H_
#define MULTICAST_RTP_H_
struct ast_multicast_rtp_options;

/*!
 * \brief Create multicast RTP options.
 *
 * These are passed to the multicast RTP engine on its creation.
 *
 * \param type The type of multicast RTP, either "basic" or "linksys"
 * \param options Miscellaneous options
 * \retval NULL Failure
 * \retval non-NULL success
 */
struct ast_multicast_rtp_options *ast_multicast_rtp_create_options(const char *type,
	const char *options);

/*!
 * \brief Free multicast RTP options
 *
 * This function is NULL-tolerant
 *
 * \param mcast_options Options to free
 */
void ast_multicast_rtp_free_options(struct ast_multicast_rtp_options *mcast_options);

/*!
 * \brief Get format specified in multicast options
 *
 * Multicast options allow for a format to be selected.
 * This function accesses the selected format and creates
 * an ast_format structure for it.
 *
 * \param mcast_options The options where a codec was specified
 * \retval NULL No format specified in the options
 * \retval non-NULL The format to use for communication
 */
struct ast_format *ast_multicast_rtp_options_get_format(struct ast_multicast_rtp_options *mcast_options);

#endif /* MULTICAST_RTP_H_ */
