/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Provide a directory of extensions
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
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
#include <asterisk/config.h>
#include <asterisk/say.h>
#include <asterisk/utils.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include "../asterisk.h"
#include "../astconf.h"

static char *tdesc = "Extension Directory";
static char *app = "Directory";

static char *synopsis = "Provide directory of voicemail extensions";
static char *descrip =
"  Directory(vm-context[|dial-context]): Presents the user with a directory\n"
"of extensions from which they  may  select  by name. The  list  of  names \n"
"and  extensions  is discovered from  voicemail.conf. The  vm-context  argument\n"
"is required, and specifies  the  context  of voicemail.conf to use.  The\n"
"dial-context is the context to use for dialing the users, and defaults to\n"
"the vm-context if unspecified. Returns 0 unless the user hangs up. It  also\n"
"sets up the channel on exit to enter the extension the user selected.\n";

/* For simplicity, I'm keeping the format compatible with the voicemail config,
   but i'm open to suggestions for isolating it */

#define DIRECTORY_CONFIG "voicemail.conf"

/* How many digits to read in */
#define NUMDIGITS 3

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *convert(char *lastname)
{
	char *tmp;
	int lcount = 0;
	tmp = malloc(NUMDIGITS + 1);
	if (tmp) {
		while((*lastname > 32) && lcount < NUMDIGITS) {
			switch(toupper(*lastname)) {
			case '1':
				tmp[lcount++] = '1';
				break;
			case '2':
			case 'A':
			case 'B':
			case 'C':
				tmp[lcount++] = '2';
				break;
			case '3':
			case 'D':
			case 'E':
			case 'F':
				tmp[lcount++] = '3';
				break;
			case '4':
			case 'G':
			case 'H':
			case 'I':
				tmp[lcount++] = '4';
				break;
			case '5':
			case 'J':
			case 'K':
			case 'L':
				tmp[lcount++] = '5';
				break;
			case '6':
			case 'M':
			case 'N':
			case 'O':
				tmp[lcount++] = '6';
				break;
			case '7':
			case 'P':
			case 'Q':
			case 'R':
			case 'S':
				tmp[lcount++] = '7';
				break;
			case '8':
			case 'T':
			case 'U':
			case 'V':
				tmp[lcount++] = '8';
				break;
			case '9':
			case 'W':
			case 'X':
			case 'Y':
			case 'Z':
				tmp[lcount++] = '9';
				break;
			}
			lastname++;
		}
		tmp[lcount] = '\0';
	}
	return tmp;
}

/* play name of mailbox owner.
 * returns:  -1 for bad or missing extension
 *           '1' for selected entry from directory
 *           '*' for skipped entry from directory
 */
static int play_mailbox_owner(struct ast_channel *chan, char *context, char *dialcontext, char *ext, char *name) {
	int res = 0;
	int loop = -1;
	char fn[256];
	char fn2[256];

	/* Check for the VoiceMail2 greeting first */
	snprintf(fn, sizeof(fn), "%s/voicemail/%s/%s/greet",
		(char *)ast_config_AST_SPOOL_DIR, context, ext);

	/* Otherwise, check for an old-style Voicemail greeting */
	snprintf(fn2, sizeof(fn2), "%s/vm/%s/greet",
		(char *)ast_config_AST_SPOOL_DIR, ext);

	if (ast_fileexists(fn, NULL, chan->language) > 0) {
		res = ast_streamfile(chan, fn, chan->language);
		if (!res) {
			res = ast_waitstream(chan, AST_DIGIT_ANY);
		}
		ast_stopstream(chan);
	} else if (ast_fileexists(fn2, NULL, chan->language) > 0) {
		res = ast_streamfile(chan, fn2, chan->language);
		if (!res) {
			res = ast_waitstream(chan, AST_DIGIT_ANY);
		}
		ast_stopstream(chan);
	} else {
		res = ast_say_character_str(chan, !ast_strlen_zero(name) ? name : ext,
					AST_DIGIT_ANY, chan->language);
	}

	while (loop) {
		if (!res) {
			res = ast_streamfile(chan, "dir-instr", chan->language);
		}
		if (!res) {
			res = ast_waitstream(chan, AST_DIGIT_ANY);
		}
		if (!res) {
			res = ast_waitfordigit(chan, 3000);
		}
		ast_stopstream(chan);
	
		if (res > -1) {
			switch (res) {
				case '1':
					loop = 0;
					if (ast_exists_extension(chan,dialcontext,ext,1,chan->callerid)) {
						strncpy(chan->exten, ext, sizeof(chan->exten)-1);
						chan->priority = 0;
						strncpy(chan->context, dialcontext, sizeof(chan->context)-1);
					} else {
						ast_log(LOG_WARNING,
							"Can't find extension '%s' in context '%s'.  "
							"Did you pass the wrong context to Directory?\n",
							ext, context);
						res = -1;
					}
					break;
	
				case '*':   
					loop = 0;
					break;
	
				default:
					res = 0;
					break;
			} /* end switch */
		} /* end if */
	} /* end while */

	return(res);
}

static int do_directory(struct ast_channel *chan, struct ast_config *cfg, char *context, char *dialcontext, char digit)
{
	/* Read in the first three digits..  "digit" is the first digit, already read */
	char ext[NUMDIGITS + 1];
	char name[80] = "";
	struct ast_variable *v;
	int res;
	int found=0;
	int lastuserchoice = 0;
	char *start, *pos, *conv,*stringp=NULL;

	if (!context || ast_strlen_zero(context)) {
		ast_log(LOG_WARNING,
			"Directory must be called with an argument "
			"(context in which to interpret extensions)\n");
		return -1;
	}

	memset(ext, 0, sizeof(ext));
	ext[0] = digit;
	res = 0;
	if (ast_readstring(chan, ext + 1, NUMDIGITS - 1, 3000, 3000, "#") < 0) res = -1;
	if (!res) {
		/* Search for all names which start with those digits */
		v = ast_variable_browse(cfg, context);
		while(v && !res) {
			/* Find all candidate extensions */
			while(v) {
				/* Find a candidate extension */
				start = strdup(v->value);
				if (start) {
					stringp=start;
					strsep(&stringp, ",");
					pos = strsep(&stringp, ",");
					if (pos) {
						strncpy(name, pos, sizeof(name) - 1);
						/* Grab the last name */
						if (strrchr(pos, ' '))
							pos = strrchr(pos, ' ') + 1;
						conv = convert(pos);
						if (conv) {
							if (!strcmp(conv, ext)) {
								/* Match! */
								found++;
								free(conv);
								free(start);
								break;
							}
							free(conv);
						}
					}
					free(start);
				}
				v = v->next;
			}

			if (v) {
				/* We have a match -- play a greeting if they have it */
				res = play_mailbox_owner(chan, context, dialcontext, v->name, name);
				switch (res) {
					case -1:
						/* user pressed '1' but extension does not exist */
						lastuserchoice = 0;
						break;
					case '1':
						/* user pressed '1' and extensions exists */
						lastuserchoice = res;
						break;
					case '*':
						/* user pressed '*' to skip something found */
						lastuserchoice = res;
						res = 0;
						break;
					default:
						break;
				}
				v = v->next;
			}
		}

		if (lastuserchoice != '1') {
			if (found) 
				res = ast_streamfile(chan, "dir-nomore", chan->language);
			else
				res = ast_streamfile(chan, "dir-nomatch", chan->language);
			if (!res)
				res = 1;
			return res;
		}
		
	}
	return res;
}

static int directory_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	struct ast_config *cfg;
	char *context, *dialcontext, *dirintro;
	if (!data) {
		ast_log(LOG_WARNING, "directory requires an argument (context)\n");
		return -1;
	}
	cfg = ast_load(DIRECTORY_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "Unable to open directory configuration %s\n", DIRECTORY_CONFIG);
		return -1;
	}
	LOCAL_USER_ADD(u);
top:
	context = ast_strdupa(data);
	dialcontext = strchr(context, '|');
	if (dialcontext) {
		*dialcontext = '\0';
		dialcontext++;
	} else
		dialcontext = context;
	dirintro = ast_variable_retrieve(cfg, context, "directoryintro");
	if (!dirintro || ast_strlen_zero(dirintro))
		dirintro = ast_variable_retrieve(cfg, "general", "directoryintro");
	if (!dirintro || ast_strlen_zero(dirintro))
		dirintro = "dir-intro";
	if (chan->_state != AST_STATE_UP) 
		res = ast_answer(chan);
	if (!res)
		res = ast_streamfile(chan, dirintro, chan->language);
	if (!res)
		res = ast_waitstream(chan, AST_DIGIT_ANY);
	ast_stopstream(chan);
	if (!res)
		res = ast_waitfordigit(chan, 5000);
	if (res > 0) {
		res = do_directory(chan, cfg, context, dialcontext, res);
		if (res > 0) {
			res = ast_waitstream(chan, AST_DIGIT_ANY);
			ast_stopstream(chan);
			if (res >= 0) {
				goto top;
			}
		}
	}
	ast_destroy(cfg);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, directory_exec, synopsis, descrip);
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
