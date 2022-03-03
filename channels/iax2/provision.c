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

/*! \file
 *
 * \brief IAX Provisioning Protocol
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/md5.h"
#include "asterisk/astdb.h"
#include "asterisk/utils.h"
#include "asterisk/acl.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_compatibility.h"

#include "include/iax2.h"
#include "include/provision.h"
#include "include/parser.h"

static int provinit = 0;

struct iax_template {
	int dead;
	char name[80];
	char src[80];
	char user[20];
	char pass[20];
	char lang[10];
	unsigned short port;
	unsigned int server;
	unsigned short serverport;
	unsigned int altserver;
	unsigned int flags;
	iax2_format format;
	unsigned int tos;
	AST_LIST_ENTRY(iax_template) list;
};

static AST_LIST_HEAD_NOLOCK_STATIC(templates, iax_template);

AST_MUTEX_DEFINE_STATIC(provlock);

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

	if (!buf || buflen < 1)
		return NULL;

	buf[0] = '\0';

	for (x = 0; x < ARRAY_LEN(iax_flags); x++) {
		if (flags & iax_flags[x].value){
			strncat(buf, iax_flags[x].name, buflen - strlen(buf) - 1);
			strncat(buf, ",", buflen - strlen(buf) - 1);
		}
	}

	if (!ast_strlen_zero(buf))
		buf[strlen(buf) - 1] = '\0';
	else
		strncpy(buf, "none", buflen - 1);

	return buf;
}

static unsigned int iax_str2flags(const char *buf)
{
	int x;
	int len;
	unsigned int flags = 0;
	char *e;
	while(buf && *buf) {
		e = strchr(buf, ',');
		if (e)
			len = e - buf;
		else
			len = 0;
		for (x = 0; x < ARRAY_LEN(iax_flags); x++) {
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

static void iax_template_copy(struct iax_template *dst, struct iax_template *src)
{
	if (!dst || !src) {
		return;
	}

	dst->dead = src->dead;
	ast_copy_string(dst->name, src->name, sizeof(dst->name));
	ast_copy_string(dst->src, src->src, sizeof(dst->src));
	ast_copy_string(dst->user, src->user, sizeof(dst->user));
	ast_copy_string(dst->pass, src->pass, sizeof(dst->pass));
	ast_copy_string(dst->lang, src->lang, sizeof(dst->lang));
	dst->port = src->port;
	dst->server = src->server;
	dst->altserver = src->altserver;
	dst->flags = src->flags;
	dst->format = src->format;
	dst->tos = src->tos;
}

static struct iax_template *iax_template_find(const char *s, int allowdead)
{
	struct iax_template *cur;

	AST_LIST_TRAVERSE(&templates, cur, list) {
		if (!strcasecmp(s, cur->name)) {
			if (!allowdead && cur->dead) {
				cur = NULL;
			}
			break;
		}
	}

	return cur;
}

char *iax_prov_complete_template(const char *line, const char *word, int pos, int state)
{
	struct iax_template *c;
	int which=0;
	char *ret = NULL;
	int wordlen = strlen(word);

	if (pos == 3) {
		ast_mutex_lock(&provlock);
		AST_LIST_TRAVERSE(&templates, c, list) {
			if (!strncasecmp(word, c->name, wordlen) && ++which > state) {
				ret = ast_strdup(c->name);
				break;
			}
		}
		ast_mutex_unlock(&provlock);
	}
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
	if (ast_db_get("iax/provisioning/cache", template, tmp, sizeof(tmp))) {
		ast_log(LOG_ERROR, "ast_db_get failed to retrieve iax/provisioning/cache/%s\n", template);
	}
	if (sscanf(tmp, "v%30x", version) != 1) {
		if (strcmp(tmp, "u")) {
			ret = iax_provision_build(&ied, version, template, force);
			if (ret)
				ast_debug(1, "Unable to create provisioning packet for '%s'\n", template);
		} else
			ret = -1;
	} else
		ast_debug(1, "Retrieved cached version '%s' = '%08x'\n", tmp, *version);
	ast_mutex_unlock(&provlock);
	return ret;
}

static int iax_template_parse(struct iax_template *cur, struct ast_config *cfg, const char *s, const char *def)
{
	struct ast_variable *v;
	int foundportno = 0;
	int foundserverportno = 0;
	int x;
	struct iax_template *src, tmp;
	const char *t;
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
				ast_log(LOG_WARNING, "Unable to locate default base template '%s' for creating '%s', omitting.\n", def, s);
		}
		if (!src)
			return -1;
		ast_mutex_lock(&provlock);
		/* Backup old data */
		iax_template_copy(&tmp, cur);
		/* Restore from src */
		iax_template_copy(cur, src);
		/* Restore important headers */
		memcpy(cur->name, tmp.name, sizeof(cur->name));
		cur->dead = tmp.dead;
		ast_mutex_unlock(&provlock);
	}
	if (def)
		ast_copy_string(cur->src, def, sizeof(cur->src));
	else
		cur->src[0] = '\0';
	v = ast_variable_browse(cfg, s);
	while(v) {
		if (!strcasecmp(v->name, "port") || !strcasecmp(v->name, "serverport")) {
			if ((sscanf(v->value, "%5d", &x) == 1) && (x > 0) && (x < 65535)) {
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
			struct ast_sockaddr addr = { {0,} };
			if (ast_sockaddr_resolve_first_af(&addr, v->value, PARSE_PORT_FORBID, AF_INET)) {
				ast_log(LOG_WARNING, "Ignoring invalid %s '%s' for '%s' at line %d\n", v->name, v->value, s, v->lineno);
			} else {
				if (!strcasecmp(v->name, "server"))
					cur->server = ast_sockaddr_ipv4(&addr);
				else
					cur->altserver = ast_sockaddr_ipv4(&addr);
			}
		} else if (!strcasecmp(v->name, "codec")) {
			struct ast_format *tmpfmt;
			if ((tmpfmt = ast_format_cache_get(v->value))) {
				cur->format = ast_format_compatibility_format2bitfield(tmpfmt);
				ao2_ref(tmpfmt, -1);
			} else
				ast_log(LOG_WARNING, "Ignoring invalid codec '%s' for '%s' at line %d\n", v->value, s, v->lineno);
		} else if (!strcasecmp(v->name, "tos")) {
			if (ast_str2tos(v->value, &cur->tos))
				ast_log(LOG_WARNING, "Invalid tos value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "user")) {
			ast_copy_string(cur->user, v->value, sizeof(cur->user));
			if (strcmp(cur->user, v->value))
				ast_log(LOG_WARNING, "Truncating username from '%s' to '%s' for '%s' at line %d\n", v->value, cur->user, s, v->lineno);
		} else if (!strcasecmp(v->name, "pass")) {
			ast_copy_string(cur->pass, v->value, sizeof(cur->pass));
			if (strcmp(cur->pass, v->value))
				ast_log(LOG_WARNING, "Truncating password from '%s' to '%s' for '%s' at line %d\n", v->value, cur->pass, s, v->lineno);
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(cur->lang, v->value, sizeof(cur->lang));
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

	cur = iax_template_find(s, 1 /* allow dead */);
	if (!cur) {
		mallocd = 1;
		cur = ast_calloc(1, sizeof(*cur));
		if (!cur) {
			ast_log(LOG_WARNING, "Out of memory!\n");
			return -1;
		}
		/* Initialize entry */
		ast_copy_string(cur->name, s, sizeof(cur->name));
		cur->dead = 1;
	}
	if (!iax_template_parse(cur, cfg, s, def))
		cur->dead = 0;

	/* Link if we're mallocd */
	if (mallocd) {
		ast_mutex_lock(&provlock);
		AST_LIST_INSERT_HEAD(&templates, cur, list);
		ast_mutex_unlock(&provlock);
	}
	return 0;
}

static const char *ifthere(const char *s)
{
	if (strlen(s))
		return s;
	else
		return "<unspecified>";
}

static const char *iax_server(unsigned int addr)
{
	struct in_addr ia;

	if (!addr)
		return "<unspecified>";

	ia.s_addr = htonl(addr);

	return ast_inet_ntoa(ia);
}


static char *iax_show_provisioning(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct iax_template *cur;
	char server[INET_ADDRSTRLEN];
	char alternate[INET_ADDRSTRLEN];
	char flags[80];	/* Has to be big enough for 'flags' too */
	int found = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show provisioning";
		e->usage =
			"Usage: iax2 show provisioning [template]\n"
			"       Lists all known IAX provisioning templates or a\n"
			"       specific one if specified.\n";
		return NULL;
	case CLI_GENERATE:
		return iax_prov_complete_template(a->line, a->word, a->pos, a->n);
	}

	if ((a->argc != 3) && (a->argc != 4))
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&provlock);
	AST_LIST_TRAVERSE(&templates, cur, list) {
		if ((a->argc == 3) || (!strcasecmp(a->argv[3], cur->name)))  {
			if (found)
				ast_cli(a->fd, "\n");
			ast_copy_string(server, iax_server(cur->server), sizeof(server));
			ast_copy_string(alternate, iax_server(cur->altserver), sizeof(alternate));
			ast_cli(a->fd, "== %s ==\n", cur->name);
			ast_cli(a->fd, "Base Templ:   %s\n", strlen(cur->src) ? cur->src : "<none>");
			ast_cli(a->fd, "Username:     %s\n", ifthere(cur->user));
			ast_cli(a->fd, "Secret:       %s\n", ifthere(cur->pass));
			ast_cli(a->fd, "Language:     %s\n", ifthere(cur->lang));
			ast_cli(a->fd, "Bind Port:    %d\n", cur->port);
			ast_cli(a->fd, "Server:       %s\n", server);
			ast_cli(a->fd, "Server Port:  %d\n", cur->serverport);
			ast_cli(a->fd, "Alternate:    %s\n", alternate);
			ast_cli(a->fd, "Flags:        %s\n", iax_provflags2str(flags, sizeof(flags), cur->flags));
			ast_cli(a->fd, "Format:       %s\n", iax2_getformatname(cur->format));
			ast_cli(a->fd, "TOS:          0x%x\n", cur->tos);
			found++;
		}
	}
	ast_mutex_unlock(&provlock);
	if (!found) {
		if (a->argc == 3)
			ast_cli(a->fd, "No provisioning templates found\n");
		else
			ast_cli(a->fd, "No provisioning template matching '%s' found\n", a->argv[3]);
	}
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_iax2_provision[] = {
	AST_CLI_DEFINE(iax_show_provisioning, "Display iax provisioning"),
};

static int iax_provision_init(void)
{
	ast_cli_register_multiple(cli_iax2_provision, sizeof(cli_iax2_provision) / sizeof(struct ast_cli_entry));
	provinit = 1;
	return 0;
}

static void iax_provision_free_templates(int dead)
{
	struct iax_template *cur;

	/* Drop dead or not (depending on dead) entries while locked */
	ast_mutex_lock(&provlock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&templates, cur, list) {
		if ((dead && cur->dead) || !dead) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(cur);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	ast_mutex_unlock(&provlock);
}

int iax_provision_unload(void)
{
	provinit = 0;
	ast_cli_unregister_multiple(cli_iax2_provision, sizeof(cli_iax2_provision) / sizeof(struct ast_cli_entry));
	iax_provision_free_templates(0 /* Remove all templates. */);

	return 0;
}

int iax_provision_reload(int reload)
{
	struct ast_config *cfg;
	struct iax_template *cur;
	char *cat;
	int found = 0;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	if (!provinit)
		iax_provision_init();

	cfg = ast_config_load2("iaxprov.conf", "chan_iax2", config_flags);
	if (cfg != NULL && cfg != CONFIG_STATUS_FILEUNCHANGED && cfg != CONFIG_STATUS_FILEINVALID) {
		/* Mark all as dead.  No need for locking */
		AST_LIST_TRAVERSE(&templates, cur, list) {
			cur->dead = 1;
		}

		/* Load as appropriate */
		cat = ast_category_browse(cfg, NULL);
		while(cat) {
			if (strcasecmp(cat, "general")) {
				iax_process_template(cfg, cat, found ? "default" : NULL);
				found++;
				ast_verb(3, "Loaded provisioning template '%s'\n", cat);
			}
			cat = ast_category_browse(cfg, cat);
		}
		ast_config_destroy(cfg);
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;
	else
		ast_log(LOG_NOTICE, "No IAX provisioning configuration found, IAX provisioning disabled.\n");

	iax_provision_free_templates(1 /* remove only marked as dead */);

	/* Purge cached signature DB entries */
	ast_db_deltree("iax/provisioning/cache", NULL);
	return 0;
}
