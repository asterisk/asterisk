/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * DISA -- Direct Inward System Access Application  6/20/2001
 * 
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * Jim Dixon <jim@lambdatel.com>
 *
 * Made only slightly more sane by Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/ulaw.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>

/*
#define	TONE_BLOCK_SIZE 320
*/

static char *tdesc = "DISA (Direct Inward System Access) Application";

static char *app = "DISA";

static char *synopsis = "DISA (Direct Inward System Access)";

static char *descrip = 
	"DISA (Direct Inward System Access) -- Allows someone from outside\n"
	"the telephone switch (PBX) to obtain an \"internal\" system dialtone\n"
	"and to place calls from it as if they were placing a call from within\n"
	"the switch. A user calls a number that connects to the DISA application\n"
	"and is given dialtone. The user enters their passcode, followed by the\n"
	"pound sign (#). If the passcode is correct, the user is then given\n"
	"system dialtone on which a call may be placed. Obviously, this type\n"
	"of access has SERIOUS security implications, and GREAT care must be\n"
	"taken NOT to compromise your security.\n\n"
	"There is a possibility of accessing DISA without password. Simply\n"
	"exchange your password with no-password.\n\n"
	"  Example: exten => s,1,DISA,no-password|local\n\n"
	"but be aware of using this for your security compromising.\n\n"
	"The arguments to this application (in extensions.conf) allow either\n"
	"specification of a single global password (that everyone uses), or\n"
	"individual passwords contained in a file. It also allow specification\n"
	"of the context on which the user will be dialing. If no context is\n"
	"specified, the DISA application defaults the context to \"disa\"\n"
	"presumably that a normal system will have a special context set up\n"
	"for DISA use with some or a lot of restrictions. The arguments are\n"
	"one of the following:\n\n"
	"    numeric-passcode\n"
	"    numeric-passcode|context\n"
	"    full-pathname-of-file-that-contains-passcodes\n\n"
	"The file that contains the passcodes (if used) allows specification\n"
	"of either just a passcode (defaulting to the \"disa\" context, or\n"
	"passcode|context on each line of the file. The file may contain blank\n"
	"lines, or comments starting with \"#\" or \";\". In addition, the\n"
	"above arguments may have |new-callerid-string appended to them, to\n"
	"specify a new (different) callerid to be used for this call, for\n"
	"example: numeric-passcode|context|\"My Phone\" <(234) 123-4567> or \n"
	"full-pathname-of-passcode-file|\"My Phone\" <(234) 123-4567>. Note that\n"
	"in the case of specifying the numeric-passcode, the context must be\n"
	"specified if the callerid is specified also.\n\n"
	"If login is successful, the application parses the dialed number in\n"
	"the specified (or default) context, and returns 0 with the new extension\n"
	"context filled-in and the priority set to 1, so that the PBX may\n"
	"re-apply the routing tables to it and complete the call normally.";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static float loudness=4096.0;

static int firstdigittimeout = 20000; /* 20 seconds first digit timeout */
static int digittimeout = 10000; /* 10 seconds subsequent digit timeout */

static void make_tone_block(unsigned char *data, float f1, float f2, int len, int *x)
{
int	i;
float	val;

	for(i = 0; i < len; i++)
	{
		val = loudness * sin((f1 * 2.0 * M_PI * (*x))/8000.0);
		val += loudness * sin((f2 * 2.0 * M_PI * (*x)++)/8000.0);
		data[i] = AST_LIN2MU((int)val);
	 }		
	  /* wrap back around from 8000 */
	if (*x >= 8000) *x = 0;
	return;
}

static int ms_diff(struct timeval *tv1, struct timeval *tv2)
{
int	ms;
	
	ms = (tv1->tv_sec - tv2->tv_sec) * 1000;
	ms += (tv1->tv_usec - tv2->tv_usec) / 1000;
	return(ms);
}

static int disa_exec(struct ast_channel *chan, void *data)
{
	int i,j,k,x;
	struct localuser *u;
	char tmp[256],arg2[256],exten[AST_MAX_EXTENSION],acctcode[20];
	struct {
		unsigned char offset[AST_FRIENDLY_OFFSET];
		unsigned char buf[640];
	} tone_block;
	char *ourcontext,*ourcallerid;
	struct ast_frame *f,wf;
	struct timeval lastout, now, lastdigittime;
	int res;
	FILE *fp;
	char *stringp=NULL;

	if (ast_set_write_format(chan,AST_FORMAT_ULAW))
	{
		ast_log(LOG_WARNING, "Unable to set write format to Mu-law on %s\n",chan->name);
		return -1;
	}
	if (ast_set_read_format(chan,AST_FORMAT_ULAW))
	{
		ast_log(LOG_WARNING, "Unable to set read format to Mu-law on %s\n",chan->name);
		return -1;
	}
	lastout.tv_sec = lastout.tv_usec = 0;
	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "disa requires an argument (passcode/passcode file)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	ourcontext = strsep(&stringp, "|");
	/* if context specified, save 2nd arg and parse third */
	if (ourcontext) {
		strcpy(arg2,ourcontext);
		ourcallerid = strsep(&stringp,"|");
	}
	  /* if context not specified, use "disa" */
	else {
		arg2[0] = 0;
		ourcallerid = NULL;
		ourcontext = "disa";
	}
	LOCAL_USER_ADD(u);
	if (chan->_state != AST_STATE_UP)
	{
		/* answer */
		ast_answer(chan);
	}
	i = k = x = 0; /* k is 0 for pswd entry, 1 for ext entry */
	exten[0] = 0;
	acctcode[0] = 0;
	/* can we access DISA without password? */ 
	if (!strcasecmp(tmp, "no-password"))
	{;
		k = 1;
		ast_log(LOG_DEBUG, "DISA no-password login success\n");
	}
	gettimeofday(&lastdigittime,NULL);
	for(;;)
	{
		gettimeofday(&now,NULL);
		  /* if outa time, give em reorder */
		if (ms_diff(&now,&lastdigittime) > 
		    ((k) ? digittimeout : firstdigittimeout))
		{
			ast_log(LOG_DEBUG,"DISA %s entry timeout on chan %s\n",
				((k) ? "extension" : "password"),chan->name);
			break;
		}
		if ((res = ast_waitfor(chan, -1) < 0)) {
			ast_log(LOG_DEBUG, "Waitfor returned %d\n", res);
			continue;
		}
			
		f = ast_read(chan);
		if (f == NULL) 
		{
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_HANGUP))
		{
			ast_frfree(f);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			if (!i || (ast_ignore_pattern(ourcontext, exten) && k)) {
				wf.frametype = AST_FRAME_VOICE;
				wf.subclass = AST_FORMAT_ULAW;
				wf.offset = AST_FRIENDLY_OFFSET;
				wf.mallocd = 0;
				wf.data = tone_block.buf;
				wf.datalen = f->datalen;
				wf.delivery.tv_sec = wf.delivery.tv_usec = 0;
				make_tone_block(tone_block.buf, 350, 440, f->datalen, &x);
				wf.samples = wf.datalen;
				ast_frfree(f);
			    if (ast_write(chan, &wf)) 
				{
			        ast_log(LOG_WARNING, "DISA Failed to write frame on %s\n",chan->name);
					LOCAL_USER_REMOVE(u);
					return -1;
				}
			} else
				ast_frfree(f);
			continue;
		}
		  /* if not DTMF, just do it again */
		if (f->frametype != AST_FRAME_DTMF) 
		{
			ast_frfree(f);
			continue;
		}

		j = f->subclass;  /* save digit */
		ast_frfree(f);
		
		gettimeofday(&lastdigittime,NULL);
		  /* got a DTMF tone */
		if (i < AST_MAX_EXTENSION) /* if still valid number of digits */
		{
			if (!k) /* if in password state */
			{
				if (j == '#') /* end of password */
				{
					  /* see if this is an integer */
					if (sscanf(tmp,"%d",&j) < 1)
					   { /* nope, it must be a filename */
						fp = fopen(tmp,"r");
						if (!fp)
						   {
							ast_log(LOG_WARNING,"DISA password file %s not found on chan %s\n",tmp,chan->name);
							LOCAL_USER_REMOVE(u);
							return -1;
						   }
						tmp[0] = 0;
						while(fgets(tmp,sizeof(tmp) - 1,fp))
						   {
							char *stringp=NULL,*stringp2;
							if (!tmp[0]) continue;
							if (tmp[strlen(tmp) - 1] == '\n') 
								tmp[strlen(tmp) - 1] = 0;
							if (!tmp[0]) continue;
							  /* skip comments */
							if (tmp[0] == '#') continue;
							if (tmp[0] == ';') continue;
							stringp=tmp;
							strsep(&stringp, "|");
							stringp2=strsep(&stringp, "|");
							if (stringp2) {
								ourcontext=stringp2;
								stringp2=strsep(&stringp, "|");
								if (stringp2) ourcallerid=stringp2;
							}
							  /* password must be in valid format (numeric) */
							if (sscanf(tmp,"%d",&j) < 1) continue;
							  /* if we got it */
							if (!strcmp(exten,tmp)) break;
						   }
						fclose(fp);
					   }
					  /* compare the two */
					if (strcmp(exten,tmp))
					{
						ast_log(LOG_WARNING,"DISA on chan %s got bad password %s\n",chan->name,exten);
						goto reorder;

					}
					 /* password good, set to dial state */
						ast_log(LOG_WARNING,"DISA on chan %s password is good\n",chan->name);
					k = 1;
					i = 0;  /* re-set buffer pointer */
					exten[sizeof(acctcode)] = 0;
					strcpy(acctcode,exten);
					exten[0] = 0;
					ast_log(LOG_DEBUG,"Successful DISA log-in on chan %s\n",chan->name);
					continue;
				}
			}
			exten[i++] = j;  /* save digit */
			exten[i] = 0;
			if (!k) continue; /* if getting password, continue doing it */
			  /* if this exists */

			  /* if can do some more, do it */
			if (!ast_matchmore_extension(chan,ourcontext,exten,1, chan->callerid)) 
				break;
		}
	}

	if (k && ast_exists_extension(chan,ourcontext,exten,1, chan->callerid))
	{
		/* We're authenticated and have a valid extension */
		if (ourcallerid && *ourcallerid)
		{
			if (chan->callerid) free(chan->callerid);
			chan->callerid = strdup(ourcallerid);
		}
		strcpy(chan->exten,exten);
		strcpy(chan->context,ourcontext);
		strcpy(chan->accountcode,acctcode);
		chan->priority = 0;
		ast_cdr_init(chan->cdr,chan);
		LOCAL_USER_REMOVE(u);
		return 0;
	}

reorder:
		
	/* something is invalid, give em reorder forever */
	x = 0;
	k = 0;	/* k = 0 means busy tone, k = 1 means silence) */
	i = 0;	/* Number of samples we've done */
	for(;;)
	{
		if (ast_waitfor(chan, -1) < 0)
			break;
		f = ast_read(chan);
		if (!f)
			break;
		if (f->frametype == AST_FRAME_VOICE) {
			wf.frametype = AST_FRAME_VOICE;
			wf.subclass = AST_FORMAT_ULAW;
			wf.offset = AST_FRIENDLY_OFFSET;
			wf.mallocd = 0;
			wf.data = tone_block.buf;
			wf.datalen = f->datalen;
			wf.samples = wf.datalen;
			if (k) 
				memset(tone_block.buf, 0x7f, wf.datalen);
			else
				make_tone_block(tone_block.buf,480.0, 620.0,wf.datalen, &x);
			i += wf.datalen / 8;
			if (i > 250) {
				i = 0;
				k = !k;
			}
		    if (ast_write(chan, &wf)) 
			{
		        ast_log(LOG_WARNING, "DISA Failed to write frame on %s\n",chan->name);
				LOCAL_USER_REMOVE(u);
				return -1;
			}
		}
		ast_frfree(f);
	}
	LOCAL_USER_REMOVE(u);
	return -1;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, disa_exec, synopsis, descrip);
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

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
