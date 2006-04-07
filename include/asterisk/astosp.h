/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief OSP support (Open Settlement Protocol)
 */

#ifndef _ASTERISK_OSP_H
#define _ASTERISK_OSP_H

#include <time.h>
#include <netinet/in.h>

#include "asterisk/channel.h"

#define OSP_DEF_PROVIDER	((char*)"default")		/* Default provider context name */
#define OSP_INVALID_HANDLE	((int)-1)				/* Invalid OSP handle, provider, transaction etc. */
#define OSP_DEF_TIMELIMIT	((unsigned int)0)		/* Default duration limit, no limit */

#define OSP_INTSTR_SIZE		((unsigned int)16)		/* Signed/unsigned int string buffer size */
#define OSP_NORSTR_SIZE		((unsigned int)256)		/* Normal string buffer size */
#define OSP_TOKSTR_SIZE		((unsigned int)4096)	/* Token string buffer size */

#define OSP_APP_SUCCESS		((char*)"SUCCESS")		/* Return status, success */
#define OSP_APP_FAILED		((char*)"FAILED")		/* Return status, failed */
#define OSP_APP_ERROR		((char*)"ERROR")		/* Return status, error */

struct ast_osp_result {
	int inhandle;
	int outhandle;
	unsigned int intimelimit;
	unsigned int outtimelimit;
	char tech[20];
	char dest[OSP_NORSTR_SIZE];
	char calling[OSP_NORSTR_SIZE];
	char token[OSP_TOKSTR_SIZE];
	int numresults;
};

/*!
 * \brief OSP Increase Use Count function
 */
void ast_osp_adduse(void);
/*!
 * \brief OSP Decrease Use Count function
 */
void ast_osp_deluse(void);
/*!
 * \brief OSP Authentication function
 * \param provider OSP provider context name
 * \param transaction OSP transaction handle, output
 * \param source Source of in_bound call
 * \param calling Calling number
 * \param called Called number
 * \param token OSP token, may be empty
 * \param timelimit Call duration limit, output
 * \return 1 Authenricated, 0 Unauthenticated, -1 Error
 */
int ast_osp_auth(
	const char* provider,		/* OSP provider context name */
	int* transaction,			/* OSP transaction handle, output */
	const char* source,			/* Source of in_bound call */
	const char* calling,		/* Calling number */
	const char* called,			/* Called number */
	const char* token,			/* OSP token, may be empty */
	unsigned int* timelimit		/* Call duration limit, output */
);
/*!
 * \brief OSP Lookup function
 * \param provider OSP provider context name
 * \param srcdev Source device of out_bound call
 * \param calling Calling number
 * \param called Called number
 * \param result Lookup results
 * \return 1 Found , 0 No route, -1 Error
 */
int ast_osp_lookup(
	const char* provider,			/* OSP provider conttext name */
	const char* srcdev,				/* Source device of out_bound call */
	const char* calling,			/* Calling number */
	const char* called,				/* Called number */
	struct ast_osp_result* result	/* OSP lookup results, in/output */
);
/*!
 * \brief OSP Next function
 * \param reason Last destination failure reason
 * \param result Lookup results, in/output
 * \return 1 Found , 0 No route, -1 Error
 */
int ast_osp_next(
	int reason,						/* Last destination failure reason */
	struct ast_osp_result *result	/* OSP lookup results, in/output */
);
/*!
 * \brief OSP Finish function
 * \param handle OSP in/out_bound transaction handle
 * \param reason Last destination failure reason
 * \param start Call start time
 * \param duration Call duration
 * \return 1 Success, 0 Failed, -1 Error
 */
int ast_osp_finish(
	int handle,						/* OSP in/out_bound transaction handle */
	int reason,						/* Last destination failure reason */
	time_t start, 					/* Call start time */
	time_t connect,					/* Call connect time */
	time_t end						/* Call end time */
);

#endif /* _ASTERISK_OSP_H */
