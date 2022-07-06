/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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
#ifndef _STIR_SHAKEN_PROFILE_PRIVATE_H
#define _STIR_SHAKEN_PROFILE_PRIVATE_H

#include "asterisk/sorcery.h"

#include "asterisk/acl.h"

enum stir_shaken_profile_behavior {
	/*! Only do STIR/SHAKEN attestation */
	STIR_SHAKEN_ATTEST = 1,
	/*! Only do STIR/SHAKEN verification */
	STIR_SHAKEN_VERIFY = 2,
	/*! Do STIR/SHAKEN attestation and verification */
	STIR_SHAKEN_ON = 3,
};

struct stir_shaken_profile {
	SORCERY_OBJECT(details);
	unsigned int stir_shaken;
	struct ast_acl_list *acl;
};

#endif /* _STIR_SHAKEN_PROFILE_PRIVATE_H */
