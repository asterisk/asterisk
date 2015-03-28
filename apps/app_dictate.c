/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * Donated by Sangoma Technologies <http://www.samgoma.com>
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
 * \brief Virtual Dictation Machine Application For Asterisk
 *
 * \author Anthony Minessale II <anthmct@yahoo.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include "asterisk/paths.h" /* use ast_config_AST_SPOOL_DIR */
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="Dictate" language="en_US">
		<synopsis>
			Virtual Dictation Machine.
		</synopsis>
		<syntax>
			<parameter name="base_dir" />
			<parameter name="filename" />
		</syntax>
		<description>
			<para>Start dictation machine using optional <replaceable>base_dir</replaceable> for files.</para>
		</description>
	</application>
 ***/

static const char app[] = "Dictate";

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
	if (!ast_streamfile(chan, file, ast_channel_language(chan))) {
		res = ast_waitstream(chan, digits);
	}
	return res;
}

static int dictate_exec(struct ast_channel *chan, const char *data)
{
	char *path = NULL, filein[256], *filename = "";
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(base);
		AST_APP_ARG(filename);
	);
	char dftbase[256];
	char *base;
	struct ast_flags flags = {0};
	struct ast_filestream *fs;
	struct ast_frame *f = NULL;
	int ffactor = 320 * 80,
		res = 0,
		done = 0,
		lastop = 0,
		samples = 0,
		speed = 1,
		digit = 0,
		len = 0,
		maxlen = 0,
		mode = 0;
	struct ast_format oldr;
	ast_format_clear(&oldr);

	snprintf(dftbase, sizeof(dftbase), "%s/dictate", ast_config_AST_SPOOL_DIR);
	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, parse);
	} else
		args.argc = 0;

	if (args.argc && !ast_strlen_zero(args.base)) {
		base = args.base;
	} else {
		base = dftbase;
	}
	if (args.argc > 1 && args.filename) {
		filename = args.filename;
	}
	ast_format_copy(&oldr, ast_channel_readformat(chan));
	if ((res = ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR)) < 0) {
		ast_log(LOG_WARNING, "Unable to set to linear mode.\n");
		return -1;
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}
	ast_safe_sleep(chan, 200);
	for (res = 0; !res;) {
		if (ast_strlen_zero(filename)) {
			if (ast_app_getdata(chan, "dictate/enter_filename", filein, sizeof(filein), 0) ||
				ast_strlen_zero(filein)) {
				res = -1;
				break;
			}
		} else {
			ast_copy_string(filein, filename, sizeof(filein));
			filename = "";
		}
		ast_mkdir(base, 0755);
		len = strlen(base) + strlen(filein) + 2;
		if (!path || len > maxlen) {
			path = ast_alloca(len);
			memset(path, 0, len);
			maxlen = len;
		} else {
			memset(path, 0, maxlen);
		}

		snprintf(path, len, "%s/%s", base, filein);
		fs = ast_writefile(path, "raw", NULL, O_CREAT|O_APPEND, 0, AST_FILE_MODE);
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
				struct ast_frame fr = {AST_FRAME_DTMF, { .integer = digit } };
				ast_queue_frame(chan, &fr);
				digit = 0;
			}
			if (f->frametype == AST_FRAME_DTMF) {
				int got = 1;
				switch(mode) {
				case DMODE_PLAY:
					switch (f->subclass.integer) {
					case '1':
						ast_set_flag(&flags, DFLAG_PAUSE);
						mode = DMODE_RECORD;
						break;
					case '2':
						speed++;
						if (speed > 4) {
							speed = 1;
						}
						res = ast_say_number(chan, speed, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
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
					switch (f->subclass.integer) {
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
					switch (f->subclass.integer) {
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
							if (!(fs = ast_openstream(chan, path, ast_channel_language(chan))))
								break;
							ast_seekstream(fs, samples, SEEK_SET);
							ast_channel_stream_set(chan, NULL);
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
						fs = ast_writefile(path, "raw", NULL, oflags, 0, AST_FILE_MODE);
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
	if (oldr.id) {
		ast_set_read_format(chan, &oldr);
	}
	return 0;
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	return res;
}

static int load_module(void)
{
	return ast_register_application_xml(app, dictate_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Virtual Dictation Machine");
