/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Michael L. Young <elgueromexicano@gmail.com>
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
 *
 * \brief Generate security events in the SIP channel
 *
 * \author Michael L. Young <elgueromexicano@gmail.com>
 */

#include "sip.h"

#ifndef _SIP_SECURITY_EVENTS_H
#define _SIP_SECURITY_EVENTS_H

void sip_report_invalid_peer(const struct sip_pvt *p);
void sip_report_failed_acl(const struct sip_pvt *p, const char *aclname);
void sip_report_inval_password(const struct sip_pvt *p, const char *responsechallenge, const char *responsehash);
void sip_report_auth_success(const struct sip_pvt *p, uint32_t *using_password);
void sip_report_session_limit(const struct sip_pvt *p);
void sip_report_failed_challenge_response(const struct sip_pvt *p, const char *response, const char *expected_response);
void sip_report_chal_sent(const struct sip_pvt *p);
void sip_report_inval_transport(const struct sip_pvt *p, const char *transport);
void sip_digest_parser(char *c, struct digestkeys *keys);
int sip_report_security_event(const struct sip_pvt *p, const struct sip_request *req, const int res);

#endif
