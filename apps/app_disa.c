/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * DISA -- Direct Inward System Access Application  6/20/2001
 * 
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * Jim Dixon <jim@lambdatel.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
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
#include <pthread.h>
#include <sys/time.h>

#define	TONE_BLOCK_SIZE 200

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
	"lines, or comments starting with \"#\" or \";\".\n\n"
	"If login is successful, the application parses the dialed number in\n"
	"the specified (or default) context, and returns 0 with the new extension\n"
	"context filled-in and the priority set to 1, so that the PBX may\n"
	"re-apply the routing tables to it and complete the call normally.";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static float loudness=8192.0;

int firstdigittimeout = 10000; /* 10 seconds first digit timeout */
int digittimeout = 5000; /* 5 seconds subsequent digit timeout */

static void make_tone_block(unsigned char *data, float f1, float f2, int *x);

static void make_tone_block(unsigned char *data, float f1, float f2, int *x)
{
int	i;
float	val;

	for(i = 0; i < TONE_BLOCK_SIZE; i++)
	{
		val = loudness * sin((f1 * 2.0 * M_PI * (*x))/8000.0);
		val += loudness * sin((f2 * 2.0 * M_PI * (*x)++)/8000.0);
		data[i] = ast_lin2mu[(int)val + 32768];
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
	char tmp[256],exten[AST_MAX_EXTENSION];
	unsigned char tone_block[TONE_BLOCK_SIZE],sil_block[TONE_BLOCK_SIZE];
	char *ourcontext;
	struct ast_frame *f,wf;
	fd_set	readfds;
	int waitfor_notime;
	struct timeval notime = { 0,0 }, lastout, now, lastdigittime;
	FILE *fp;

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
	  /* make block of silence */
	memset(sil_block,0x7f,TONE_BLOCK_SIZE);
	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "disa requires an argument (passcode/passcode file)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp));
	strtok(tmp, "|");
	ourcontext = strtok(NULL, "|");
	  /* if context not specified, use "disa" */
	if (!ourcontext) ourcontext = "disa";
	LOCAL_USER_ADD(u);
	if (chan->state != AST_STATE_UP)
	{
		/* answer */
		ast_answer(chan);
	}
	i = k = x = 0; /* k is 0 for pswd entry, 1 for ext entry */
	exten[0] = 0;
	/* can we access DISA without password? */ 
	if (!strcasecmp(tmp, "no-password"))
	{
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
			goto reorder;
		}
		  /* if first digit or ignore, send dialtone */
		if ((!i) || (ast_ignore_pattern(ourcontext,exten) && k)) 
		{
			gettimeofday(&now,NULL);
			if (lastout.tv_sec && 
				(ms_diff(&now,&lastout) < 25)) continue;
			lastout.tv_sec = now.tv_sec;
			lastout.tv_usec = now.tv_usec;
			wf.frametype = AST_FRAME_VOICE;
			wf.subclass = AST_FORMAT_ULAW;
			wf.offset = AST_FRIENDLY_OFFSET;
			wf.mallocd = 0;
			wf.data = tone_block;
			wf.datalen = TONE_BLOCK_SIZE;				
			/* make this tone block */
			make_tone_block(tone_block,350.0,440.0,&x);
			wf.timelen = wf.datalen / 8;
		        if (ast_write(chan, &wf)) 
			{
		                ast_log(LOG_WARNING, "DISA Failed to write frame on %s\n",chan->name);
				LOCAL_USER_REMOVE(u);
				return -1;
			}
		}
		waitfor_notime = notime.tv_usec + notime.tv_sec * 1000;
		if (!ast_waitfor_nandfds(&chan, 1, &(chan->fds[0]), 1, NULL, NULL,
			&waitfor_notime)) continue;
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
							if (!tmp[0]) continue;
							if (tmp[strlen(tmp) - 1] == '\n') 
								tmp[strlen(tmp) - 1] = 0;
							if (!tmp[0]) continue;
							  /* skip comments */
							if (tmp[0] == '#') continue;
							if (tmp[0] == ';') continue;
							strtok(tmp, "|");
							ourcontext = strtok(NULL, "|");
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
					k = 1;
					i = 0;  /* re-set buffer pointer */
					exten[0] = 0;
					ast_log(LOG_DEBUG,"Successful DISA log-in on chan %s\n",chan->name);
					continue;
				}
			}
			exten[i++] = j;  /* save digit */
			exten[i] = 0;
			if (!k) continue; /* if getting password, continue doing it */
			  /* if this exists */
			if (ast_exists_extension(chan,ourcontext,exten,1, chan->callerid))
			{
				strcpy(chan->exten,exten);
				strcpy(chan->context,ourcontext);
				chan->priority = 0;
				LOCAL_USER_REMOVE(u);
				return 0;
			}
			  /* if can do some more, do it */
			if (ast_canmatch_extension(chan,ourcontext,exten,1, chan->callerid)) continue;
		}
reorder:
		/* something is invalid, give em reorder forever */
		x = 0;
		for(;;)
		{
			for(i = 0; i < 10; i++)
			{
				do gettimeofday(&now,NULL);
				while (lastout.tv_sec && 
					(ms_diff(&now,&lastout) < 25)) ;
				lastout.tv_sec = now.tv_sec;
				lastout.tv_usec = now.tv_usec;
				wf.frametype = AST_FRAME_VOICE;
				wf.subclass = AST_FORMAT_ULAW;
				wf.offset = AST_FRIENDLY_OFFSET;
				wf.mallocd = 0;
				wf.data = tone_block;
				wf.datalen = TONE_BLOCK_SIZE;
				/* make this tone block */
				make_tone_block(tone_block,480.0,620.0,&x);
				wf.timelen = wf.datalen / 8;
			        if (ast_write(chan, &wf)) 
				{
			                ast_log(LOG_WARNING, "DISA Failed to write frame on %s\n",chan->name);
					LOCAL_USER_REMOVE(u);
					return -1;
				}
				FD_ZERO(&readfds);
				FD_SET(chan->fds[0],&readfds);
				  /* if no read avail, do send again */
				if (select(chan->fds[0] + 1,&readfds,NULL,
					NULL,&notime) < 1) continue;
				  /* read frame */
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
				ast_frfree(f);
			}
			for(i = 0; i < 10; i++)
			{
				do gettimeofday(&now,NULL);
				while (lastout.tv_sec && 
					(ms_diff(&now,&lastout) < 25)) ;
				lastout.tv_sec = now.tv_sec;
				lastout.tv_usec = now.tv_usec;
				wf.frametype = AST_FRAME_VOICE;
				wf.subclass = AST_FORMAT_ULAW;
				wf.offset = AST_FRIENDLY_OFFSET;
				wf.mallocd = 0;
				wf.data = sil_block;
				wf.datalen = TONE_BLOCK_SIZE;
				wf.timelen = wf.datalen / 8;
			        if (ast_write(chan, &wf)) 
				{
			                ast_log(LOG_WARNING, "DISA Failed to write frame on %s\n",chan->name);
					LOCAL_USER_REMOVE(u);
					return -1;
				}
				FD_ZERO(&readfds);
				FD_SET(chan->fds[0],&readfds);
				  /* if no read avail, do send again */
				if (select(chan->fds[0] + 1,&readfds,NULL,
					NULL,&notime) < 1) continue;
				  /* read frame */
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
				ast_frfree(f);
			}
		}
	}
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

char *key()
{
	return ASTERISK_GPL_KEY;
}
