/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MySQL CDR logger 
 * 
 * James Sharp <jsharp@psychoses.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 */


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

#include <mysql.h>

#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "MySQL CDR Backend";
static char *name = "mysql";
static char *config = "cdr_mysql.conf";

static MYSQL *mysql;

static int mysql_log(struct ast_cdr *cdr)
{
  struct tm *tm;
  struct timeval *tv;
  struct timezone *tz;
  char *sqlcmd, *timestr;
  time_t t;


  tv = (struct timeval *)malloc(sizeof(struct timeval));
  tz = (struct timezone *)malloc(sizeof(struct timezone));
  sqlcmd = (char *)malloc(2048);
  timestr = (char*)malloc(128);
  memset(sqlcmd,0,2048);


  gettimeofday(tv,tz);
  t = tv->tv_sec;
  tm = localtime(&t);
  strftime(timestr,128,DATE_FORMAT,tm);
  

  ast_log(LOG_DEBUG,"cdr_mysql: inserting a CDR record.\n");
  sprintf(sqlcmd,"insert into cdr values ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,%i,%i,'%s')",timestr,cdr->clid,cdr->src, cdr->dst, cdr->dcontext,cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata,cdr->duration,cdr->billsec,cdr->disposition,cdr->amaflags, cdr->accountcode);
  
  ast_log(LOG_DEBUG,"cdr_mysql: SQL command as follows:  %s\n",sqlcmd);
  
  if (mysql_real_query(mysql,sqlcmd,strlen(sqlcmd)))
  {
      ast_log(LOG_ERROR,"Failed to insert into database.");
      free(sqlcmd);
      return -1;
  }
  free(sqlcmd);
  return 0;
}



char *description(void)
{
  return desc;
}

int unload_module(void)
{ 

  mysql_close(mysql);
  ast_cdr_unregister(name);
  return 0;
}

int load_module(void)
{
  int res;
  struct ast_config *cfg;
  struct ast_variable *var;

  char *hostname, *dbname, *dbuser, *password;

  cfg = ast_load(config);
  if (!cfg) {
    ast_log(LOG_WARNING, "Unable to load config for mysql CDR's: %s\n", config);
    return 0;
  }
  
  var = ast_variable_browse(cfg, "global");
  if (!var) {
    /* nothing configured */
    return 0;
  }

  hostname = ast_variable_retrieve(cfg,"global","hostname");
  dbname = ast_variable_retrieve(cfg,"global","dbname");
  dbuser = ast_variable_retrieve(cfg,"global","user");
  password = ast_variable_retrieve(cfg,"global","password");
  ast_log(LOG_DEBUG,"cdr_mysql: got hostname of %s\n",hostname);
  ast_log(LOG_DEBUG,"cdr_mysql: got user of %s\n",dbuser);
  ast_log(LOG_DEBUG,"cdr_mysql: got dbname of %s\n",dbname);
  ast_log(LOG_DEBUG,"cdr_mysql: got password of %s\n",password);

  if (hostname == NULL)
    {
      ast_log(LOG_ERROR,"Database server hostname not specified.\n");
      return -1;
    }
  if (dbuser == NULL)
    {
      ast_log(LOG_ERROR,"Database dbuser not specified.\n");
      return -1;
    }
  if (dbname == NULL)
    {
      ast_log(LOG_ERROR,"Database dbname not specified.\n");
      return -1;
    }
  if (password == NULL)
    {
      ast_log(LOG_ERROR,"Database password not specified.\n");
      return -1;
    }


  mysql = mysql_init(NULL);

  mysql = mysql_real_connect(mysql, hostname, dbuser, password, dbname, 0, NULL, 0);

  if (mysql == NULL) {
    ast_log(LOG_ERROR, "Failed to connect to mysql database.\n");
    return -1;
  } else {
    ast_log(LOG_DEBUG,"Successfully connected to MySQL database.\n");
  }
  

  res = ast_cdr_register(name, desc, mysql_log);
  if (res) {
    ast_log(LOG_ERROR, "Unable to register MySQL CDR handling\n");
  }
  return res;
}

int reload(void)
{

  struct ast_config *cfg;
  struct ast_variable *var;

  char *hostname, *dbname, *password, *dbuser;
  
  mysql_close(mysql);


  cfg = ast_load(config);
  if (!cfg) {
    ast_log(LOG_WARNING, "Unable to load MySQL CDR config %s\n", config);
    return 0;
  }
  
  var = ast_variable_browse(cfg, "global");
  if (!var) {
    /* nothing configured */
    return 0;
  }

  hostname = ast_variable_retrieve(cfg,"global","hostname");
  dbname = ast_variable_retrieve(cfg,"global","dbname");
  dbuser = ast_variable_retrieve(cfg,"global","user");
  password = ast_variable_retrieve(cfg,"global","password");
  ast_log(LOG_DEBUG,"cdr_mysql: got hostname of %s\n",hostname);
  ast_log(LOG_DEBUG,"cdr_mysql: got dbname of %s\n",dbname);
  ast_log(LOG_DEBUG,"cdr_mysql: got dbuser of %s\n",dbuser);
  ast_log(LOG_DEBUG,"cdr_mysql: got password of %s\n",password);

  if (hostname == NULL)
    {
      ast_log(LOG_ERROR,"Database server hostname not specified.\n");
      return -1;
    }
  if (dbname == NULL)
    {
      ast_log(LOG_ERROR,"Database dbname not specified.\n");
      return -1;
    }
  if (dbuser == NULL)
    {
      ast_log(LOG_ERROR,"Database dbuser not specified.\n");
      return -1;
    }
  
  if (password == NULL)
    {
      ast_log(LOG_ERROR,"Database password not specified.\n");
      return -1;
    }
  
 mysql = mysql_init(NULL);
 
 mysql = mysql_real_connect(mysql, hostname, dbuser, password, dbname, 0, NULL, 0);
 
 if (mysql == NULL) {
   ast_log(LOG_ERROR, "Failed to connect to mysql database.\n");
   return -1;
 } else {
   ast_log(LOG_DEBUG,"Successfully connected to MySQL database.\n");
 }
 



  return 0;
}

int usecount(void)
{
  return 0;
}

char *key()
{
  return ASTERISK_GPL_KEY;
}
