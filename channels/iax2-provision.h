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

#include "iax2-parser.h"

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

extern char *iax_provflags2str(char *buf, int buflen, unsigned int flags);
extern int iax_provision_reload(void);
extern int iax_provision_unload(void);
extern int iax_provision_build(struct iax_ie_data *provdata, unsigned int *signature, const char *template, int force);
extern int iax_provision_version(unsigned int *signature, const char *template, int force);
extern char *iax_prov_complete_template(char *line, char *word, int pos, int state);
