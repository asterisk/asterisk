/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Christos Ricudis
 *
 * Christos Ricudis <ricudis@itc.auth.gr>
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
 * \brief Connect to PostgreSQL
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/lock.h"

#include "libpq-fe.h"

static char *tdesc = "Simple PostgreSQL Interface";

static char *app = "PGSQL";

static char *synopsis = "Do several SQLy things";

static char *descrip = 
"PGSQL():  Do several SQLy things\n"
"Syntax:\n"
"  PGSQL(Connect var option-string)\n"
"    Connects to a database.  Option string contains standard PostgreSQL\n"
"    parameters like host=, dbname=, user=.  Connection identifer returned\n"
"    in ${var}\n"
"  PGSQL(Query var ${connection_identifier} query-string)\n"
"    Executes standard SQL query contained in query-string using established\n"
"    connection identified by ${connection_identifier}. Reseult of query is\n"
"    is stored in ${var}.\n"
"  PGSQL(Fetch statusvar ${result_identifier} var1 var2 ... varn)\n"
"    Fetches a single row from a result set contained in ${result_identifier}.\n"
"    Assigns returned fields to ${var1} ... ${varn}.  ${statusvar} is set TRUE\n"
"    if additional rows exist in reseult set.\n"
"  PGSQL(Clear ${result_identifier})\n"
"    Frees memory and datastructures associated with result set.\n" 
"  PGSQL(Disconnect ${connection_identifier})\n"
"    Disconnects from named connection to PostgreSQL.\n" ;

/*

Syntax of SQL commands : 

	Connect var option-string
	
	Connects to a database using the option-string and stores the 
	connection identifier in ${var}
	
	
	Query var ${connection_identifier} query-string
	
	Submits query-string to database backend and stores the result
	identifier in ${var}
	
	
	Fetch statusvar ${result_identifier} var1 var2 var3 ... varn
	
	Fetches a row from the query and stores end-of-table status in 
	${statusvar} and columns in ${var1}..${varn}
	
	
	Clear ${result_identifier}

	Clears data structures associated with ${result_identifier}
	
	
	Disconnect ${connection_identifier}
	
	Disconnects from named connection
	
	
EXAMPLES OF USE : 

exten => s,2,PGSQL(Connect connid host=localhost user=asterisk dbname=credit)
exten => s,3,PGSQL(Query resultid ${connid} SELECT username,credit FROM credit WHERE callerid=${CALLERIDNUM})
exten => s,4,PGSQL(Fetch fetchid ${resultid} datavar1 datavar2)
exten => s,5,GotoIf(${fetchid}?6:8)
exten => s,6,Festival("User ${datavar1} currently has credit balance of ${datavar2} dollars.")	
exten => s,7,Goto(s,4)
exten => s,8,PGSQL(Clear ${resultid})
exten => s,9,PGSQL(Disconnect ${connid})

*/

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define AST_PGSQL_ID_DUMMY 0
#define AST_PGSQL_ID_CONNID 1
#define AST_PGSQL_ID_RESID 2
#define AST_PGSQL_ID_FETCHID 3

struct ast_PGSQL_id {
	int identifier_type; /* 0=dummy, 1=connid, 2=resultid */
	int identifier;
	void *data;
	AST_LIST_ENTRY(ast_PGSQL_id) entries;
} *ast_PGSQL_id;

AST_LIST_HEAD(PGSQLidshead,ast_PGSQL_id) PGSQLidshead;

static void *find_identifier(int identifier,int identifier_type) {
	struct PGSQLidshead *headp;
	struct ast_PGSQL_id *i;
	void *res=NULL;
	int found=0;
	
	headp=&PGSQLidshead;
	
	if (AST_LIST_LOCK(headp)) {
		ast_log(LOG_WARNING,"Unable to lock identifiers list\n");
	} else {
		AST_LIST_TRAVERSE(headp,i,entries) {
			if ((i->identifier==identifier) && (i->identifier_type==identifier_type)) {
				found=1;
				res=i->data;
				break;
			}
		}
		if (!found) {
			ast_log(LOG_WARNING,"Identifier %d, identifier_type %d not found in identifier list\n",identifier,identifier_type);
		}
		AST_LIST_UNLOCK(headp);
	}
	
	return(res);
}

static int add_identifier(int identifier_type,void *data) {
	struct ast_PGSQL_id *i,*j;
	struct PGSQLidshead *headp;
	int maxidentifier=0;
	
	headp=&PGSQLidshead;
	i=NULL;
	j=NULL;
	
	if (AST_LIST_LOCK(headp)) {
		ast_log(LOG_WARNING,"Unable to lock identifiers list\n");
		return(-1);
	} else {
 		i=malloc(sizeof(struct ast_PGSQL_id));
		AST_LIST_TRAVERSE(headp,j,entries) {
			if (j->identifier>maxidentifier) {
				maxidentifier=j->identifier;
			}
		}
		
		i->identifier=maxidentifier+1;
		i->identifier_type=identifier_type;
		i->data=data;
		AST_LIST_INSERT_HEAD(headp,i,entries);
		AST_LIST_UNLOCK(headp);
	}
	return(i->identifier);
}

static int del_identifier(int identifier,int identifier_type) {
	struct ast_PGSQL_id *i;
	struct PGSQLidshead *headp;
	int found=0;
	
        headp=&PGSQLidshead;
        
        if (AST_LIST_LOCK(headp)) {
		ast_log(LOG_WARNING,"Unable to lock identifiers list\n");
	} else {
		AST_LIST_TRAVERSE(headp,i,entries) {
			if ((i->identifier==identifier) && 
			    (i->identifier_type==identifier_type)) {
				AST_LIST_REMOVE(headp,i,entries);
				free(i);
				found=1;
				break;
			}
		}
		AST_LIST_UNLOCK(headp);
	}
	                
	if (found==0) {
		ast_log(LOG_WARNING,"Could not find identifier %d, identifier_type %d in list to delete\n",identifier,identifier_type);
		return(-1);
	} else {
		return(0);
	}
}

static int aPGSQL_connect(struct ast_channel *chan, void *data) {
	
	char *s1;
	char s[100] = "";
	char *optionstring;
	char *var;
	int l;
	int res;
	PGconn *karoto;
	int id;
	char *stringp=NULL;
	 
	
	res=0;
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l -1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	var=strsep(&stringp," ");
	optionstring=strsep(&stringp,"\n");
		
      	karoto = PQconnectdb(optionstring);
        if (PQstatus(karoto) == CONNECTION_BAD) {
        	ast_log(LOG_WARNING,"Connection to database using '%s' failed. postgress reports : %s\n", optionstring,
                                                 PQerrorMessage(karoto));
        	res=-1;
        } else {
        	ast_log(LOG_WARNING,"adding identifier\n");
		id=add_identifier(AST_PGSQL_ID_CONNID,karoto);
		snprintf(s, sizeof(s), "%d", id);
		pbx_builtin_setvar_helper(chan,var,s);
	}
 	
	free(s1);
	return res;
}

static int aPGSQL_query(struct ast_channel *chan, void *data) {
	

	char *s1,*s2,*s3,*s4;
	char s[100] = "";
	char *querystring;
	char *var;
	int l;
	int res,nres;
	PGconn *karoto;
	PGresult *PGSQLres;
	int id,id1;
	char *stringp=NULL;
	 
	
	res=0;
	l=strlen(data)+2;
	s1=malloc(l);
	s2=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	while (1) {	/* ugly trick to make branches with break; */
		var=s3;
		s4=strsep(&stringp," ");
		id=atoi(s4);
		querystring=strsep(&stringp,"\n");
		if ((karoto=find_identifier(id,AST_PGSQL_ID_CONNID))==NULL) {
			ast_log(LOG_WARNING,"Invalid connection identifier %d passed in aPGSQL_query\n",id);
			res=-1;
			break;
		}
		PGSQLres=PQexec(karoto,querystring);
		if (PGSQLres==NULL) {
			ast_log(LOG_WARNING,"aPGSQL_query: Connection Error (connection identifier = %d, error message : %s)\n",id,PQerrorMessage(karoto));
			res=-1;
			break;
		}
		if (PQresultStatus(PGSQLres) == PGRES_BAD_RESPONSE ||
		    PQresultStatus(PGSQLres) == PGRES_NONFATAL_ERROR ||
		    PQresultStatus(PGSQLres) == PGRES_FATAL_ERROR) {
		    	ast_log(LOG_WARNING,"aPGSQL_query: Query Error (connection identifier : %d, error message : %s)\n",id,PQcmdStatus(PGSQLres));
		    	res=-1;
		    	break;
		}
		nres=PQnfields(PGSQLres); 
		id1=add_identifier(AST_PGSQL_ID_RESID,PGSQLres);
		snprintf(s, sizeof(s), "%d", id1);
		pbx_builtin_setvar_helper(chan,var,s);
	 	break;
	}
	
	free(s1);
	free(s2);

	return(res);
}


static int aPGSQL_fetch(struct ast_channel *chan, void *data) {
	
	char *s1,*s2,*fetchid_var,*s4,*s5,*s6,*s7;
	char s[100];
	char *var;
	int l;
	int res;
	PGresult *PGSQLres;
	int id,id1,i,j,fnd;
	int *lalares=NULL;
	int nres;
        struct ast_var_t *variables;
        struct varshead *headp;
	char *stringp=NULL;
        
        headp=&chan->varshead;
	
	res=0;
	l=strlen(data)+2;
	s7=NULL;
	s1=malloc(l);
	s2=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	fetchid_var=strsep(&stringp," ");
	while (1) {	/* ugly trick to make branches with break; */
	  var=fetchid_var; /* fetchid */
		fnd=0;
		
		AST_LIST_TRAVERSE(headp,variables,entries) {
	    if (strncasecmp(ast_var_name(variables),fetchid_var,strlen(fetchid_var))==0) {
	                        s7=ast_var_value(variables);
	                        fnd=1;
                                break;
			}
		}
		
		if (fnd==0) { 
			s7="0";
	    pbx_builtin_setvar_helper(chan,fetchid_var,s7);
		}

		s4=strsep(&stringp," ");
		id=atoi(s4); /* resultid */
		if ((PGSQLres=find_identifier(id,AST_PGSQL_ID_RESID))==NULL) {
			ast_log(LOG_WARNING,"Invalid result identifier %d passed in aPGSQL_fetch\n",id);
			res=-1;
			break;
		}
		id=atoi(s7); /*fetchid */
		if ((lalares=find_identifier(id,AST_PGSQL_ID_FETCHID))==NULL) {
	    i=0;       /* fetching the very first row */
		} else {
			i=*lalares;
			free(lalares);
	    del_identifier(id,AST_PGSQL_ID_FETCHID); /* will re-add it a bit later */
		}

	  if (i<PQntuples(PGSQLres)) {
		nres=PQnfields(PGSQLres); 
		ast_log(LOG_WARNING,"ast_PGSQL_fetch : nres = %d i = %d ;\n",nres,i);
		for (j=0;j<nres;j++) {
			s5=strsep(&stringp," ");
			if (s5==NULL) {
				ast_log(LOG_WARNING,"ast_PGSQL_fetch : More tuples (%d) than variables (%d)\n",nres,j);
				break;
			}
			s6=PQgetvalue(PGSQLres,i,j);
			if (s6==NULL) { 
				ast_log(LOG_WARNING,"PWgetvalue(res,%d,%d) returned NULL in ast_PGSQL_fetch\n",i,j);
				break;
			}
			ast_log(LOG_WARNING,"===setting variable '%s' to '%s'\n",s5,s6);
			pbx_builtin_setvar_helper(chan,s5,s6);
		}
			lalares=malloc(sizeof(int));
	    *lalares = ++i; /* advance to the next row */
	    id1 = add_identifier(AST_PGSQL_ID_FETCHID,lalares);
		} else {
	    ast_log(LOG_WARNING,"ast_PGSQL_fetch : EOF\n");
	    id1 = 0; /* no more rows */
		}
		snprintf(s, sizeof(s), "%d", id1);
	  ast_log(LOG_WARNING,"Setting var '%s' to value '%s'\n",fetchid_var,s);
	  pbx_builtin_setvar_helper(chan,fetchid_var,s);
	 	break;
	}
	
	free(s1);
	free(s2);
	return(res);
}

static int aPGSQL_reset(struct ast_channel *chan, void *data) {
	
	char *s1,*s3;
	int l;
	PGconn *karoto;
	int id;
	char *stringp=NULL;
	 
	
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	id=atoi(s3);
	if ((karoto=find_identifier(id,AST_PGSQL_ID_CONNID))==NULL) {
		ast_log(LOG_WARNING,"Invalid connection identifier %d passed in aPGSQL_reset\n",id);
	} else {
		PQreset(karoto);
	} 
	free(s1);
	return(0);
	
}

static int aPGSQL_clear(struct ast_channel *chan, void *data) {
	
	char *s1,*s3;
	int l;
	PGresult *karoto;
	int id;
	char *stringp=NULL;
	 
	
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	id=atoi(s3);
	if ((karoto=find_identifier(id,AST_PGSQL_ID_RESID))==NULL) {
		ast_log(LOG_WARNING,"Invalid result identifier %d passed in aPGSQL_clear\n",id);
	} else {
		PQclear(karoto);
		del_identifier(id,AST_PGSQL_ID_RESID);
	}
	free(s1);
	return(0);
	
}

	   
	   
	
static int aPGSQL_disconnect(struct ast_channel *chan, void *data) {
	
	char *s1,*s3;
	int l;
	PGconn *karoto;
	int id;
	char *stringp=NULL;
	 
	
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	id=atoi(s3);
	if ((karoto=find_identifier(id,AST_PGSQL_ID_CONNID))==NULL) {
		ast_log(LOG_WARNING,"Invalid connection identifier %d passed in aPGSQL_disconnect\n",id);
	} else {
		PQfinish(karoto);
		del_identifier(id,AST_PGSQL_ID_CONNID);
	} 
	free(s1);
	return(0);
	
}

static int aPGSQL_debug(struct ast_channel *chan, void *data) {
	ast_log(LOG_WARNING,"Debug : %s\n",(char *)data);
	return(0);
}

static int PGSQL_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int result;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "APP_PGSQL requires an argument (see manual)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	result=0;
	
	if (strncasecmp("connect",data,strlen("connect"))==0) {
		result=(aPGSQL_connect(chan,data));
	} else 	if (strncasecmp("query",data,strlen("query"))==0) {
		result=(aPGSQL_query(chan,data));
	} else 	if (strncasecmp("fetch",data,strlen("fetch"))==0) {
		result=(aPGSQL_fetch(chan,data));
	} else 	if (strncasecmp("reset",data,strlen("reset"))==0) {
		result=(aPGSQL_reset(chan,data));
	} else 	if (strncasecmp("clear",data,strlen("clear"))==0) {
		result=(aPGSQL_clear(chan,data));
	} else  if (strncasecmp("debug",data,strlen("debug"))==0) {
		result=(aPGSQL_debug(chan,data));
	} else 	if (strncasecmp("disconnect",data,strlen("disconnect"))==0) {
		result=(aPGSQL_disconnect(chan,data));
	} else {
		ast_log(LOG_WARNING, "Unknown APP_PGSQL argument : %s\n",(char *)data);
		result=-1;	
	}
		
	LOCAL_USER_REMOVE(u);                                                                                
	
	return result;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;
	
	return res;
}

int load_module(void)
{
	struct PGSQLidshead *headp;
	
        headp=&PGSQLidshead;
        
	AST_LIST_HEAD_INIT(headp);
	return ast_register_application(app, PGSQL_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
