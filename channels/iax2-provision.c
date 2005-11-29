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

/*! \file
 * \brief IAX Provisioning Protocol 
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/options.h"
#include "asterisk/md5.h"
#include "asterisk/astdb.h"
#include "asterisk/utils.h"
#include "iax2.h"
#include "iax2-provision.h"
#include "iax2-parser.h"

#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST 0x02
#endif

static int provinit = 0;

struct iax_template {
	int dead;
	char name[80];
	char src[80];
	struct iax_template *next;
	char user[20];
	char pass[20];
	char lang[10];
	unsigned short port;
	unsigned int server;
	unsigned short serverport;
	unsigned int altserver;
	unsigned int flags;
	unsigned int format;
	int tos;	
} *templates;

static struct iax_flag {
	char *name;
	int value;
} iax_flags[] = {
	{ "register", PROV_FLAG_REGISTER },
	{ "secure", PROV_FLAG_SECURE },
	{ "heartbeat", PROV_FLAG_HEARTBEAT },
	{ "debug", PROV_FLAG_DEBUG },
	{ "disablecid", PROV_FLAG_DIS_CALLERID },
	{ "disablecw", PROV_FLAG_DIS_CALLWAIT },
	{ "disablecidcw", PROV_FLAG_DIS_CIDCW },
	{ "disable3way", PROV_FLAG_DIS_THREEWAY },
};

char *iax_provflags2str(char *buf, int buflen, unsigned int flags)
{
	int x;
	if (!buf || buflen < 1) {
		return(NULL);
	}
	buf[0] = '\0';
	for (x=0;x<sizeof(iax_flags) / sizeof(iax_flags[0]); x++) {
		if (flags & iax_flags[x].value){
			strncat(buf, iax_flags[x].name, buflen - strlen(buf) - 1);
			strncat(buf, ",", buflen - strlen(buf) - 1);
		}
	}
	if (strlen(buf)) 
		buf[strlen(buf) - 1] = '\0';
	else
		strncpy(buf, "none", buflen - 1);
	return buf;
}

static unsigned int iax_str2flags(const char *buf)
{
	int x;
	int len;
	int found;
	unsigned int flags = 0;
	char *e;
	while(buf && *buf) {
		e = strchr(buf, ',');
		if (e)
			len = e - buf;
		else
			len = 0;
		found = 0;
		for (x=0;x<sizeof(iax_flags) / sizeof(iax_flags[0]); x++) {
			if ((len && !strncasecmp(iax_flags[x].name, buf, len)) ||
			    (!len && !strcasecmp(iax_flags[x].name, buf))) {
				flags |= iax_flags[x].value;
				break;
			}
		}
		if (e) {
			buf = e + 1;
			while(*buf && (*buf < 33))
				buf++;
		} else
			break;
	}
	return flags;
}
AST_MUTEX_DEFINE_STATIC(provlock);

static struct iax_template *iax_template_find(const char *s, int allowdead)
{
	struct iax_template *cur;
	cur = templates;
	while(cur) {
		if (!strcasecmp(s, cur->name)) {
			if (!allowdead && cur->dead)
				cur = NULL;
			break;
		}
		cur = cur->next;
	}
	return cur;
}

char *iax_prov_complete_template(char *line, char *word, int pos, int state)
{
	struct iax_template *c;
	int which=0;
	char *ret;
	ast_mutex_lock(&provlock);
	c = templates;
	while(c) {
		if (!strncasecmp(word, c->name, strlen(word))) {
			if (++which > state)
				break;
		}
		c = c->next;
	}
	if (c) {
		ret = strdup(c->name);
	} else
		ret = NULL;
	ast_mutex_unlock(&provlock);
	return ret;
}

static unsigned int prov_ver_calc(struct iax_ie_data *provdata)
{
	struct MD5Context md5;
	unsigned int tmp[4];
	MD5Init(&md5);
	MD5Update(&md5, provdata->buf, provdata->pos);
	MD5Final((unsigned char *)tmp, &md5);
	return tmp[0] ^ tmp[1] ^ tmp[2] ^ tmp[3];
}

int iax_provision_build(struct iax_ie_data *provdata, unsigned int *signature, const char *template, int force)
{
	struct iax_template *cur;
	unsigned int sig;
	char tmp[40];
	memset(provdata, 0, sizeof(*provdata));
	ast_mutex_lock(&provlock);
	cur = iax_template_find(template, 1);
	/* If no match, try searching for '*' */
	if (!cur)
		cur = iax_template_find("*", 1);
	if (cur) {
		/* found it -- add information elements as appropriate */
		if (force || strlen(cur->user))
			iax_ie_append_str(provdata, PROV_IE_USER, cur->user);
		if (force || strlen(cur->pass))
			iax_ie_append_str(provdata, PROV_IE_PASS, cur->pass);
		if (force || strlen(cur->lang))
			iax_ie_append_str(provdata, PROV_IE_LANG, cur->lang);
		if (force || cur->port)
			iax_ie_append_short(provdata, PROV_IE_PORTNO, cur->port);
		if (force || cur->server)
			iax_ie_append_int(provdata, PROV_IE_SERVERIP, cur->server);
		if (force || cur->serverport)
			iax_ie_append_short(provdata, PROV_IE_SERVERPORT, cur->serverport);
		if (force || cur->altserver)
			iax_ie_append_int(provdata, PROV_IE_ALTSERVER, cur->altserver);
		if (force || cur->flags)
			iax_ie_append_int(provdata, PROV_IE_FLAGS, cur->flags);
		if (force || cur->format)
			iax_ie_append_int(provdata, PROV_IE_FORMAT, cur->format);
		if (force || cur->tos)
			iax_ie_append_byte(provdata, PROV_IE_TOS, cur->tos);
		
		/* Calculate checksum of message so far */
		sig = prov_ver_calc(provdata);
		if (signature)
			*signature = sig;
		/* Store signature */
		iax_ie_append_int(provdata, PROV_IE_PROVVER, sig);
		/* Cache signature for later verification so we need not recalculate all this */
		snprintf(tmp, sizeof(tmp), "v0x%08x", sig);
		ast_db_put("iax/provisioning/cache", template, tmp);
	} else
		ast_db_put("iax/provisioning/cache", template, "u");
	ast_mutex_unlock(&provlock);
	return cur ? 0 : -1;
}

int iax_provision_version(unsigned int *version, const char *template, int force)
{
	char tmp[80] = "";
	struct iax_ie_data ied;
	int ret=0;
	memset(&ied, 0, sizeof(ied));

	ast_mutex_lock(&provlock);
	ast_db_get("iax/provisioning/cache", template, tmp, sizeof(tmp));
	if (sscanf(tmp, "v%x", version) != 1) {
		if (strcmp(tmp, "u")) {
			ret = iax_provision_build(&ied, version, template, force);
			if (ret)
				ast_log(LOG_DEBUG, "Unable to create provisioning packet for '%s'\n", template);
		} else
			ret = -1;
	} else if (option_debug)
		ast_log(LOG_DEBUG, "Retrieved cached version '%s' = '%08x'\n", tmp, *version);
	ast_mutex_unlock(&provlock);
	return ret;
}

static int iax_template_parse(struct iax_template *cur, struct ast_config *cfg, char *s, char *def)
{
	struct ast_variable *v;
	int foundportno = 0;
	int foundserverportno = 0;
	int x;
	struct in_addr ia;
	struct hostent *hp;
	struct ast_hostent h;
	struct iax_template *src, tmp;
	char *t;
	if (def) {
		t = ast_variable_retrieve(cfg, s ,"template");
		src = NULL;
		if (t && strlen(t)) {
			src = iax_template_find(t, 0);
			if (!src)
				ast_log(LOG_WARNING, "Unable to find base template '%s' for creating '%s'.  Trying '%s'\n", t, s, def);
			else
				def = t;
		} 
		if (!src) {
			src = iax_template_find(def, 0);
			if (!src)
				ast_log(LOG_WARNING, "Unable to locate default base template '%s' for creating '%s', omitting.", def, s);
		}
		if (!src)
			return -1;
		ast_mutex_lock(&provlock);	
		/* Backup old data */
		memcpy(&tmp, cur, sizeof(tmp));
		/* Restore from src */
		memcpy(cur, src, sizeof(tmp));
		/* Restore important headers */
		memcpy(cur->name, tmp.name, sizeof(cur->name));
		cur->dead = tmp.dead;
		cur->next = tmp.next;
		ast_mutex_unlock(&provlock);	
	}
	if (def)
		strncpy(cur->src, def, sizeof(cur->src) - 1);
	else
		cur->src[0] = '\0';
	v = ast_variable_browse(cfg, s);
	while(v) {
		if (!strcasecmp(v->name, "port") || !strcasecmp(v->name, "serverport")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x > 0) && (x < 65535)) {
				if (!strcasecmp(v->name, "port")) {
					cur->port = x;
					foundportno = 1;
				} else {
					cur->serverport = x;
					foundserverportno = 1;
				}
			} else
				ast_log(LOG_WARNING, "Ignoring invalid %s '%s' for '%s' at line %d\n", v->name, v->value, s, v->lineno);
		} else if (!strcasecmp(v->name, "server") || !strcasecmp(v->name, "altserver")) {
			hp = ast_gethostbyname(v->value, &h);
			if (hp) {
				memcpy(&ia, hp->h_addr, sizeof(ia));
				if (!strcasecmp(v->name, "server"))
					cur->server = ntohl(ia.s_addr);
				else
					cur->altserver = ntohl(ia.s_addr);
			} else 
				ast_log(LOG_WARNING, "Ignoring invalid %s '%s' for '%s' at line %d\n", v->name, v->value, s, v->lineno);
		} else if (!strcasecmp(v->name, "codec")) {
			if ((x = ast_getformatbyname(v->value)) > 0) {
				cur->format = x;
			} else
				ast_log(LOG_WARNING, "Ignoring invalid codec '%s' for '%s' at line %d\n", v->value, s, v->lineno);
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%d", &x) == 1)
				cur->tos = x & 0xff;
			else if (!strcasecmp(v->value, "lowdelay"))
				cur->tos = IPTOS_LOWDELAY;
			else if (!strcasecmp(v->value, "throughput"))
				cur->tos = IPTOS_THROUGHPUT;
			else if (!strcasecmp(v->value, "reliability"))
				cur->tos = IPTOS_RELIABILITY;
			else if (!strcasecmp(v->value, "mincost"))
				cur->tos = IPTOS_MINCOST;
			else if (!strcasecmp(v->value, "none"))
				cur->tos = 0;
			else
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
		} else if (!strcasecmp(v->name, "user")) {
			strncpy(cur->user, v->value, sizeof(cur->user) - 1);
			if (strcmp(cur->user, v->value))
				ast_log(LOG_WARNING, "Truncating username from '%s' to '%s' for '%s' at line %d\n", v->value, cur->user, s, v->lineno);
		} else if (!strcasecmp(v->name, "pass")) {
			strncpy(cur->pass, v->value, sizeof(cur->pass) - 1);
			if (strcmp(cur->pass, v->value))
				ast_log(LOG_WARNING, "Truncating password from '%s' to '%s' for '%s' at line %d\n", v->value, cur->pass, s, v->lineno);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(cur->lang, v->value, sizeof(cur->lang) - 1);
			if (strcmp(cur->lang, v->value))
				ast_log(LOG_WARNING, "Truncating language from '%s' to '%s' for '%s' at line %d\n", v->value, cur->lang, s, v->lineno);
		} else if (!strcasecmp(v->name, "flags")) {
			cur->flags = iax_str2flags(v->value);
		} else if (!strncasecmp(v->name, "flags", 5) && strchr(v->name, '+')) {
			cur->flags |= iax_str2flags(v->value);
		} else if (!strncasecmp(v->name, "flags", 5) && strchr(v->name, '-')) {
			cur->flags &= ~iax_str2flags(v->value);
		} else if (strcasecmp(v->name, "template")) {
			ast_log(LOG_WARNING, "Unknown keyword '%s' in definition of '%s' at line %d\n", v->name, s, v->lineno);
		}
		v = v->next;
	}
	if (!foundportno)
		cur->port = IAX_DEFAULT_PORTNO;
	if (!foundserverportno)
		cur->serverport = IAX_DEFAULT_PORTNO;
	return 0;
}

static int iax_process_template(struct ast_config *cfg, char *s, char *def)
{
	/* Find an already existing one if there */
	struct iax_template *cur;
	int mallocd = 0;
	cur = templates;
	while(cur) {
		if (!strcasecmp(cur->name, s))
			break;
		cur = cur->next;
	}
	if (!cur) {
		mallocd = 1;
		cur = malloc(sizeof(struct iax_template));
		if (!cur) {
			ast_log(LOG_WARNING, "Out of memory!\n");
			return -1;
		}
		/* Initialize entry */
		memset(cur, 0, sizeof(*cur));
		strncpy(cur->name, s, sizeof(cur->name) - 1);
		cur->dead = 1;
	}
	if (!iax_template_parse(cur, cfg, s, def))
		cur->dead = 0;

	/* Link if we're mallocd */
	if (mallocd) {
		ast_mutex_lock(&provlock);
		cur->next = templates;
		templates = cur;
		ast_mutex_unlock(&provlock);
	}
	return 0;
}

static char show_provisioning_usage[] = 
"Usage: iax show provisioning [template]\n"
"       Lists all known IAX provisioning templates or a\n"
"       specific one if specified.\n";

static const char *ifthere(const char *s)
{
	if (strlen(s))
		return s;
	else
		return "<unspecified>";
}

static const char *iax_server(char *a, int alen, unsigned int addr)
{
	struct in_addr ia;
	if (!addr)
		return "<unspecified>";
	ia.s_addr = htonl(addr);
	return ast_inet_ntoa(a, alen, ia);
}


static int iax_show_provisioning(int fd, int argc, char *argv[])
{
	struct iax_template *cur;
	char iabuf[80];	/* Has to be big enough for 'flags' too */
	int found = 0;
	if ((argc != 3) && (argc != 4))
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&provlock);
	for (cur = templates;cur;cur = cur->next) {
		if ((argc == 3) || (!strcasecmp(argv[3], cur->name)))  {
			if (found) ast_cli(fd, "\n");
			ast_cli(fd, "== %s ==\n", cur->name);
			ast_cli(fd, "Base Templ:   %s\n", strlen(cur->src) ? cur->src : "<none>");
			ast_cli(fd, "Username:     %s\n", ifthere(cur->user));
			ast_cli(fd, "Secret:       %s\n", ifthere(cur->pass));
			ast_cli(fd, "Language:     %s\n", ifthere(cur->lang));
			ast_cli(fd, "Bind Port:    %d\n", cur->port);
			ast_cli(fd, "Server:       %s\n", iax_server(iabuf, sizeof(iabuf), cur->server));
			ast_cli(fd, "Server Port:  %d\n", cur->serverport);
			ast_cli(fd, "Alternate:    %s\n", iax_server(iabuf, sizeof(iabuf), cur->altserver));
			ast_cli(fd, "Flags:        %s\n", iax_provflags2str(iabuf, sizeof(iabuf), cur->flags));
			ast_cli(fd, "Format:       %s\n", ast_getformatname(cur->format));
			ast_cli(fd, "TOS:          %d\n", cur->tos);
			found++;
		}
	}
	ast_mutex_unlock(&provlock);
	if (!found) {
		if (argc == 3)
			ast_cli(fd, "No provisioning templates found\n");
		else
			ast_cli(fd, "No provisioning template matching '%s' found\n", argv[3]);
	}
	return RESULT_SUCCESS;
}

static struct ast_cli_entry  cli_show_provisioning = 
	{ { "iax2", "show", "provisioning", NULL }, iax_show_provisioning, "Show iax provisioning", show_provisioning_usage, iax_prov_complete_template };

static int iax_provision_init(void)
{
	ast_cli_register(&cli_show_provisioning);
	provinit = 1;
	return 0;
}

int iax_provision_unload(void)
{
	provinit = 0;
	ast_cli_unregister(&cli_show_provisioning);
	return 0;
}

int iax_provision_reload(void)
{
	struct ast_config *cfg;
	struct iax_template *cur, *prev, *next;
	char *cat;
	int found = 0;
	if (!provinit)
		iax_provision_init();
	/* Mark all as dead.  No need for locking */
	cur = templates;
	while(cur) {
		cur->dead = 1;
		cur = cur->next;
	}
	cfg = ast_config_load("iaxprov.conf");
	if (cfg) {
		/* Load as appropriate */
		cat = ast_category_browse(cfg, NULL);
		while(cat) {
			if (strcasecmp(cat, "general")) {
				iax_process_template(cfg, cat, found ? "default" : NULL);
				found++;
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Loaded provisioning template '%s'\n", cat);
			}
			cat = ast_category_browse(cfg, cat);
		}
		ast_config_destroy(cfg);
	} else
		ast_log(LOG_NOTICE, "No IAX provisioning configuration found, IAX provisioning disabled.\n");
	ast_mutex_lock(&provlock);
	/* Drop dead entries while locked */
	prev = NULL;
	cur = templates;
	while(cur) {
		next = cur->next;
		if (cur->dead) {
			if (prev)
				prev->next = next;
			else
				templates = next;
			free(cur);
		} else 
			prev = cur;
		cur = next;
	}
	ast_mutex_unlock(&provlock);
	/* Purge cached signature DB entries */
	ast_db_deltree("iax/provisioning/cache", NULL);
	return 0;
	
}
