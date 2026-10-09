/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * Michael Bradeen <mbradeen@sangoma.com>
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
 * \brief Channel-locked PJSIP information interface
 */

#ifndef _CHAN_PJSIP_CHANNEL_INFO_H
#define _CHAN_PJSIP_CHANNEL_INFO_H

#include <pjsip.h>

#include "asterisk/channel.h"
#include "asterisk/res_pjsip_session.h"

struct pjsip_channel_uri {
	char value[PJSIP_MAX_URL_SIZE];
	/*! Printed length, or -1 if the URI did not fit. */
	int length;
};

/*! All members are protected by the owning channel's lock. */
struct pjsip_channel_info {
	struct ast_datastore *datastore;
	struct pjsip_channel_uri target_uri;
	struct pjsip_channel_uri local_uri;
	struct pjsip_channel_uri remote_uri;
	/*! Unescaped received tag; NULL if unavailable or allocation failed. */
	char *remote_tag;
	/*! Signaling security, or -1 if unavailable. */
	int secure;
	/* Retained because active/pending media objects may be shared and changed
	 * outside the channel lock. RTP APIs provide live addresses/statistics.
	 */
	struct ast_rtp_instance *audio_rtp;
	struct ast_rtp_instance *video_rtp;
	unsigned int audio_secure;
	unsigned int video_secure;
	unsigned int audio_held;
	unsigned int video_held;
};

/*!
 * \brief Create channel-owned PJSIP information before channel publication
 * \pre The caller holds the channel lock and executes in the session serializer.
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_channel_info_create(struct ast_channel *chan, struct ast_sip_session *session);

/*!
 * \brief Refresh channel information under the channel lock
 * \pre The caller executes in the session serializer.
 */
void pjsip_channel_info_update(struct ast_sip_session *session);

/*!
 * \brief Schedule a refresh from any thread without waiting for the serializer
 */
void pjsip_channel_info_schedule_update(struct ast_sip_session *session);

/*!
 * \brief Find the channel's information, or NULL if it has none
 * \pre The caller holds the channel lock.
 * \note The result is borrowed and may only be used while the channel is locked.
 * Do not release it with ao2_cleanup(). The channel datastore owns its lifetime.
 */
struct pjsip_channel_info *pjsip_channel_info_get(struct ast_channel *chan);

/*! \brief Register the channel information session supplement. */
void pjsip_channel_info_register(void);

/*! \brief Unregister the channel information session supplement. */
void pjsip_channel_info_unregister(void);

#endif /* _CHAN_PJSIP_CHANNEL_INFO_H */
