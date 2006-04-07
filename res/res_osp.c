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
 * \brief Provide Open Settlement Protocol capability
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg See also: \ref app_osplookup.c
 */

#include <sys/types.h>
#include <osp.h>
#include <osputils.h>
#include <openssl/err.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/say.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/crypto.h"
#include "asterisk/md5.h"
#include "asterisk/cli.h"
#include "asterisk/io.h"
#include "asterisk/lock.h"
#include "asterisk/astosp.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/causes.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"

/* OSP Authentication Policy */
enum osp_authpolicy {
	OSP_AUTH_NO,
	OSP_AUTH_YES,
	OSP_AUTH_EXCLUSIVE
};

#define OSP_CONFIG_FILE			((char*)"osp.conf")
#define OSP_GENERAL_CAT			((char*)"general")
#define OSP_MAX_CERTS			((unsigned int)10)
#define OSP_MAX_SRVS			((unsigned int)10)
#define OSP_DEF_MAXCONNECTIONS	((unsigned int)20)
#define OSP_MIN_MAXCONNECTIONS	((unsigned int)1)
#define OSP_MAX_MAXCONNECTIONS	((unsigned int)1000)
#define OSP_DEF_RETRYDELAY		((unsigned int)0)
#define OSP_MIN_RETRYDELAY		((unsigned int)0)
#define OSP_MAX_RETRYDELAY		((unsigned int)10)
#define OSP_DEF_RETRYLIMIT		((unsigned int)2)
#define OSP_MIN_RETRYLIMIT		((unsigned int)0)
#define OSP_MAX_RETRYLIMIT		((unsigned int)100)
#define OSP_DEF_TIMEOUT			((unsigned int)500)
#define OSP_MIN_TIMEOUT			((unsigned int)200)
#define OSP_MAX_TIMEOUT			((unsigned int)10000)
#define OSP_DEF_AUTHPOLICY		((enum osp_authpolicy)OSP_AUTH_YES)
#define OSP_AUDIT_URL			((char*)"localhost")
#define OSP_LOCAL_VALIDATION	((int)1)
#define OSP_SSL_LIFETIME		((unsigned int)300)
#define OSP_HTTP_PERSISTENCE	((int)1)
#define OSP_CUSTOMER_ID			((char*)"")
#define OSP_DEVICE_ID			((char*)"")
#define OSP_DEF_DESTINATIONS	((unsigned int)5)

struct osp_provider {
	char name[OSP_NORSTR_SIZE];
	char privatekey[OSP_NORSTR_SIZE];
	char localcert[OSP_NORSTR_SIZE];
	unsigned int cacount;
	char cacerts[OSP_MAX_CERTS][OSP_NORSTR_SIZE]; 
	unsigned int spcount;
	char srvpoints[OSP_MAX_SRVS][OSP_NORSTR_SIZE];
	int maxconnections;
	int retrydelay;
	int retrylimit;
	int timeout;
	char source[OSP_NORSTR_SIZE];
	enum osp_authpolicy authpolicy;
	OSPTPROVHANDLE handle;
	struct osp_provider *next;
};

AST_MUTEX_DEFINE_STATIC(osplock);
static unsigned int osp_usecount = 0;
static int osp_initialized = 0;
static int osp_hardware = 0;
static struct osp_provider* ospproviders = NULL;
static unsigned int osp_tokenformat = TOKEN_ALGO_SIGNED;

static int osp_buildProvider(
	struct ast_config* cfg,		/* OSP configuration */
	char* provider);			/* OSP provider context name */
static int osp_getPolicy(
	const char* provider,		/* OSP provider context name */
	int* policy);				/* OSP authentication policy, output */
static int osp_genTransaction(
	const char* provider,		/* OSP provider context name */
	int* transaction,			/* OSP transaction handle, output */
	unsigned int sourcesize,	/* Size of source buffer, in/output */
	char* source);				/* Source of provider context, output */
static int osp_valToken(
	int transaction,			/* OSP transaction handle */
	const char* source,			/* Source of in_bound call */
	const char* dest,			/* Destination of in_bound call */
	const char* calling,		/* Calling number */
	const char* called,			/* Called number */
	const char* token,			/* OSP token, may be empty */
	unsigned int* timelimit);	/* Call duration limit, output */
static unsigned int osp_choTimelimit(
	unsigned int in,			/* In_bound OSP timelimit */
	unsigned int out);			/* Out_bound OSP timelimit */
static enum OSPEFAILREASON reason2cause(
	int reason);				/* Last call failure reason */
static int osp_chkDest(
	const char* callednum,			/* Called number */
	const char* callingnum,			/* Calling number */
	char* destination,				/* Destination IP in OSP format */
	unsigned int tokenlen,			/* OSP token length */
	const char* token,				/* OSP token */
	enum OSPEFAILREASON* cause,		/* Failure cause, output */
	struct ast_osp_result* result);	/* OSP lookup results, in/output */

static int osp_load(void);
static int osp_unload(void);
static int osp_show(int fd, int argc, char *argv[]);

static int osp_buildProvider(
	struct ast_config *cfg,		/* OSP configuration */
	char* provider)				/* OSP provider context name */
{
	int res;
	unsigned int t, i, j;
	struct osp_provider* p;
	struct ast_variable* v;
	OSPTPRIVATEKEY privatekey;
	OSPTCERT localcert;
	const char* psrvpoints[OSP_MAX_SRVS];
	OSPTCERT cacerts[OSP_MAX_CERTS];
	const OSPTCERT* pcacerts[OSP_MAX_CERTS];
	int error = OSPC_ERR_NO_ERROR;

	p = ast_calloc(1, sizeof(*p));
	if (!p) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return(-1);
	}

	ast_copy_string(p->name, provider, sizeof(p->name));
	p->handle = OSP_INVALID_HANDLE;
	snprintf(p->privatekey, sizeof(p->privatekey), "%s/%s-privatekey.pem", ast_config_AST_KEY_DIR, provider);
	snprintf(p->localcert, sizeof(p->localcert), "%s/%s-localcert.pem", ast_config_AST_KEY_DIR, provider);
	p->maxconnections = OSP_DEF_MAXCONNECTIONS;
	p->retrydelay = OSP_DEF_RETRYDELAY;
	p->retrylimit = OSP_DEF_RETRYLIMIT;
	p->timeout = OSP_DEF_TIMEOUT;
	p->authpolicy = OSP_DEF_AUTHPOLICY;

	v = ast_variable_browse(cfg, provider);
	while(v) {
		if (!strcasecmp(v->name, "privatekey")) {
			if (v->value[0] == '/') {
				ast_copy_string(p->privatekey, v->value, sizeof(p->privatekey));
			} else {
				snprintf(p->privatekey, sizeof(p->privatekey), "%s/%s", ast_config_AST_KEY_DIR, v->value);
			}
			ast_log(LOG_DEBUG, "OSP: privatekey '%s'\n", p->privatekey);
		} else if (!strcasecmp(v->name, "localcert")) {
			if (v->value[0] == '/') {
				ast_copy_string(p->localcert, v->value, sizeof(p->localcert));
			} else {
				snprintf(p->localcert, sizeof(p->localcert), "%s/%s", ast_config_AST_KEY_DIR, v->value);
			}
			ast_log(LOG_DEBUG, "OSP: localcert '%s'\n", p->localcert);
		} else if (!strcasecmp(v->name, "cacert")) {
			if (p->cacount < OSP_MAX_CERTS) {
				if (v->value[0] == '/') {
					ast_copy_string(p->cacerts[p->cacount], v->value, sizeof(p->cacerts[0]));
				} else {
					snprintf(p->cacerts[p->cacount], sizeof(p->cacerts[0]), "%s/%s", ast_config_AST_KEY_DIR, v->value);
				}
				ast_log(LOG_DEBUG, "OSP: cacert[%d]: '%s'\n", p->cacount, p->cacerts[p->cacount]);
				p->cacount++;
			} else {
				ast_log(LOG_WARNING, "OSP: Too many CA Certificates at line %d\n", v->lineno);
			}
		} else if (!strcasecmp(v->name, "servicepoint")) {
			if (p->spcount < OSP_MAX_SRVS) {
				ast_copy_string(p->srvpoints[p->spcount], v->value, sizeof(p->srvpoints[0]));
				ast_log(LOG_DEBUG, "OSP: servicepoint[%d]: '%s'\n", p->spcount, p->srvpoints[p->spcount]);
				p->spcount++;
			} else {
				ast_log(LOG_WARNING, "OSP: Too many Service Points at line %d\n", v->lineno);
			}
		} else if (!strcasecmp(v->name, "maxconnections")) {
			if ((sscanf(v->value, "%d", &t) == 1) && (t >= OSP_MIN_MAXCONNECTIONS) && (t <= OSP_MAX_MAXCONNECTIONS)) {
				p->maxconnections = t;
				ast_log(LOG_DEBUG, "OSP: maxconnections '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: maxconnections should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_MAXCONNECTIONS, OSP_MAX_MAXCONNECTIONS, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "retrydelay")) {
			if ((sscanf(v->value, "%d", &t) == 1) && (t >= OSP_MIN_RETRYDELAY) && (t <= OSP_MAX_RETRYDELAY)) {
				p->retrydelay = t;
				ast_log(LOG_DEBUG, "OSP: retrydelay '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: retrydelay should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_RETRYDELAY, OSP_MAX_RETRYDELAY, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "retrylimit")) {
			if ((sscanf(v->value, "%d", &t) == 1) && (t >= OSP_MIN_RETRYLIMIT) && (t <= OSP_MAX_RETRYLIMIT)) {
				p->retrylimit = t;
				ast_log(LOG_DEBUG, "OSP: retrylimit '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: retrylimit should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_RETRYLIMIT, OSP_MAX_RETRYLIMIT, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "timeout")) {
			if ((sscanf(v->value, "%d", &t) == 1) && (t >= OSP_MIN_TIMEOUT) && (t <= OSP_MAX_TIMEOUT)) {
				p->timeout = t;
				ast_log(LOG_DEBUG, "OSP: timeout '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: timeout should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_TIMEOUT, OSP_MAX_TIMEOUT, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "source")) {
			ast_copy_string(p->source, v->value, sizeof(p->source));
			ast_log(LOG_DEBUG, "OSP: source '%s'\n", p->source);
		} else if (!strcasecmp(v->name, "authpolicy")) {
			if ((sscanf(v->value, "%d", &t) == 1) && ((t == OSP_AUTH_NO) || (t == OSP_AUTH_YES) || (t == OSP_AUTH_EXCLUSIVE))) {
				p->authpolicy = t;
				ast_log(LOG_DEBUG, "OSP: authpolicy '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: authpolicy should be %d, %d or %d, not '%s' at line %d\n", 
					OSP_AUTH_NO, OSP_AUTH_YES, OSP_AUTH_EXCLUSIVE, v->value, v->lineno);
			}
		}
		v = v->next;
	}

	error = OSPPUtilLoadPEMPrivateKey(p->privatekey, &privatekey);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to load privatekey '%s'\n", p->privatekey);
		free(p);
		return(-1);
	}

	error = OSPPUtilLoadPEMCert(p->localcert, &localcert);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to load localcert '%s'\n", p->localcert);
		if (privatekey.PrivateKeyData) {
			free(privatekey.PrivateKeyData);
		}
		free(p);
		return(-1);
	}

	if (p->cacount < 1) {
		snprintf(p->cacerts[p->cacount], sizeof(p->cacerts[0]), "%s/%s-cacert.pem", ast_config_AST_KEY_DIR, provider);
		ast_log(LOG_DEBUG, "OSP: cacert[%d]: '%s'\n", p->cacount, p->cacerts[p->cacount]);
		p->cacount++;
	}
	for (i = 0; i < p->cacount; i++) {
		error = OSPPUtilLoadPEMCert(p->cacerts[i], &cacerts[i]);
		if (error != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to load cacert '%s'\n", p->cacerts[i]);
			for (j = 0; j < i; j++) {
				if (cacerts[j].CertData) {
					free(cacerts[j].CertData);
				}
			}
			if (localcert.CertData) {
				free(localcert.CertData);
			}
			if (privatekey.PrivateKeyData) {
				free(privatekey.PrivateKeyData);
			}
			free(p);
			return(-1);
		}
		pcacerts[i] = &cacerts[i];
	}
	
	for (i = 0; i < p->spcount; i++) {
		psrvpoints[i] = p->srvpoints[i];
	}

	error = OSPPProviderNew(
		p->spcount, psrvpoints,
		NULL,
		OSP_AUDIT_URL,
		&privatekey,
		&localcert,
		p->cacount, pcacerts,
		OSP_LOCAL_VALIDATION,
		OSP_SSL_LIFETIME,
		p->maxconnections,
		OSP_HTTP_PERSISTENCE,
		p->retrydelay,
		p->retrylimit,
		p->timeout,
		OSP_CUSTOMER_ID,
		OSP_DEVICE_ID,
		&p->handle);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to initialize provider '%s'\n", provider);
		free(p);
		res = -1;
	} else {
		ast_log(LOG_DEBUG, "OSP: provider '%s'\n", provider);
		ast_mutex_lock(&osplock);
		p->next = ospproviders;
		ospproviders = p;
		ast_mutex_unlock(&osplock); 	
		res = 0;
	}

	for (i = 0; i < p->cacount; i++) {
		if (cacerts[i].CertData) {
			free(cacerts[i].CertData);
		}
	}
	if (localcert.CertData) {
		free(localcert.CertData);
	}
	if (privatekey.PrivateKeyData) {
		free(privatekey.PrivateKeyData);
	}

	return(res);
}

static int osp_getPolicy(
	const char* provider,		/* OSP provider context name */
	int* policy)				/* OSP authentication policy, output */
{
	int res = 0;
	struct osp_provider* p;

	ast_mutex_lock(&osplock);
	p = ospproviders;
	while(p) {
		if (!strcasecmp(p->name, provider)) {
			*policy = p->authpolicy;
			ast_log(LOG_DEBUG, "OSP: authpolicy '%d'\n", *policy);
			res = 1;
			break;
		}
		p = p->next;
	}
	ast_mutex_unlock(&osplock);

	return(res);
}

static int osp_genTransaction(
	const char* provider,		/* OSP provider context name */
	int* transaction,			/* OSP transaction handle, output */
	unsigned int sourcesize,	/* Size of source buffer, in/output */
	char* source)				/* Source of provider context, output */
{
	int res = 0;
	struct osp_provider *p;
	int error;

	ast_mutex_lock(&osplock);
	p = ospproviders;
	while(p) {
		if (!strcasecmp(p->name, provider)) {
			error = OSPPTransactionNew(p->handle, transaction);
			if (error == OSPC_ERR_NO_ERROR) {
				ast_log(LOG_DEBUG, "OSP: transaction '%d'\n", *transaction);
				ast_copy_string(source, p->source, sourcesize);
				ast_log(LOG_DEBUG, "OSP: source '%s'\n", source);
				res = 1;
			} else {
				*transaction = OSP_INVALID_HANDLE;
				ast_log(LOG_WARNING, "OSP: Unable to create transaction handle\n");
				res = -1;
			}
			break;
		}
		p = p->next;
	}
	ast_mutex_unlock(&osplock);

	return(res);
}

static int osp_valToken(
	int transaction,			/* OSP transaction handle */
	const char* source,			/* Source of in_bound call */
	const char* dest,			/* Destination of in_bound call */
	const char* calling,		/* Calling number */
	const char* called,			/* Called number */
	const char* token,			/* OSP token, may be empty */
	unsigned int* timelimit)	/* Call duration limit, output */
{
	int res = 0;
	char tokenstr[OSP_TOKSTR_SIZE];
	int tokenlen;
	unsigned int authorised;
	unsigned int dummy = 0;
	int error;

	tokenlen = ast_base64decode(tokenstr, token, strlen(token));
	error = OSPPTransactionValidateAuthorisation(
		transaction, 
		source, dest, NULL, NULL,
		calling ? calling : "", OSPC_E164, 
		called, OSPC_E164, 
		0, NULL,
		tokenlen, tokenstr, 
		&authorised, 
		timelimit, 
		&dummy, NULL, 
		osp_tokenformat); 
	if (error == OSPC_ERR_NO_ERROR) {
		if (authorised) {
			ast_log(LOG_DEBUG, "OSP: Authorised\n");
			res = 1;
		}
	}
	return(res);
}

int ast_osp_auth(
	const char* provider,		/* OSP provider context name */
	int* transaction,			/* OSP transaction handle, output */
	const char* source,			/* Source of in_bound call */
	const char* calling,		/* Calling number */
	const char* called,			/* Called number */
	const char* token,			/* OSP token, may be empty */
	unsigned int* timelimit)	/* Call duration limit, output */
{
	int res;
	char dest[OSP_NORSTR_SIZE];
	int policy = OSP_AUTH_YES;

	*transaction = OSP_INVALID_HANDLE;
	*timelimit = OSP_DEF_TIMELIMIT;

	res = osp_getPolicy(provider, &policy);
	if (!res) {
		ast_log(LOG_WARNING, "OSP: Unabe to find authentication policy\n");
		return(-1);
	}

	switch (policy) {
		case OSP_AUTH_NO:
			res = 1;
			break;
		case OSP_AUTH_EXCLUSIVE:
			if (ast_strlen_zero(token)) {
				res = 0;
			} else if ((res = osp_genTransaction(provider, transaction, sizeof(dest), dest)) <= 0) {
				*transaction = OSP_INVALID_HANDLE;
				ast_log(LOG_WARNING, "OSP: Unable to generate transaction handle\n");
				res = -1;
			} else {
				res = osp_valToken(*transaction, source, dest, calling, called, token, timelimit);
			}
			break;
		case OSP_AUTH_YES:
		default:
			if (ast_strlen_zero(token)) {
				res = 1;
			} else if ((res = osp_genTransaction(provider, transaction, sizeof(dest), dest)) <= 0) {
				*transaction = OSP_INVALID_HANDLE;
				ast_log(LOG_WARNING, "OSP: Unable to generate transaction handle\n");
				res = -1;
			} else {
				res = osp_valToken(*transaction, source, dest, calling, called, token, timelimit);
			}
			break;
	}

	if (!res) {
		OSPPTransactionRecordFailure(*transaction, OSPC_FAIL_CALL_REJECTED);
	}

	return(res); 	
}

static unsigned int osp_choTimelimit(
	unsigned int in,			/* In_bound OSP timelimit */
	unsigned int out)			/* Out_bound OSP timelimit */
{
	if (in == OSP_DEF_TIMELIMIT) {
		return (out);
	} else if (out == OSP_DEF_TIMELIMIT) {
		return (in);
	} else {
		return(in < out ? in : out);
	}
}

static int osp_chkDest(
	const char* callednum,			/* Called number */
	const char* callingnum,			/* Calling number */
	char* destination,				/* Destination IP in OSP format */
	unsigned int tokenlen,			/* OSP token length */
	const char* token,				/* OSP token */
	enum OSPEFAILREASON* cause,		/* Failure cause, output */
	struct ast_osp_result* result)	/* OSP lookup results, in/output */
{
	int res = 0;
	OSPE_DEST_OSP_ENABLED enabled;
	OSPE_DEST_PROT protocol;
	int error;

	if (strlen(destination) <= 2) {
		*cause = OSPC_FAIL_INCOMPATIBLE_DEST;
	} else {
		error = OSPPTransactionIsDestOSPEnabled(result->outhandle, &enabled);
		if ((error == OSPC_ERR_NO_ERROR) && (enabled == OSPE_OSP_FALSE)) {
			result->token[0] = '\0';
		} else {
			ast_base64encode(result->token, token, tokenlen, sizeof(result->token) - 1);
		}

		error = OSPPTransactionGetDestProtocol(result->outhandle, &protocol);
		if (error != OSPC_ERR_NO_ERROR) {
			*cause = OSPC_FAIL_PROTOCOL_ERROR; 
		} else {
			res = 1;
			/* Strip leading and trailing brackets */
			destination[strlen(destination) - 1] = '\0';
			switch(protocol) {
				case OSPE_DEST_PROT_H323_SETUP:
					ast_copy_string(result->tech, "H323", sizeof(result->tech));
					ast_log(LOG_DEBUG, "OSP: protocol '%d'\n", protocol);
					snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
					ast_copy_string(result->calling, callingnum, sizeof(result->calling));
					break;
				case OSPE_DEST_PROT_SIP:
					ast_copy_string(result->tech, "SIP", sizeof(result->tech));
					ast_log(LOG_DEBUG, "OSP: protocol '%d'\n", protocol);
					snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
					ast_copy_string(result->calling, callingnum, sizeof(result->calling));
					break;
				case OSPE_DEST_PROT_IAX:
					ast_copy_string(result->tech, "IAX", sizeof(result->tech));
					ast_log(LOG_DEBUG, "OSP: protocol '%d'\n", protocol);
					snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
					ast_copy_string(result->calling, callingnum, sizeof(result->calling));
					break;
				default:
					ast_log(LOG_DEBUG, "OSP: Unknown protocol '%d'\n", protocol);
					*cause = OSPC_FAIL_PROTOCOL_ERROR; 
					res = 0;
			}
		}
	}
	return(res);
}

int ast_osp_lookup(
	const char* provider,			/* OSP provider conttext name */
	const char* srcdev,				/* Source device of out_bound call */
	const char* calling,			/* Calling number */
	const char* called,				/* Called number */
	struct ast_osp_result* result)	/* OSP lookup results, in/output */
{
	int res;
	char source[OSP_NORSTR_SIZE];
	unsigned int callidlen;
	char callidstr[OSPC_CALLID_MAXSIZE];
	char callingnum[OSP_NORSTR_SIZE];
	char callednum[OSP_NORSTR_SIZE];
	char destination[OSP_NORSTR_SIZE];
	unsigned int tokenlen;
	char token[OSP_TOKSTR_SIZE];
	unsigned int dummy = 0;
	enum OSPEFAILREASON cause;
	int error;

	result->outhandle = OSP_INVALID_HANDLE;
	result->tech[0] = '\0';
	result->dest[0] = '\0';
	result->calling[0] = '\0';
	result->token[0] = '\0';
	result->numresults = 0;
	result->outtimelimit = OSP_DEF_TIMELIMIT;

	if ((res = osp_genTransaction(provider, &result->outhandle, sizeof(source), source)) <= 0) {
		result->outhandle = OSP_INVALID_HANDLE;
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		ast_log(LOG_WARNING, "OSP: Unable to generate transaction handle\n");
		return(-1);
	}

	res = 0;
	dummy = 0;
	result->numresults = OSP_DEF_DESTINATIONS;
	error = OSPPTransactionRequestAuthorisation(
		result->outhandle, 
		source, srcdev,
		calling ? calling : "", OSPC_E164, 
		called, OSPC_E164, 
		NULL, 
		0, NULL, 
		NULL, 
		&result->numresults, 
		&dummy, NULL);
	if (error != OSPC_ERR_NO_ERROR) {
		result->numresults = 0;
		OSPPTransactionRecordFailure(result->outhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return(res);
	}

	if (!result->numresults) {
		result->numresults = 0;
		OSPPTransactionRecordFailure(result->outhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return(res);
	}

	callidlen = sizeof(callidstr);
	tokenlen = sizeof(token);
	error = OSPPTransactionGetFirstDestination(
		result->outhandle, 
		0, NULL, NULL, 
		&result->outtimelimit, 
		&callidlen, callidstr,
		sizeof(callednum), callednum, 
		sizeof(callingnum), callingnum, 
		sizeof(destination), destination, 
		0, NULL, 
		&tokenlen, token);
	if (error != OSPC_ERR_NO_ERROR) {
		result->token[0] = '\0';
		result->numresults = 0;
		result->outtimelimit = OSP_DEF_TIMELIMIT;
		OSPPTransactionRecordFailure(result->outhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		ast_log(LOG_DEBUG, "OSP: Unable to get first route\n");
		return(res);
	}

	do {
		result->outtimelimit = osp_choTimelimit(result->intimelimit, result->outtimelimit);
		ast_log(LOG_DEBUG, "OSP: outtimelimit '%d'\n", result->outtimelimit);
		ast_log(LOG_DEBUG, "OSP: called '%s'\n", callednum);
		ast_log(LOG_DEBUG, "OSP: calling '%s'\n", callingnum);
		ast_log(LOG_DEBUG, "OSP: destination '%s'\n", destination);
		ast_log(LOG_DEBUG, "OSP: token size '%d'\n", tokenlen);

		res = osp_chkDest(callednum, callingnum, destination, tokenlen, token, &cause, result);
		if (!res) {
			result->numresults--;
			if (result->numresults) {
				callidlen = sizeof(callidstr);
				tokenlen = sizeof(token);
				error = OSPPTransactionGetNextDestination(
					result->outhandle, 
					cause, 
					0, NULL, NULL, 
					&result->outtimelimit, 
					&callidlen, callidstr,
					sizeof(callednum), callednum, 
					sizeof(callingnum), callingnum, 
					sizeof(destination), destination, 
					0, NULL, 
					&tokenlen, token);
				if (error != OSPC_ERR_NO_ERROR) {
					result->token[0] = '\0';
					result->numresults = 0;
					result->outtimelimit = OSP_DEF_TIMELIMIT;
					OSPPTransactionRecordFailure(result->outhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
					if (result->inhandle != OSP_INVALID_HANDLE) {
						OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
					}
					break;
				}
			} else {
				result->token[0] = '\0';
				result->numresults = 0;
				result->outtimelimit = OSP_DEF_TIMELIMIT;
				OSPPTransactionRecordFailure(result->outhandle, cause);
				if (result->inhandle != OSP_INVALID_HANDLE) {
					OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
				}
			}
		} else {
			result->numresults--;
		}
	} while(!res && result->numresults);

	return(res);
}

static enum OSPEFAILREASON reason2cause(
	int reason)						/* Last call failure reason */
{
	enum OSPEFAILREASON cause;

	switch(reason) {
		case AST_CAUSE_NOTDEFINED:
			cause = OSPC_FAIL_NONE;
			break;
		case AST_CAUSE_BUSY:
			cause = OSPC_FAIL_USER_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			cause = OSPC_FAIL_SWITCHING_EQUIPMENT_CONGESTION;
			break;
		case AST_CAUSE_UNALLOCATED:
			cause = OSPC_FAIL_UNALLOC_NUMBER;
			break;
		case AST_CAUSE_NOANSWER:
			cause = OSPC_FAIL_NO_ANSWER_FROM_USER;
			break;
		case AST_CAUSE_NORMAL:
		default:
			cause = OSPC_FAIL_NORMAL_CALL_CLEARING;
			break;
	}

	return(cause);
}

int ast_osp_next(
	int reason,						/* Last desintaion failure reason */
	struct ast_osp_result *result)	/* OSP lookup results, output */
{
	int res = 0;
	unsigned int callidlen;
	char callidstr[OSPC_CALLID_MAXSIZE];
	char callingnum[OSP_NORSTR_SIZE];
	char callednum[OSP_NORSTR_SIZE];
	char destination[OSP_NORSTR_SIZE];
	unsigned int tokenlen;
	char token[OSP_TOKSTR_SIZE];
	enum OSPEFAILREASON cause;
	int error;

	result->tech[0] = '\0';
	result->dest[0] = '\0';
	result->calling[0] = '\0';
	result->token[0] = '\0';
	result->outtimelimit = OSP_DEF_TIMELIMIT;

	if (result->outhandle == OSP_INVALID_HANDLE) {
		result->numresults = 0;
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		ast_log(LOG_WARNING, "OSP: Transaction handle undefined\n");
		return(-1);
	}

	cause = reason2cause(reason);
	if (!result->numresults) {
		OSPPTransactionRecordFailure(result->outhandle, cause);
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		ast_log(LOG_DEBUG, "OSP: No more destination\n");
		return(res);
	}

	while(!res && result->numresults) {
		result->numresults--;
		callidlen = sizeof(callidstr);
		tokenlen = sizeof(token);
		error = OSPPTransactionGetNextDestination(
			result->outhandle, 
			cause, 
			0, NULL, NULL, 
			&result->outtimelimit, 
			&callidlen, callidstr,
			sizeof(callednum), callednum, 
			sizeof(callingnum), callingnum, 
			sizeof(destination), destination, 
			0, NULL, 
			&tokenlen, token);
		if (error == OSPC_ERR_NO_ERROR) {
			result->outtimelimit = osp_choTimelimit(result->intimelimit, result->outtimelimit);
			ast_log(LOG_DEBUG, "OSP: outtimelimit '%d'\n", result->outtimelimit);
			ast_log(LOG_DEBUG, "OSP: called '%s'\n", callednum);
			ast_log(LOG_DEBUG, "OSP: calling '%s'\n", callingnum);
			ast_log(LOG_DEBUG, "OSP: destination '%s'\n", destination);
			ast_log(LOG_DEBUG, "OSP: token size '%d'\n", tokenlen);

			res = osp_chkDest(callednum, callingnum, destination, tokenlen, token, &cause, result);
			if (!res && !result->numresults) {
				OSPPTransactionRecordFailure(result->outhandle, cause);
				if (result->inhandle != OSP_INVALID_HANDLE) {
					OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
				}
			}
		} else {
			result->token[0] = '\0';
			result->numresults = 0;
			result->outtimelimit = OSP_DEF_TIMELIMIT;
			OSPPTransactionRecordFailure(result->outhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
			if (result->inhandle != OSP_INVALID_HANDLE) {
				OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
			}
		}
	}

	return(res);
}

int ast_osp_finish(
	int handle,						/* OSP in/out_bound transaction handle */
	int reason,						/* Last destination failure reason */
	time_t start, 					/* Call start time */
	time_t connect,					/* Call connect time */
	time_t end)						/* Call end time*/
{
	int res = 1;
	unsigned int dummy = 0;
	enum OSPEFAILREASON cause;
	time_t alert = 0;
	unsigned isPddInfoPresent = 0;
	unsigned pdd = 0;
	unsigned releaseSource = 0;
	unsigned char *confId = "";
	int error;
	
	if (handle == OSP_INVALID_HANDLE) {
		return(res);
	}

	if ((cause = reason2cause(reason)) != OSPC_FAIL_NONE) {
		OSPPTransactionRecordFailure(handle, cause);
	}
	error = OSPPTransactionReportUsage(
		handle, 
		difftime(end, connect), start, end, alert, connect, 
		isPddInfoPresent, pdd, 
		releaseSource, 
		confId,
		0, 0, 0, 0,
		&dummy, NULL);
	if (error == OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Usage reported\n");
		res = 1;
	} else {
		ast_log(LOG_DEBUG, "OSP: Unable to report usage, error = %d\n", error);
		res = 0;
	}
	OSPPTransactionDelete(handle);

	return(res);
}

void ast_osp_adduse(void)
{
	osp_usecount++;
}

void ast_osp_deluse(void)
{
	if (osp_usecount > 0) {
		osp_usecount--;
	}
}

static char osp_usage[] =
"Usage: show osp\n"
"       Displays information on Open Settlement Protocol support\n";

static struct ast_cli_entry osp_cli = {
	{"show", "osp", NULL}, 
	osp_show, 
	"Displays OSP information", 
	osp_usage 
};

static int osp_load(void)
{
	char* t;
	unsigned int v;
	struct ast_config* cfg;
	int error = OSPC_ERR_NO_ERROR;

	cfg = ast_config_load(OSP_CONFIG_FILE);
	if (cfg) {
		t = ast_variable_retrieve(cfg, OSP_GENERAL_CAT, "accelerate");
		if (t && ast_true(t)) {
			if ((error = OSPPInit(1)) != OSPC_ERR_NO_ERROR) {
				ast_log(LOG_WARNING, "OSP: Unable to enable hardware accelleration\n");
				OSPPInit(0);
			} else {
				osp_hardware = 1;
			}
		} else {
			OSPPInit(0);
		}
		ast_log(LOG_DEBUG, "OSP: osp_hardware '%d'\n", osp_hardware);

		t = ast_variable_retrieve(cfg, OSP_GENERAL_CAT, "tokenformat");
		if (t) {
			if ((sscanf(t, "%d", &v) == 1) && 
				((v == TOKEN_ALGO_SIGNED) || (v == TOKEN_ALGO_UNSIGNED) || (v == TOKEN_ALGO_BOTH))) 
			{
				osp_tokenformat = v;
			} else {
				ast_log(LOG_WARNING, "tokenformat should be an integer from %d, %d or %d, not '%s'\n", 
					TOKEN_ALGO_SIGNED, TOKEN_ALGO_UNSIGNED, TOKEN_ALGO_BOTH, t);
			}
		}
		ast_log(LOG_DEBUG, "OSP: osp_tokenformat '%d'\n", osp_tokenformat);

		t = ast_category_browse(cfg, NULL);
		while(t) {
			if (strcasecmp(t, OSP_GENERAL_CAT)) {
				osp_buildProvider(cfg, t);
			}
			t = ast_category_browse(cfg, t);
		}

		osp_initialized = 1;

		ast_config_destroy(cfg);
	} else {
		ast_log(LOG_WARNING, "OSP: Unable to find configuration. OSP support disabled\n");
	}
	ast_log(LOG_DEBUG, "OSP: osp_initialized '%d'\n", osp_initialized);

	return(0);
}

static int osp_unload(void)
{
	struct osp_provider* p;
	struct osp_provider* next;

	if (osp_initialized) {
		ast_mutex_lock(&osplock);
		p = ospproviders;
		while(p) {
			next = p->next;
			OSPPProviderDelete(p->handle, 0);
			free(p);
			p = next;
		}
		ospproviders = NULL;
		ast_mutex_unlock(&osplock);

		OSPPCleanup();

		osp_usecount = 0;
		osp_tokenformat = TOKEN_ALGO_SIGNED;
		osp_hardware = 0;
		osp_initialized = 0;
	}
	return(0);
}

static int osp_show(int fd, int argc, char *argv[])
{
	int i;
	int found = 0;
	struct osp_provider* p;
	char* provider = NULL;
	char* tokenalgo;

	if ((argc < 2) || (argc > 3)) {
		return(RESULT_SHOWUSAGE);
	}
	if (argc > 2) {
		provider = argv[2];
	}
	if (!provider) {
		switch (osp_tokenformat) {
			case TOKEN_ALGO_BOTH:
				tokenalgo = "Both";
				break;
			case TOKEN_ALGO_UNSIGNED:
				tokenalgo = "Unsigned";
				break;
			case TOKEN_ALGO_SIGNED:
			default:
				tokenalgo = "Signed";
				break;
		}
		ast_cli(fd, "OSP: %s %s %s\n", 
			osp_initialized ? "Initialized" : "Uninitialized", osp_hardware ? "Accelerated" : "Normal", tokenalgo);
	}

	ast_mutex_lock(&osplock);
	p = ospproviders;
	while(p) {
		if (!provider || !strcasecmp(p->name, provider)) {
			if (found) {
				ast_cli(fd, "\n");
			}
			ast_cli(fd, " == OSP Provider '%s' == \n", p->name);
			ast_cli(fd, "Local Private Key: %s\n", p->privatekey);
			ast_cli(fd, "Local Certificate: %s\n", p->localcert);
			for (i = 0; i < p->cacount; i++) {
				ast_cli(fd, "CA Certificate %d:  %s\n", i + 1, p->cacerts[i]);
			}
			for (i = 0; i < p->spcount; i++) {
				ast_cli(fd, "Service Point %d:   %s\n", i + 1, p->srvpoints[i]);
			}
			ast_cli(fd, "Max Connections:   %d\n", p->maxconnections);
			ast_cli(fd, "Retry Delay:       %d seconds\n", p->retrydelay);
			ast_cli(fd, "Retry Limit:       %d\n", p->retrylimit);
			ast_cli(fd, "Timeout:           %d milliseconds\n", p->timeout);
			ast_cli(fd, "Source:            %s\n", strlen(p->source) ? p->source : "<unspecified>");
			ast_cli(fd, "Auth Policy        %d\n", p->authpolicy);
			ast_cli(fd, "OSP Handle:        %d\n", p->handle);
			found++;
		}
		p = p->next;
	}
	ast_mutex_unlock(&osplock);

	if (!found) {
		if (provider) {
			ast_cli(fd, "Unable to find OSP provider '%s'\n", provider);
		} else {
			ast_cli(fd, "No OSP providers configured\n");
		}
	}
	return(RESULT_SUCCESS);
}

int load_module(void)
{
	osp_load();
	ast_cli_register(&osp_cli);
	return(0);
}

int reload(void)
{
	ast_cli_unregister(&osp_cli);
	osp_unload();
	osp_load();
	ast_cli_register(&osp_cli);
	return(0);
}

int unload_module(void)
{
	ast_cli_unregister(&osp_cli);
	osp_unload();
	return(0);
}

char *description(void)
{
	return("Open Settlement Protocol Support");
}

int usecount(void)
{
	return(osp_usecount);
}

char *key()
{
	return(ASTERISK_GPL_KEY);
}

