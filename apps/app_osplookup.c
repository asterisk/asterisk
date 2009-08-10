/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Open Settlement Protocol (OSP) Applications
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>osptk</depend>
	<depend>ssl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <osp/osp.h>
#include <osp/osputils.h>

#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/astosp.h"

/* OSP Buffer Sizes */
#define OSP_INTSTR_SIZE			((unsigned int)16)			/* OSP signed/unsigned int string buffer size */
#define OSP_NORSTR_SIZE			((unsigned int)256)			/* OSP normal string buffer size */
#define OSP_TOKSTR_SIZE			((unsigned int)4096)			/* OSP token string buffer size */

/* OSP Constants */
#define OSP_INVALID_HANDLE		((int)-1)				/* Invalid OSP handle, provider, transaction etc. */
#define OSP_CONFIG_FILE			((const char*)"osp.conf")		/* OSP configuration file name */
#define OSP_GENERAL_CAT			((const char*)"general")		/* OSP global configuration context name */
#define OSP_DEF_PROVIDER		((const char*)"default")		/* OSP default provider context name */
#define OSP_MAX_CERTS			((unsigned int)10)			/* OSP max number of cacerts */
#define OSP_MAX_SRVS			((unsigned int)10)			/* OSP max number of service points */
#define OSP_DEF_MAXCONNECTIONS		((unsigned int)20)			/* OSP default max_connections */
#define OSP_MIN_MAXCONNECTIONS		((unsigned int)1)			/* OSP min max_connections */
#define OSP_MAX_MAXCONNECTIONS		((unsigned int)1000)			/* OSP max max_connections */
#define OSP_DEF_RETRYDELAY		((unsigned int)0)			/* OSP default retry delay */
#define OSP_MIN_RETRYDELAY		((unsigned int)0)			/* OSP min retry delay */
#define OSP_MAX_RETRYDELAY		((unsigned int)10)			/* OSP max retry delay */
#define OSP_DEF_RETRYLIMIT		((unsigned int)2)			/* OSP default retry times */
#define OSP_MIN_RETRYLIMIT		((unsigned int)0)			/* OSP min retry times */
#define OSP_MAX_RETRYLIMIT		((unsigned int)100)			/* OSP max retry times */
#define OSP_DEF_TIMEOUT			((unsigned int)500)			/* OSP default timeout in ms */
#define OSP_MIN_TIMEOUT			((unsigned int)200)			/* OSP min timeout in ms */
#define OSP_MAX_TIMEOUT			((unsigned int)10000)			/* OSP max timeout in ms */
#define OSP_DEF_AUTHPOLICY		((enum osp_authpolicy)OSP_AUTH_YES)
#define OSP_AUDIT_URL			((const char*)"localhost")		/* OSP default Audit URL */
#define OSP_LOCAL_VALIDATION		((int)1)				/* Validate OSP token locally */
#define OSP_SSL_LIFETIME		((unsigned int)300)			/* SSL life time, in seconds */
#define OSP_HTTP_PERSISTENCE		((int)1)				/* In seconds */
#define OSP_CUSTOMER_ID			((const char*)"")			/* OSP customer ID */
#define OSP_DEVICE_ID			((const char*)"")			/* OSP device ID */
#define OSP_DEF_DESTINATIONS		((unsigned int)5)			/* OSP default max number of destinations */
#define OSP_DEF_TIMELIMIT		((unsigned int)0)			/* OSP default duration limit, no limit */

/* OSP Authentication Policy */
enum osp_authpolicy {
	OSP_AUTH_NO,		/* Accept any call */
	OSP_AUTH_YES,		/* Accept call with valid OSP token or without OSP token */
	OSP_AUTH_EXCLUSIVE	/* Only accept call with valid OSP token */
};

/* OSP Provider */
struct osp_provider {
	char name[OSP_NORSTR_SIZE];				/* OSP provider context name */
	char privatekey[OSP_NORSTR_SIZE];			/* OSP private key file name */
	char localcert[OSP_NORSTR_SIZE];			/* OSP local cert file name */
	unsigned int cacount;					/* Number of cacerts */
	char cacerts[OSP_MAX_CERTS][OSP_NORSTR_SIZE]; 		/* Cacert file names */
	unsigned int spcount;					/* Number of service points */
	char srvpoints[OSP_MAX_SRVS][OSP_NORSTR_SIZE];		/* Service point URLs */
	int maxconnections;					/* Max number of connections */
	int retrydelay;						/* Retry delay */
	int retrylimit;						/* Retry limit */
	int timeout;						/* Timeout in ms */
	char source[OSP_NORSTR_SIZE];				/* IP of self */
	enum osp_authpolicy authpolicy;				/* OSP authentication policy */
	OSPTPROVHANDLE handle;					/* OSP provider handle */
	struct osp_provider* next;				/* Pointer to next OSP provider */
};

/* OSP Application In/Output Results */
struct osp_result {
	int inhandle;						/* Inbound transaction handle */
	int outhandle;						/* Outbound transaction handle */
	unsigned int intimelimit;				/* Inbound duration limit */
	unsigned int outtimelimit;				/* Outbound duration limit */
	char tech[20];						/* Asterisk TECH string */
	char dest[OSP_NORSTR_SIZE];				/* Destination in called@IP format */
	char calling[OSP_NORSTR_SIZE];				/* Calling number, may be translated */
	char token[OSP_TOKSTR_SIZE];				/* Outbound OSP token */
	unsigned int numresults;				/* Number of remain destinations */
};

/* OSP Module Global Variables */
AST_MUTEX_DEFINE_STATIC(osplock);				/* Lock of OSP provider list */
static int osp_initialized = 0;					/* Init flag */
static int osp_hardware = 0;					/* Hardware accelleration flag */
static struct osp_provider* ospproviders = NULL;		/* OSP provider list */
static unsigned int osp_tokenformat = TOKEN_ALGO_SIGNED;	/* Token format supported */

/* OSP Client Wrapper APIs */

/*!
 * \brief Create OSP provider handle according to configuration
 * \param cfg OSP configuration
 * \param provider OSP provider context name
 * \return 1 Success, 0 Failed, -1 Error
 */
static int osp_create_provider(struct ast_config* cfg, const char* provider)
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

	if (!(p = ast_calloc(1, sizeof(*p)))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	ast_copy_string(p->name, provider, sizeof(p->name));
	snprintf(p->privatekey, sizeof(p->privatekey), "%s/%s-privatekey.pem", ast_config_AST_KEY_DIR, provider);
	snprintf(p->localcert, sizeof(p->localcert), "%s/%s-localcert.pem", ast_config_AST_KEY_DIR, provider);
	p->maxconnections = OSP_DEF_MAXCONNECTIONS;
	p->retrydelay = OSP_DEF_RETRYDELAY;
	p->retrylimit = OSP_DEF_RETRYLIMIT;
	p->timeout = OSP_DEF_TIMEOUT;
	p->authpolicy = OSP_DEF_AUTHPOLICY;
	p->handle = OSP_INVALID_HANDLE;

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
			if ((sscanf(v->value, "%30d", &t) == 1) && (t >= OSP_MIN_MAXCONNECTIONS) && (t <= OSP_MAX_MAXCONNECTIONS)) {
				p->maxconnections = t;
				ast_log(LOG_DEBUG, "OSP: maxconnections '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: maxconnections should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_MAXCONNECTIONS, OSP_MAX_MAXCONNECTIONS, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "retrydelay")) {
			if ((sscanf(v->value, "%30d", &t) == 1) && (t >= OSP_MIN_RETRYDELAY) && (t <= OSP_MAX_RETRYDELAY)) {
				p->retrydelay = t;
				ast_log(LOG_DEBUG, "OSP: retrydelay '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: retrydelay should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_RETRYDELAY, OSP_MAX_RETRYDELAY, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "retrylimit")) {
			if ((sscanf(v->value, "%30d", &t) == 1) && (t >= OSP_MIN_RETRYLIMIT) && (t <= OSP_MAX_RETRYLIMIT)) {
				p->retrylimit = t;
				ast_log(LOG_DEBUG, "OSP: retrylimit '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: retrylimit should be an integer from %d to %d, not '%s' at line %d\n", 
					OSP_MIN_RETRYLIMIT, OSP_MAX_RETRYLIMIT, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "timeout")) {
			if ((sscanf(v->value, "%30d", &t) == 1) && (t >= OSP_MIN_TIMEOUT) && (t <= OSP_MAX_TIMEOUT)) {
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
			if ((sscanf(v->value, "%30d", &t) == 1) && ((t == OSP_AUTH_NO) || (t == OSP_AUTH_YES) || (t == OSP_AUTH_EXCLUSIVE))) {
				p->authpolicy = t;
				ast_log(LOG_DEBUG, "OSP: authpolicy '%d'\n", t);
			} else {
				ast_log(LOG_WARNING, "OSP: authpolicy should be %d, %d or %d, not '%s' at line %d\n", 
					OSP_AUTH_NO, OSP_AUTH_YES, OSP_AUTH_EXCLUSIVE, v->value, v->lineno);
			}
		}
		v = v->next;
	}

	error = OSPPUtilLoadPEMPrivateKey((unsigned char *) p->privatekey, &privatekey);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to load privatekey '%s', error '%d'\n", p->privatekey, error);
		free(p);
		return 0;
	}

	error = OSPPUtilLoadPEMCert((unsigned char *) p->localcert, &localcert);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to load localcert '%s', error '%d'\n", p->localcert, error);
		if (privatekey.PrivateKeyData) {
			free(privatekey.PrivateKeyData);
		}
		free(p);
		return 0;
	}

	if (p->cacount < 1) {
		snprintf(p->cacerts[p->cacount], sizeof(p->cacerts[0]), "%s/%s-cacert.pem", ast_config_AST_KEY_DIR, provider);
		ast_log(LOG_DEBUG, "OSP: cacert[%d]: '%s'\n", p->cacount, p->cacerts[p->cacount]);
		p->cacount++;
	}
	for (i = 0; i < p->cacount; i++) {
		error = OSPPUtilLoadPEMCert((unsigned char *) p->cacerts[i], &cacerts[i]);
		if (error != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to load cacert '%s', error '%d'\n", p->cacerts[i], error);
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
			return 0;
		}
		pcacerts[i] = &cacerts[i];
	}
	
	for (i = 0; i < p->spcount; i++) {
		psrvpoints[i] = p->srvpoints[i];
	}

	error = OSPPProviderNew(p->spcount, psrvpoints, NULL, OSP_AUDIT_URL, &privatekey, &localcert, p->cacount, pcacerts, OSP_LOCAL_VALIDATION,
				OSP_SSL_LIFETIME, p->maxconnections, OSP_HTTP_PERSISTENCE, p->retrydelay, p->retrylimit,p->timeout, OSP_CUSTOMER_ID,
				OSP_DEVICE_ID, &p->handle);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to create provider '%s', error '%d'\n", provider, error);
		free(p);
		res = -1;
	} else {
		ast_log(LOG_DEBUG, "OSP: provider '%s'\n", provider);
		ast_mutex_lock(&osplock);
		p->next = ospproviders;
		ospproviders = p;
		ast_mutex_unlock(&osplock); 	
		res = 1;
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

	return res;
}

/*!
 * \brief Get OSP authenticiation policy of provider
 * \param provider OSP provider context name
 * \param policy OSP authentication policy, output
 * \return 1 Success, 0 Failed, -1 Error
 */
static int osp_get_policy(const char* provider, int* policy)
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

	return res;
}

/*!
 * \brief Create OSP transaction handle
 * \param provider OSP provider context name
 * \param transaction OSP transaction handle, output
 * \param sourcesize Size of source buffer, in/output
 * \param source Source of provider, output
 * \return 1 Success, 0 Failed, -1 Error
 */
static int osp_create_transaction(const char* provider, int* transaction, unsigned int sourcesize, char* source)
{
	int res = 0;
	struct osp_provider* p;
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
				ast_log(LOG_DEBUG, "OSP: Unable to create transaction handle, error '%d'\n", error);
				res = -1;
			}
			break;
		}
		p = p->next;
	}
	ast_mutex_unlock(&osplock);

	return res;
}

/*!
 * \brief Convert address to "[x.x.x.x]" or "host.domain" format
 * \param src Source address string
 * \param dst Destination address string
 * \param buffersize Size of dst buffer
 */
static void osp_convert_address(
	const char* src,
	char* dst,
	int buffersize)
{
	struct in_addr inp;

	if (inet_aton(src, &inp) != 0) {
		snprintf(dst, buffersize, "[%s]", src);
	} else {
		snprintf(dst, buffersize, "%s", src);
	}
}

/*!
 * \brief Validate OSP token of inbound call
 * \param transaction OSP transaction handle
 * \param source Source of inbound call
 * \param dest Destination of inbound call
 * \param calling Calling number
 * \param called Called number
 * \param token OSP token, may be empty
 * \param timelimit Call duration limit, output
 * \return 1 Success, 0 Failed, -1 Error
 */
static int osp_validate_token(int transaction, const char* source, const char* dest, const char* calling, const char* called, const char* token, unsigned int* timelimit)
{
	int res;
	int tokenlen;
	unsigned char tokenstr[OSP_TOKSTR_SIZE];
	char src[OSP_NORSTR_SIZE];
	char dst[OSP_NORSTR_SIZE];
	unsigned int authorised;
	unsigned int dummy = 0;
	int error;

	tokenlen = ast_base64decode(tokenstr, token, strlen(token));
	osp_convert_address(source, src, sizeof(src));
	osp_convert_address(dest, dst, sizeof(dst));
	error = OSPPTransactionValidateAuthorisation(
		transaction, 
		src, dst, NULL, NULL,
		calling ? calling : "", OSPC_E164, 
		called, OSPC_E164, 
		0, NULL,
		tokenlen, (char *) tokenstr, 
		&authorised, 
		timelimit, 
		&dummy, NULL, 
		osp_tokenformat); 
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Unable to validate inbound token\n");
		res = -1;
	} else if (authorised) {
		ast_log(LOG_DEBUG, "OSP: Authorised\n");
		res = 1;
	} else {
		ast_log(LOG_DEBUG, "OSP: Unauthorised\n");
		res = 0;
	}
	
	return res;
}

/*!
 * \brief Choose min duration limit
 * \param in Inbound duration limit
 * \param out Outbound duration limit
 * \return min duration limit
 */
static unsigned int osp_choose_timelimit(unsigned int in, unsigned int out)
{
	if (in == OSP_DEF_TIMELIMIT) {
		return out;
	} else if (out == OSP_DEF_TIMELIMIT) {
		return in;
	} else {
		return in < out ? in : out;
	}
}

/*!
 * \brief Choose min duration limit
 * \param called Called number
 * \param calling Calling number
 * \param destination Destination IP in '[x.x.x.x]' format
 * \param tokenlen OSP token length
 * \param token OSP token
 * \param reason Failure reason, output
 * \param result OSP lookup results, in/output
 * \return 1 Success, 0 Failed, -1 Error
 */
static int osp_check_destination(const char* called, const char* calling, char* destination, unsigned int tokenlen, const char* token, enum OSPEFAILREASON* reason, struct osp_result* result)
{
	int res;
	OSPE_DEST_OSP_ENABLED enabled;
	OSPE_DEST_PROT protocol;
	int error;

	if (strlen(destination) <= 2) {
		ast_log(LOG_DEBUG, "OSP: Wrong destination format '%s'\n", destination);
		*reason = OSPC_FAIL_NORMAL_UNSPECIFIED;
		return -1;
	} 

	if ((error = OSPPTransactionIsDestOSPEnabled(result->outhandle, &enabled)) != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Unable to get destination OSP version, error '%d'\n", error);
		*reason = OSPC_FAIL_NORMAL_UNSPECIFIED;
		return -1;
	}

	if (enabled == OSPE_OSP_FALSE) {
		result->token[0] = '\0';
	} else {
		ast_base64encode(result->token, (const unsigned char *) token, tokenlen, sizeof(result->token) - 1);
	}

	if ((error = OSPPTransactionGetDestProtocol(result->outhandle, &protocol)) != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Unable to get destination protocol, error '%d'\n", error);
		*reason = OSPC_FAIL_NORMAL_UNSPECIFIED; 
		result->token[0] = '\0';
		return -1;
	} 

	res = 1;
	/* Strip leading and trailing brackets */
	destination[strlen(destination) - 1] = '\0';
	switch(protocol) {
		case OSPE_DEST_PROT_H323_SETUP:
			ast_log(LOG_DEBUG, "OSP: protocol '%d'\n", protocol);
			ast_copy_string(result->tech, "H323", sizeof(result->tech));
			snprintf(result->dest, sizeof(result->dest), "%s@%s", called, destination + 1);
			ast_copy_string(result->calling, calling, sizeof(result->calling));
			break;
		case OSPE_DEST_PROT_SIP:
			ast_log(LOG_DEBUG, "OSP: protocol '%d'\n", protocol);
			ast_copy_string(result->tech, "SIP", sizeof(result->tech));
			snprintf(result->dest, sizeof(result->dest), "%s@%s", called, destination + 1);
			ast_copy_string(result->calling, calling, sizeof(result->calling));
			break;
		case OSPE_DEST_PROT_IAX:
			ast_log(LOG_DEBUG, "OSP: protocol '%d'\n", protocol);
			ast_copy_string(result->tech, "IAX", sizeof(result->tech));
			snprintf(result->dest, sizeof(result->dest), "%s@%s", called, destination + 1);
			ast_copy_string(result->calling, calling, sizeof(result->calling));
			break;
		default:
			ast_log(LOG_DEBUG, "OSP: Unknown protocol '%d'\n", protocol);
			*reason = OSPC_FAIL_PROTOCOL_ERROR; 
			result->token[0] = '\0';
			res = 0;
	}

	return res;
}

/*!
 * \brief Convert Asterisk status to TC code
 * \param cause Asterisk hangup cause
 * \return OSP TC code
 */
static enum OSPEFAILREASON asterisk2osp(int cause)
{
	return (enum OSPEFAILREASON)cause;
}

/*!
 * \brief OSP Authentication function
 * \param provider OSP provider context name
 * \param transaction OSP transaction handle, output
 * \param source Source of inbound call
 * \param calling Calling number
 * \param called Called number
 * \param token OSP token, may be empty
 * \param timelimit Call duration limit, output
 * \return 1 Authenricated, 0 Unauthenticated, -1 Error
 */
static int osp_auth(const char* provider, int* transaction, const char* source, const char* calling, const char* called, const char* token, unsigned int* timelimit)
{
	int res;
	int policy = OSP_AUTH_YES;
	char dest[OSP_NORSTR_SIZE];

	*transaction = OSP_INVALID_HANDLE;
	*timelimit = OSP_DEF_TIMELIMIT;
	res = osp_get_policy(provider, &policy);
	if (!res) {
		ast_log(LOG_DEBUG, "OSP: Unabe to find OSP authentication policy\n");
		return res;
	}

	switch (policy) {
		case OSP_AUTH_NO:
			res = 1;
			break;
		case OSP_AUTH_EXCLUSIVE:
			if (ast_strlen_zero(token)) {
				res = 0;
			} else if ((res = osp_create_transaction(provider, transaction, sizeof(dest), dest)) <= 0) {
				ast_log(LOG_DEBUG, "OSP: Unable to generate transaction handle\n");
				*transaction = OSP_INVALID_HANDLE;
				res = 0;
			} else if((res = osp_validate_token(*transaction, source, dest, calling, called, token, timelimit)) <= 0) {
				OSPPTransactionRecordFailure(*transaction, OSPC_FAIL_CALL_REJECTED);
			}
			break;
		case OSP_AUTH_YES:
		default:
			if (ast_strlen_zero(token)) {
				res = 1;
			} else if ((res = osp_create_transaction(provider, transaction, sizeof(dest), dest)) <= 0) {
				ast_log(LOG_DEBUG, "OSP: Unable to generate transaction handle\n");
				*transaction = OSP_INVALID_HANDLE;
				res = 0;
			} else if((res = osp_validate_token(*transaction, source, dest, calling, called, token, timelimit)) <= 0) {
				OSPPTransactionRecordFailure(*transaction, OSPC_FAIL_CALL_REJECTED);
			}
			break;
	}

	return res;
}

/*!
 * \brief OSP Lookup function
 * \param provider OSP provider context name
 * \param srcdev Source device of outbound call
 * \param calling Calling number
 * \param called Called number
 * \param result Lookup results
 * \return 1 Found , 0 No route, -1 Error
 */
static int osp_lookup(const char* provider, const char* srcdev, const char* calling, const char* called, struct osp_result* result)
{
	int res;
	char source[OSP_NORSTR_SIZE];
	unsigned int callidlen;
	char callid[OSPC_CALLID_MAXSIZE];
	char callingnum[OSP_NORSTR_SIZE];
	char callednum[OSP_NORSTR_SIZE];
	char destination[OSP_NORSTR_SIZE];
	unsigned int tokenlen;
	char token[OSP_TOKSTR_SIZE];
	char src[OSP_NORSTR_SIZE];
	char dev[OSP_NORSTR_SIZE];
	unsigned int dummy = 0;
	enum OSPEFAILREASON reason;
	int error;

	result->outhandle = OSP_INVALID_HANDLE;
	result->tech[0] = '\0';
	result->dest[0] = '\0';
	result->calling[0] = '\0';
	result->token[0] = '\0';
	result->numresults = 0;
	result->outtimelimit = OSP_DEF_TIMELIMIT;

	if ((res = osp_create_transaction(provider, &result->outhandle, sizeof(source), source)) <= 0) {
		ast_log(LOG_DEBUG, "OSP: Unable to generate transaction handle\n");
		result->outhandle = OSP_INVALID_HANDLE;
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		}
		return -1;
	}

	osp_convert_address(source, src, sizeof(src));
	osp_convert_address(srcdev, dev, sizeof(dev));
	result->numresults = OSP_DEF_DESTINATIONS;
	error = OSPPTransactionRequestAuthorisation(result->outhandle, src, dev, calling ? calling : "",
			OSPC_E164, called, OSPC_E164, NULL, 0, NULL, NULL, &result->numresults, &dummy, NULL);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Unable to request authorization\n");
		result->numresults = 0;
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		}
		return -1;
	}

	if (!result->numresults) {
		ast_log(LOG_DEBUG, "OSP: No more destination\n");
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return 0;
	}

	callidlen = sizeof(callid);
	tokenlen = sizeof(token);
	error = OSPPTransactionGetFirstDestination(result->outhandle, 0, NULL, NULL, &result->outtimelimit, &callidlen, callid,
			sizeof(callednum), callednum, sizeof(callingnum), callingnum, sizeof(destination), destination, 0, NULL, &tokenlen, token);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Unable to get first route\n");
		result->numresults = 0;
		result->outtimelimit = OSP_DEF_TIMELIMIT;
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return -1;
	}

	result->numresults--;
	result->outtimelimit = osp_choose_timelimit(result->intimelimit, result->outtimelimit);
	ast_log(LOG_DEBUG, "OSP: outtimelimit '%d'\n", result->outtimelimit);
	ast_log(LOG_DEBUG, "OSP: called '%s'\n", callednum);
	ast_log(LOG_DEBUG, "OSP: calling '%s'\n", callingnum);
	ast_log(LOG_DEBUG, "OSP: destination '%s'\n", destination);
	ast_log(LOG_DEBUG, "OSP: token size '%d'\n", tokenlen);

	if ((res = osp_check_destination(callednum, callingnum, destination, tokenlen, token, &reason, result)) > 0) {
		return 1;
	}

	if (!result->numresults) {
		ast_log(LOG_DEBUG, "OSP: No more destination\n");
		result->outtimelimit = OSP_DEF_TIMELIMIT;
		OSPPTransactionRecordFailure(result->outhandle, reason);
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return 0;
	}

	while(result->numresults) {
		callidlen = sizeof(callid);
		tokenlen = sizeof(token);
		error = OSPPTransactionGetNextDestination(result->outhandle, reason, 0, NULL, NULL, &result->outtimelimit, &callidlen, callid,
				sizeof(callednum), callednum, sizeof(callingnum), callingnum, sizeof(destination), destination, 0, NULL, &tokenlen, token);
		if (error == OSPC_ERR_NO_ERROR) {
			result->numresults--;
			result->outtimelimit = osp_choose_timelimit(result->intimelimit, result->outtimelimit);
			ast_log(LOG_DEBUG, "OSP: outtimelimit '%d'\n", result->outtimelimit);
			ast_log(LOG_DEBUG, "OSP: called '%s'\n", callednum);
			ast_log(LOG_DEBUG, "OSP: calling '%s'\n", callingnum);
			ast_log(LOG_DEBUG, "OSP: destination '%s'\n", destination);
			ast_log(LOG_DEBUG, "OSP: token size '%d'\n", tokenlen);
			if ((res = osp_check_destination(callednum, callingnum, destination, tokenlen, token, &reason, result)) > 0) {
				break;
			} else if (!result->numresults) {
				ast_log(LOG_DEBUG, "OSP: No more destination\n");
				OSPPTransactionRecordFailure(result->outhandle, reason);
				if (result->inhandle != OSP_INVALID_HANDLE) {
					OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
				}
				res = 0;
				break;
			}
		} else {
			ast_log(LOG_DEBUG, "OSP: Unable to get route, error '%d'\n", error);
			result->numresults = 0;
			result->outtimelimit = OSP_DEF_TIMELIMIT;
			if (result->inhandle != OSP_INVALID_HANDLE) {
				OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
			}
			res = -1;
			break;
		}
	}
	return res;
}

/*!
 * \brief OSP Lookup Next function
 * \param cause Asterisk hangup cuase
 * \param result Lookup results, in/output
 * \return 1 Found , 0 No route, -1 Error
 */
static int osp_next(int cause, struct osp_result* result)
{
	int res;
	unsigned int callidlen;
	char callid[OSPC_CALLID_MAXSIZE];
	char callingnum[OSP_NORSTR_SIZE];
	char callednum[OSP_NORSTR_SIZE];
	char destination[OSP_NORSTR_SIZE];
	unsigned int tokenlen;
	char token[OSP_TOKSTR_SIZE];
	enum OSPEFAILREASON reason;
	int error;

	result->tech[0] = '\0';
	result->dest[0] = '\0';
	result->calling[0] = '\0';
	result->token[0] = '\0';
	result->outtimelimit = OSP_DEF_TIMELIMIT;

	if (result->outhandle == OSP_INVALID_HANDLE) {
		ast_log(LOG_DEBUG, "OSP: Transaction handle undefined\n");
		result->numresults = 0;
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		}
		return -1;
	}

	reason = asterisk2osp(cause);

	if (!result->numresults) {
		ast_log(LOG_DEBUG, "OSP: No more destination\n");
		OSPPTransactionRecordFailure(result->outhandle, reason);
		if (result->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return 0;
	}

	while(result->numresults) {
		callidlen = sizeof(callid);
		tokenlen = sizeof(token);
		error = OSPPTransactionGetNextDestination(result->outhandle, reason, 0, NULL, NULL, &result->outtimelimit, &callidlen,
				callid, sizeof(callednum), callednum, sizeof(callingnum), callingnum, sizeof(destination), destination, 0, NULL, &tokenlen, token);
		if (error == OSPC_ERR_NO_ERROR) {
			result->numresults--;
			result->outtimelimit = osp_choose_timelimit(result->intimelimit, result->outtimelimit);
			ast_log(LOG_DEBUG, "OSP: outtimelimit '%d'\n", result->outtimelimit);
			ast_log(LOG_DEBUG, "OSP: called '%s'\n", callednum);
			ast_log(LOG_DEBUG, "OSP: calling '%s'\n", callingnum);
			ast_log(LOG_DEBUG, "OSP: destination '%s'\n", destination);
			ast_log(LOG_DEBUG, "OSP: token size '%d'\n", tokenlen);
			if ((res = osp_check_destination(callednum, callingnum, destination, tokenlen, token, &reason, result)) > 0) {
				res = 1;
				break;
			} else if (!result->numresults) {
				ast_log(LOG_DEBUG, "OSP: No more destination\n");
				OSPPTransactionRecordFailure(result->outhandle, reason);
				if (result->inhandle != OSP_INVALID_HANDLE) {
					OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
				}
				res = 0;
				break;
			}
		} else {
			ast_log(LOG_DEBUG, "OSP: Unable to get route, error '%d'\n", error);
			result->token[0] = '\0';
			result->numresults = 0;
			result->outtimelimit = OSP_DEF_TIMELIMIT;
			if (result->inhandle != OSP_INVALID_HANDLE) {
				OSPPTransactionRecordFailure(result->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
			}
			res = -1;
			break;
		}
	}

	return res;
}

/*!
 * \brief OSP Finish function
 * \param handle OSP in/outbound transaction handle
 * \param recorded If failure reason has been recorded
 * \param cause Asterisk hangup cause
 * \param start Call start time
 * \param connect Call connect time
 * \param end Call end time
 * \param release Who release first, 0 source, 1 destination
 * \return 1 Success, 0 Failed, -1 Error
 */
static int osp_finish(int handle, int recorded, int cause, time_t start, time_t connect, time_t end, unsigned int release)
{
	int res;
	enum OSPEFAILREASON reason;
	time_t alert = 0;
	unsigned isPddInfoPresent = 0;
	unsigned pdd = 0;
	unsigned int dummy = 0;
	int error;
	
	if (handle == OSP_INVALID_HANDLE) {
		return 0;
	}

	if (!recorded) {
		reason = asterisk2osp(cause);
		OSPPTransactionRecordFailure(handle, reason);
	}

	error = OSPPTransactionReportUsage(handle, difftime(end, connect), start, end, alert, connect, isPddInfoPresent, pdd,
						release, (unsigned char *) "", 0, 0, 0, 0, &dummy, NULL);
	if (error == OSPC_ERR_NO_ERROR) {
		ast_log(LOG_DEBUG, "OSP: Usage reported\n");
		res = 1;
	} else {
		ast_log(LOG_DEBUG, "OSP: Unable to report usage, error '%d'\n", error);
		res = -1;
	}
	OSPPTransactionDelete(handle);

	return res;
}

/* OSP Application APIs */

/*!
 * \brief OSP Application OSPAuth
 * \param chan Channel
 * \param data Parameter
 * \return 0 Success, -1 Failed
 */
static int ospauth_exec(struct ast_channel* chan, void* data)
{
	int res;
	struct ast_module_user *u;
	const char* provider = OSP_DEF_PROVIDER;
	int priority_jump = 0;
	struct varshead *headp;
	struct ast_var_t *current;
	const char *source = "";
	const char *token = "";
	int handle;
	unsigned int timelimit;
	char buffer[OSP_INTSTR_SIZE];
	const char *status;
	char *tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);

	u = ast_module_user_add(chan);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		ast_module_user_remove(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_log(LOG_DEBUG, "OSPAuth: provider '%s'\n", provider);

	if ((args.options) && (strchr(args.options, 'j'))) {
		priority_jump = 1;
	}
	ast_log(LOG_DEBUG, "OSPAuth: priority jump '%d'\n", priority_jump);

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPPEERIP")) {
			source = ast_var_value(current);
		} else if (!strcasecmp(ast_var_name(current), "OSPINTOKEN")) {
			token = ast_var_value(current);
		}
	}
	ast_log(LOG_DEBUG, "OSPAuth: source '%s'\n", source);
	ast_log(LOG_DEBUG, "OSPAuth: token size '%zd'\n", strlen(token));

	
	if ((res = osp_auth(provider, &handle, source, chan->cid.cid_num, chan->exten, token, &timelimit)) > 0) {
		status = AST_OSP_SUCCESS;
	} else {
		timelimit = OSP_DEF_TIMELIMIT;
		if (!res) {
			status = AST_OSP_FAILED;
		} else {
			status = AST_OSP_ERROR;
		}
	}

	snprintf(buffer, sizeof(buffer), "%d", handle);
	pbx_builtin_setvar_helper(chan, "OSPINHANDLE", buffer);
	ast_log(LOG_DEBUG, "OSPAuth: OSPINHANDLE '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", timelimit);
	pbx_builtin_setvar_helper(chan, "OSPINTIMELIMIT", buffer);
	ast_log(LOG_DEBUG, "OSPAuth: OSPINTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPAUTHSTATUS", status);
	ast_log(LOG_DEBUG, "OSPAuth: %s\n", status);

	if(res <= 0) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
			res = 0;
		} else {
			res = -1;
		}
	} else {
		res = 0;
	}

	ast_module_user_remove(u);

	return res;
}

/*!
 * \brief OSP Application OSPLookup
 * \param chan Channel
 * \param data Parameter
 * \return 0 Success, -1 Failed
 */
static int osplookup_exec(struct ast_channel* chan, void* data)
{
	int res, cres;
	struct ast_module_user *u;
	const char *provider = OSP_DEF_PROVIDER;
	int priority_jump = 0;
	struct varshead *headp;
	struct ast_var_t* current;
	const char *srcdev = "";
	char buffer[OSP_TOKSTR_SIZE];
	struct osp_result result;
	const char *status;
	char *tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(exten);
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPLookup: Arg required, OSPLookup(exten[|provider[|options]])\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		ast_module_user_remove(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	ast_log(LOG_DEBUG, "OSPLookup: exten '%s'\n", args.exten);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_log(LOG_DEBUG, "OSPlookup: provider '%s'\n", provider);

	if ((args.options) && (strchr(args.options, 'j'))) {
		priority_jump = 1;
	}
	ast_log(LOG_DEBUG, "OSPLookup: priority jump '%d'\n", priority_jump);

	result.inhandle = OSP_INVALID_HANDLE;
	result.intimelimit = OSP_DEF_TIMELIMIT;

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &result.inhandle) != 1) {
				result.inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPINTIMELIMIT")) {
			if (sscanf(ast_var_value(current), "%30d", &result.intimelimit) != 1) {
				result.intimelimit = OSP_DEF_TIMELIMIT;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPPEERIP")) {
			srcdev = ast_var_value(current);
		}
	}
	ast_log(LOG_DEBUG, "OSPLookup: OSPINHANDLE '%d'\n", result.inhandle);
	ast_log(LOG_DEBUG, "OSPLookup: OSPINTIMELIMIT '%d'\n", result.intimelimit);
	ast_log(LOG_DEBUG, "OSPLookup: source device '%s'\n", srcdev);
	
	if ((cres = ast_autoservice_start(chan)) < 0) {
		ast_module_user_remove(u);
		return -1;
	}

	if ((res = osp_lookup(provider, srcdev, chan->cid.cid_num, args.exten, &result)) > 0) {
		status = AST_OSP_SUCCESS;
	} else {
		result.tech[0] = '\0';
		result.dest[0] = '\0';
		result.calling[0] = '\0';
		result.token[0] = '\0'; 
		result.numresults = 0;
		result.outtimelimit = OSP_DEF_TIMELIMIT;
		if (!res) {
			status = AST_OSP_FAILED;
		} else {
			status = AST_OSP_ERROR;
		}
	}

	snprintf(buffer, sizeof(buffer), "%d", result.outhandle);
	pbx_builtin_setvar_helper(chan, "OSPOUTHANDLE", buffer);
	ast_log(LOG_DEBUG, "OSPLookup: OSPOUTHANDLE '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPTECH", result.tech);
	ast_log(LOG_DEBUG, "OSPLookup: OSPTECH '%s'\n", result.tech);
	pbx_builtin_setvar_helper(chan, "OSPDEST", result.dest);
	ast_log(LOG_DEBUG, "OSPLookup: OSPDEST '%s'\n", result.dest);
	pbx_builtin_setvar_helper(chan, "OSPCALLING", result.calling);
	ast_log(LOG_DEBUG, "OSPLookup: OSPCALLING '%s'\n", result.calling);
	pbx_builtin_setvar_helper(chan, "OSPOUTTOKEN", result.token);
	ast_log(LOG_DEBUG, "OSPLookup: OSPOUTTOKEN size '%zd'\n", strlen(result.token));
	snprintf(buffer, sizeof(buffer), "%d", result.numresults);
	pbx_builtin_setvar_helper(chan, "OSPRESULTS", buffer);
	ast_log(LOG_DEBUG, "OSPLookup: OSPRESULTS '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", result.outtimelimit);
	pbx_builtin_setvar_helper(chan, "OSPOUTTIMELIMIT", buffer);
	ast_log(LOG_DEBUG, "OSPLookup: OSPOUTTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPLOOKUPSTATUS", status);
	ast_log(LOG_DEBUG, "OSPLookup: %s\n", status);

	if (!strcasecmp(result.tech, "SIP")) {
		if (!ast_strlen_zero(result.token)) {
			snprintf(buffer, sizeof(buffer), "P-OSP-Auth-Token: %s", result.token);
			pbx_builtin_setvar_helper(chan, "_SIPADDHEADER", buffer);
			ast_log(LOG_DEBUG, "OSPLookup: SIPADDHEADER size '%zd'\n", strlen(buffer));
		}
	} else if (!strcasecmp(result.tech, "H323")) {
	} else if (!strcasecmp(result.tech, "IAX")) {
	}

	if ((cres = ast_autoservice_stop(chan)) < 0) {
		ast_module_user_remove(u);
		return -1;
	}

	if(res <= 0) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
			res = 0;
		} else {
			res = -1;
		}
	} else {
		res = 0;
	}

	ast_module_user_remove(u);

	return res;
}

/*!
 * \brief OSP Application OSPNext
 * \param chan Channel
 * \param data Parameter
 * \return 0 Success, -1 Failed
 */
static int ospnext_exec(struct ast_channel* chan, void* data)
{
	int res;
	struct ast_module_user *u;
	int priority_jump = 0;
	int cause = 0;
	struct varshead* headp;
	struct ast_var_t* current;
	struct osp_result result;
	char buffer[OSP_TOKSTR_SIZE];
	const char* status;
	char* tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cause);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPNext: Arg required, OSPNext(cause[|options])\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		ast_module_user_remove(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	if (!ast_strlen_zero(args.cause) && sscanf(args.cause, "%30d", &cause) != 1) {
		cause = 0;
	}
	ast_log(LOG_DEBUG, "OSPNext: cause '%d'\n", cause);

	if ((args.options) && (strchr(args.options, 'j'))) {
		priority_jump = 1;
	}
	ast_log(LOG_DEBUG, "OSPNext: priority jump '%d'\n", priority_jump);

	result.inhandle = OSP_INVALID_HANDLE;
	result.outhandle = OSP_INVALID_HANDLE;
	result.intimelimit = OSP_DEF_TIMELIMIT;
	result.numresults = 0;

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &result.inhandle) != 1) {
				result.inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPOUTHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &result.outhandle) != 1) {
				result.outhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPINTIMELIMIT")) {
			if (sscanf(ast_var_value(current), "%30d", &result.intimelimit) != 1) {
				result.intimelimit = OSP_DEF_TIMELIMIT;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPRESULTS")) {
			if (sscanf(ast_var_value(current), "%30d", &result.numresults) != 1) {
				result.numresults = 0;
			}
		}
	}
	ast_log(LOG_DEBUG, "OSPNext: OSPINHANDLE '%d'\n", result.inhandle);
	ast_log(LOG_DEBUG, "OSPNext: OSPOUTHANDLE '%d'\n", result.outhandle);
	ast_log(LOG_DEBUG, "OSPNext: OSPINTIMELIMIT '%d'\n", result.intimelimit);
	ast_log(LOG_DEBUG, "OSPNext: OSPRESULTS '%d'\n", result.numresults);

	if ((res = osp_next(cause, &result)) > 0) {
		status = AST_OSP_SUCCESS;
	} else {
		result.tech[0] = '\0';
		result.dest[0] = '\0';
		result.calling[0] = '\0';
		result.token[0] = '\0'; 
		result.numresults = 0;
		result.outtimelimit = OSP_DEF_TIMELIMIT;
		if (!res) {
			status = AST_OSP_FAILED;
		} else {
			status = AST_OSP_ERROR;
		}
	}

	pbx_builtin_setvar_helper(chan, "OSPTECH", result.tech);
	ast_log(LOG_DEBUG, "OSPNext: OSPTECH '%s'\n", result.tech);
	pbx_builtin_setvar_helper(chan, "OSPDEST", result.dest);
	ast_log(LOG_DEBUG, "OSPNext: OSPDEST '%s'\n", result.dest);
	pbx_builtin_setvar_helper(chan, "OSPCALLING", result.calling);
	ast_log(LOG_DEBUG, "OSPNext: OSPCALLING '%s'\n", result.calling);
	pbx_builtin_setvar_helper(chan, "OSPOUTTOKEN", result.token);
	ast_log(LOG_DEBUG, "OSPNext: OSPOUTTOKEN size '%zd'\n", strlen(result.token));
	snprintf(buffer, sizeof(buffer), "%d", result.numresults);
	pbx_builtin_setvar_helper(chan, "OSPRESULTS", buffer);
	ast_log(LOG_DEBUG, "OSPNext: OSPRESULTS '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", result.outtimelimit);
	pbx_builtin_setvar_helper(chan, "OSPOUTTIMELIMIT", buffer);
	ast_log(LOG_DEBUG, "OSPNext: OSPOUTTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPNEXTSTATUS", status);
	ast_log(LOG_DEBUG, "OSPNext: %s\n", status);

	if (!strcasecmp(result.tech, "SIP")) {
		if (!ast_strlen_zero(result.token)) {
			snprintf(buffer, sizeof(buffer), "P-OSP-Auth-Token: %s", result.token);
			pbx_builtin_setvar_helper(chan, "_SIPADDHEADER", buffer);
			ast_log(LOG_DEBUG, "OSPLookup: SIPADDHEADER size '%zd'\n", strlen(buffer));
		}
	} else if (!strcasecmp(result.tech, "H323")) {
	} else if (!strcasecmp(result.tech, "IAX")) {
	}

	if(res <= 0) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
			res = 0;
		} else {
			res = -1;
		}
	} else {
		res = 0;
	}

	ast_module_user_remove(u);

	return res;
}

/*!
 * \brief OSP Application OSPFinish
 * \param chan Channel
 * \param data Parameter
 * \return 0 Success, -1 Failed
 */
static int ospfinished_exec(struct ast_channel* chan, void* data)
{
	int res = 1;
	struct ast_module_user *u;
	int priority_jump = 0;
	int cause = 0;
	struct varshead *headp;
	struct ast_var_t *current;
	int inhandle = OSP_INVALID_HANDLE;
	int outhandle = OSP_INVALID_HANDLE;
	int recorded = 0;
	time_t start, connect, end;
	unsigned int release;
	char buffer[OSP_INTSTR_SIZE];
	const char *status;
	char *tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cause);
		AST_APP_ARG(options);
	);
	
	u = ast_module_user_add(chan);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		ast_module_user_remove(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	if ((args.options) && (strchr(args.options, 'j'))) {
		priority_jump = 1;
	}
	ast_log(LOG_DEBUG, "OSPFinish: priority jump '%d'\n", priority_jump);

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &inhandle) != 1) {
				inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPOUTHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &outhandle) != 1) {
				outhandle = OSP_INVALID_HANDLE;
			}
		} else if (!recorded &&
			(!strcasecmp(ast_var_name(current), "OSPAUTHSTATUS") ||
			!strcasecmp(ast_var_name(current), "OSPLOOKUPSTATUS") || 
			!strcasecmp(ast_var_name(current), "OSPNEXTSTATUS"))) 
		{
			if (strcasecmp(ast_var_value(current), AST_OSP_SUCCESS)) {
				recorded = 1;
			}
		}
	}
	ast_log(LOG_DEBUG, "OSPFinish: OSPINHANDLE '%d'\n", inhandle);
	ast_log(LOG_DEBUG, "OSPFinish: OSPOUTHANDLE '%d'\n", outhandle);
	ast_log(LOG_DEBUG, "OSPFinish: recorded '%d'\n", recorded);

	if (!ast_strlen_zero(args.cause) && sscanf(args.cause, "%30d", &cause) != 1) {
		cause = 0;
	}
	ast_log(LOG_DEBUG, "OSPFinish: cause '%d'\n", cause);

	if (chan->cdr) {
		start = chan->cdr->start.tv_sec;
		connect = chan->cdr->answer.tv_sec;
		if (connect) {
			end = time(NULL);
		} else {
			end = connect;
		}
	} else {
		start = 0;
		connect = 0;
		end = 0;
	}
	ast_log(LOG_DEBUG, "OSPFinish: start '%ld'\n", start);
	ast_log(LOG_DEBUG, "OSPFinish: connect '%ld'\n", connect);
	ast_log(LOG_DEBUG, "OSPFinish: end '%ld'\n", end);

	release = chan->_softhangup ? 0 : 1;

	if (osp_finish(outhandle, recorded, cause, start, connect, end, release) <= 0) {
		ast_log(LOG_DEBUG, "OSPFinish: Unable to report usage for outbound call\n");
	}
	switch (cause) {
		case AST_CAUSE_NORMAL_CLEARING:
			break;
		default:
			cause = AST_CAUSE_NO_ROUTE_DESTINATION;
			break;
	}
	if (osp_finish(inhandle, recorded, cause, start, connect, end, release) <= 0) {
		ast_log(LOG_DEBUG, "OSPFinish: Unable to report usage for inbound call\n");
	}
	snprintf(buffer, sizeof(buffer), "%d", OSP_INVALID_HANDLE);
	pbx_builtin_setvar_helper(chan, "OSPOUTHANDLE", buffer);
	pbx_builtin_setvar_helper(chan, "OSPINHANDLE", buffer);

	if (res > 0) {
		status = AST_OSP_SUCCESS;
	} else if (!res) {
		status = AST_OSP_FAILED;
	} else {
		status = AST_OSP_ERROR;
	}
	pbx_builtin_setvar_helper(chan, "OSPFINISHSTATUS", status);

	if(!res) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
			res = 0;
		} else {
			res = -1;
		}
	} else {
		res = 0;
	}

	ast_module_user_remove(u);

	return res;
}

/* OSP Module APIs */

static int osp_load(void)
{
	const char* t;
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
			if ((sscanf(t, "%30d", &v) == 1) && 
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
				osp_create_provider(cfg, t);
			}
			t = ast_category_browse(cfg, t);
		}

		osp_initialized = 1;

		ast_config_destroy(cfg);
	} else {
		ast_log(LOG_WARNING, "OSP: Unable to find configuration. OSP support disabled\n");
		return 0;
	}
	ast_log(LOG_DEBUG, "OSP: osp_initialized '%d'\n", osp_initialized);

	return 1;
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

		osp_tokenformat = TOKEN_ALGO_SIGNED;
		osp_hardware = 0;
		osp_initialized = 0;
	}
	return 0;
}

static int osp_show(int fd, int argc, char* argv[])
{
	int i;
	int found = 0;
	struct osp_provider* p;
	const char* provider = NULL;
	const char* tokenalgo;

	if ((argc < 2) || (argc > 3)) {
		return RESULT_SHOWUSAGE;
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
	return RESULT_SUCCESS;
}

static const char* app1= "OSPAuth";
static const char* synopsis1 = "OSP authentication";
static const char* descrip1 = 
"  OSPAuth([provider[|options]]):  Authenticate a SIP INVITE by OSP and sets\n"
"the variables:\n"
" ${OSPINHANDLE}:  The inbound call transaction handle\n"
" ${OSPINTIMELIMIT}:  The inbound call duration limit in seconds\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the authentication was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPAUTHSTATUS	The status of the OSP Auth attempt as a text string, one of\n"
"		SUCCESS | FAILED | ERROR\n";

static const char* app2= "OSPLookup";
static const char* synopsis2 = "Lookup destination by OSP";
static const char* descrip2 = 
"  OSPLookup(exten[|provider[|options]]):  Looks up an extension via OSP and sets\n"
"the variables, where 'n' is the number of the result beginning with 1:\n"
" ${OSPOUTHANDLE}:  The OSP Handle for anything remaining\n"
" ${OSPTECH}:  The technology to use for the call\n"
" ${OSPDEST}:  The destination to use for the call\n"
" ${OSPCALLING}:  The calling number to use for the call\n"
" ${OSPOUTTOKEN}:  The actual OSP token as a string\n"
" ${OSPOUTTIMELIMIT}:  The outbound call duration limit in seconds\n"
" ${OSPRESULTS}:  The number of OSP results total remaining\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the lookup was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPLOOKUPSTATUS	The status of the OSP Lookup attempt as a text string, one of\n"
"		SUCCESS | FAILED | ERROR\n";

static const char* app3 = "OSPNext";
static const char* synopsis3 = "Lookup next destination by OSP";
static const char* descrip3 = 
"  OSPNext(cause[|options]):  Looks up the next OSP Destination for ${OSPOUTHANDLE}\n"
"See OSPLookup for more information\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the lookup was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPNEXTSTATUS	The status of the OSP Next attempt as a text string, one of\n"
"		SUCCESS | FAILED |ERROR\n";

static const char* app4 = "OSPFinish";
static const char* synopsis4 = "Record OSP entry";
static const char* descrip4 = 
"  OSPFinish([status[|options]]):  Records call state for ${OSPINHANDLE}, according to\n"
"status, which should be one of BUSY, CONGESTION, ANSWER, NOANSWER, or CHANUNAVAIL\n"
"or coincidentally, just what the Dial application stores in its ${DIALSTATUS}.\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the finish attempt was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPFINISHSTATUS	The status of the OSP Finish attempt as a text string, one of\n"
"		SUCCESS | FAILED |ERROR \n";

static const char osp_usage[] =
"Usage: osp show\n"
"       Displays information on Open Settlement Protocol support\n";

static struct ast_cli_entry cli_osp[] = {
	{ { "osp", "show", NULL},
	osp_show, "Displays OSP information",
	osp_usage },
};

static int load_module(void)
{
	int res;
	
	if(!osp_load())
		return AST_MODULE_LOAD_DECLINE;

	ast_cli_register_multiple(cli_osp, sizeof(cli_osp) / sizeof(struct ast_cli_entry));
	res = ast_register_application(app1, ospauth_exec, synopsis1, descrip1);
	res |= ast_register_application(app2, osplookup_exec, synopsis2, descrip2);
	res |= ast_register_application(app3, ospnext_exec, synopsis3, descrip3);
	res |= ast_register_application(app4, ospfinished_exec, synopsis4, descrip4);

	return res;
}

static int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(app4);
	res |= ast_unregister_application(app3);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app1);
	ast_cli_unregister_multiple(cli_osp, sizeof(cli_osp) / sizeof(struct ast_cli_entry));
	osp_unload();

	ast_module_user_hangup_all();

	return res;
}

static int reload(void)
{
	osp_unload();
	osp_load();

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Open Settlement Protocol Applications",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		);
