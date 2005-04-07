/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * FreeTDS CDR logger
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 *
 * Table Structure for `cdr`
 *
 * Created on: 05/20/2004 16:16
 * Last changed on: 07/27/2004 20:01

CREATE TABLE [dbo].[cdr] (
	[accountcode] [varchar] (20) NULL ,
	[src] [varchar] (80) NULL ,
	[dst] [varchar] (80) NULL ,
	[dcontext] [varchar] (80) NULL ,
	[clid] [varchar] (80) NULL ,
	[channel] [varchar] (80) NULL ,
	[dstchannel] [varchar] (80) NULL ,
	[lastapp] [varchar] (80) NULL ,
	[lastdata] [varchar] (80) NULL ,
	[start] [datetime] NULL ,
	[answer] [datetime] NULL ,
	[end] [datetime] NULL ,
	[duration] [int] NULL ,
	[billsec] [int] NULL ,
	[disposition] [varchar] (20) NULL ,
	[amaflags] [varchar] (16) NULL ,
	[uniqueid] [varchar] (32) NULL
) ON [PRIMARY]

*/

#include <sys/types.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include "../asterisk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include <tds.h>
#include <tdsconvert.h>
#include <ctype.h>

#if !defined(TDS_INT_EXIT) 
#define TDS_PRE_0_62
#warning "You have older TDS, you should upgrade!"
#endif

#define DATE_FORMAT "%Y/%m/%d %T"

static char *desc = "MSSQL CDR Backend";
static char *name = "mssql";
static char *config = "cdr_tds.conf";

static char *hostname = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL, *charset = NULL, *language = NULL;

static int connected = 0;

AST_MUTEX_DEFINE_STATIC(tds_lock);

static TDSSOCKET *tds;
static TDSLOGIN *login;
static TDSCONTEXT *context;

static char *stristr(const char*, const char*);
static char *anti_injection(const char *, int);
static void get_date(char *, struct timeval);

static int mssql_connect(void);
static int mssql_disconnect(void);

static int tds_log(struct ast_cdr *cdr)
{
	char sqlcmd[2048], start[80], answer[80], end[80];
	char *accountcode, *src, *dst, *dcontext, *clid, *channel, *dstchannel, *lastapp, *lastdata, *uniqueid;
	int res = 0;
	int retried = 0;
#ifdef TDS_PRE_0_62
	TDS_INT result_type;
#endif

	ast_mutex_lock(&tds_lock);

	memset(sqlcmd, 0, 2048);

	accountcode = anti_injection(cdr->accountcode, 20);
	src = anti_injection(cdr->src, 80);
	dst = anti_injection(cdr->dst, 80);
	dcontext = anti_injection(cdr->dcontext, 80);
	clid = anti_injection(cdr->clid, 80);
	channel = anti_injection(cdr->channel, 80);
	dstchannel = anti_injection(cdr->dstchannel, 80);
	lastapp = anti_injection(cdr->lastapp, 80);
	lastdata = anti_injection(cdr->lastdata, 80);
	uniqueid = anti_injection(cdr->uniqueid, 32);

	get_date(start, cdr->start);
	get_date(answer, cdr->answer);
	get_date(end, cdr->end);

	sprintf(
		sqlcmd,
		"INSERT INTO cdr "
		"("
			"accountcode, "
			"src, "
			"dst, "
			"dcontext, "
			"clid, "
			"channel, "
			"dstchannel, "
			"lastapp, "
			"lastdata, "
			"start, "
			"answer, "
			"[end], "
			"duration, "
			"billsec, "
			"disposition, "
			"amaflags, "
			"uniqueid"
		") "
		"VALUES "
		"("
			"'%s', "	/* accountcode */
			"'%s', "	/* src */
			"'%s', "	/* dst */
			"'%s', "	/* dcontext */
			"'%s', "	/* clid */
			"'%s', "	/* channel */
			"'%s', "	/* dstchannel */
			"'%s', "	/* lastapp */
			"'%s', "	/* lastdata */
			"%s, "		/* start */
			"%s, "		/* answer */
			"%s, "		/* end */
			"%i, "		/* duration */
			"%i, "		/* billsec */
			"'%s', "	/* disposition */
			"'%s', "	/* amaflags */
			"'%s'"		/* uniqueid */
		")",
		accountcode,
		src,
		dst,
		dcontext,
		clid,
		channel,
		dstchannel,
		lastapp,
		lastdata,
		start,
		answer,
		end,
		cdr->duration,
		cdr->billsec,
		ast_cdr_disp2str(cdr->disposition),
		ast_cdr_flags2str(cdr->amaflags),
		uniqueid
	);

	do {
		if (!connected) {
			if (mssql_connect())
				ast_log(LOG_ERROR, "Failed to reconnect to SQL database.\n");
			else
				ast_log(LOG_WARNING, "Reconnected to SQL database.\n");

			retried = 1;	/* note that we have now tried */
		}

#ifdef TDS_PRE_0_62
		if (!connected || (tds_submit_query(tds, sqlcmd) != TDS_SUCCEED) || (tds_process_simple_query(tds, &result_type) != TDS_SUCCEED || result_type != TDS_CMD_SUCCEED))
#else
		if (!connected || (tds_submit_query(tds, sqlcmd) != TDS_SUCCEED) || (tds_process_simple_query(tds) != TDS_SUCCEED))
#endif
		{
			ast_log(LOG_ERROR, "Failed to insert Call Data Record into SQL database.\n");

			mssql_disconnect();	/* this is ok even if we are already disconnected */
		}
	} while (!connected && !retried);

	free(accountcode);
	free(src);
	free(dst);
	free(dcontext);
	free(clid);
	free(channel);
	free(dstchannel);
	free(lastapp);
	free(lastdata);
	free(uniqueid);

	ast_mutex_unlock(&tds_lock);

	return res;
}

/* Return the offset of one string within another.
   Copyright (C) 1994, 1996, 1997, 2000, 2001 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

/*
 * My personal strstr() implementation that beats most other algorithms.
 * Until someone tells me otherwise, I assume that this is the
 * fastest implementation of strstr() in C.
 * I deliberately chose not to comment it.  You should have at least
 * as much fun trying to understand it, as I had to write it :-).
 *
 * Stephen R. van den Berg, berg@pool.informatik.rwth-aachen.de	*/

static char *
stristr (phaystack, pneedle)
     const char *phaystack;
     const char *pneedle;
{
  typedef unsigned chartype;

  const unsigned char *haystack, *needle;
  chartype b;
  const unsigned char *rneedle;

  haystack = (const unsigned char *) phaystack;

  if ((b = toupper(*(needle = (const unsigned char *) pneedle))))
    {
      chartype c;
      haystack--;		/* possible ANSI violation */

      {
	chartype a;
	do
	  if (!(a = toupper(*++haystack)))
	    goto ret0;
	while (a != b);
      }

      if (!(c = toupper(*++needle)))
	goto foundneedle;
      ++needle;
      goto jin;

      for (;;)
	{
	  {
	    chartype a;
	    if (0)
	    jin:{
		if ((a = toupper(*++haystack)) == c)
		  goto crest;
	      }
	    else
	      a = toupper(*++haystack);
	    do
	      {
		for (; a != b; a = toupper(*++haystack))
		  {
		    if (!a)
		      goto ret0;
		    if ((a = toupper(*++haystack)) == b)
		      break;
		    if (!a)
		      goto ret0;
		  }
	      }
	    while ((a = toupper(*++haystack)) != c);
	  }
	crest:
	  {
	    chartype a;
	    {
	      const unsigned char *rhaystack;
	      if (toupper(*(rhaystack = haystack-- + 1)) == (a = toupper(*(rneedle = needle))))
		do
		  {
		    if (!a)
		      goto foundneedle;
		    if (toupper(*++rhaystack) != (a = toupper(*++needle)))
		      break;
		    if (!a)
		      goto foundneedle;
		  }
		while (toupper(*++rhaystack) == (a = toupper(*++needle)));
	      needle = rneedle;	/* took the register-poor aproach */
	    }
	    if (!a)
	      break;
	  }
	}
    }
foundneedle:
  return (char *) haystack;
ret0:
  return 0;
}

static char *anti_injection(const char *str, int len)
{
	/* Reference to http://www.nextgenss.com/papers/advanced_sql_injection.pdf */

	char *buf;
	char *buf_ptr, *srh_ptr;
	char *known_bad[] = {"select", "insert", "update", "delete", "drop", ";", "--", "\0"};
	int idx;

	if ((buf = malloc(len + 1)) == NULL)
	{
		ast_log(LOG_ERROR, "cdr_tds:  Out of memory error\n");
		return NULL;
	}
	memset(buf, 0, len);

	buf_ptr = buf;

	/* Escape single quotes */
	for (; *str && strlen(buf) < len; str++)
	{
		if (*str == '\'')
			*buf_ptr++ = '\'';
		*buf_ptr++ = *str;
	}
	*buf_ptr = '\0';

	/* Erase known bad input */
	for (idx=0; *known_bad[idx]; idx++)
	{
		while((srh_ptr = stristr(buf, known_bad[idx]))) /* fix me! */
		{
			memmove(srh_ptr, srh_ptr+strlen(known_bad[idx]), strlen(srh_ptr+strlen(known_bad[idx]))+1);
		}
	}

	return buf;
}

static void get_date(char *dateField, struct timeval tv)
{
	struct tm tm;
	time_t t;
	char buf[80];

	/* To make sure we have date variable if not insert null to SQL */
	if (tv.tv_sec && tv.tv_usec)
	{
		t = tv.tv_sec;
		localtime_r(&t, &tm);
		strftime(buf, 80, DATE_FORMAT, &tm);
		sprintf(dateField, "'%s'", buf);
	}
	else
	{
		strcpy(dateField, "null");
	}
}

char *description(void)
{
	return desc;
}

static int mssql_disconnect(void)
{
	if (tds) {
		tds_free_socket(tds);
		tds = NULL;
	}

	if (context) {
		tds_free_context(context);
		context = NULL;
	}

	if (login) {
		tds_free_login(login);
		login = NULL;
	}

	connected = 0;

	return 0;
}

static int mssql_connect(void)
{
	TDSCONNECTINFO *connection = NULL;
	char query[128];

	/* Connect to M$SQL Server */
	if (!(login = tds_alloc_login()))
	{
		ast_log(LOG_ERROR, "tds_alloc_login() failed.\n");
		return -1;
	}
	
	tds_set_server(login, hostname);
	tds_set_user(login, dbuser);
	tds_set_passwd(login, password);
	tds_set_app(login, "TSQL");
	tds_set_library(login, "TDS-Library");
#ifndef TDS_PRE_0_62
	tds_set_client_charset(login, charset);
#endif
	tds_set_language(login, language);
	tds_set_packet(login, 512);
	tds_set_version(login, 7, 0);

	if (!(context = tds_alloc_context()))
	{
		ast_log(LOG_ERROR, "tds_alloc_context() failed.\n");
		goto connect_fail;
	}

	if (!(tds = tds_alloc_socket(context, 512))) {
		ast_log(LOG_ERROR, "tds_alloc_socket() failed.\n");
		goto connect_fail;
	}

	tds_set_parent(tds, NULL);
	connection = tds_read_config_info(tds, login, context->locale);
	if (!connection)
	{
		ast_log(LOG_ERROR, "tds_read_config() failed.\n");
		goto connect_fail;
	}

	if (tds_connect(tds, connection) == TDS_FAIL)
	{
		ast_log(LOG_ERROR, "Failed to connect to MSSQL server.\n");
		tds = NULL;	/* freed by tds_connect() on error */
		tds_free_connect(connection);
		connection = NULL;
		goto connect_fail;
	}
	tds_free_connect(connection);
	connection = NULL;

	sprintf(query, "USE %s", dbname);
#ifdef TDS_PRE_0_62
	if ((tds_submit_query(tds, query) != TDS_SUCCEED) || (tds_process_simple_query(tds, &result_type) != TDS_SUCCEED || result_type != TDS_CMD_SUCCEED))
#else
	if ((tds_submit_query(tds, query) != TDS_SUCCEED) || (tds_process_simple_query(tds) != TDS_SUCCEED))
#endif
	{
		ast_log(LOG_ERROR, "Could not change database (%s)\n", dbname);
		goto connect_fail;
	}

	connected = 1;
	return 0;

connect_fail:
	mssql_disconnect();
	return -1;
}

static int tds_unload_module(void)
{
	mssql_disconnect();

	ast_cdr_unregister(name);

	if (hostname) free(hostname);
	if (dbname) free(dbname);
	if (dbuser) free(dbuser);
	if (password) free(password);
	if (charset) free(charset);
	if (language) free(language);

	return 0;
}

static int tds_load_module(void)
{
	int res = 0;
	struct ast_config *cfg;
	struct ast_variable *var;
	char *ptr = NULL;
#ifdef TDS_PRE_0_62
	TDS_INT result_type;
#endif

	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config for MSSQL CDR's: %s\n", config);
		return 0;
	}

	var = ast_variable_browse(cfg, "global");
	if (!var) /* nothing configured */
		return 0;

	ptr = ast_variable_retrieve(cfg, "global", "hostname");
	if (ptr)
		hostname = strdup(ptr);
	else
		ast_log(LOG_ERROR,"Database server hostname not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "dbname");
	if (ptr)
		dbname = strdup(ptr);
	else
		ast_log(LOG_ERROR,"Database dbname not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "user");
	if (ptr)
		dbuser = strdup(ptr);
	else
		ast_log(LOG_ERROR,"Database dbuser not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "password");
	if (ptr)
		password = strdup(ptr);
	else
		ast_log(LOG_ERROR,"Database password not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "charset");
	if (ptr)
		charset = strdup(ptr);
	else
		charset = strdup("iso_1");

	ptr = ast_variable_retrieve(cfg, "global", "language");
	if (ptr)
		language = strdup(ptr);
	else
		language = strdup("us_english");

	ast_config_destroy(cfg);

	mssql_connect();

	/* Register MSSQL CDR handler */
	res = ast_cdr_register(name, desc, tds_log);
	if (res)
	{
		ast_log(LOG_ERROR, "Unable to register MSSQL CDR handling\n");
	}

	return res;
}

int reload(void)
{
	tds_unload_module();
	return tds_load_module();
}

int load_module(void)
{
	return tds_load_module();
}

int unload_module(void)
{
	return tds_unload_module();
}

int usecount(void)
{
	/* Simplistic use count */
	if (ast_mutex_trylock(&tds_lock)) {
		return 1;
	} else {
		ast_mutex_unlock(&tds_lock);
		return 0;
	}
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
