/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/astdb.h"
#include "asterisk/callerid.h"
#include "asterisk/privacy.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk.h"

int ast_privacy_check(char *dest, char *cid)
{
	char tmp[256] = "";
	char *trimcid = "";
	char *n, *l;
	int res;
	char key[256], result[256];
	if (cid)
		strncpy(tmp, cid, sizeof(tmp) - 1);
	ast_callerid_parse(tmp, &n, &l);
	if (l) {
		ast_shrink_phone_number(l);
		trimcid = l;
	}
	snprintf(key, sizeof(key), "%s/%s", dest, trimcid);
	res = ast_db_get("privacy", key, result, sizeof(result));
	if (!res) {
		if (!strcasecmp(result, "allow"))
			return AST_PRIVACY_ALLOW;
		if (!strcasecmp(result, "deny"))
			return AST_PRIVACY_DENY;
		if (!strcasecmp(result, "kill"))
			return AST_PRIVACY_KILL;
		if (!strcasecmp(result, "torture"))
			return AST_PRIVACY_TORTURE;
	}
	return AST_PRIVACY_UNKNOWN;
}

int ast_privacy_reset(char *dest)
{
	if (!dest)
		return -1;
	return ast_db_deltree("privacy", dest);
}

int ast_privacy_set(char *dest, char *cid, int status)
{
	char tmp[256] = "";
	char *trimcid = "";
	char *n, *l;
	int res;
	char key[256];
	if (cid)
		strncpy(tmp, cid, sizeof(tmp) - 1);
	ast_callerid_parse(tmp, &n, &l);
	if (l) {
		ast_shrink_phone_number(l);
		trimcid = l;
	}
	if (ast_strlen_zero(trimcid)) {
		/* Don't store anything for empty Caller*ID */
		return 0;
	}
	snprintf(key, sizeof(key), "%s/%s", dest, trimcid);
	if (status == AST_PRIVACY_UNKNOWN) 
		res = ast_db_del("privacy", key);
	else if (status == AST_PRIVACY_ALLOW)
		res = ast_db_put("privacy", key, "allow");
	else if (status == AST_PRIVACY_DENY)
		res = ast_db_put("privacy", key, "deny");
	else if (status == AST_PRIVACY_KILL)
		res = ast_db_put("privacy", key, "kill");
	else if (status == AST_PRIVACY_TORTURE)
		res = ast_db_put("privacy", key, "torture");
	else
		res = -1;
	return res;
}
