/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to record a sound file
 * 
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * Matthew Fredrickson <creslin@linux-support.net>
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
#include <asterisk/dsp.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Trivial Record Application";

static char *app = "Record";

static char *synopsis = "Record to a file";

static char *descrip = 
"  Record(filename:format|silence[|maxduration][|option])\n\n"
"Records from the channel into a given filename. If the file exists it will\n"
"be overwritten.\n"
"- 'format' is the format of the file type to be recorded (wav, gsm, etc).\n"
"- 'silence' is the number of seconds of silence to allow before returning.\n"
"- 'maxduration' is the maximum recording duration in seconds. If missing\n"
"or 0 there is no maximum.\n"
"- 'option' may be 'skip' to return immediately if the line is not up,\n"
"or 'noanswer' to attempt to record even if the line is not up.\n\n"
"If filename contains '%d', these characters will be replaced with a number\n"
"incremented by one each time the file is recorded. \n\n"
"Formats: g723, g729, gsm, h263, ulaw, alaw, vox, wav, WAV\n\n"
"User can press '#' to terminate the recording and continue to the next priority.\n\n"
"Returns -1 when the user hangs up.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int record_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int count = 0;
	int percentflag = 0;
	char fil[256];
	char tmp[256];
	char ext[10];
	char *vdata;
	int i = 0;
	int j = 0;

	struct ast_filestream *s = '\0';
	struct localuser *u;
	struct ast_frame *f = NULL;
	
	struct ast_dsp *sildet = NULL;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int silence = 0;		/* amount of silence to allow */
	int gotsilence = 0;		/* did we timeout for silence? */
	char silencestr[5];
	char durationstr[8];
	int maxduration = 0;		/* max duration of recording */
	int gottimeout = 0;		/* did we timeout for maxduration exceeded? */
	time_t timeout = 0;
	char option[16];
	int option_skip = 0;
	int option_noanswer = 0;
	int rfmt = 0;
	int flags;
	char *end=NULL;
	char *p=NULL;



	/* The next few lines of code parse out the filename and header from the input string */
	if (!data) { /* no data implies no filename or anything is present */
		ast_log(LOG_WARNING, "Record requires an argument (filename)\n");
		return -1;
	}
	vdata = ast_strdupa(data);
	
	p = vdata;
	while(p && (p=strchr(p,':'))) {
		end=p;
		if(!strcasecmp(end,":end")) {
			*end='\0';
			end++;
			break;
		}
		p++;
		end=NULL;
	}
	

	for (; vdata[i] && (vdata[i] != ':') && (vdata[i] != '|'); i++ ) {
		if ((vdata[i] == '%') && (vdata[i+1] == 'd')) {
			percentflag = 1;                      /* the wildcard is used */
		}
		if (j < sizeof(fil) - 1)
			fil[j++] = vdata[i];
	}
	fil[j] = '\0';

	if (vdata[i] != ':') {
		ast_log(LOG_WARNING, "No extension found\n");
		return -1;
	}
	i++;

	j = 0;
	for (; vdata[i] && (vdata[i] != '|'); i++)
		if (j < sizeof(ext) - 1)
			ext[j++] = vdata[i];
	ext[j] = '\0';

	if (vdata[i] == '|')
		i++;

	j = 0;
	for (; vdata[i] && (vdata[i] != '|'); i++)
		if (j < sizeof(silencestr) - 1)
			silencestr[j++] = vdata[i];
	silencestr[j] = '\0';

	if (j > 0) {
		silence = atoi(silencestr);
		if (silence > 0)
			silence *= 1000;
	}

	if (vdata[i] == '|')
		i++;

	j = 0;
	for (; vdata[i] && (vdata[i] != '|'); i++)
		if (j < sizeof(durationstr) - 1)
			durationstr[j++] = vdata[i];
	durationstr[j] = '\0';

	if (j > 0)
		maxduration = atoi(durationstr);

	if (vdata[i] == '|')
		i++;

	j = 0;
	for (; vdata[i] && (vdata[i] != '|'); i++)
		if (j < sizeof(option) - 1)
			option[j++] = vdata[i];
	option[j] = '\0';

	if (!strcasecmp(option, "skip"))
		option_skip = 1;
	if (!strcasecmp(option, "noanswer"))
		option_noanswer = 1;

	/* done parsing */

	
	/* these are to allow the use of the %d in the config file for a wild card of sort to
	  create a new file with the inputed name scheme */
	if (percentflag) {
		do {
			snprintf(tmp, sizeof(tmp), fil, count);
			count++;
		} while ( ast_fileexists(tmp, ext, chan->language) != -1 );
		pbx_builtin_setvar_helper(chan, "RECORDED_FILE", tmp);
	} else
		strncpy(tmp, fil, sizeof(tmp)-1);
	/* end of routine mentioned */

	LOCAL_USER_ADD(u);

	if (chan->_state != AST_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			LOCAL_USER_REMOVE(u);
			return 0;
		} else if (!option_noanswer) {
			/* Otherwise answer unless we're supposed to record while on-hook */
			res = ast_answer(chan);
		}
	}
	
	if (!res) {
		/* Some code to play a nice little beep to signify the start of the record operation */
		res = ast_streamfile(chan, "beep", chan->language);
		if (!res) {
			res = ast_waitstream(chan, "");
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", chan->name);
		}
		ast_stopstream(chan);

		/* The end of beep code.  Now the recording starts */


		if (silence > 0) {
	        	rfmt = chan->readformat;
        		res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
        		if (res < 0) {
                		ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
        		        return -1;
        		}
			sildet = ast_dsp_new();
        		if (!sildet) {
                		ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
                		return -1;
		       	}
			ast_dsp_set_threshold(sildet, 256);
		}


		flags = end ? O_CREAT|O_APPEND|O_WRONLY : O_CREAT|O_TRUNC|O_WRONLY;
		s = ast_writefile( tmp, ext, NULL, flags , 0, 0644);


		if (s) {
			if (maxduration > 0)
				timeout = time(NULL) + (time_t)maxduration;

			while (ast_waitfor(chan, -1) > -1) {
				if (maxduration > 0 && time(NULL) > timeout) {
					gottimeout = 1;
					break;
				}

				f = ast_read(chan);
				if (!f) {
					res = -1;
					break;
				}
				if (f->frametype == AST_FRAME_VOICE) {
					res = ast_writestream(s, f);
					
					if (res) {
						ast_log(LOG_WARNING, "Problem writing frame\n");
						break;
					}

					if (silence > 0) {
						dspsilence = 0;
						ast_dsp_silence(sildet, f, &dspsilence);
						if (dspsilence) {
							totalsilence = dspsilence;
						} else {
							totalsilence = 0;
						}
	                                	if (totalsilence > silence) {
        	                        	        /* Ended happily with silence */
							ast_frfree(f);
							gotsilence = 1;
                	                	        break;
	                     	        	}
					}
				}
				if (f->frametype == AST_FRAME_VIDEO) {
					res = ast_writestream(s, f);
					
					if (res) {
						ast_log(LOG_WARNING, "Problem writing frame\n");
						break;
					}
				}
				if ((f->frametype == AST_FRAME_DTMF) &&
					(f->subclass == '#')) {
					ast_frfree(f);
					break;
				}
				ast_frfree(f);
			}
			if (!f) {
					ast_log(LOG_DEBUG, "Got hangup\n");
					res = -1;
			}

			if (gotsilence) {
				ast_stream_rewind(s, silence-1000);
				ast_truncstream(s);
			} else if (!gottimeout) {
				/* Strip off the last 1/4 second of it */
				ast_stream_rewind(s, 250);
				ast_truncstream(s);
			}
			ast_closestream(s);
		} else			
			ast_log(LOG_WARNING, "Could not create file %s\n", fil);
	} else
		ast_log(LOG_WARNING, "Could not answer channel '%s'\n", chan->name);

	LOCAL_USER_REMOVE(u);
	if ((silence > 0) && rfmt) {
	        res = ast_set_read_format(chan, rfmt);
        	if (res)
        	        ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
		if (sildet)
			ast_dsp_free(sildet);
	}
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, record_exec, synopsis, descrip);
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
