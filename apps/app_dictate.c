/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Virtual Dictation Machine Application For Asterisk
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * Donated by Sangoma Technologies <http://www.samgoma.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>	/* for mkdir */
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk.h"

static char *tdesc = "Virtual Dictation Machine";
static char *app = "Dictate";
static char *synopsis = "Virtual Dictation Machine";
static char *desc = "  Dictate([<base_dir>])\n"
"Start dictation machine using optional base dir for files.\n";


STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

typedef enum {
	DFLAG_RECORD = (1 << 0),
	DFLAG_PLAY = (1 << 1),
	DFLAG_TRUNC = (1 << 2),
	DFLAG_PAUSE = (1 << 3),
} dflags;

typedef enum {
	DMODE_INIT,
	DMODE_RECORD,
	DMODE_PLAY
} dmodes;

#define ast_toggle_flag(it,flag) if(ast_test_flag(it, flag)) ast_clear_flag(it, flag); else ast_set_flag(it, flag)

static int play_and_wait(struct ast_channel *chan, char *file, char *digits) 
{
	int res = -1;
	if (!ast_streamfile(chan, file, chan->language)) {
		res = ast_waitstream(chan, digits);
	}
	return res;
}

static int dictate_exec(struct ast_channel *chan, void *data)
{
	char *mydata, *argv[2], *path = NULL, filein[256];
	char dftbase[256];
	char *base;
	struct ast_flags flags = {0};
	struct ast_filestream *fs;
	struct ast_frame *f = NULL;
	struct localuser *u;
	int ffactor = 320 * 80,
		res = 0,
		argc = 0,
		done = 0,
		oldr = 0,
		lastop = 0,
		samples = 0,
		speed = 1,
		digit = 0,
		len = 0,
		maxlen = 0,
		mode = 0;
		

	snprintf(dftbase, sizeof(dftbase), "%s/dictate", ast_config_AST_SPOOL_DIR);
	if (data && !ast_strlen_zero(data) && (mydata = ast_strdupa(data))) {
		argc = ast_separate_app_args(mydata, '|', argv, sizeof(argv) / sizeof(argv[0]));
	}
	
	if (argc) {
		base = argv[0];
	} else {
		base = dftbase;
	}

	oldr = chan->readformat;
	if ((res = ast_set_read_format(chan, AST_FORMAT_SLINEAR)) < 0) {
		ast_log(LOG_WARNING, "Unable to set to linear mode.\n");
		return -1;
	}

	LOCAL_USER_ADD(u);
	ast_answer(chan);
	ast_safe_sleep(chan, 200);
	for(res = 0; !res;) {
		if (ast_app_getdata(chan, "dictate/enter_filename", filein, sizeof(filein), 0) || 
			ast_strlen_zero(filein)) {
			res = -1;
			break;
		}
		
		mkdir(base, 0755);
		len = strlen(base) + strlen(filein) + 2;
		if (!path || len > maxlen) {
			path = alloca(len);
			memset(path, 0, len);
			maxlen = len;
		} else {
			memset(path, 0, maxlen);
		}

		snprintf(path, len, "%s/%s", base, filein);
		fs = ast_writefile(path, "raw", NULL, O_CREAT|O_APPEND, 0, 0700);
		mode = DMODE_PLAY;
		memset(&flags, 0, sizeof(flags));
		ast_set_flag(&flags, DFLAG_PAUSE);
		digit = play_and_wait(chan, "dictate/forhelp", AST_DIGIT_ANY);
		done = 0;
		speed = 1;
		res = 0;
		lastop = 0;
		samples = 0;
		while (!done && ((res = ast_waitfor(chan, -1)) > -1) && fs && (f = ast_read(chan))) {
			if (digit) {
				struct ast_frame fr = {AST_FRAME_DTMF, digit};
				ast_queue_frame(chan, &fr);
				digit = 0;
			}
			if ((f->frametype == AST_FRAME_DTMF)) {
				int got = 1;
				switch(mode) {
				case DMODE_PLAY:
					switch(f->subclass) {
					case '1':
						ast_set_flag(&flags, DFLAG_PAUSE);
						mode = DMODE_RECORD;
						break;
					case '2':
						speed++;
						if (speed > 4) {
							speed = 1;
						}
						res = ast_say_number(chan, speed, AST_DIGIT_ANY, chan->language, (char *) NULL);
						break;
					case '7':
						samples -= ffactor;
						if(samples < 0) {
							samples = 0;
						}
						ast_seekstream(fs, samples, SEEK_SET);
						break;
					case '8':
						samples += ffactor;
						ast_seekstream(fs, samples, SEEK_SET);
						break;
						
					default:
						got = 0;
					}
					break;
				case DMODE_RECORD:
					switch(f->subclass) {
					case '1':
						ast_set_flag(&flags, DFLAG_PAUSE);
						mode = DMODE_PLAY;
						break;
					case '8':
						ast_toggle_flag(&flags, DFLAG_TRUNC);
						lastop = 0;
						break;
					default:
						got = 0;
					}
					break;
				default:
					got = 0;
				}
				if (!got) {
					switch(f->subclass) {
					case '#':
						done = 1;
						continue;
						break;
					case '*':
						ast_toggle_flag(&flags, DFLAG_PAUSE);
						if (ast_test_flag(&flags, DFLAG_PAUSE)) {
							digit = play_and_wait(chan, "dictate/pause", AST_DIGIT_ANY);
						} else {
							digit = play_and_wait(chan, mode == DMODE_PLAY ? "dictate/playback" : "dictate/record", AST_DIGIT_ANY);
						}
						break;
					case '0':
						ast_set_flag(&flags, DFLAG_PAUSE);
						digit = play_and_wait(chan, "dictate/paused", AST_DIGIT_ANY);
						switch(mode) {
						case DMODE_PLAY:
							digit = play_and_wait(chan, "dictate/play_help", AST_DIGIT_ANY);
							break;
						case DMODE_RECORD:
							digit = play_and_wait(chan, "dictate/record_help", AST_DIGIT_ANY);
							break;
						}
						if (digit == 0) {
							digit = play_and_wait(chan, "dictate/both_help", AST_DIGIT_ANY);
						} else if (digit < 0) {
							done = 1;
							break;
						}
						break;
					}
				}
				
			} else if (f->frametype == AST_FRAME_VOICE) {
				switch(mode) {
					struct ast_frame *fr;
					int x;
				case DMODE_PLAY:
					if (lastop != DMODE_PLAY) {
						if (ast_test_flag(&flags, DFLAG_PAUSE)) {
							digit = play_and_wait(chan, "dictate/playback_mode", AST_DIGIT_ANY);
							if (digit == 0) {
								digit = play_and_wait(chan, "dictate/paused", AST_DIGIT_ANY);
							} else if (digit < 0) {
								break;
							}
						}
						if (lastop != DFLAG_PLAY) {
							lastop = DFLAG_PLAY;
							ast_closestream(fs);
							fs = ast_openstream(chan, path, chan->language);
							ast_seekstream(fs, samples, SEEK_SET);
							chan->stream = NULL;
						}
						lastop = DMODE_PLAY;
					}

					if (!ast_test_flag(&flags, DFLAG_PAUSE)) {
						for (x = 0; x < speed; x++) {
							if ((fr = ast_readframe(fs))) {
								ast_write(chan, fr);
								samples += fr->samples;
								ast_frfree(fr);
								fr = NULL;
							} else {
								samples = 0;
								ast_seekstream(fs, 0, SEEK_SET);
							}
						}
					}
					break;
				case DMODE_RECORD:
					if (lastop != DMODE_RECORD) {
						int oflags = O_CREAT | O_WRONLY;
						if (ast_test_flag(&flags, DFLAG_PAUSE)) {						
							digit = play_and_wait(chan, "dictate/record_mode", AST_DIGIT_ANY);
							if (digit == 0) {
								digit = play_and_wait(chan, "dictate/paused", AST_DIGIT_ANY);
							} else if (digit < 0) {
								break;
							}
						}
						lastop = DMODE_RECORD;
						ast_closestream(fs);
						if ( ast_test_flag(&flags, DFLAG_TRUNC)) {
							oflags |= O_TRUNC;
							digit = play_and_wait(chan, "dictate/truncating_audio", AST_DIGIT_ANY);
						} else {
							oflags |= O_APPEND;
						}
						fs = ast_writefile(path, "raw", NULL, oflags, 0, 0700);
						if (ast_test_flag(&flags, DFLAG_TRUNC)) {
							ast_seekstream(fs, 0, SEEK_SET);
							ast_clear_flag(&flags, DFLAG_TRUNC);
						} else {
							ast_seekstream(fs, 0, SEEK_END);
						}
					}
					if (!ast_test_flag(&flags, DFLAG_PAUSE)) {
						res = ast_writestream(fs, f);
					}
					break;
				}
				
			}

			ast_frfree(f);
		}
	}
	if (oldr) {
		ast_set_read_format(chan, oldr);
	}
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
	return ast_register_application(app, dictate_exec, synopsis, desc);
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

