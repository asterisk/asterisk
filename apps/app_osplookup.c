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
 * \extref The OSP Toolkit: http://www.transnexus.com
 * \extref OpenSSL http://www.openssl.org
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>osptk</depend>
	<depend>openssl</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <osp/osp.h>
#include <osp/osputils.h>
#include <osp/ospb64.h>

#include "asterisk/paths.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/astosp.h"

/*** DOCUMENTATION
	<application name="OSPAuth" language="en_US">
		<synopsis>
			OSP Authentication.
		</synopsis>
		<syntax>
			<parameter name="provider">
				<para>The name of the provider that authenticates the call.</para>
			</parameter>
			<parameter name="options">
				<para>Reserverd.</para>
			</parameter>
		</syntax>
		<description>
			<para>Authenticate a call by OSP.</para>
			<para>Input variables:</para>
			<variablelist>
				<variable name="OSPINPEERIP">
					<para>The last hop IP address.</para>
				</variable>
				<variable name="OSPINTOKEN">
					<para>The inbound OSP token.</para>
				</variable>
			</variablelist>
			<para>Output variables:</para>
			<variablelist>
				<variable name="OSPINHANDLE">
					<para>The inbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPINTIMELIMIT">
					<para>The inbound call duration limit in seconds.</para>
				</variable>
			</variablelist>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="OSPAUTHSTATUS">
					<para>The status of OSPAuth attempt as a text string, one of</para>
					<value name="SUCCESS" />
					<value name="FAILED" />
					<value name="ERROR" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">OSPLookup</ref>
			<ref type="application">OSPNext</ref>
			<ref type="application">OSPFinish</ref>
		</see-also>
	</application>
	<application name="OSPLookup" language="en_US">
		<synopsis>
			Lookup destination by OSP.
		</synopsis>
		<syntax>
			<parameter name="exten" required="true">
				<para>The exten of the call.</para>
			</parameter>
			<parameter name="provider">
				<para>The name of the provider that is used to route the call.</para>
			</parameter>
			<parameter name="options">
				<enumlist>
					<enum name="h">
						<para>generate H323 call id for the outbound call</para>
					</enum>
					<enum name="s">
						<para>generate SIP call id for the outbound call. Have not been implemented</para>
					</enum>
					<enum name="i">
						<para>generate IAX call id for the outbound call. Have not been implemented</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Looks up destination via OSP.</para>
			<para>Input variables:</para>
			<variablelist>
				<variable name="OSPINACTUALSRC">
					<para>The actual source device IP address in indirect mode.</para>
				</variable>
				<variable name="OSPINPEERIP">
					<para>The last hop IP address.</para>
				</variable>
				<variable name="OSPINTECH">
					<para>The inbound channel technology for the call.</para>
				</variable>
				<variable name="OSPINHANDLE">
					<para>The inbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPINTIMELIMIT">
					<para>The inbound call duration limit in seconds.</para>
				</variable>
				<variable name="OSPINNETWORKID">
					<para>The inbound source network ID.</para>
				</variable>
				<variable name="OSPINNPRN">
					<para>The inbound routing number.</para>
				</variable>
				<variable name="OSPINNPCIC">
					<para>The inbound carrier identification code.</para>
				</variable>
				<variable name="OSPINNPDI">
					<para>The inbound number portability database dip indicator.</para>
				</variable>
				<variable name="OSPINSPID">
					<para>The inbound service provider identity.</para>
				</variable>
				<variable name="OSPINOCN">
					<para>The inbound operator company number.</para>
				</variable>
				<variable name="OSPINSPN">
					<para>The inbound service provider name.</para>
				</variable>
				<variable name="OSPINALTSPN">
					<para>The inbound alternate service provider name.</para>
				</variable>
				<variable name="OSPINMCC">
					<para>The inbound mobile country code.</para>
				</variable>
				<variable name="OSPINMNC">
					<para>The inbound mobile network code.</para>
				</variable>
				<variable name="OSPINTOHOST">
					<para>The inbound To header host part.</para>
				</variable>
				<variable name="OSPINRPIDUSER">
					<para>The inbound Remote-Party-ID header user part.</para>
				</variable>
				<variable name="OSPINPAIUSER">
					<para>The inbound P-Asserted-Identify header user part.</para>
				</variable>
				<variable name="OSPINDIVUSER">
					<para>The inbound Diversion header user part.</para>
				</variable>
				<variable name="OSPINDIVHOST">
					<para>The inbound Diversion header host part.</para>
				</variable>
				<variable name="OSPINPCIUSER">
					<para>The inbound P-Charge-Info header user part.</para>
				</variable>
				<variable name="OSPINCUSTOMINFOn">
					<para>The inbound custom information, where <literal>n</literal> is the index beginning with <literal>1</literal>
					upto <literal>8</literal>.</para>
				</variable>
			</variablelist>
			<para>Output variables:</para>
			<variablelist>
				<variable name="OSPOUTHANDLE">
					<para>The outbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPOUTTECH">
					<para>The outbound channel technology for the call.</para>
				</variable>
				<variable name="OSPDESTINATION">
					<para>The outbound destination IP address.</para>
				</variable>
				<variable name="OSPOUTCALLING">
					<para>The outbound calling number.</para>
				</variable>
				<variable name="OSPOUTCALLED">
					<para>The outbound called number.</para>
				</variable>
				<variable name="OSPOUTNETWORKID">
					<para>The outbound destination network ID.</para>
				</variable>
				<variable name="OSPOUTNPRN">
					<para>The outbound routing number.</para>
				</variable>
				<variable name="OSPOUTNPCIC">
					<para>The outbound carrier identification code.</para>
				</variable>
				<variable name="OSPOUTNPDI">
					<para>The outbound number portability database dip indicator.</para>
				</variable>
				<variable name="OSPOUTSPID">
					<para>The outbound service provider identity.</para>
				</variable>
				<variable name="OSPOUTOCN">
					<para>The outbound operator company number.</para>
				</variable>
				<variable name="OSPOUTSPN">
					<para>The outbound service provider name.</para>
				</variable>
				<variable name="OSPOUTALTSPN">
					<para>The outbound alternate service provider name.</para>
				</variable>
				<variable name="OSPOUTMCC">
					<para>The outbound mobile country code.</para>
				</variable>
				<variable name="OSPOUTMNC">
					<para>The outbound mobile network code.</para>
				</variable>
				<variable name="OSPOUTTOKEN">
					<para>The outbound OSP token.</para>
				</variable>
				<variable name="OSPDESTREMAILS">
					<para>The number of remained destinations.</para>
				</variable>
				<variable name="OSPOUTTIMELIMIT">
					<para>The outbound call duration limit in seconds.</para>
				</variable>
				<variable name="OSPOUTCALLIDTYPES">
					<para>The outbound Call-ID types.</para>
				</variable>
				<variable name="OSPOUTCALLID">
					<para>The outbound Call-ID. Only for H.323.</para>
				</variable>
				<variable name="OSPDIALSTR">
					<para>The outbound Dial command string.</para>
				</variable>
			</variablelist>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="OSPLOOKUPSTATUS">
					<para>The status of OSPLookup attempt as a text string, one of</para>
					<value name="SUCCESS" />
					<value name="FAILED" />
					<value name="ERROR" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">OSPAuth</ref>
			<ref type="application">OSPNext</ref>
			<ref type="application">OSPFinish</ref>
		</see-also>
	</application>
	<application name="OSPNext" language="en_US">
		<synopsis>
			Lookup next destination by OSP.
		</synopsis>
		<description>
			<para>Looks up the next destination via OSP.</para>
			<para>Input variables:</para>
			<variablelist>
				<variable name="OSPINHANDLE">
					<para>The inbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPOUTHANDLE">
					<para>The outbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPINTIMELIMIT">
					<para>The inbound call duration limit in seconds.</para>
				</variable>
				<variable name="OSPOUTCALLIDTYPES">
					<para>The outbound Call-ID types.</para>
				</variable>
				<variable name="OSPDESTREMAILS">
					<para>The number of remained destinations.</para>
				</variable>
			</variablelist>
			<para>Output variables:</para>
			<variablelist>
				<variable name="OSPOUTTECH">
					<para>The outbound channel technology.</para>
				</variable>
				<variable name="OSPDESTINATION">
					<para>The destination IP address.</para>
				</variable>
				<variable name="OSPOUTCALLING">
					<para>The outbound calling number.</para>
				</variable>
				<variable name="OSPOUTCALLED">
					<para>The outbound called number.</para>
				</variable>
				<variable name="OSPOUTNETWORKID">
					<para>The outbound destination network ID.</para>
				</variable>
				<variable name="OSPOUTNPRN">
					<para>The outbound routing number.</para>
				</variable>
				<variable name="OSPOUTNPCIC">
					<para>The outbound carrier identification code.</para>
				</variable>
				<variable name="OSPOUTNPDI">
					<para>The outbound number portability database dip indicator.</para>
				</variable>
				<variable name="OSPOUTSPID">
					<para>The outbound service provider identity.</para>
				</variable>
				<variable name="OSPOUTOCN">
					<para>The outbound operator company number.</para>
				</variable>
				<variable name="OSPOUTSPN">
					<para>The outbound service provider name.</para>
				</variable>
				<variable name="OSPOUTALTSPN">
					<para>The outbound alternate service provider name.</para>
				</variable>
				<variable name="OSPOUTMCC">
					<para>The outbound mobile country code.</para>
				</variable>
				<variable name="OSPOUTMNC">
					<para>The outbound mobile network code.</para>
				</variable>
				<variable name="OSPOUTTOKEN">
					<para>The outbound OSP token.</para>
				</variable>
				<variable name="OSPDESTREMAILS">
					<para>The number of remained destinations.</para>
				</variable>
				<variable name="OSPOUTTIMELIMIT">
					<para>The outbound call duration limit in seconds.</para>
				</variable>
				<variable name="OSPOUTCALLID">
					<para>The outbound Call-ID. Only for H.323.</para>
				</variable>
				<variable name="OSPDIALSTR">
					<para>The outbound Dial command string.</para>
				</variable>
			</variablelist>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="OSPNEXTSTATUS">
					<para>The status of the OSPNext attempt as a text string, one of</para>
					<value name="SUCCESS" />
					<value name="FAILED" />
					<value name="ERROR" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">OSPAuth</ref>
			<ref type="application">OSPLookup</ref>
			<ref type="application">OSPFinish</ref>
		</see-also>
	</application>
	<application name="OSPFinish" language="en_US">
		<synopsis>
			Report OSP entry.
		</synopsis>
		<syntax>
			<parameter name="cause">
				<para>Hangup cause.</para>
			</parameter>
			<parameter name="options">
				<para>Reserved.</para>
			</parameter>
		</syntax>
		<description>
			<para>Report call state.</para>
			<para>Input variables:</para>
			<variablelist>
				<variable name="OSPINHANDLE">
					<para>The inbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPOUTHANDLE">
					<para>The outbound call OSP transaction handle.</para>
				</variable>
				<variable name="OSPAUTHSTATUS">
					<para>The OSPAuth status.</para>
				</variable>
				<variable name="OSPLOOKUPSTATUS">
					<para>The OSPLookup status.</para>
				</variable>
				<variable name="OSPNEXTSTATUS">
					<para>The OSPNext status.</para>
				</variable>
				<variable name="OSPINAUDIOQOS">
					<para>The inbound call leg audio QoS string.</para>
				</variable>
				<variable name="OSPOUTAUDIOQOS">
					<para>The outbound call leg audio QoS string.</para>
				</variable>
			</variablelist>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="OSPFINISHSTATUS">
					<para>The status of the OSPFinish attempt as a text string, one of</para>
					<value name="SUCCESS" />
					<value name="FAILED" />
					<value name="ERROR" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">OSPAuth</ref>
			<ref type="application">OSPLookup</ref>
			<ref type="application">OSPNext</ref>
		</see-also>
	</application>
 ***/

/* OSP Buffer Sizes */
#define OSP_SIZE_INTSTR		((unsigned int)16)			/* OSP signed/unsigned int string buffer size */
#define OSP_SIZE_NORSTR		((unsigned int)256)			/* OSP normal string buffer size */
#define OSP_SIZE_KEYSTR		((unsigned int)1024)		/* OSP certificate string buffer size */
#define OSP_SIZE_TOKSTR		((unsigned int)4096)		/* OSP token string buffer size */
#define OSP_SIZE_TECHSTR	((unsigned int)32)			/* OSP signed/unsigned int string buffer size */
#define OSP_SIZE_UUID		((unsigned int)16)			/* UUID size */
#define OSP_SIZE_UUIDSTR	((unsigned int)36)			/* UUID string size */
#define OSP_SIZE_QOSSTR		((unsigned int)1024)		/* QoS string buffer size */

/* Call ID Type*/
#define OSP_CALLID_UNDEF	((unsigned int)0)			/* Undefined */
#define OSP_CALLID_SIP		((unsigned int)(1 << 0))	/* SIP */
#define OSP_CALLID_H323		((unsigned int)(1 << 1))	/* H.323 */
#define OSP_CALLID_IAX		((unsigned int)(1 << 2))	/* IAX2 */
#define OSP_CALLID_MAXNUM	((unsigned int)3)			/* Max number of call ID types */

/* OSP Supported Destination Protocols */
#define OSP_PROT_SIP		((const char*)"SIP")		/* SIP protocol name */
#define OSP_PROT_H323		((const char*)"H323")		/* H.323 Q.931 protocol name*/
#define OSP_PROT_IAX		((const char*)"IAX")		/* IAX2 protocol name */
#define OSP_PROT_SKYPE		((const char*)"SKYPE")		/* Skype protocol name */

/* OSP supported Destination Tech */
#define OSP_TECH_SIP		((const char*)"SIP")		/* SIP tech name */
#define OSP_TECH_H323		((const char*)"H323")		/* OH323 tech name */
#define OSP_TECH_IAX		((const char*)"IAX2")		/* IAX2 tech name */
#define OSP_TECH_SKYPE		((const char*)"SKYPE")		/* Skype tech name */

/* SIP OSP header field name */
#define OSP_SIP_HEADER		((const char*)"P-OSP-Auth-Token")

/* OSP Authentication Policy */
enum osp_authpolicy {
	OSP_AUTH_NO = 0,	/* Accept any call */
	OSP_AUTH_YES,		/* Accept call with valid OSP token or without OSP token */
	OSP_AUTH_EXC		/* Only accept call with valid OSP token */
};

/* OSP Work Mode */
enum osp_workmode {
	OSP_MODE_DIRECT= 0,	/* Direct */
	OSP_MODE_INDIRECT	/* Indirect */
};

/* OSP Service Type */
enum osp_srvtype {
	OSP_SRV_VOICE = 0,	/* Normal voice service */
	OSP_SRV_NPQUERY		/* Ported number query service */
};

/* OSP Constants */
#define OSP_OK					((int)1)					/* OSP function call successful */
#define OSP_FAILED				((int)0)					/* OSP function call failed */
#define OSP_ERROR				((int)-1)					/* OSP function call error */
#define OSP_AST_OK				((int)0)					/* Asterisk function call successful */
#define OSP_AST_ERROR			((int)-1)					/* Asterisk function call error */
#define OSP_INVALID_HANDLE		((int)-1)					/* Invalid OSP handle, provider, transaction etc. */
#define OSP_CONFIG_FILE			((const char*)"osp.conf")	/* OSP configuration file name */
#define OSP_GENERAL_CAT			((const char*)"general")	/* OSP global configuration context name */
#define OSP_DEF_PROVIDER		((const char*)"default")	/* OSP default provider context name */
#define OSP_MAX_CERTS			((unsigned int)10)			/* OSP max number of cacerts */
#define OSP_MAX_SPOINTS			((unsigned int)10)			/* OSP max number of service points */
#define OSP_DEF_MAXCONNECT		((unsigned int)20)			/* OSP default max_connections */
#define OSP_MIN_MAXCONNECT		((unsigned int)1)			/* OSP min max_connections */
#define OSP_MAX_MAXCONNECT		((unsigned int)1000)		/* OSP max max_connections */
#define OSP_DEF_RETRYDELAY		((unsigned int)0)			/* OSP default retry delay */
#define OSP_MIN_RETRYDELAY		((unsigned int)0)			/* OSP min retry delay */
#define OSP_MAX_RETRYDELAY		((unsigned int)10)			/* OSP max retry delay */
#define OSP_DEF_RETRYLIMIT		((unsigned int)2)			/* OSP default retry times */
#define OSP_MIN_RETRYLIMIT		((unsigned int)0)			/* OSP min retry times */
#define OSP_MAX_RETRYLIMIT		((unsigned int)100)			/* OSP max retry times */
#define OSP_DEF_TIMEOUT			((unsigned int)500)			/* OSP default timeout in ms */
#define OSP_MIN_TIMEOUT			((unsigned int)200)			/* OSP min timeout in ms */
#define OSP_MAX_TIMEOUT			((unsigned int)10000)		/* OSP max timeout in ms */
#define OSP_DEF_AUTHPOLICY		OSP_AUTH_YES				/* OSP default auth policy, yes */
#define OSP_AUDIT_URL			((const char*)"localhost")	/* OSP default Audit URL */
#define OSP_LOCAL_VALIDATION	((int)1)					/* Validate OSP token locally */
#define OSP_SSL_LIFETIME		((unsigned int)300)			/* SSL life time, in seconds */
#define OSP_HTTP_PERSISTENCE	((int)1)					/* In seconds */
#define OSP_CUSTOMER_ID			((const char*)"")			/* OSP customer ID */
#define OSP_DEVICE_ID			((const char*)"")			/* OSP device ID */
#define OSP_DEF_MAXDESTS		((unsigned int)12)			/* OSP default max number of destinations */
#define OSP_DEF_TIMELIMIT		((unsigned int)0)			/* OSP default duration limit, no limit */
#define OSP_DEF_PROTOCOL		OSP_PROT_SIP				/* OSP default signaling protocol, SIP */
#define OSP_DEF_WORKMODE		OSP_MODE_DIRECT				/* OSP default work mode, direct */
#define OSP_DEF_SRVTYPE			OSP_SRV_VOICE				/* OSP default service type, voice */
#define OSP_MAX_CUSTOMINFO		((unsigned int)8)			/* OSP max number of custom info */
#define OSP_DEF_INTSTATS		((int)-1)					/* OSP default int statistic */
#define OSP_DEF_FLOATSTATS		((float)-1)					/* OSP default float statistic */

/* OSP Provider */
struct osp_provider {
	OSPTPROVHANDLE handle;							/* OSP provider handle */
	char name[OSP_SIZE_NORSTR];						/* OSP provider context name */
	char privatekey[OSP_SIZE_NORSTR];				/* OSP private key file name */
	char localcert[OSP_SIZE_NORSTR];				/* OSP local cert file name */
	unsigned int canum;								/* Number of cacerts */
	char cacerts[OSP_MAX_CERTS][OSP_SIZE_NORSTR];	/* Cacert file names */
	unsigned int spnum;								/* Number of service points */
	char spoints[OSP_MAX_SPOINTS][OSP_SIZE_NORSTR];	/* Service point URLs */
	unsigned int maxconnect;						/* Max number of connections */
	unsigned int retrydelay;						/* Retry delay */
	unsigned int retrylimit;						/* Retry limit */
	unsigned int timeout;							/* Timeout in ms */
	char source[OSP_SIZE_NORSTR];					/* IP of self */
	enum osp_authpolicy authpolicy;					/* OSP authentication policy */
	const char* defprotocol;						/* OSP default signaling protocol */
	enum osp_workmode workmode;						/* OSP work mode */
	enum osp_srvtype srvtype;						/* OSP service type */
	struct osp_provider* next;						/* Pointer to next OSP provider */
};

/* Call ID */
struct osp_callid {
	unsigned char buf[OSP_SIZE_NORSTR];		/* Call ID string */
	unsigned int len;						/* Call ID length */
};

/* Number Portability Data */
struct osp_npdata {
	const char* rn;							/* Rounding Number */
	const char* cic;						/* Carrier Identification Code */
	int npdi;								/* NP Database Dip Indicator */
	const char* opname[OSPC_OPNAME_NUMBER];	/* Operator Names */
};

/* SIP Header Parameters */
struct osp_headers {
	const char* rpiduser;					/* Remote-Party-ID header user info */
	const char* paiuser;					/* P-Asserted-Identity header user info */
	const char* divuser;					/* Diversion header user info */
	const char* divhost;					/* Diversion header host info */
	const char* pciuser;					/* P-Charge-Info header user info */
};

/* OSP Application In/Output Results */
struct osp_results {
	int inhandle;										/* Inbound transaction handle */
	int outhandle;										/* Outbound transaction handle */
	unsigned int intimelimit;							/* Inbound duration limit */
	unsigned int outtimelimit;							/* Outbound duration limit */
	char intech[OSP_SIZE_TECHSTR];						/* Inbound Asterisk TECH string */
	char outtech[OSP_SIZE_TECHSTR];						/* Outbound Asterisk TECH string */
	char dest[OSP_SIZE_NORSTR];							/* Outbound destination IP address */
	char calling[OSP_SIZE_NORSTR];						/* Outbound calling number, may be translated */
	char called[OSP_SIZE_NORSTR];						/* Outbound called number, may be translated */
	char token[OSP_SIZE_TOKSTR];						/* Outbound OSP token */
	char networkid[OSP_SIZE_NORSTR];					/* Outbound network ID */
	char nprn[OSP_SIZE_NORSTR];							/* Outbound NP routing number */
	char npcic[OSP_SIZE_NORSTR];						/* Outbound NP carrier identification code */
	int npdi;											/* Outbound NP database dip indicator */
	char opname[OSPC_OPNAME_NUMBER][OSP_SIZE_NORSTR];	/* Outbound Operator names */
	unsigned int numdests;								/* Number of remain outbound destinations */
	struct osp_callid outcallid;						/* Outbound call ID */
};

/* OSP Call Leg */
enum osp_callleg {
	OSP_CALL_INBOUND,	/* Inbound call leg */
	OSP_CALL_OUTBOUND	/* Outbound call leg */
};

/* OSP Media Stream Direction */
enum osp_direction {
	OSP_DIR_RX = 0,		/* Receive */
	OSP_DIR_TX,			/* Send */
	OSP_DIR_NUMBER		/* Number of directions */
};

/* OSP Metrics */
struct osp_metrics {
	int value;			/* Value */
	float min;			/* Minimum */
	float max;			/* Maximum */
	float avg;			/* Average */
	float sdev;			/* Standard deviation */
};

/* OSP Module Global Variables */
AST_MUTEX_DEFINE_STATIC(osp_lock);							/* Lock of OSP provider list */
static int osp_initialized = 0;								/* Init flag */
static int osp_hardware = 0;								/* Hardware acceleration flag */
static int osp_security = 0;								/* Using security features flag */
static struct osp_provider* osp_providers = NULL;			/* OSP provider list */
static unsigned int osp_tokenformat = TOKEN_ALGO_SIGNED;	/* Token format supported */

/* OSP default certificates */
const char* B64PKey = "MIIBOgIBAAJBAK8t5l+PUbTC4lvwlNxV5lpl+2dwSZGW46dowTe6y133XyVEwNiiRma2YNk3xKs/TJ3Wl9Wpns2SYEAJsFfSTukCAwEAAQJAPz13vCm2GmZ8Zyp74usTxLCqSJZNyMRLHQWBM0g44Iuy4wE3vpi7Wq+xYuSOH2mu4OddnxswCP4QhaXVQavTAQIhAOBVCKXtppEw9UaOBL4vW0Ed/6EA/1D8hDW6St0h7EXJAiEAx+iRmZKhJD6VT84dtX5ZYNVk3j3dAcIOovpzUj9a0CECIEduTCapmZQ5xqAEsLXuVlxRtQgLTUD4ZxDElPn8x0MhAiBE2HlcND0+qDbvtwJQQOUzDgqg5xk3w8capboVdzAlQQIhAMC+lDL7+gDYkNAft5Mu+NObJmQs4Cr+DkDFsKqoxqrm";
const char* B64LCert = "MIIBeTCCASMCEHqkOHVRRWr+1COq3CR/xsowDQYJKoZIhvcNAQEEBQAwOzElMCMGA1UEAxMcb3NwdGVzdHNlcnZlci50cmFuc25leHVzLmNvbTESMBAGA1UEChMJT1NQU2VydmVyMB4XDTA1MDYyMzAwMjkxOFoXDTA2MDYyNDAwMjkxOFowRTELMAkGA1UEBhMCQVUxEzARBgNVBAgTClNvbWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDBcMA0GCSqGSIb3DQEBAQUAA0sAMEgCQQCvLeZfj1G0wuJb8JTcVeZaZftncEmRluOnaME3ustd918lRMDYokZmtmDZN8SrP0yd1pfVqZ7NkmBACbBX0k7pAgMBAAEwDQYJKoZIhvcNAQEEBQADQQDnV8QNFVVJx/+7IselU0wsepqMurivXZzuxOmTEmTVDzCJx1xhA8jd3vGAj7XDIYiPub1PV23eY5a2ARJuw5w9";
const char* B64CACert = "MIIBYDCCAQoCAQEwDQYJKoZIhvcNAQEEBQAwOzElMCMGA1UEAxMcb3NwdGVzdHNlcnZlci50cmFuc25leHVzLmNvbTESMBAGA1UEChMJT1NQU2VydmVyMB4XDTAyMDIwNDE4MjU1MloXDTEyMDIwMzE4MjU1MlowOzElMCMGA1UEAxMcb3NwdGVzdHNlcnZlci50cmFuc25leHVzLmNvbTESMBAGA1UEChMJT1NQU2VydmVyMFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAPGeGwV41EIhX0jEDFLRXQhDEr50OUQPq+f55VwQd0TQNts06BP29+UiNdRW3c3IRHdZcJdC1Cg68ME9cgeq0h8CAwEAATANBgkqhkiG9w0BAQQFAANBAGkzBSj1EnnmUxbaiG1N4xjIuLAWydun7o3bFk2tV8dBIhnuh445obYyk1EnQ27kI7eACCILBZqi2MHDOIMnoN0=";

/* OSP Client Wrapper APIs */

/*!
 * \brief Create OSP provider handle according to configuration
 * \param cfg OSP configuration
 * \param name OSP provider context name
 * \return OSP_OK Success, OSP_FAILED Failed, OSP_ERROR Error
 */
static int osp_create_provider(
	struct ast_config* cfg,
	const char* name)
{
	int res = OSP_FAILED;
	struct ast_variable* var;
	struct osp_provider* provider;
	OSPTPRIVATEKEY privatekey;
	OSPT_CERT localcert;
	OSPT_CERT cacerts[OSP_MAX_CERTS];
	const OSPT_CERT* pcacerts[OSP_MAX_CERTS];
	const char* pspoints[OSP_MAX_SPOINTS];
	unsigned char privatekeydata[OSP_SIZE_KEYSTR];
	unsigned char localcertdata[OSP_SIZE_KEYSTR];
	unsigned char cacertdata[OSP_SIZE_KEYSTR];
	int i, num, error = OSPC_ERR_NO_ERROR;

	if (!(provider = ast_calloc(1, sizeof(*provider)))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return OSP_ERROR;
	}

	/* ast_calloc has set 0 in provider */
	provider->handle = OSP_INVALID_HANDLE;
	ast_copy_string(provider->name, name, sizeof(provider->name));
	snprintf(provider->privatekey, sizeof(provider->privatekey), "%s/%s-privatekey.pem", ast_config_AST_KEY_DIR, name);
	snprintf(provider->localcert, sizeof(provider->localcert), "%s/%s-localcert.pem", ast_config_AST_KEY_DIR, name);
	snprintf(provider->cacerts[0], sizeof(provider->cacerts[0]), "%s/%s-cacert_0.pem", ast_config_AST_KEY_DIR, name);
	provider->maxconnect = OSP_DEF_MAXCONNECT;
	provider->retrydelay = OSP_DEF_RETRYDELAY;
	provider->retrylimit = OSP_DEF_RETRYLIMIT;
	provider->timeout = OSP_DEF_TIMEOUT;
	provider->authpolicy = OSP_DEF_AUTHPOLICY;
	provider->defprotocol = OSP_DEF_PROTOCOL;
	provider->workmode = OSP_DEF_WORKMODE;
	provider->srvtype = OSP_DEF_SRVTYPE;

	for (var = ast_variable_browse(cfg, name); var != NULL; var = var->next) {
		if (!strcasecmp(var->name, "privatekey")) {
			if (osp_security) {
				if (var->value[0] == '/') {
					ast_copy_string(provider->privatekey, var->value, sizeof(provider->privatekey));
				} else {
					snprintf(provider->privatekey, sizeof(provider->privatekey), "%s/%s", ast_config_AST_KEY_DIR, var->value);
				}
				ast_debug(1, "OSP: privatekey '%s'\n", provider->privatekey);
			}
		} else if (!strcasecmp(var->name, "localcert")) {
			if (osp_security) {
				if (var->value[0] == '/') {
					ast_copy_string(provider->localcert, var->value, sizeof(provider->localcert));
				} else {
					snprintf(provider->localcert, sizeof(provider->localcert), "%s/%s", ast_config_AST_KEY_DIR, var->value);
				}
				ast_debug(1, "OSP: localcert '%s'\n", provider->localcert);
			}
		} else if (!strcasecmp(var->name, "cacert")) {
			if (osp_security) {
				if (provider->canum < OSP_MAX_CERTS) {
					if (var->value[0] == '/') {
						ast_copy_string(provider->cacerts[provider->canum], var->value, sizeof(provider->cacerts[provider->canum]));
					} else {
						snprintf(provider->cacerts[provider->canum], sizeof(provider->cacerts[provider->canum]), "%s/%s", ast_config_AST_KEY_DIR, var->value);
					}
					ast_debug(1, "OSP: cacerts[%d]: '%s'\n", provider->canum, provider->cacerts[provider->canum]);
					provider->canum++;
				} else {
					ast_log(LOG_WARNING, "OSP: Too many CA Certificates at line %d\n", var->lineno);
				}
			}
		} else if (!strcasecmp(var->name, "servicepoint")) {
			if (provider->spnum < OSP_MAX_SPOINTS) {
				ast_copy_string(provider->spoints[provider->spnum], var->value, sizeof(provider->spoints[provider->spnum]));
				ast_debug(1, "OSP: servicepoint[%d]: '%s'\n", provider->spnum, provider->spoints[provider->spnum]);
				provider->spnum++;
			} else {
				ast_log(LOG_WARNING, "OSP: Too many Service Points at line %d\n", var->lineno);
			}
		} else if (!strcasecmp(var->name, "maxconnect")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && (num >= OSP_MIN_MAXCONNECT) && (num <= OSP_MAX_MAXCONNECT)) {
				provider->maxconnect = num;
				ast_debug(1, "OSP: maxconnect '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: maxconnect should be an integer from %d to %d, not '%s' at line %d\n",
					OSP_MIN_MAXCONNECT, OSP_MAX_MAXCONNECT, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "retrydelay")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && (num >= OSP_MIN_RETRYDELAY) && (num <= OSP_MAX_RETRYDELAY)) {
				provider->retrydelay = num;
				ast_debug(1, "OSP: retrydelay '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: retrydelay should be an integer from %d to %d, not '%s' at line %d\n",
					OSP_MIN_RETRYDELAY, OSP_MAX_RETRYDELAY, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "retrylimit")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && (num >= OSP_MIN_RETRYLIMIT) && (num <= OSP_MAX_RETRYLIMIT)) {
				provider->retrylimit = num;
				ast_debug(1, "OSP: retrylimit '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: retrylimit should be an integer from %d to %d, not '%s' at line %d\n",
					OSP_MIN_RETRYLIMIT, OSP_MAX_RETRYLIMIT, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "timeout")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && (num >= OSP_MIN_TIMEOUT) && (num <= OSP_MAX_TIMEOUT)) {
				provider->timeout = num;
				ast_debug(1, "OSP: timeout '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: timeout should be an integer from %d to %d, not '%s' at line %d\n",
					OSP_MIN_TIMEOUT, OSP_MAX_TIMEOUT, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "source")) {
			ast_copy_string(provider->source, var->value, sizeof(provider->source));
			ast_debug(1, "OSP: source '%s'\n", provider->source);
		} else if (!strcasecmp(var->name, "authpolicy")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && ((num == OSP_AUTH_NO) || (num == OSP_AUTH_YES) || (num == OSP_AUTH_EXC))) {
				provider->authpolicy = num;
				ast_debug(1, "OSP: authpolicy '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: authpolicy should be %d, %d or %d, not '%s' at line %d\n",
					OSP_AUTH_NO, OSP_AUTH_YES, OSP_AUTH_EXC, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "defprotocol")) {
			if (!strcasecmp(var->value, OSP_PROT_SIP)) {
				provider->defprotocol = OSP_PROT_SIP;
				ast_debug(1, "OSP: default protocol SIP\n");
			} else if (!strcasecmp(var->value, OSP_PROT_H323)) {
				provider->defprotocol = OSP_PROT_H323;
				ast_debug(1, "OSP: default protocol H.323\n");
			} else if (!strcasecmp(var->value, OSP_PROT_IAX)) {
				provider->defprotocol = OSP_PROT_IAX;
				ast_debug(1, "OSP: default protocol IAX\n");
			} else if (!strcasecmp(var->value, OSP_PROT_SKYPE)) {
				provider->defprotocol = OSP_PROT_SKYPE;
				ast_debug(1, "OSP: default protocol Skype\n");
			} else {
				ast_log(LOG_WARNING, "OSP: default protocol should be %s, %s, %s or %s not '%s' at line %d\n",
					OSP_PROT_SIP, OSP_PROT_H323, OSP_PROT_IAX, OSP_PROT_SKYPE, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "workmode")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && ((num == OSP_MODE_DIRECT) || (num == OSP_MODE_INDIRECT))) {
				provider->workmode = num;
				ast_debug(1, "OSP: workmode '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: workmode should be %d or %d, not '%s' at line %d\n",
					OSP_MODE_DIRECT, OSP_MODE_INDIRECT, var->value, var->lineno);
			}
		} else if (!strcasecmp(var->name, "servicetype")) {
			if ((sscanf(var->value, "%30d", &num) == 1) && ((num == OSP_SRV_VOICE) || (num == OSP_SRV_NPQUERY))) {
				provider->srvtype = num;
				ast_debug(1, "OSP: servicetype '%d'\n", num);
			} else {
				ast_log(LOG_WARNING, "OSP: servicetype should be %d or %d, not '%s' at line %d\n",
					OSP_SRV_VOICE, OSP_SRV_NPQUERY, var->value, var->lineno);
			}
		}
	}

	if (provider->canum == 0) {
		provider->canum = 1;
	}

	for (i = 0; i < provider->spnum; i++) {
		pspoints[i] = provider->spoints[i];
	}

	if (osp_security) {
		privatekey.PrivateKeyData = NULL;
		privatekey.PrivateKeyLength = 0;

		localcert.CertData = NULL;
		localcert.CertDataLength = 0;

		for (i = 0; i < provider->canum; i++) {
			cacerts[i].CertData = NULL;
			cacerts[i].CertDataLength = 0;
		}

		if ((error = OSPPUtilLoadPEMPrivateKey((unsigned char*)provider->privatekey, &privatekey)) != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to load privatekey '%s', error '%d'\n", provider->privatekey, error);
		} else if ((error = OSPPUtilLoadPEMCert((unsigned char*)provider->localcert, &localcert)) != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to load localcert '%s', error '%d'\n", provider->localcert, error);
		} else {
			for (i = 0; i < provider->canum; i++) {
				if ((error = OSPPUtilLoadPEMCert((unsigned char*)provider->cacerts[i], &cacerts[i])) != OSPC_ERR_NO_ERROR) {
					ast_log(LOG_WARNING, "OSP: Unable to load cacert '%s', error '%d'\n", provider->cacerts[i], error);
					break;
				} else {
					pcacerts[i] = &cacerts[i];
				}
			}
		}
	} else {
		privatekey.PrivateKeyData = privatekeydata;
		privatekey.PrivateKeyLength = sizeof(privatekeydata);

		localcert.CertData = localcertdata;
		localcert.CertDataLength = sizeof(localcertdata);

		cacerts[0].CertData = cacertdata;
		cacerts[0].CertDataLength = sizeof(cacertdata);
		pcacerts[0] = &cacerts[0];

		if ((error = OSPPBase64Decode(B64PKey, strlen(B64PKey), privatekey.PrivateKeyData, &privatekey.PrivateKeyLength)) != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to decode private key, error '%d'\n", error);
		} else if ((error = OSPPBase64Decode(B64LCert, strlen(B64LCert), localcert.CertData, &localcert.CertDataLength)) != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to decode local cert, error '%d'\n", error);
		} else if ((error = OSPPBase64Decode(B64CACert, strlen(B64CACert), cacerts[0].CertData, &cacerts[0].CertDataLength)) != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to decode cacert, error '%d'\n", error);
		}
	}

	if (error == OSPC_ERR_NO_ERROR) {
		error = OSPPProviderNew(provider->spnum,
			pspoints,
			NULL,
			OSP_AUDIT_URL,
			&privatekey,
			&localcert,
			provider->canum,
			pcacerts,
			OSP_LOCAL_VALIDATION,
			OSP_SSL_LIFETIME,
			provider->maxconnect,
			OSP_HTTP_PERSISTENCE,
			provider->retrydelay,
			provider->retrylimit,
			provider->timeout,
			OSP_CUSTOMER_ID,
			OSP_DEVICE_ID,
			&provider->handle);
		if (error != OSPC_ERR_NO_ERROR) {
			ast_log(LOG_WARNING, "OSP: Unable to create provider '%s', error '%d'\n", name, error);
			res = OSP_ERROR;
		} else {
			ast_debug(1, "OSP: provider '%s'\n", name);
			ast_mutex_lock(&osp_lock);
			provider->next = osp_providers;
			osp_providers = provider;
			ast_mutex_unlock(&osp_lock);
			res = OSP_OK;
		}
	}

	if (osp_security) {
		for (i = 0; i < provider->canum; i++) {
			if (cacerts[i].CertData) {
				ast_free(cacerts[i].CertData);
			}
		}
		if (localcert.CertData) {
			ast_free(localcert.CertData);
		}
		if (privatekey.PrivateKeyData) {
			ast_free(privatekey.PrivateKeyData);
		}
	}

	if (res != OSP_OK) {
		ast_free(provider);
	}

	return res;
}

/*!
 * \brief Get OSP provider by name
 * \param name OSP provider context name
 * \param provider OSP provider structure
 * \return OSP_OK Success, OSP_FAILED Failed, OSP_ERROR Error
 */
static int osp_get_provider(
	const char* name,
	struct osp_provider** provider)
{
	int res = OSP_FAILED;
	struct osp_provider* p;

	*provider = NULL;

	ast_mutex_lock(&osp_lock);
	for (p = osp_providers; p != NULL; p = p->next) {
		if (!strcasecmp(p->name, name)) {
			*provider = p;
			ast_debug(1, "OSP: find provider '%s'\n", name);
			res = OSP_OK;
			break;
		}
	}
	ast_mutex_unlock(&osp_lock);

	return res;
}

/*!
 * \brief Create OSP transaction handle
 * \param name OSP provider context name
 * \param trans OSP transaction handle, output
 * \param source Source of provider, output
 * \param srcsize Size of source buffer, in
 * \return OSK_OK Success, OSK_FAILED Failed, OSP_ERROR Error
 */
static int osp_create_transaction(
	const char* name,
	int* trans,
	char* source,
	unsigned int srcsize)
{
	int res = OSP_FAILED;
	struct osp_provider* provider;
	int error;

	if ((trans == NULL) || (source == NULL) || (srcsize <= 0)) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	*trans = OSP_INVALID_HANDLE;
	*source = '\0';

	ast_mutex_lock(&osp_lock);
	for (provider = osp_providers; provider; provider = provider->next) {
		if (!strcasecmp(provider->name, name)) {
			error = OSPPTransactionNew(provider->handle, trans);
			if (error == OSPC_ERR_NO_ERROR) {
				ast_debug(1, "OSP: transaction '%d'\n", *trans);
				ast_copy_string(source, provider->source, srcsize);
				ast_debug(1, "OSP: source '%s'\n", source);
				res = OSP_OK;
			} else {
				*trans = OSP_INVALID_HANDLE;
				ast_debug(1, "OSP: Unable to create transaction handle, error '%d'\n", error);
				*source = '\0';
				res = OSP_ERROR;
			}
			break;
		}
	}
	ast_mutex_unlock(&osp_lock);

	return res;
}

/*!
 * \brief Convert "address:port" to "[x.x.x.x]:port" or "hostname:port" format
 * \param src Source address string
 * \param dest Destination address string
 * \param destsize Size of dest buffer
 */
static void osp_convert_inout(
	const char* src,
	char* dest,
	unsigned int destsize)
{
	struct in_addr inp;
	char buffer[OSP_SIZE_NORSTR];
	char* port;

	if ((dest != NULL) && (destsize > 0)) {
		if (!ast_strlen_zero(src)) {
			ast_copy_string(buffer, src, sizeof(buffer));

			if((port = strchr(buffer, ':')) != NULL) {
				*port = '\0';
				port++;
			}

			if (inet_pton(AF_INET, buffer, &inp) == 1) {
				if (port != NULL) {
					snprintf(dest, destsize, "[%s]:%s", buffer, port);
				} else {
					snprintf(dest, destsize, "[%s]", buffer);
				}
				dest[destsize - 1] = '\0';
			} else {
				ast_copy_string(dest, src, destsize);
			}
		} else {
			*dest = '\0';
		}
	}
}

/*!
 * \brief Convert "[x.x.x.x]:port" or "hostname:prot" to "address:port" format
 * \param src Source address string
 * \param dest Destination address string
 * \param destsize Size of dest buffer
 */
static void osp_convert_outin(
	const char* src,
	char* dest,
	unsigned int destsize)
{
	char buffer[OSP_SIZE_NORSTR];
	char* end;
	char* port;

	if ((dest != NULL) && (destsize > 0)) {
		if (!ast_strlen_zero(src)) {
			ast_copy_string(buffer, src, sizeof(buffer));

			if (buffer[0] == '[') {
				if((port = strchr(buffer + 1, ':')) != NULL) {
					*port = '\0';
					port++;
				}

				if ((end = strchr(buffer + 1, ']')) != NULL) {
					*end = '\0';
				}

				if (port != NULL) {
					snprintf(dest, destsize, "%s:%s", buffer + 1, port);
					dest[destsize - 1] = '\0';
				} else {
					ast_copy_string(dest, buffer + 1, destsize);
				}
			} else {
				ast_copy_string(dest, src, destsize);
			}
		} else {
			*dest = '\0';
		}
	}
}

/*!
 * \brief Validate OSP token of inbound call
 * \param trans OSP transaction handle
 * \param source Source of inbound call
 * \param destination Destination of inbound call
 * \param calling Calling number
 * \param called Called number
 * \param token OSP token, may be empty
 * \param timelimit Call duration limit, output
 * \return OSP_OK Success, OSP_FAILED Failed, OSP_ERROR Error
 */
static int osp_validate_token(
	int trans,
	const char* source,
	const char* destination,
	const char* calling,
	const char* called,
	const char* token,
	unsigned int* timelimit)
{
	int res;
	int tokenlen;
	unsigned char tokenstr[OSP_SIZE_TOKSTR];
	char src[OSP_SIZE_NORSTR];
	char dest[OSP_SIZE_NORSTR];
	unsigned int authorised;
	unsigned int dummy = 0;
	int error;

	if (timelimit == NULL) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	tokenlen = ast_base64decode(tokenstr, token, strlen(token));
	osp_convert_inout(source, src, sizeof(src));
	osp_convert_inout(destination, dest, sizeof(dest));
	error = OSPPTransactionValidateAuthorisation(trans,
		src,
		dest,
		NULL,
		NULL,
		calling ? calling : "",
		OSPC_NFORMAT_E164,
		called,
		OSPC_NFORMAT_E164,
		0,
		NULL,
		tokenlen,
		(char*)tokenstr,
		&authorised,
		timelimit,
		&dummy,
		NULL,
		osp_tokenformat);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to validate inbound token, error '%d'\n", error);
		*timelimit = 0;
		res = OSP_ERROR;
	} else if (authorised) {
		ast_debug(1, "OSP: Authorised\n");
		res = OSP_OK;
	} else {
		ast_debug(1, "OSP: Unauthorised\n");
		res = OSP_FAILED;
	}

	return res;
}

/*!
 * \brief Choose min duration limit
 * \param in Inbound duration limit
 * \param out Outbound duration limit
 * \return min duration limit
 */
static unsigned int osp_choose_timelimit(
	unsigned int in,
	unsigned int out)
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
 * \param provider OSP provider
 * \param calling Calling number
 * \param called Called number
 * \param destination Destination IP in '[x.x.x.x]' format
 * \param tokenlen OSP token length
 * \param token OSP token
 * \param reason Failure reason, output
 * \param results OSP lookup results, in/output
 * \return OSP_OK Success, OSP_FAILED Failed, OSP_ERROR Error
 */
static int osp_check_destination(
	struct osp_provider* provider,
	const char* calling,
	const char* called,
	const char* destination,
	unsigned int tokenlen,
	const char* token,
	OSPEFAILREASON* reason,
	struct osp_results* results)
{
	int res;
	OSPE_DEST_OSPENABLED enabled;
	OSPE_PROTOCOL_NAME protocol;
	char dest[OSP_SIZE_NORSTR];
	OSPE_OPERATOR_NAME type;
	int error;

	if ((provider == NULL) || (reason == NULL) || (results == NULL)) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	if ((error = OSPPTransactionIsDestOSPEnabled(results->outhandle, &enabled)) != OSPC_ERR_NO_ERROR) {
		ast_debug(1, "OSP: Unable to get destination OSP version, error '%d'\n", error);
		*reason = OSPC_FAIL_NORMAL_UNSPECIFIED;
		return OSP_ERROR;
	}

	if (enabled == OSPC_DOSP_FALSE) {
		results->token[0] = '\0';
	} else {
		ast_base64encode(results->token, (const unsigned char*)token, tokenlen, sizeof(results->token) - 1);
	}

	if ((error = OSPPTransactionGetDestinationNetworkId(results->outhandle, sizeof(results->networkid), results->networkid)) != OSPC_ERR_NO_ERROR) {
		ast_debug(1, "OSP: Unable to get destination network ID, error '%d'\n", error);
		results->networkid[0] = '\0';
	}

	error = OSPPTransactionGetNumberPortabilityParameters(results->outhandle,
		sizeof(results->nprn),
		results->nprn,
		sizeof(results->npcic),
		results->npcic,
		&results->npdi);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_debug(1, "OSP: Unable to get number portability parameters, error '%d'\n", error);
		results->nprn[0] = '\0';
		results->npcic[0] = '\0';
		results->npdi = 0;
	}

	for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
		error = OSPPTransactionGetOperatorName(results->outhandle, type, sizeof(results->opname[type]), results->opname[type]);
		if (error != OSPC_ERR_NO_ERROR) {
			ast_debug(1, "OSP: Unable to get operator name of type '%d', error '%d'\n", type, error);
			results->opname[type][0] = '\0';
		}
	}

	if ((error = OSPPTransactionGetDestProtocol(results->outhandle, &protocol)) != OSPC_ERR_NO_ERROR) {
		ast_debug(1, "OSP: Unable to get destination protocol, error '%d'\n", error);
		*reason = OSPC_FAIL_NORMAL_UNSPECIFIED;
		results->token[0] = '\0';
		results->networkid[0] = '\0';
		results->nprn[0] = '\0';
		results->npcic[0] = '\0';
		results->npdi = 0;
		for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
			results->opname[type][0] = '\0';
		}
		return OSP_ERROR;
	}

	res = OSP_OK;
	osp_convert_outin(destination, dest, sizeof(dest));
	switch(protocol) {
	case OSPC_PROTNAME_SIP:
		ast_debug(1, "OSP: protocol SIP\n");
		ast_copy_string(results->outtech, OSP_TECH_SIP, sizeof(results->outtech));
		ast_copy_string(results->dest, dest, sizeof(results->dest));
		ast_copy_string(results->calling, calling, sizeof(results->calling));
		ast_copy_string(results->called, called, sizeof(results->called));
		break;
	case OSPC_PROTNAME_Q931:
		ast_debug(1, "OSP: protocol Q.931\n");
		ast_copy_string(results->outtech, OSP_TECH_H323, sizeof(results->outtech));
		ast_copy_string(results->dest, dest, sizeof(results->dest));
		ast_copy_string(results->calling, calling, sizeof(results->calling));
		ast_copy_string(results->called, called, sizeof(results->called));
		break;
	case OSPC_PROTNAME_IAX:
		ast_debug(1, "OSP: protocol IAX\n");
		ast_copy_string(results->outtech, OSP_TECH_IAX, sizeof(results->outtech));
		ast_copy_string(results->dest, dest, sizeof(results->dest));
		ast_copy_string(results->calling, calling, sizeof(results->calling));
		ast_copy_string(results->called, called, sizeof(results->called));
		break;
	case OSPC_PROTNAME_SKYPE:
		ast_debug(1, "OSP: protocol Skype\n");
		ast_copy_string(results->outtech, OSP_TECH_SKYPE, sizeof(results->outtech));
		ast_copy_string(results->dest, dest, sizeof(results->dest));
		ast_copy_string(results->calling, calling, sizeof(results->calling));
		ast_copy_string(results->called, called, sizeof(results->called));
		break;
	case OSPC_PROTNAME_UNDEFINED:
	case OSPC_PROTNAME_UNKNOWN:
		ast_debug(1, "OSP: unknown/undefined protocol '%d'\n", protocol);
		ast_debug(1, "OSP: use default protocol '%s'\n", provider->defprotocol);
		ast_copy_string(results->outtech, provider->defprotocol, sizeof(results->outtech));
		ast_copy_string(results->dest, dest, sizeof(results->dest));
		ast_copy_string(results->calling, calling, sizeof(results->calling));
		ast_copy_string(results->called, called, sizeof(results->called));
		break;
	case OSPC_PROTNAME_LRQ:
	case OSPC_PROTNAME_T37:
	case OSPC_PROTNAME_T38:
	case OSPC_PROTNAME_SMPP:
	case OSPC_PROTNAME_XMPP:
	default:
		ast_log(LOG_WARNING, "OSP: unsupported protocol '%d'\n", protocol);
		*reason = OSPC_FAIL_PROTOCOL_ERROR;
		results->token[0] = '\0';
		results->networkid[0] = '\0';
		results->nprn[0] = '\0';
		results->npcic[0] = '\0';
		results->npdi = 0;
		for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
			results->opname[type][0] = '\0';
		}
		res = OSP_FAILED;
		break;
	}

	return res;
}

/*!
 * \brief Convert Asterisk status to TC code
 * \param cause Asterisk hangup cause
 * \return OSP TC code
 */
static OSPEFAILREASON asterisk2osp(
	int cause)
{
	return (OSPEFAILREASON)cause;
}

/*!
 * \brief OSP Authentication function
 * \param name OSP provider context name
 * \param trans OSP transaction handle, output
 * \param source Source of inbound call
 * \param calling Calling number
 * \param called Called number
 * \param token OSP token, may be empty
 * \param timelimit Call duration limit, output
 * \return OSP_OK Authenricated, OSP_FAILED Unauthenticated, OSP_ERROR Error
 */
static int osp_auth(
	const char* name,
	int* trans,
	const char* source,
	const char* calling,
	const char* called,
	const char* token,
	unsigned int* timelimit)
{
	int res;
	struct osp_provider* provider = NULL;
	char dest[OSP_SIZE_NORSTR];

	if ((trans == NULL) || (timelimit == NULL)) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	*trans = OSP_INVALID_HANDLE;
	*timelimit = OSP_DEF_TIMELIMIT;

	if ((res = osp_get_provider(name, &provider)) <= 0) {
		ast_debug(1, "OSP: Unabe to find OSP provider '%s'\n", name);
		return res;
	}

	switch (provider->authpolicy) {
	case OSP_AUTH_NO:
		res = OSP_OK;
		break;
	case OSP_AUTH_EXC:
		if (ast_strlen_zero(token)) {
			res = OSP_FAILED;
		} else if ((res = osp_create_transaction(name, trans, dest, sizeof(dest))) <= 0) {
			ast_debug(1, "OSP: Unable to generate transaction handle\n");
			*trans = OSP_INVALID_HANDLE;
			res = OSP_FAILED;
		} else if((res = osp_validate_token(*trans, source, dest, calling, called, token, timelimit)) <= 0) {
			OSPPTransactionRecordFailure(*trans, OSPC_FAIL_CALL_REJECTED);
		}
		break;
	case OSP_AUTH_YES:
	default:
		if (ast_strlen_zero(token)) {
			res = OSP_OK;
		} else if ((res = osp_create_transaction(name, trans, dest, sizeof(dest))) <= 0) {
			ast_debug(1, "OSP: Unable to generate transaction handle\n");
			*trans = OSP_INVALID_HANDLE;
			res = OSP_FAILED;
		} else if((res = osp_validate_token(*trans, source, dest, calling, called, token, timelimit)) <= 0) {
			OSPPTransactionRecordFailure(*trans, OSPC_FAIL_CALL_REJECTED);
		}
		break;
	}

	return res;
}

/*!
 * \brief Create a UUID
 * \param uuid UUID buffer
 * \param bufsize UUID buffer size
 * \return OSK_OK Created, OSP_ERROR Error
 */
static int osp_create_uuid(
	unsigned char* uuid,
	unsigned int* bufsize)
{
	int i, res;
	long int tmp[OSP_SIZE_UUID / sizeof(long int)];

	if ((uuid != NULL) && (*bufsize >= OSP_SIZE_UUID)) {
		for (i = 0; i < OSP_SIZE_UUID / sizeof(long int); i++) {
			tmp[i] = ast_random();
		}
		memcpy(uuid, tmp, OSP_SIZE_UUID);
		*bufsize = OSP_SIZE_UUID;
		res = OSP_OK;
	} else {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		res = OSP_ERROR;
	}

	return res;
}

/*!
 * \brief UUID to string
 * \param uuid UUID
 * \param buffer String buffer
 * \param bufsize String buffer size
 * \return OSP_OK Successed, OSP_ERROR Error
 */
static int osp_uuid2str(
	unsigned char* uuid,
	char* buffer,
	unsigned int bufsize)
{
	int res;

	if ((uuid != NULL) && (bufsize > OSP_SIZE_UUIDSTR)) {
		snprintf(buffer, bufsize, "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-"
					  "%02hhx%02hhx-%02hhx%02hhx-"
					  "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
			uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
			uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
		res = OSP_OK;
	} else {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		res = OSP_ERROR;
	}

	return res;
}

/*!
 * \brief Create a call ID according to the type
 * \param type Call ID type
 * \param callid Call ID buffer
 * \return OSK_OK Created, OSP_FAILED Not create, OSP_ERROR Error
 */
static int osp_create_callid(
	unsigned int type,
	struct osp_callid* callid)
{
	int res;

	if (callid == NULL) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	callid->len = sizeof(callid->buf);
	switch (type) {
	case OSP_CALLID_H323:
		res = osp_create_uuid(callid->buf, &callid->len);
		break;
	case OSP_CALLID_SIP:
	case OSP_CALLID_IAX:
		res = OSP_FAILED;
		break;
	default:
		res = OSP_ERROR;
		break;
	}

	if ((res != OSP_OK) && (callid->len != 0)) {
		callid->buf[0] = '\0';
		callid->len = 0;
	}

	return res;
}

/*!
 * \brief OSP Lookup function
 * \param name OSP provider context name
 * \param callidtypes Call ID types
 * \param actualsrc Actual source device in indirect mode
 * \param srcdev Source device of outbound call
 * \param calling Calling number
 * \param called Called number
 * \param snetid Source network ID
 * \param np NP parameters
 * \param headers SIP header parameters
 * \param cinfo Custom info
 * \param results Lookup results
 * \return OSP_OK Found , OSP_FAILED No route, OSP_ERROR Error
 */
static int osp_lookup(
	const char* name,
	unsigned int callidtypes,
	const char* actualsrc,
	const char* srcdev,
	const char* calling,
	const char* called,
	const char* snetid,
	struct osp_npdata* np,
	struct osp_headers* headers,
	const char* cinfo[],
	struct osp_results* results)
{
	int res;
	struct osp_provider* provider = NULL;
	OSPE_PROTOCOL_NAME protocol;
	char source[OSP_SIZE_NORSTR];
	char callingnum[OSP_SIZE_NORSTR];
	char callednum[OSP_SIZE_NORSTR];
	char destination[OSP_SIZE_NORSTR];
	char* tmp;
	unsigned int tokenlen;
	char token[OSP_SIZE_TOKSTR];
	char src[OSP_SIZE_NORSTR];
	char dev[OSP_SIZE_NORSTR];
	char host[OSP_SIZE_NORSTR];
	unsigned int i, type;
	struct osp_callid callid;
	unsigned int callidnum;
	OSPT_CALL_ID* callids[OSP_CALLID_MAXNUM];
	char dest[OSP_SIZE_NORSTR];
	const char* preferred[2] = { NULL };
	unsigned int dummy = 0;
	OSPEFAILREASON reason;
	int error;

	if (results == NULL) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	osp_convert_inout(results->dest, dest, sizeof(dest));

	results->outhandle = OSP_INVALID_HANDLE;
	results->outtech[0] = '\0';
	results->calling[0] = '\0';
	results->called[0] = '\0';
	results->token[0] = '\0';
	results->networkid[0] = '\0';
	results->nprn[0] = '\0';
	results->npcic[0] = '\0';
	results->npdi = 0;
	for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
		results->opname[type][0] = '\0';
	}
	results->numdests = 0;
	results->outtimelimit = OSP_DEF_TIMELIMIT;

	if ((res = osp_get_provider(name, &provider)) <= 0) {
		ast_debug(1, "OSP: Unabe to find OSP provider '%s'\n", name);
		return res;
	}

	if ((res = osp_create_transaction(name, &results->outhandle, source, sizeof(source))) <= 0) {
		ast_debug(1, "OSP: Unable to generate transaction handle\n");
		results->outhandle = OSP_INVALID_HANDLE;
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		}
		return OSP_ERROR;
	}

	if (!strcasecmp(results->intech, OSP_TECH_SIP)) {
		protocol = OSPC_PROTNAME_SIP;
	} else if (!strcasecmp(results->intech, OSP_TECH_H323)) {
		protocol = OSPC_PROTNAME_Q931;
	} else if (!strcasecmp(results->intech, OSP_TECH_IAX)) {
		protocol = OSPC_PROTNAME_IAX;
	} else if (!strcasecmp(results->intech, OSP_TECH_SKYPE)) {
		protocol = OSPC_PROTNAME_SKYPE;
	} else {
		protocol = OSPC_PROTNAME_SIP;
	}
	OSPPTransactionSetProtocol(results->outhandle, OSPC_PROTTYPE_SOURCE, protocol);

	if (!ast_strlen_zero(snetid)) {
		OSPPTransactionSetNetworkIds(results->outhandle, snetid, "");
	}

	OSPPTransactionSetNumberPortability(results->outhandle, np->rn, np->cic, np->npdi);

	for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
		OSPPTransactionSetOperatorName(results->outhandle, type, np->opname[type]);
	}

    OSPPTransactionSetRemotePartyId(results->outhandle, OSPC_NFORMAT_E164, headers->rpiduser);
    OSPPTransactionSetAssertedId(results->outhandle, OSPC_NFORMAT_E164, headers->paiuser);
	osp_convert_inout(headers->divhost, host, sizeof(host));
	OSPPTransactionSetDiversion(results->outhandle, headers->divuser, host);
    OSPPTransactionSetChargeInfo(results->outhandle, OSPC_NFORMAT_E164, headers->pciuser);

	if (cinfo != NULL) {
		for (i = 0; i < OSP_MAX_CUSTOMINFO; i++) {
			if (!ast_strlen_zero(cinfo[i])) {
				OSPPTransactionSetCustomInfo(results->outhandle, i, cinfo[i]);
			}
		}
	}

	ast_copy_string(callednum, called, sizeof(callednum));
	if((tmp = strchr(callednum, ';')) != NULL) {
		*tmp = '\0';
	}

	callidnum = 0;
	callids[0] = NULL;
	for (i = 0; i < OSP_CALLID_MAXNUM; i++) {
		type = 1 << i;
		if (callidtypes & type) {
			error = osp_create_callid(type, &callid);
			if (error == 1) {
				callids[callidnum] = OSPPCallIdNew(callid.len, callid.buf);
				callidnum++;
			}
		}
	}

	if (provider->workmode == OSP_MODE_INDIRECT) {
		osp_convert_inout(srcdev, src, sizeof(src));
		if (ast_strlen_zero(actualsrc)) {
			osp_convert_inout(srcdev, dev, sizeof(dev));
		} else {
			osp_convert_inout(actualsrc, dev, sizeof(dev));
		}
	} else {
		osp_convert_inout(source, src, sizeof(src));
		osp_convert_inout(srcdev, dev, sizeof(dev));
	}

	if (provider->srvtype == OSP_SRV_NPQUERY) {
		OSPPTransactionSetServiceType(results->outhandle, OSPC_SERVICE_NPQUERY);
		if (!ast_strlen_zero(dest)) {
			preferred[0] = dest;
		}
		results->numdests = 1;
	} else {
		OSPPTransactionSetServiceType(results->outhandle, OSPC_SERVICE_VOICE);
		results->numdests = OSP_DEF_MAXDESTS;
	}

	error = OSPPTransactionRequestAuthorisation(results->outhandle,
		src,
		dev,
		calling ? calling : "",
		OSPC_NFORMAT_E164,
		callednum,
		OSPC_NFORMAT_E164,
		NULL,
		callidnum,
		callids,
		preferred,
		&results->numdests,
		&dummy,
		NULL);

	for (i = 0; i < callidnum; i++) {
		OSPPCallIdDelete(&callids[i]);
	}

	if (error != OSPC_ERR_NO_ERROR) {
		ast_log(LOG_WARNING, "OSP: Unable to request authorization, error '%d'\n", error);
		results->numdests = 0;
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		}
		return OSP_ERROR;
	}

	if (!results->numdests) {
		ast_debug(1, "OSP: No more destination\n");
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return OSP_FAILED;
	}

	results->outcallid.len = sizeof(results->outcallid.buf);
	tokenlen = sizeof(token);
	error = OSPPTransactionGetFirstDestination(results->outhandle,
		0,
		NULL,
		NULL,
		&results->outtimelimit,
		&results->outcallid.len,
		results->outcallid.buf,
		sizeof(callednum),
		callednum,
		sizeof(callingnum),
		callingnum,
		sizeof(destination),
		destination,
		0,
		NULL,
		&tokenlen,
		token);
	if (error != OSPC_ERR_NO_ERROR) {
		ast_debug(1, "OSP: Unable to get first route, error '%d'\n", error);
		results->numdests = 0;
		results->outtimelimit = OSP_DEF_TIMELIMIT;
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return OSP_ERROR;
	}

	results->numdests--;
	results->outtimelimit = osp_choose_timelimit(results->intimelimit, results->outtimelimit);
	ast_debug(1, "OSP: outtimelimit '%d'\n", results->outtimelimit);
	ast_debug(1, "OSP: calling '%s'\n", callingnum);
	ast_debug(1, "OSP: called '%s'\n", callednum);
	ast_debug(1, "OSP: destination '%s'\n", destination);
	ast_debug(1, "OSP: token size '%d'\n", tokenlen);

	if ((res = osp_check_destination(provider, callingnum, callednum, destination, tokenlen, token, &reason, results)) > 0) {
		return OSP_OK;
	}

	if (!results->numdests) {
		ast_debug(1, "OSP: No more destination\n");
		results->outtimelimit = OSP_DEF_TIMELIMIT;
		OSPPTransactionRecordFailure(results->outhandle, reason);
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return OSP_FAILED;
	}

	while(results->numdests) {
		results->outcallid.len = sizeof(results->outcallid.buf);
		tokenlen = sizeof(token);
		error = OSPPTransactionGetNextDestination(results->outhandle,
			reason,
			0,
			NULL,
			NULL,
			&results->outtimelimit,
			&results->outcallid.len,
			results->outcallid.buf,
			sizeof(callednum),
			callednum,
			sizeof(callingnum),
			callingnum,
			sizeof(destination),
			destination,
			0,
			NULL,
			&tokenlen,
			token);
		if (error == OSPC_ERR_NO_ERROR) {
			results->numdests--;
			results->outtimelimit = osp_choose_timelimit(results->intimelimit, results->outtimelimit);
			ast_debug(1, "OSP: outtimelimit '%d'\n", results->outtimelimit);
			ast_debug(1, "OSP: calling '%s'\n", callingnum);
			ast_debug(1, "OSP: called '%s'\n", callednum);
			ast_debug(1, "OSP: destination '%s'\n", destination);
			ast_debug(1, "OSP: token size '%d'\n", tokenlen);

			if ((res = osp_check_destination(provider, callingnum, callednum, destination, tokenlen, token, &reason, results)) > 0) {
				break;
			} else if (!results->numdests) {
				ast_debug(1, "OSP: No more destination\n");
				OSPPTransactionRecordFailure(results->outhandle, reason);
				if (results->inhandle != OSP_INVALID_HANDLE) {
					OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
				}
				res = OSP_FAILED;
				break;
			}
		} else {
			ast_debug(1, "OSP: Unable to get route, error '%d'\n", error);
			results->numdests = 0;
			results->outtimelimit = OSP_DEF_TIMELIMIT;
			if (results->inhandle != OSP_INVALID_HANDLE) {
				OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
			}
			res = OSP_ERROR;
			break;
		}
	}

	return res;
}

/*!
 * \brief OSP Lookup Next function
 * \param name OSP provider name
 * \param cause Asterisk hangup cause
 * \param results Lookup results, in/output
 * \return OSP_OK Found , OSP_FAILED No route, OSP_ERROR Error
 */
static int osp_next(
	const char* name,
	int cause,
	struct osp_results* results)
{
	int res;
	struct osp_provider* provider = NULL;
	char calling[OSP_SIZE_NORSTR];
	char called[OSP_SIZE_NORSTR];
	char dest[OSP_SIZE_NORSTR];
	unsigned int tokenlen;
	char token[OSP_SIZE_TOKSTR];
	OSPEFAILREASON reason;
	OSPE_OPERATOR_NAME type;
	int error;

	if (results == NULL) {
		ast_log(LOG_ERROR, "Invalid parameters\n");
		return OSP_ERROR;
	}

	results->outtech[0] = '\0';
	results->dest[0] = '\0';
	results->calling[0] = '\0';
	results->called[0] = '\0';
	results->token[0] = '\0';
	results->networkid[0] = '\0';
	results->nprn[0] = '\0';
	results->npcic[0] = '\0';
	results->npdi = 0;
	for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
		results->opname[type][0] = '\0';
	}
	results->outtimelimit = OSP_DEF_TIMELIMIT;

	if ((res = osp_get_provider(name, &provider)) <= 0) {
		ast_debug(1, "OSP: Unabe to find OSP provider '%s'\n", name);
		return res;
	}

	if (results->outhandle == OSP_INVALID_HANDLE) {
		ast_debug(1, "OSP: Transaction handle undefined\n");
		results->numdests = 0;
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
		}
		return OSP_ERROR;
	}

	reason = asterisk2osp(cause);

	if (!results->numdests) {
		ast_debug(1, "OSP: No more destination\n");
		OSPPTransactionRecordFailure(results->outhandle, reason);
		if (results->inhandle != OSP_INVALID_HANDLE) {
			OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
		}
		return OSP_FAILED;
	}

	while(results->numdests) {
		results->outcallid.len = sizeof(results->outcallid.buf);
		tokenlen = sizeof(token);
		error = OSPPTransactionGetNextDestination(
			results->outhandle,
			reason,
			0,
			NULL,
			NULL,
			&results->outtimelimit,
			&results->outcallid.len,
			results->outcallid.buf,
			sizeof(called),
			called,
			sizeof(calling),
			calling,
			sizeof(dest),
			dest,
			0,
			NULL,
			&tokenlen,
			token);
		if (error == OSPC_ERR_NO_ERROR) {
			results->numdests--;
			results->outtimelimit = osp_choose_timelimit(results->intimelimit, results->outtimelimit);
			ast_debug(1, "OSP: outtimelimit '%d'\n", results->outtimelimit);
			ast_debug(1, "OSP: calling '%s'\n", calling);
			ast_debug(1, "OSP: called '%s'\n", called);
			ast_debug(1, "OSP: destination '%s'\n", dest);
			ast_debug(1, "OSP: token size '%d'\n", tokenlen);

			if ((res = osp_check_destination(provider, calling, called, dest, tokenlen, token, &reason, results)) > 0) {
				res = OSP_OK;
				break;
			} else if (!results->numdests) {
				ast_debug(1, "OSP: No more destination\n");
				OSPPTransactionRecordFailure(results->outhandle, reason);
				if (results->inhandle != OSP_INVALID_HANDLE) {
					OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NO_ROUTE_TO_DEST);
				}
				res = OSP_FAILED;
				break;
			}
		} else {
			ast_debug(1, "OSP: Unable to get route, error '%d'\n", error);
			results->token[0] = '\0';
			results->numdests = 0;
			results->outtimelimit = OSP_DEF_TIMELIMIT;
			if (results->inhandle != OSP_INVALID_HANDLE) {
				OSPPTransactionRecordFailure(results->inhandle, OSPC_FAIL_NORMAL_UNSPECIFIED);
			}
			res = OSP_ERROR;
			break;
		}
	}

	return res;
}

/*!
 * \brief Get integer from variable string
 * \param vstr Variable string
 * \return OSP_DEF_INTSTATS Error
 */
static int osp_get_varint(
	const char* vstr)
{
	char* tmp;
	int value = OSP_DEF_INTSTATS;

	if (!ast_strlen_zero(vstr)) {
		if ((tmp = strchr(vstr, '=')) != NULL) {
			tmp++;
			if (sscanf(tmp, "%30d", &value) != 1) {
				value = OSP_DEF_INTSTATS;
			}
		}
	}

	return value;
}

/*!
 * \brief Get float from variable string
 * \param vstr Variable string
 * \return OSP_DEF_FLOATSTATS Error
 */
static float osp_get_varfloat(
	const char* vstr)
{
	char* tmp;
	float value = OSP_DEF_FLOATSTATS;

	if (!ast_strlen_zero(vstr)) {
		if ((tmp = strchr(vstr, '=')) != NULL) {
			tmp++;
			if (sscanf(tmp, "%30f", &value) != 1) {
				value = OSP_DEF_FLOATSTATS;
			}
		}
	}

	return value;
}

/*!
 * \brief Report QoS
 * \param trans OSP in/outbound transaction handle
 * \param leg Inbound/outbound
 * \param qos QoS string
 * \return OSP_OK Success, OSP_FAILED Failed, OSP_ERROR Error
 */
static int osp_report_qos(
	int trans,
	enum osp_callleg leg,
	const char* qos)
{
	int res = OSP_FAILED;
	enum osp_direction dir;
	char buffer[OSP_SIZE_NORSTR];
	char* tmp;
	char* item;
	int totalpackets[OSP_DIR_NUMBER];
	struct osp_metrics lost[OSP_DIR_NUMBER];
	struct osp_metrics jitter[OSP_DIR_NUMBER];
	struct osp_metrics rtt;
	int value;

	if (!ast_strlen_zero(qos)) {
		for (dir = OSP_DIR_RX; dir < OSP_DIR_NUMBER; dir++) {
			totalpackets[dir] = OSP_DEF_INTSTATS;
		}

		for (dir = OSP_DIR_RX; dir < OSP_DIR_NUMBER; dir++) {
			lost[dir].value = OSP_DEF_INTSTATS;
			lost[dir].min = OSP_DEF_FLOATSTATS;
			lost[dir].max = OSP_DEF_FLOATSTATS;
			lost[dir].avg = OSP_DEF_FLOATSTATS;
			lost[dir].sdev = OSP_DEF_FLOATSTATS;
		}

		for (dir = OSP_DIR_RX; dir < OSP_DIR_NUMBER; dir++) {
			jitter[dir].value = OSP_DEF_INTSTATS;
			jitter[dir].min = OSP_DEF_FLOATSTATS;
			jitter[dir].max = OSP_DEF_FLOATSTATS;
			jitter[dir].avg = OSP_DEF_FLOATSTATS;
			jitter[dir].sdev = OSP_DEF_FLOATSTATS;
		}

		rtt.value = OSP_DEF_INTSTATS;
		rtt.min = OSP_DEF_FLOATSTATS;
		rtt.max = OSP_DEF_FLOATSTATS;
		rtt.avg = OSP_DEF_FLOATSTATS;
		rtt.sdev = OSP_DEF_FLOATSTATS;

		ast_copy_string(buffer, qos, sizeof(buffer));
		for (item = strtok_r(buffer, ";", &tmp); item; item = strtok_r(NULL, ";", &tmp)) {
			if (!strncasecmp(item, "rxcount", strlen("rxcount"))) {
				totalpackets[OSP_DIR_RX] = osp_get_varint(item);
			} else if (!strncasecmp(item, "txcount", strlen("txcount"))) {
				totalpackets[OSP_DIR_TX] = osp_get_varint(item);
			} else if (!strncasecmp(item, "lp", strlen("lp"))) {
				lost[OSP_DIR_RX].value = osp_get_varint(item);
			} else if (!strncasecmp(item, "minrxlost", strlen("minrxlost"))) {
				lost[OSP_DIR_RX].min = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "maxrxlost", strlen("maxrxlost"))) {
				lost[OSP_DIR_RX].max = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "avgrxlost", strlen("avgrxlost"))) {
				lost[OSP_DIR_RX].avg = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "stdevrxlost", strlen("stdevrxlost"))) {
				lost[OSP_DIR_RX].sdev = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "rlp", strlen("rlp"))) {
				lost[OSP_DIR_TX].value = osp_get_varint(item);
			} else if (!strncasecmp(item, "reported_minlost", strlen("reported_minlost"))) {
				lost[OSP_DIR_TX].min = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "reported_maxlost", strlen("reported_maxlost"))) {
				lost[OSP_DIR_TX].max = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "reported_avglost", strlen("reported_avglost"))) {
				lost[OSP_DIR_TX].avg = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "reported_stdevlost", strlen("reported_stdevlost"))) {
				lost[OSP_DIR_TX].sdev = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "rxjitter", strlen("rxjitter"))) {
				jitter[OSP_DIR_RX].value = osp_get_varint(item);
			} else if (!strncasecmp(item, "minrxjitter", strlen("minrxjitter"))) {
				jitter[OSP_DIR_RX].min = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "maxrxjitter", strlen("maxrxjitter"))) {
				jitter[OSP_DIR_RX].max = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "avgrxjitter", strlen("avgjitter"))) {
				jitter[OSP_DIR_RX].avg = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "stdevrxjitter", strlen("stdevjitter"))) {
				jitter[OSP_DIR_RX].sdev = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "txjitter", strlen("txjitter"))) {
				jitter[OSP_DIR_TX].value = osp_get_varint(item);
			} else if (!strncasecmp(item, "reported_minjitter", strlen("reported_minjitter"))) {
				jitter[OSP_DIR_TX].min = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "reported_maxjitter", strlen("reported_maxjitter"))) {
				jitter[OSP_DIR_TX].max = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "reported_avgjitter", strlen("reported_avgjitter"))) {
				jitter[OSP_DIR_TX].avg = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "reported_stdevjitter", strlen("reported_stdevjitter"))) {
				jitter[OSP_DIR_TX].sdev = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "rtt", strlen("rtt"))) {
				rtt.value = osp_get_varint(item);
			} else if (!strncasecmp(item, "minrtt", strlen("minrtt"))) {
				rtt.min = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "maxrtt", strlen("maxrtt"))) {
				rtt.max = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "avgrtt", strlen("avgrtt"))) {
				rtt.avg = osp_get_varfloat(item);
			} else if (!strncasecmp(item, "stdevrtt", strlen("stdevrtt"))) {
				rtt.sdev = osp_get_varfloat(item);
			}
		}

		ast_debug(1, "OSP: call leg '%d'\n", leg);
		ast_debug(1, "OSP: rxcount '%d'\n", totalpackets[OSP_DIR_RX]);
		ast_debug(1, "OSP: txcount '%d'\n", totalpackets[OSP_DIR_TX]);
		ast_debug(1, "OSP: lp '%d'\n",lost[OSP_DIR_RX].value);
		ast_debug(1, "OSP: minrxlost '%f'\n", lost[OSP_DIR_RX].min);
		ast_debug(1, "OSP: maxrxlost '%f'\n", lost[OSP_DIR_RX].max);
		ast_debug(1, "OSP: avgrxlost '%f'\n", lost[OSP_DIR_RX].avg);
		ast_debug(1, "OSP: stdevrxlost '%f'\n", lost[OSP_DIR_RX].sdev);
		ast_debug(1, "OSP: rlp '%d'\n", lost[OSP_DIR_TX].value);
		ast_debug(1, "OSP: reported_minlost '%f'\n", lost[OSP_DIR_TX].min);
		ast_debug(1, "OSP: reported_maxlost '%f'\n", lost[OSP_DIR_TX].max);
		ast_debug(1, "OSP: reported_avglost '%f'\n", lost[OSP_DIR_TX].avg);
		ast_debug(1, "OSP: reported_stdevlost '%f'\n", lost[OSP_DIR_TX].sdev);
		ast_debug(1, "OSP: rxjitter '%d'\n", jitter[OSP_DIR_RX].value);
		ast_debug(1, "OSP: minrxjitter '%f'\n", jitter[OSP_DIR_RX].min);
		ast_debug(1, "OSP: maxrxjitter '%f'\n", jitter[OSP_DIR_RX].max);
		ast_debug(1, "OSP: avgrxjitter '%f'\n", jitter[OSP_DIR_RX].avg);
		ast_debug(1, "OSP: stdevrxjitter '%f'\n", jitter[OSP_DIR_RX].sdev);
		ast_debug(1, "OSP: txjitter '%d'\n", jitter[OSP_DIR_TX].value);
		ast_debug(1, "OSP: reported_minjitter '%f'\n", jitter[OSP_DIR_TX].min);
		ast_debug(1, "OSP: reported_maxjitter '%f'\n", jitter[OSP_DIR_TX].max);
		ast_debug(1, "OSP: reported_avgjitter '%f'\n", jitter[OSP_DIR_TX].avg);
		ast_debug(1, "OSP: reported_stdevjitter '%f'\n", jitter[OSP_DIR_TX].sdev);
		ast_debug(1, "OSP: rtt '%d'\n", rtt.value);
		ast_debug(1, "OSP: minrtt '%f'\n", rtt.min);
		ast_debug(1, "OSP: maxrtt '%f'\n", rtt.max);
		ast_debug(1, "OSP: avgrtt '%f'\n", rtt.avg);
		ast_debug(1, "OSP: stdevrtt '%f'\n", rtt.sdev);

		if (leg == OSP_CALL_INBOUND) {
			OSPPTransactionSetPackets(trans, OSPC_SMETRIC_RTP, OSPC_SDIR_SRCREP, totalpackets[OSP_DIR_RX]);
			OSPPTransactionSetPackets(trans, OSPC_SMETRIC_RTCP, OSPC_SDIR_DESTREP, totalpackets[OSP_DIR_TX]);
			if (lost[OSP_DIR_RX].value >= 0) {
				value = lost[OSP_DIR_RX].value;
			} else {
				value = (int)lost[OSP_DIR_RX].avg;
			}
			OSPPTransactionSetLost(trans, OSPC_SMETRIC_RTP, OSPC_SDIR_SRCREP, value, OSP_DEF_INTSTATS);
			if (lost[OSP_DIR_TX].value >= 0) {
				value = lost[OSP_DIR_TX].value;
			} else {
				value = (int)lost[OSP_DIR_TX].avg;
			}
			OSPPTransactionSetLost(trans, OSPC_SMETRIC_RTCP, OSPC_SDIR_DESTREP, value, OSP_DEF_INTSTATS);
			if (jitter[OSP_DIR_RX].value >= 0) {
				value = jitter[OSP_DIR_RX].value;
			} else {
				value = (int)jitter[OSP_DIR_RX].avg;
			}
			OSPPTransactionSetJitter(trans,
				OSPC_SMETRIC_RTP,
				OSPC_SDIR_SRCREP,
				OSP_DEF_INTSTATS,
				(int)jitter[OSP_DIR_RX].min,
				(int)jitter[OSP_DIR_RX].max,
				value, jitter[OSP_DIR_RX].sdev);
			if (jitter[OSP_DIR_TX].value >= 0) {
				value = jitter[OSP_DIR_TX].value;
			} else {
				value = (int)jitter[OSP_DIR_TX].avg;
			}
			OSPPTransactionSetJitter(trans, OSPC_SMETRIC_RTCP, OSPC_SDIR_DESTREP,
				OSP_DEF_INTSTATS, (int)jitter[OSP_DIR_TX].min, (int)jitter[OSP_DIR_TX].max, value, jitter[OSP_DIR_TX].sdev);
		} else {
			OSPPTransactionSetPackets(trans, OSPC_SMETRIC_RTP, OSPC_SDIR_DESTREP, totalpackets[OSP_DIR_RX]);
			OSPPTransactionSetPackets(trans, OSPC_SMETRIC_RTCP, OSPC_SDIR_SRCREP, totalpackets[OSP_DIR_TX]);
			OSPPTransactionSetLost(trans, OSPC_SMETRIC_RTP, OSPC_SDIR_DESTREP, lost[OSP_DIR_RX].value, OSP_DEF_INTSTATS);
			OSPPTransactionSetLost(trans, OSPC_SMETRIC_RTCP, OSPC_SDIR_SRCREP, lost[OSP_DIR_TX].value, OSP_DEF_INTSTATS);
			if (jitter[OSP_DIR_RX].value >= 0) {
				value = jitter[OSP_DIR_RX].value;
			} else {
				value = (int)jitter[OSP_DIR_RX].avg;
			}
			OSPPTransactionSetJitter(trans,
				OSPC_SMETRIC_RTP,
				OSPC_SDIR_DESTREP,
				OSP_DEF_INTSTATS,
				(int)jitter[OSP_DIR_RX].min,
				(int)jitter[OSP_DIR_RX].max,
				value,
				jitter[OSP_DIR_RX].sdev);
			if (jitter[OSP_DIR_TX].value >= 0) {
				value = jitter[OSP_DIR_TX].value;
			} else {
				value = (int)jitter[OSP_DIR_TX].avg;
			}
			OSPPTransactionSetJitter(trans,
				OSPC_SMETRIC_RTCP,
				OSPC_SDIR_SRCREP,
				OSP_DEF_INTSTATS,
				(int)jitter[OSP_DIR_TX].min,
				(int)jitter[OSP_DIR_TX].max,
				value,
				jitter[OSP_DIR_TX].sdev);
		}

		res = OSP_OK;
	}

	return res;
}

/*!
 * \brief OSP Finish function
 * \param trans OSP in/outbound transaction handle
 * \param recorded If failure reason has been recorded
 * \param cause Asterisk hangup cause
 * \param start Call start time
 * \param connect Call connect time
 * \param end Call end time
 * \param release Who release first, 0 source, 1 destination
 * \param inqos Inbound QoS string
 * \param outqos Outbound QoS string
 * \return OSP_OK Success, OSP_FAILED Failed, OSP_ERROR Error
 */
static int osp_finish(
	int trans,
	int recorded,
	int cause,
	time_t start,
	time_t connect,
	time_t end,
	unsigned int release,
	const char* inqos,
	const char* outqos)
{
	int res;
	OSPEFAILREASON reason;
	time_t alert = 0;
	unsigned isPddInfoPresent = 0;
	unsigned pdd = 0;
	unsigned int dummy = 0;
	int error;

	if (trans == OSP_INVALID_HANDLE) {
		return OSP_FAILED;
	}

	OSPPTransactionSetRoleInfo(trans, OSPC_RSTATE_STOP, OSPC_RFORMAT_OSP, OSPC_RVENDOR_ASTERISK);

	if (!recorded) {
		reason = asterisk2osp(cause);
		OSPPTransactionRecordFailure(trans, reason);
	}

	osp_report_qos(trans, OSP_CALL_INBOUND, inqos);
	osp_report_qos(trans, OSP_CALL_OUTBOUND, outqos);

	error = OSPPTransactionReportUsage(trans,
		difftime(end, connect),
		start,
		end,
		alert,
		connect,
		isPddInfoPresent,
		pdd,
		release,
		NULL,
		OSP_DEF_INTSTATS,
		OSP_DEF_INTSTATS,
		OSP_DEF_INTSTATS,
		OSP_DEF_INTSTATS,
		&dummy,
		NULL);
	if (error == OSPC_ERR_NO_ERROR) {
		ast_debug(1, "OSP: Usage reported\n");
		res = OSP_OK;
	} else {
		ast_debug(1, "OSP: Unable to report usage, error '%d'\n", error);
		res = OSP_ERROR;
	}
	OSPPTransactionDelete(trans);

	return res;
}

/* OSP Application APIs */

/*!
 * \brief OSP Application OSPAuth
 * \param chan Channel
 * \param data Parameter
 * \return OSP_AST_OK Success, OSP_AST_ERROR Error
 */
static int ospauth_exec(
	struct ast_channel *chan,
	const char *data)
{
	int res;
	const char* provider = OSP_DEF_PROVIDER;
	struct varshead* headp;
	struct ast_var_t* current;
	const char* source = "";
	const char* token = "";
	int handle;
	unsigned int timelimit;
	char buffer[OSP_SIZE_INTSTR];
	const char* status;
	char* tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);

	tmp = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, tmp);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_debug(1, "OSPAuth: provider '%s'\n", provider);

	headp = ast_channel_varshead(chan);
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcmp(ast_var_name(current), "OSPINPEERIP")) {
			source = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINTOKEN")) {
			token = ast_var_value(current);
		}
	}

	ast_debug(1, "OSPAuth: source '%s'\n", source);
	ast_debug(1, "OSPAuth: token size '%zd'\n", strlen(token));

	res = osp_auth(provider, &handle, source,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
		ast_channel_exten(chan), token, &timelimit);
	if (res > 0) {
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
	ast_debug(1, "OSPAuth: OSPINHANDLE '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", timelimit);
	pbx_builtin_setvar_helper(chan, "OSPINTIMELIMIT", buffer);
	ast_debug(1, "OSPAuth: OSPINTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPAUTHSTATUS", status);
	ast_debug(1, "OSPAuth: %s\n", status);

	if(res != OSP_OK) {
		res = OSP_AST_ERROR;
	} else {
		res = OSP_AST_OK;
	}

	return res;
}

/*!
 * \brief OSP Application OSPLookup
 * \param chan Channel
 * \param data Parameter
 * \return OSP_AST_OK Success, OSP_AST_ERROR Error
 */
static int osplookup_exec(
	struct ast_channel* chan,
	const char * data)
{
	int res;
	const char* provider = OSP_DEF_PROVIDER;
	unsigned int callidtypes = OSP_CALLID_UNDEF;
	struct varshead* headp;
	struct ast_var_t* current;
	const char* actualsrc = "";
	const char* srcdev = "";
	const char* snetid = "";
	struct osp_npdata np;
	OSPE_OPERATOR_NAME type;
	struct osp_headers headers;
	unsigned int i;
	const char* cinfo[OSP_MAX_CUSTOMINFO] = { NULL };
	char buffer[OSP_SIZE_TOKSTR];
	struct osp_results results;
	const char* status;
	char* tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(exten);
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPLookup: Arg required, OSPLookup(exten[,provider[,options]])\n");
		return OSP_AST_ERROR;
	}

	tmp = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, tmp);

	ast_debug(1, "OSPLookup: exten '%s'\n", args.exten);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_debug(1, "OSPlookup: provider '%s'\n", provider);

	if (args.options) {
		if (strchr(args.options, 'h')) {
			callidtypes |= OSP_CALLID_H323;
		}
		if (strchr(args.options, 's')) {
			callidtypes |= OSP_CALLID_SIP;
		}
		if (strchr(args.options, 'i')) {
			callidtypes |= OSP_CALLID_IAX;
		}
	}
	ast_debug(1, "OSPLookup: call id types '%d'\n", callidtypes);

	results.inhandle = OSP_INVALID_HANDLE;
	results.intimelimit = OSP_DEF_TIMELIMIT;
	results.dest[0] = '\0';

	np.rn = "";
	np.cic = "";
	np.npdi = 0;
	for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
		np.opname[type] = "";
	}

	headers.rpiduser = "";
	headers.paiuser = "";
	headers.divuser = "";
	headers.divhost = "";
	headers.pciuser = "";

	headp = ast_channel_varshead(chan);
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcmp(ast_var_name(current), "OSPINACTUALSRC")) {
			actualsrc = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINPEERIP")) {
			srcdev = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINTECH")) {
			ast_copy_string(results.intech, ast_var_value(current), sizeof(results.intech));
		} else if (!strcmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &results.inhandle) != 1) {
				results.inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcmp(ast_var_name(current), "OSPINTIMELIMIT")) {
			if (sscanf(ast_var_value(current), "%30d", &results.intimelimit) != 1) {
				results.intimelimit = OSP_DEF_TIMELIMIT;
			}
		} else if (!strcmp(ast_var_name(current), "OSPINNETWORKID")) {
			snetid = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINNPRN")) {
			np.rn = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINNPCIC")) {
			np.cic = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINNPDI")) {
			if (ast_true(ast_var_value(current))) {
				np.npdi = 1;
			}
		} else if (!strcmp(ast_var_name(current), "OSPINSPID")) {
			np.opname[OSPC_OPNAME_SPID] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINOCN")) {
			np.opname[OSPC_OPNAME_OCN] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINSPN")) {
			np.opname[OSPC_OPNAME_SPN] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINALTSPN")) {
			np.opname[OSPC_OPNAME_ALTSPN] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINMCC")) {
			np.opname[OSPC_OPNAME_MCC] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINMNC")) {
			np.opname[OSPC_OPNAME_MNC] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINTOHOST")) {
			ast_copy_string(results.dest, ast_var_value(current), sizeof(results.dest));
		} else if (!strcmp(ast_var_name(current), "OSPINRPIDUSER")) {
			headers.rpiduser = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINPAIUSER")) {
			headers.paiuser = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINDIVUSER")) {
			headers.divuser = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINDIVHOST")) {
			headers.divhost = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINPCIUSER")) {
			headers.pciuser = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO1")) {
			cinfo[0] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO2")) {
			cinfo[1] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO3")) {
			cinfo[2] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO4")) {
			cinfo[3] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO5")) {
			cinfo[4] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO6")) {
			cinfo[5] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO7")) {
			cinfo[6] = ast_var_value(current);
		} else if (!strcmp(ast_var_name(current), "OSPINCUSTOMINFO8")) {
			cinfo[7] = ast_var_value(current);
		}
	}
	ast_debug(1, "OSPLookup: actual source device '%s'\n", actualsrc);
	ast_debug(1, "OSPLookup: source device '%s'\n", srcdev);
	ast_debug(1, "OSPLookup: OSPINTECH '%s'\n", results.intech);
	ast_debug(1, "OSPLookup: OSPINHANDLE '%d'\n", results.inhandle);
	ast_debug(1, "OSPLookup: OSPINTIMELIMIT '%d'\n", results.intimelimit);
	ast_debug(1, "OSPLookup: OSPINNETWORKID '%s'\n", snetid);
	ast_debug(1, "OSPLookup: OSPINNPRN '%s'\n", np.rn);
	ast_debug(1, "OSPLookup: OSPINNPCIC '%s'\n", np.cic);
	ast_debug(1, "OSPLookup: OSPINNPDI '%d'\n", np.npdi);
	ast_debug(1, "OSPLookup: OSPINSPID '%s'\n", np.opname[OSPC_OPNAME_SPID]);
	ast_debug(1, "OSPLookup: OSPINOCN '%s'\n", np.opname[OSPC_OPNAME_OCN]);
	ast_debug(1, "OSPLookup: OSPINSPN '%s'\n", np.opname[OSPC_OPNAME_SPN]);
	ast_debug(1, "OSPLookup: OSPINALTSPN '%s'\n", np.opname[OSPC_OPNAME_ALTSPN]);
	ast_debug(1, "OSPLookup: OSPINMCC '%s'\n", np.opname[OSPC_OPNAME_MCC]);
	ast_debug(1, "OSPLookup: OSPINMNC '%s'\n", np.opname[OSPC_OPNAME_MNC]);
	ast_debug(1, "OSPLookup: OSPINTOHOST '%s'\n", results.dest);
	ast_debug(1, "OSPLookup: OSPINRPIDUSER '%s'\n", headers.rpiduser);
	ast_debug(1, "OSPLookup: OSPINPAIUSER '%s'\n", headers.paiuser);
	ast_debug(1, "OSPLookup: OSPINDIVUSER '%s'\n", headers.divuser);
	ast_debug(1, "OSPLookup: OSPINDIVHOST'%s'\n", headers.divhost);
	ast_debug(1, "OSPLookup: OSPINPCIUSER '%s'\n", headers.pciuser);
	for (i = 0; i < OSP_MAX_CUSTOMINFO; i++) {
		if (!ast_strlen_zero(cinfo[i])) {
			ast_debug(1, "OSPLookup: OSPINCUSTOMINFO%d '%s'\n", i, cinfo[i]);
		}
	}

	if (ast_autoservice_start(chan) < 0) {
		return OSP_AST_ERROR;
	}

	res = osp_lookup(provider, callidtypes, actualsrc, srcdev,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
		args.exten, snetid, &np, &headers, cinfo, &results);
	if (res > 0) {
		status = AST_OSP_SUCCESS;
	} else {
		results.outtech[0] = '\0';
		results.dest[0] = '\0';
		results.calling[0] = '\0';
		results.called[0] = '\0';
		results.token[0] = '\0';
		results.networkid[0] = '\0';
		results.nprn[0] = '\0';
		results.npcic[0] = '\0';
		results.npdi = 0;
		for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
			results.opname[type][0] = '\0';
		}
		results.numdests = 0;
		results.outtimelimit = OSP_DEF_TIMELIMIT;
		results.outcallid.buf[0] = '\0';
		results.outcallid.len = 0;
		if (!res) {
			status = AST_OSP_FAILED;
		} else {
			status = AST_OSP_ERROR;
		}
	}

	snprintf(buffer, sizeof(buffer), "%d", results.outhandle);
	pbx_builtin_setvar_helper(chan, "OSPOUTHANDLE", buffer);
	ast_debug(1, "OSPLookup: OSPOUTHANDLE '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPOUTTECH", results.outtech);
	ast_debug(1, "OSPLookup: OSPOUTTECH '%s'\n", results.outtech);
	pbx_builtin_setvar_helper(chan, "OSPDESTINATION", results.dest);
	ast_debug(1, "OSPLookup: OSPDESTINATION '%s'\n", results.dest);
	pbx_builtin_setvar_helper(chan, "OSPOUTCALLING", results.calling);
	ast_debug(1, "OSPLookup: OSPOUTCALLING '%s'\n", results.calling);
	pbx_builtin_setvar_helper(chan, "OSPOUTCALLED", results.called);
	ast_debug(1, "OSPLookup: OSPOUTCALLED '%s'\n", results.called);
	pbx_builtin_setvar_helper(chan, "OSPOUTNETWORKID", results.networkid);
	ast_debug(1, "OSPLookup: OSPOUTNETWORKID '%s'\n", results.networkid);
	pbx_builtin_setvar_helper(chan, "OSPOUTNPRN", results.nprn);
	ast_debug(1, "OSPLookup: OSPOUTNPRN '%s'\n", results.nprn);
	pbx_builtin_setvar_helper(chan, "OSPOUTNPCIC", results.npcic);
	ast_debug(1, "OSPLookup: OSPOUTNPCIC '%s'\n", results.npcic);
	snprintf(buffer, sizeof(buffer), "%d", results.npdi);
	pbx_builtin_setvar_helper(chan, "OSPOUTNPDI", buffer);
	ast_debug(1, "OSPLookup: OSPOUTNPDI'%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPOUTSPID", results.opname[OSPC_OPNAME_SPID]);
	ast_debug(1, "OSPLookup: OSPOUTSPID '%s'\n", results.opname[OSPC_OPNAME_SPID]);
	pbx_builtin_setvar_helper(chan, "OSPOUTOCN", results.opname[OSPC_OPNAME_OCN]);
	ast_debug(1, "OSPLookup: OSPOUTOCN '%s'\n", results.opname[OSPC_OPNAME_OCN]);
	pbx_builtin_setvar_helper(chan, "OSPOUTSPN", results.opname[OSPC_OPNAME_SPN]);
	ast_debug(1, "OSPLookup: OSPOUTSPN '%s'\n", results.opname[OSPC_OPNAME_SPN]);
	pbx_builtin_setvar_helper(chan, "OSPOUTALTSPN", results.opname[OSPC_OPNAME_ALTSPN]);
	ast_debug(1, "OSPLookup: OSPOUTALTSPN '%s'\n", results.opname[OSPC_OPNAME_ALTSPN]);
	pbx_builtin_setvar_helper(chan, "OSPOUTMCC", results.opname[OSPC_OPNAME_MCC]);
	ast_debug(1, "OSPLookup: OSPOUTMCC '%s'\n", results.opname[OSPC_OPNAME_MCC]);
	pbx_builtin_setvar_helper(chan, "OSPOUTMNC", results.opname[OSPC_OPNAME_MNC]);
	ast_debug(1, "OSPLookup: OSPOUTMNC '%s'\n", results.opname[OSPC_OPNAME_MNC]);
	pbx_builtin_setvar_helper(chan, "OSPOUTTOKEN", results.token);
	ast_debug(1, "OSPLookup: OSPOUTTOKEN size '%zd'\n", strlen(results.token));
	snprintf(buffer, sizeof(buffer), "%d", results.numdests);
	pbx_builtin_setvar_helper(chan, "OSPDESTREMAILS", buffer);
	ast_debug(1, "OSPLookup: OSPDESTREMAILS '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", results.outtimelimit);
	pbx_builtin_setvar_helper(chan, "OSPOUTTIMELIMIT", buffer);
	ast_debug(1, "OSPLookup: OSPOUTTIMELIMIT '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", callidtypes);
	pbx_builtin_setvar_helper(chan, "OSPOUTCALLIDTYPES", buffer);
	ast_debug(1, "OSPLookup: OSPOUTCALLIDTYPES '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPLOOKUPSTATUS", status);
	ast_debug(1, "OSPLookup: %s\n", status);

	if (!strcasecmp(results.outtech, OSP_TECH_SIP)) {
		snprintf(buffer, sizeof(buffer), "%s/%s@%s", results.outtech, results.called, results.dest);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
		if (!ast_strlen_zero(results.token)) {
			snprintf(buffer, sizeof(buffer), "%s: %s", OSP_SIP_HEADER, results.token);
			pbx_builtin_setvar_helper(chan, "_SIPADDHEADER", buffer);
			ast_debug(1, "OSPLookup: SIPADDHEADER size '%zd'\n", strlen(buffer));
		}
	} else if (!strcasecmp(results.outtech, OSP_TECH_H323)) {
		if ((callidtypes & OSP_CALLID_H323) && (results.outcallid.len != 0)) {
			osp_uuid2str(results.outcallid.buf, buffer, sizeof(buffer));
		} else {
			buffer[0] = '\0';
		}
		pbx_builtin_setvar_helper(chan, "OSPOUTCALLID", buffer);
		snprintf(buffer, sizeof(buffer), "%s/%s@%s", results.outtech, results.called, results.dest);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
	} else if (!strcasecmp(results.outtech, OSP_TECH_IAX)) {
		snprintf(buffer, sizeof(buffer), "%s/%s/%s", results.outtech, results.dest, results.called);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
	} else if (!strcasecmp(results.outtech, OSP_TECH_SKYPE)) {
		snprintf(buffer, sizeof(buffer), "%s/%s", results.outtech, results.called);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
	}

	if (ast_autoservice_stop(chan) < 0) {
		return OSP_AST_ERROR;
	}

	if(res != OSP_OK) {
		res = OSP_AST_ERROR;
	} else {
		res = OSP_AST_OK;
	}

	return res;
}

/*!
 * \brief OSP Application OSPNext
 * \param chan Channel
 * \param data Parameter
 * \return OSP_AST_OK Success, OSP_AST_ERROR Error
 */
static int ospnext_exec(
	struct ast_channel* chan,
	const char * data)
{
	int res;
	const char* provider = OSP_DEF_PROVIDER;
	int cause = 0;
	struct varshead* headp;
	struct ast_var_t* current;
	struct osp_results results;
	OSPE_OPERATOR_NAME type;
	char buffer[OSP_SIZE_TOKSTR];
	unsigned int callidtypes = OSP_CALLID_UNDEF;
	const char* status;
	char* tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cause);
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPNext: Arg required, OSPNext(cause[,provider[,options]])\n");
		return OSP_AST_ERROR;
	}

	tmp = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, tmp);

	if (!ast_strlen_zero(args.cause) && sscanf(args.cause, "%30d", &cause) != 1) {
		cause = 0;
	}
	ast_debug(1, "OSPNext: cause '%d'\n", cause);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_debug(1, "OSPlookup: provider '%s'\n", provider);

	results.inhandle = OSP_INVALID_HANDLE;
	results.outhandle = OSP_INVALID_HANDLE;
	results.intimelimit = OSP_DEF_TIMELIMIT;
	results.numdests = 0;

	headp = ast_channel_varshead(chan);
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &results.inhandle) != 1) {
				results.inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcmp(ast_var_name(current), "OSPOUTHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &results.outhandle) != 1) {
				results.outhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcmp(ast_var_name(current), "OSPINTIMELIMIT")) {
			if (sscanf(ast_var_value(current), "%30d", &results.intimelimit) != 1) {
				results.intimelimit = OSP_DEF_TIMELIMIT;
			}
		} else if (!strcmp(ast_var_name(current), "OSPOUTCALLIDTYPES")) {
			if (sscanf(ast_var_value(current), "%30d", &callidtypes) != 1) {
				callidtypes = OSP_CALLID_UNDEF;
			}
		} else if (!strcmp(ast_var_name(current), "OSPDESTREMAILS")) {
			if (sscanf(ast_var_value(current), "%30d", &results.numdests) != 1) {
				results.numdests = 0;
			}
		}
	}
	ast_debug(1, "OSPNext: OSPINHANDLE '%d'\n", results.inhandle);
	ast_debug(1, "OSPNext: OSPOUTHANDLE '%d'\n", results.outhandle);
	ast_debug(1, "OSPNext: OSPINTIMELIMIT '%d'\n", results.intimelimit);
	ast_debug(1, "OSPNext: OSPOUTCALLIDTYPES '%d'\n", callidtypes);
	ast_debug(1, "OSPNext: OSPDESTREMAILS '%d'\n", results.numdests);

	if ((res = osp_next(provider, cause, &results)) > 0) {
		status = AST_OSP_SUCCESS;
	} else {
		results.outtech[0] = '\0';
		results.dest[0] = '\0';
		results.calling[0] = '\0';
		results.called[0] = '\0';
		results.token[0] = '\0';
		results.networkid[0] = '\0';
		results.nprn[0] = '\0';
		results.npcic[0] = '\0';
		results.npdi = 0;
		for (type = OSPC_OPNAME_START; type < OSPC_OPNAME_NUMBER; type++) {
			results.opname[type][0] = '\0';
		}
		results.numdests = 0;
		results.outtimelimit = OSP_DEF_TIMELIMIT;
		results.outcallid.buf[0] = '\0';
		results.outcallid.len = 0;
		if (!res) {
			status = AST_OSP_FAILED;
		} else {
			status = AST_OSP_ERROR;
		}
	}

	pbx_builtin_setvar_helper(chan, "OSPOUTTECH", results.outtech);
	ast_debug(1, "OSPNext: OSPOUTTECH '%s'\n", results.outtech);
	pbx_builtin_setvar_helper(chan, "OSPDESTINATION", results.dest);
	ast_debug(1, "OSPNext: OSPDESTINATION '%s'\n", results.dest);
	pbx_builtin_setvar_helper(chan, "OSPOUTCALLING", results.calling);
	ast_debug(1, "OSPNext: OSPOUTCALLING '%s'\n", results.calling);
	pbx_builtin_setvar_helper(chan, "OSPOUTCALLED", results.called);
	ast_debug(1, "OSPNext: OSPOUTCALLED'%s'\n", results.called);
	pbx_builtin_setvar_helper(chan, "OSPOUTNETWORKID", results.networkid);
	ast_debug(1, "OSPLookup: OSPOUTNETWORKID '%s'\n", results.networkid);
	pbx_builtin_setvar_helper(chan, "OSPOUTNPRN", results.nprn);
	ast_debug(1, "OSPLookup: OSPOUTNPRN '%s'\n", results.nprn);
	pbx_builtin_setvar_helper(chan, "OSPOUTNPCIC", results.npcic);
	ast_debug(1, "OSPLookup: OSPOUTNPCIC '%s'\n", results.npcic);
	snprintf(buffer, sizeof(buffer), "%d", results.npdi);
	pbx_builtin_setvar_helper(chan, "OSPOUTNPDI", buffer);
	ast_debug(1, "OSPLookup: OSPOUTNPDI'%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPOUTSPID", results.opname[OSPC_OPNAME_SPID]);
	ast_debug(1, "OSPLookup: OSPOUTSPID '%s'\n", results.opname[OSPC_OPNAME_SPID]);
	pbx_builtin_setvar_helper(chan, "OSPOUTOCN", results.opname[OSPC_OPNAME_OCN]);
	ast_debug(1, "OSPLookup: OSPOUTOCN '%s'\n", results.opname[OSPC_OPNAME_OCN]);
	pbx_builtin_setvar_helper(chan, "OSPOUTSPN", results.opname[OSPC_OPNAME_SPN]);
	ast_debug(1, "OSPLookup: OSPOUTSPN '%s'\n", results.opname[OSPC_OPNAME_SPN]);
	pbx_builtin_setvar_helper(chan, "OSPOUTALTSPN", results.opname[OSPC_OPNAME_ALTSPN]);
	ast_debug(1, "OSPLookup: OSPOUTALTSPN '%s'\n", results.opname[OSPC_OPNAME_ALTSPN]);
	pbx_builtin_setvar_helper(chan, "OSPOUTMCC", results.opname[OSPC_OPNAME_MCC]);
	ast_debug(1, "OSPLookup: OSPOUTMCC '%s'\n", results.opname[OSPC_OPNAME_MCC]);
	pbx_builtin_setvar_helper(chan, "OSPOUTMNC", results.opname[OSPC_OPNAME_MNC]);
	ast_debug(1, "OSPLookup: OSPOUTMNC '%s'\n", results.opname[OSPC_OPNAME_MNC]);
	pbx_builtin_setvar_helper(chan, "OSPOUTTOKEN", results.token);
	ast_debug(1, "OSPNext: OSPOUTTOKEN size '%zd'\n", strlen(results.token));
	snprintf(buffer, sizeof(buffer), "%d", results.numdests);
	pbx_builtin_setvar_helper(chan, "OSPDESTREMAILS", buffer);
	ast_debug(1, "OSPNext: OSPDESTREMAILS '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", results.outtimelimit);
	pbx_builtin_setvar_helper(chan, "OSPOUTTIMELIMIT", buffer);
	ast_debug(1, "OSPNext: OSPOUTTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPNEXTSTATUS", status);
	ast_debug(1, "OSPNext: %s\n", status);

	if (!strcasecmp(results.outtech, OSP_TECH_SIP)) {
		snprintf(buffer, sizeof(buffer), "%s/%s@%s", results.outtech, results.called, results.dest);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
		if (!ast_strlen_zero(results.token)) {
			snprintf(buffer, sizeof(buffer), "%s: %s", OSP_SIP_HEADER, results.token);
			pbx_builtin_setvar_helper(chan, "_SIPADDHEADER", buffer);
			ast_debug(1, "OSPLookup: SIPADDHEADER size '%zd'\n", strlen(buffer));
		}
	} else if (!strcasecmp(results.outtech, OSP_TECH_H323)) {
		if ((callidtypes & OSP_CALLID_H323) && (results.outcallid.len != 0)) {
			osp_uuid2str(results.outcallid.buf, buffer, sizeof(buffer));
		} else {
			buffer[0] = '\0';
		}
		pbx_builtin_setvar_helper(chan, "OSPOUTCALLID", buffer);
		snprintf(buffer, sizeof(buffer), "%s/%s@%s", results.outtech, results.called, results.dest);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
	} else if (!strcasecmp(results.outtech, OSP_TECH_IAX)) {
		snprintf(buffer, sizeof(buffer), "%s/%s/%s", results.outtech, results.dest, results.called);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
	} else if (!strcasecmp(results.outtech, OSP_TECH_SKYPE)) {
		snprintf(buffer, sizeof(buffer), "%s/%s", results.outtech, results.called);
		pbx_builtin_setvar_helper(chan, "OSPDIALSTR", buffer);
	}

	if(res != OSP_OK) {
		res = OSP_AST_ERROR;
	} else {
		res = OSP_AST_OK;
	}

	return res;
}

/*!
 * \brief OSP Application OSPFinish
 * \param chan Channel
 * \param data Parameter
 * \return OSP_AST_OK Success, OSP_AST_ERROR Error
 */
static int ospfinished_exec(
	struct ast_channel* chan,
	const char * data)
{
	int res = OSP_OK;
	int cause = 0;
	struct varshead* headp;
	struct ast_var_t* current;
	int inhandle = OSP_INVALID_HANDLE;
	int outhandle = OSP_INVALID_HANDLE;
	int recorded = 0;
	time_t start = 0, connect = 0, end = 0;
	unsigned int release;
	char buffer[OSP_SIZE_INTSTR];
	char inqos[OSP_SIZE_QOSSTR] = { 0 };
	char outqos[OSP_SIZE_QOSSTR] = { 0 };
	const char* status;
	char* tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cause);
		AST_APP_ARG(options);
	);

	tmp = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, tmp);

	headp = ast_channel_varshead(chan);
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &inhandle) != 1) {
				inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcmp(ast_var_name(current), "OSPOUTHANDLE")) {
			if (sscanf(ast_var_value(current), "%30d", &outhandle) != 1) {
				outhandle = OSP_INVALID_HANDLE;
			}
		} else if (!recorded &&
			(!strcmp(ast_var_name(current), "OSPAUTHSTATUS") ||
			!strcmp(ast_var_name(current), "OSPLOOKUPSTATUS") ||
			!strcmp(ast_var_name(current), "OSPNEXTSTATUS")))
		{
			if (strcmp(ast_var_value(current), AST_OSP_SUCCESS)) {
				recorded = 1;
			}
		} else if (!strcmp(ast_var_name(current), "OSPINAUDIOQOS")) {
			ast_copy_string(inqos, ast_var_value(current), sizeof(inqos));
		} else if (!strcmp(ast_var_name(current), "OSPOUTAUDIOQOS")) {
			ast_copy_string(outqos, ast_var_value(current), sizeof(outqos));
		}
	}
	ast_debug(1, "OSPFinish: OSPINHANDLE '%d'\n", inhandle);
	ast_debug(1, "OSPFinish: OSPOUTHANDLE '%d'\n", outhandle);
	ast_debug(1, "OSPFinish: recorded '%d'\n", recorded);
	ast_debug(1, "OSPFinish: OSPINAUDIOQOS '%s'\n", inqos);
	ast_debug(1, "OSPFinish: OSPOUTAUDIOQOS '%s'\n", outqos);

	if (!ast_strlen_zero(args.cause) && sscanf(args.cause, "%30d", &cause) != 1) {
		cause = 0;
	}
	ast_debug(1, "OSPFinish: cause '%d'\n", cause);

	if (!ast_tvzero(ast_channel_creationtime(chan))) {
		start = ast_channel_creationtime(chan).tv_sec;
	}
	if (!ast_tvzero(ast_channel_answertime(chan))) {
		connect = ast_channel_answertime(chan).tv_sec;
	}
	if (connect) {
		end = time(NULL);
	} else {
		end = connect;
	}

	ast_debug(1, "OSPFinish: start '%ld'\n", start);
	ast_debug(1, "OSPFinish: connect '%ld'\n", connect);
	ast_debug(1, "OSPFinish: end '%ld'\n", end);

	release = ast_check_hangup(chan) ? 0 : 1;

	if (osp_finish(outhandle, recorded, cause, start, connect, end, release, inqos, outqos) <= 0) {
		ast_debug(1, "OSPFinish: Unable to report usage for outbound call\n");
	}
	switch (cause) {
	case AST_CAUSE_NORMAL_CLEARING:
		break;
	default:
		cause = AST_CAUSE_NO_ROUTE_DESTINATION;
		break;
	}
	if (osp_finish(inhandle, recorded, cause, start, connect, end, release, inqos, outqos) <= 0) {
		ast_debug(1, "OSPFinish: Unable to report usage for inbound call\n");
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

	if(res != OSP_OK) {
		res = OSP_AST_ERROR;
	} else {
		res = OSP_AST_OK;
	}

	return res;
}

/* OSP Module APIs */

static int osp_unload(void)
{
	struct osp_provider* provider;
	struct osp_provider* next;

	if (osp_initialized) {
		ast_mutex_lock(&osp_lock);
		for (provider = osp_providers; provider; provider = next) {
			next = provider->next;
			OSPPProviderDelete(provider->handle, 0);
			ast_free(provider);
		}
		osp_providers = NULL;
		ast_mutex_unlock(&osp_lock);

		OSPPCleanup();

		osp_tokenformat = TOKEN_ALGO_SIGNED;
		osp_security = 0;
		osp_hardware = 0;
		osp_initialized = 0;
	}

	return 0;
}

static int osp_load(int reload)
{
	const char* cvar;
	unsigned int ivar;
	struct ast_config* cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int error = OSPC_ERR_NO_ERROR;

	if ((cfg = ast_config_load(OSP_CONFIG_FILE, config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", OSP_CONFIG_FILE);
		return 0;
	}

	if (cfg) {
		if (reload) {
			osp_unload();
		}

		if ((cvar = ast_variable_retrieve(cfg, OSP_GENERAL_CAT, "accelerate")) && ast_true(cvar)) {
			if ((error = OSPPInit(1)) != OSPC_ERR_NO_ERROR) {
				ast_log(LOG_WARNING, "OSP: Unable to enable hardware acceleration, error='%d'\n", error);
				OSPPInit(0);
			} else {
				osp_hardware = 1;
			}
		} else {
			OSPPInit(0);
		}
		ast_debug(1, "OSP: osp_hardware '%d'\n", osp_hardware);

		if ((cvar = ast_variable_retrieve(cfg, OSP_GENERAL_CAT, "securityfeatures")) && ast_true(cvar)) {
			osp_security = 1;
		}
		ast_debug(1, "OSP: osp_security '%d'\n", osp_security);

		if ((cvar = ast_variable_retrieve(cfg, OSP_GENERAL_CAT, "tokenformat"))) {
			if ((sscanf(cvar, "%30d", &ivar) == 1) &&
				((ivar == TOKEN_ALGO_SIGNED) || (ivar == TOKEN_ALGO_UNSIGNED) || (ivar == TOKEN_ALGO_BOTH)))
			{
				osp_tokenformat = ivar;
			} else {
				ast_log(LOG_WARNING, "tokenformat should be an integer from %d, %d or %d, not '%s'\n",
					TOKEN_ALGO_SIGNED, TOKEN_ALGO_UNSIGNED, TOKEN_ALGO_BOTH, cvar);
			}
		}
		ast_debug(1, "OSP: osp_tokenformat '%d'\n", osp_tokenformat);

		for (cvar = ast_category_browse(cfg, NULL); cvar != NULL; cvar = ast_category_browse(cfg, cvar)) {
			if (strcasecmp(cvar, OSP_GENERAL_CAT)) {
				osp_create_provider(cfg, cvar);
			}
		}

		osp_initialized = 1;

		ast_config_destroy(cfg);
	} else {
		ast_log(LOG_WARNING, "OSP: Unable to find configuration. OSP support disabled\n");
		return 0;
	}
	ast_debug(1, "OSP: osp_initialized '%d'\n", osp_initialized);

	return 1;
}

static char *handle_cli_osp_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	int found = 0;
	struct osp_provider* provider;
	const char* name = NULL;
	const char* tokenalgo;

	switch (cmd) {
	case CLI_INIT:
		e->command = "osp show";
		e->usage =
			"Usage: osp show\n"
			"       Displays information on Open Settlement Protocol support\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc < 2) || (a->argc > 3)) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc > 2) {
		name = a->argv[2];
	}

	if (!name) {
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
		ast_cli(a->fd, "OSP: %s/%s/%s/%s\n",
			osp_initialized ? "Initialized" : "Uninitialized",
			osp_hardware ? "Accelerated" : "Normal",
			osp_security ? "Enabled" : "Disabled",
			tokenalgo);
	}

	ast_mutex_lock(&osp_lock);
	for (provider = osp_providers; provider; provider = provider->next) {
		if (!name || !strcasecmp(provider->name, name)) {
			if (found) {
				ast_cli(a->fd, "\n");
			}
			ast_cli(a->fd, " == OSP Provider '%s' == \n", provider->name);
			if (osp_security) {
				ast_cli(a->fd, "Local Private Key: %s\n", provider->privatekey);
				ast_cli(a->fd, "Local Certificate: %s\n", provider->localcert);
				for (i = 0; i < provider->canum; i++) {
					ast_cli(a->fd, "CA Certificate %d:  %s\n", i + 1, provider->cacerts[i]);
				}
			}
			for (i = 0; i < provider->spnum; i++) {
				ast_cli(a->fd, "Service Point %d:   %s\n", i + 1, provider->spoints[i]);
			}
			ast_cli(a->fd, "Max Connections:   %d\n", provider->maxconnect);
			ast_cli(a->fd, "Retry Delay:       %d seconds\n", provider->retrydelay);
			ast_cli(a->fd, "Retry Limit:       %d\n", provider->retrylimit);
			ast_cli(a->fd, "Timeout:           %d milliseconds\n", provider->timeout);
			ast_cli(a->fd, "Source:            %s\n", strlen(provider->source) ? provider->source : "<unspecified>");
			ast_cli(a->fd, "Auth Policy        %d\n", provider->authpolicy);
			ast_cli(a->fd, "Default protocol   %s\n", provider->defprotocol);
			ast_cli(a->fd, "Work mode          %d\n", provider->workmode);
			ast_cli(a->fd, "Service type       %d\n", provider->srvtype);
			ast_cli(a->fd, "OSP Handle:        %d\n", provider->handle);
			found++;
		}
	}
	ast_mutex_unlock(&osp_lock);

	if (!found) {
		if (name) {
			ast_cli(a->fd, "Unable to find OSP provider '%s'\n", name);
		} else {
			ast_cli(a->fd, "No OSP providers configured\n");
		}
	}

	return CLI_SUCCESS;
}

/* OSPAuth() dialplan application */
static const char app1[] = "OSPAuth";

/* OSPLookup() dialplan application */
static const char app2[] = "OSPLookup";

/* OSPNext() dialplan application */
static const char app3[] = "OSPNext";

/* OSPFinish() dialplan application */
static const char app4[] = "OSPFinish";

static struct ast_cli_entry cli_osp[] = {
	AST_CLI_DEFINE(handle_cli_osp_show, "Displays OSF information")
};

static int load_module(void)
{
	int res;

	if (!osp_load(0))
		return AST_MODULE_LOAD_DECLINE;

	ast_cli_register_multiple(cli_osp, sizeof(cli_osp) / sizeof(struct ast_cli_entry));
	res = ast_register_application_xml(app1, ospauth_exec);
	res |= ast_register_application_xml(app2, osplookup_exec);
	res |= ast_register_application_xml(app3, ospnext_exec);
	res |= ast_register_application_xml(app4, ospfinished_exec);

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

	return res;
}

static int reload(void)
{
	osp_load(1);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Open Settlement Protocol Applications",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
