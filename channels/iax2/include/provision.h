/*
 * IAX Provisioning Protocol
 *
 * Sub-information elements
 *
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 */

/*! \file
 *  \brief IAX2 Provisioning protocol
 */

#ifndef __IAX2_PROVISION_H
#define __IAX2_PROVISION_H

#include "parser.h"

#define PROV_IE_USEDHCP 	1	/* Presense only */
#define PROV_IE_IPADDR		2	/* 32-bit */
#define PROV_IE_SUBNET		3	/* 32-bit */
#define PROV_IE_GATEWAY		4	/* 32-bit */
#define PROV_IE_PORTNO		5	/* 16-bit */
#define PROV_IE_USER		6	/* < 20 bytes */
#define PROV_IE_PASS		7	/* < 20 bytes */
#define PROV_IE_SERVERUSER	8	/* < 20 bytes */
#define PROV_IE_SERVERPASS	9	/* < 20 bytes */
#define PROV_IE_LANG		10	/* < 10 bytes */
#define PROV_IE_TOS			11	/* 8-bits */
#define PROV_IE_FLAGS		12	/* 32-bits */
#define PROV_IE_FORMAT		13	/* 32-bits */
#define PROV_IE_AESKEY		14	/* 128-bits */
#define PROV_IE_SERVERIP	15	/* 32-bits */
#define PROV_IE_SERVERPORT	16	/* 16-bits */
#define PROV_IE_NEWAESKEY	17	/* 128-bits */
#define PROV_IE_PROVVER		18	/* 32-bits */
#define PROV_IE_ALTSERVER	19	/* 32-bits */

#define PROV_FLAG_REGISTER		(1 << 0)
#define PROV_FLAG_SECURE		(1 << 1)
#define PROV_FLAG_HEARTBEAT		(1 << 2)
#define PROV_FLAG_DEBUG			(1 << 3)

#define PROV_FLAG_DIS_CALLERID	(1 << 4)	/* Caller-ID Disabled */
#define PROV_FLAG_DIS_CALLWAIT	(1 << 5)	/* Caller-ID / Call Waiting Disable */
#define PROV_FLAG_DIS_CIDCW		(1 << 6)	/* CID/CW Disabled */
#define PROV_FLAG_DIS_THREEWAY	(1 << 7)	/* Three-way calling, transfer disabled */

char *iax_provflags2str(char *buf, int buflen, unsigned int flags);
int iax_provision_reload(int reload);
int iax_provision_unload(void);
int iax_provision_build(struct iax_ie_data *provdata, unsigned int *signature, const char *template, int force);
int iax_provision_version(unsigned int *signature, const char *template, int force);
char *iax_prov_complete_template(const char *line, const char *word, int pos, int state);

#endif
