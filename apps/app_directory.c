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
 
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/config.h>
#include <asterisk/say.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include "../asterisk.h"

static char *tdesc = "Extension Directory";
static char *app = "Directory";

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
			default:
			}
			lastname++;
		}
		tmp[lcount] = '\0';
	}
	return tmp;
}

static int do_directory(struct ast_channel *chan, struct ast_config *cfg, char *context, char digit)
{
	/* Read in the first three digits..  "digit" is the first digit, already read */
	char ext[NUMDIGITS + 1];
	struct ast_variable *v;
	int res;
	int found=0;
	char *start, *pos, *conv;
	char fn[256];
	memset(ext, 0, sizeof(ext));
	ext[0] = digit;
	res = ast_readstring(chan, ext + 1, NUMDIGITS, 3000, 3000, "#");
	if (!res) {
		/* Search for all names which start with those digits */
		v = ast_variable_browse(cfg, context);
		while(v && !res) {
			/* Find all candidate extensions */
			while(v) {
				/* Find a candidate extension */
				start = strdup(v->value);
				if (start) {
					strtok(start, ",");
					pos = strtok(NULL, ",");
					if (pos) {
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
				snprintf(fn, sizeof(fn), "%s/vm/%s/greet", AST_SPOOL_DIR, v->name);
				if (ast_fileexists(fn, NULL)) {
					res = ast_streamfile(chan, fn);
					if (!res)
						res = ast_waitstream(chan, AST_DIGIT_ANY);
					ast_stopstream(chan);
				} else {
					res = ast_say_digit_str(chan, v->name);
				}
ahem:
				if (!res)
					res = ast_streamfile(chan, "dir-instr");
				if (!res)
					res = ast_waitstream(chan, AST_DIGIT_ANY);
				if (!res)
					res = ast_waitfordigit(chan, 3000);
				ast_stopstream(chan);
				if (res > -1) {
					if (res == '1') {
						strncpy(chan->exten, v->name, sizeof(chan->exten));
						chan->priority = 0;
						strncpy(chan->context, context, sizeof(chan->context));
						res = 0;
						break;
					} else if (res == '*') {
						res = 0;
						v = v->next;
					} else {
						res = 0;
						goto ahem;
					}
				}
			} else {
				if (found) 
					res = ast_streamfile(chan, "dir-nomore");
				else
					res = ast_streamfile(chan, "dir-nomatch");
				if (!res)
					res = 1;
				return res;
			}
		}
		
	}
	return res;
}

static int directory_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	struct ast_config *cfg;
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
	if (!res)
		res = ast_streamfile(chan, "dir-intro");
	if (!res)
		res = ast_waitstream(chan, AST_DIGIT_ANY);
	ast_stopstream(chan);
	if (!res)
		res = ast_waitfordigit(chan, 5000);
	if (res > 0) {
		res = do_directory(chan, cfg, (char *)data, res);
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
	return ast_register_application(app, directory_exec);
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
