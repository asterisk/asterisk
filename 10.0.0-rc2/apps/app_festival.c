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
 * \brief Connect to festival
 *
 * \author Christos Ricudis <ricudis@itc.auth.gr>
 *
 * \extref  The Festival Speech Synthesis System - http://www.cstr.ed.ac.uk/projects/festival/
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/md5.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/endian.h"

#define FESTIVAL_CONFIG "festival.conf"
#define MAXLEN 180
#define MAXFESTLEN 2048

/*** DOCUMENTATION
	<application name="Festival" language="en_US">
		<synopsis>
			Say text to the user.
		</synopsis>
		<syntax>
			<parameter name="text" required="true" />
			<parameter name="intkeys" />
		</syntax>
		<description>
			<para>Connect to Festival, send the argument, get back the waveform, play it to the user,
			allowing any given interrupt keys to immediately terminate and return the value, or
			<literal>any</literal> to allow any number back (useful in dialplan).</para>
		</description>
	</application>
 ***/

static char *app = "Festival";

static char *socket_receive_file_to_buff(int fd, int *size)
{
	/* Receive file (probably a waveform file) from socket using
	 * Festival key stuff technique, but long winded I know, sorry
	 * but will receive any file without closing the stream or
	 * using OOB data
	 */
	static char *file_stuff_key = "ft_StUfF_key"; /* must == Festival's key */
	char *buff, *tmp;
	int bufflen;
	int n,k,i;
	char c;

	bufflen = 1024;
	if (!(buff = ast_malloc(bufflen)))
		return NULL;
	*size = 0;

	for (k = 0; file_stuff_key[k] != '\0';) {
		n = read(fd, &c, 1);
		if (n == 0)
			break;  /* hit stream eof before end of file */
		if ((*size) + k + 1 >= bufflen) {
			/* +1 so you can add a terminating NULL if you want */
			bufflen += bufflen / 4;
			if (!(tmp = ast_realloc(buff, bufflen))) {
				ast_free(buff);
				return NULL;
			}
			buff = tmp;
		}
		if (file_stuff_key[k] == c)
			k++;
		else if ((c == 'X') && (file_stuff_key[k+1] == '\0')) {
			/* It looked like the key but wasn't */
			for (i = 0; i < k; i++, (*size)++)
				buff[*size] = file_stuff_key[i];
			k = 0;
			/* omit the stuffed 'X' */
		} else {
			for (i = 0; i < k; i++, (*size)++)
				buff[*size] = file_stuff_key[i];
			k = 0;
			buff[*size] = c;
			(*size)++;
		}
	}

	return buff;
}

static int send_waveform_to_fd(char *waveform, int length, int fd)
{
	int res;
#if __BYTE_ORDER == __BIG_ENDIAN
	int x;
	char c;
#endif

	res = ast_safe_fork(0);
	if (res < 0)
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res) {
		return res;
	}
	dup2(fd, 0);
	ast_close_fds_above_n(0);
	if (ast_opt_high_priority)
		ast_set_priority(0);
#if __BYTE_ORDER == __BIG_ENDIAN
	for (x = 0; x < length; x += 2) {
		c = *(waveform + x + 1);
		*(waveform + x + 1) = *(waveform + x);
		*(waveform + x) = c;
	}
#endif

	if (write(0, waveform, length) < 0) {
		/* Cannot log -- all FDs are already closed */
	}

	close(fd);
	_exit(0);
}

static int send_waveform_to_channel(struct ast_channel *chan, char *waveform, int length, char *intkeys)
{
	int res = 0;
	int fds[2];
	int needed = 0;
	struct ast_format owriteformat;
	struct ast_frame *f;
	struct myframe {
		struct ast_frame f;
		char offset[AST_FRIENDLY_OFFSET];
		char frdata[2048];
	} myf = {
		.f = { 0, },
	};

	ast_format_clear(&owriteformat);
	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}

	/* Answer if it's not already going */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);
	ast_indicate(chan, -1);
	
	ast_format_copy(&owriteformat, &chan->writeformat);
	res = ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		return -1;
	}
	
	res = send_waveform_to_fd(waveform, length, fds[1]);
	if (res >= 0) {
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			res = ast_waitfor(chan, 1000);
			if (res < 1) {
				res = -1;
				break;
			}
			f = ast_read(chan);
			if (!f) {
				ast_log(LOG_WARNING, "Null frame == hangup() detected\n");
				res = -1;
				break;
			}
			if (f->frametype == AST_FRAME_DTMF) {
				ast_debug(1, "User pressed a key\n");
				if (intkeys && strchr(intkeys, f->subclass.integer)) {
					res = f->subclass.integer;
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_VOICE) {
				/* Treat as a generator */
				needed = f->samples * 2;
				if (needed > sizeof(myf.frdata)) {
					ast_log(LOG_WARNING, "Only able to deliver %d of %d requested samples\n",
						(int)sizeof(myf.frdata) / 2, needed/2);
					needed = sizeof(myf.frdata);
				}
				res = read(fds[0], myf.frdata, needed);
				if (res > 0) {
					myf.f.frametype = AST_FRAME_VOICE;
					ast_format_set(&myf.f.subclass.format, AST_FORMAT_SLINEAR, 0);
					myf.f.datalen = res;
					myf.f.samples = res / 2;
					myf.f.offset = AST_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.data.ptr = myf.frdata;
					if (ast_write(chan, &myf.f) < 0) {
						res = -1;
						ast_frfree(f);
						break;
					}
					if (res < needed) { /* last frame */
						ast_debug(1, "Last frame\n");
						res = 0;
						ast_frfree(f);
						break;
					}
				} else {
					ast_debug(1, "No more waveform\n");
					res = 0;
				}
			}
			ast_frfree(f);
		}
	}
	close(fds[0]);
	close(fds[1]);

	if (!res && owriteformat.id)
		ast_set_write_format(chan, &owriteformat);
	return res;
}

static int festival_exec(struct ast_channel *chan, const char *vdata)
{
	int usecache;
	int res = 0;
	struct sockaddr_in serv_addr;
	struct hostent *serverhost;
	struct ast_hostent ahp;
	int fd;
	FILE *fs;
	const char *host;
	const char *cachedir;
	const char *temp;
	const char *festivalcommand;
	int port = 1314;
	int n;
	char ack[4];
	char *waveform;
	int filesize;
	char bigstring[MAXFESTLEN];
	int i;
	struct MD5Context md5ctx;
	unsigned char MD5Res[16];
	char MD5Hex[33] = "";
	char koko[4] = "";
	char cachefile[MAXFESTLEN]="";
	int readcache = 0;
	int writecache = 0;
	int strln;
	int fdesc = -1;
	char buffer[16384];
	int seekpos = 0;	
	char *data;	
	struct ast_config *cfg;
	char *newfestivalcommand;
	struct ast_flags config_flags = { 0 };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(text);
		AST_APP_ARG(interrupt);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "festival requires an argument (text)\n");
		return -1;
	}

	cfg = ast_config_load(FESTIVAL_CONFIG, config_flags);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", FESTIVAL_CONFIG);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " FESTIVAL_CONFIG " is in an invalid format.  Aborting.\n");
		return -1;
	}

	if (!(host = ast_variable_retrieve(cfg, "general", "host"))) {
		host = "localhost";
	}
	if (!(temp = ast_variable_retrieve(cfg, "general", "port"))) {
		port = 1314;
	} else {
		port = atoi(temp);
	}
	if (!(temp = ast_variable_retrieve(cfg, "general", "usecache"))) {
		usecache = 0;
	} else {
		usecache = ast_true(temp);
	}
	if (!(cachedir = ast_variable_retrieve(cfg, "general", "cachedir"))) {
		cachedir = "/tmp/";
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	if (!(festivalcommand = ast_variable_retrieve(cfg, "general", "festivalcommand"))) {
		const char *startcmd = "(tts_textasterisk \"";
		const char *endcmd = "\" 'file)(quit)\n";

		strln = strlen(startcmd) + strlen(args.text) + strlen(endcmd) + 1;
		newfestivalcommand = alloca(strln);
		snprintf(newfestivalcommand, strln, "%s%s%s", startcmd, args.text, endcmd);
		festivalcommand = newfestivalcommand;
	} else { /* This else parses the festivalcommand that we're sent from the config file for \n's, etc */
		int x, j;
		newfestivalcommand = alloca(strlen(festivalcommand) + strlen(args.text) + 1);

		for (x = 0, j = 0; x < strlen(festivalcommand); x++) {
			if (festivalcommand[x] == '\\' && festivalcommand[x + 1] == 'n') {
				newfestivalcommand[j++] = '\n';
				x++;
			} else if (festivalcommand[x] == '\\') {
				newfestivalcommand[j++] = festivalcommand[x + 1];
				x++;
			} else if (festivalcommand[x] == '%' && festivalcommand[x + 1] == 's') {
				sprintf(&newfestivalcommand[j], "%s", args.text); /* we know it is big enough */
				j += strlen(args.text);
				x++;
			} else
				newfestivalcommand[j++] = festivalcommand[x];
		}
		newfestivalcommand[j] = '\0';
		festivalcommand = newfestivalcommand;
	}
	
	if (args.interrupt && !strcasecmp(args.interrupt, "any"))
		args.interrupt = AST_DIGIT_ANY;

	ast_debug(1, "Text passed to festival server : %s\n", args.text);
	/* Connect to local festival server */
	
	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (fd < 0) {
		ast_log(LOG_WARNING, "festival_client: can't get socket\n");
		ast_config_destroy(cfg);
		return -1;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));

	if ((serv_addr.sin_addr.s_addr = inet_addr(host)) == -1) {
		/* its a name rather than an ipnum */
		serverhost = ast_gethostbyname(host, &ahp);

		if (serverhost == NULL) {
			ast_log(LOG_WARNING, "festival_client: gethostbyname failed\n");
			ast_config_destroy(cfg);
			return -1;
		}
		memmove(&serv_addr.sin_addr, serverhost->h_addr, serverhost->h_length);
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
		ast_log(LOG_WARNING, "festival_client: connect to server failed\n");
		ast_config_destroy(cfg);
		return -1;
	}

	/* Compute MD5 sum of string */
	MD5Init(&md5ctx);
	MD5Update(&md5ctx, (unsigned char *)args.text, strlen(args.text));
	MD5Final(MD5Res, &md5ctx);
	MD5Hex[0] = '\0';

	/* Convert to HEX and look if there is any matching file in the cache 
		directory */
	for (i = 0; i < 16; i++) {
		snprintf(koko, sizeof(koko), "%X", MD5Res[i]);
		strncat(MD5Hex, koko, sizeof(MD5Hex) - strlen(MD5Hex) - 1);
	}
	readcache = 0;
	writecache = 0;
	if (strlen(cachedir) + strlen(MD5Hex) + 1 <= MAXFESTLEN && (usecache == -1)) {
		snprintf(cachefile, sizeof(cachefile), "%s/%s", cachedir, MD5Hex);
		fdesc = open(cachefile, O_RDWR);
		if (fdesc == -1) {
			fdesc = open(cachefile, O_CREAT | O_RDWR, AST_FILE_MODE);
			if (fdesc != -1) {
				writecache = 1;
				strln = strlen(args.text);
				ast_debug(1, "line length : %d\n", strln);
    				if (write(fdesc,&strln,sizeof(int)) < 0) {
					ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
				}
    				if (write(fdesc,data,strln) < 0) {
					ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
				}
				seekpos = lseek(fdesc, 0, SEEK_CUR);
				ast_debug(1, "Seek position : %d\n", seekpos);
			}
		} else {
    			if (read(fdesc,&strln,sizeof(int)) != sizeof(int)) {
				ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
			}
			ast_debug(1, "Cache file exists, strln=%d, strlen=%d\n", strln, (int)strlen(args.text));
			if (strlen(args.text) == strln) {
				ast_debug(1, "Size OK\n");
    				if (read(fdesc,&bigstring,strln) != strln) {
					ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
				}
				bigstring[strln] = 0;
				if (strcmp(bigstring, args.text) == 0) { 
					readcache = 1;
				} else {
					ast_log(LOG_WARNING, "Strings do not match\n");
				}
			} else {
				ast_log(LOG_WARNING, "Size mismatch\n");
			}
		}
	}

	if (readcache == 1) {
		close(fd);
		fd = fdesc;
		ast_debug(1, "Reading from cache...\n");
	} else {
		ast_debug(1, "Passing text to festival...\n");
		fs = fdopen(dup(fd), "wb");

		fprintf(fs, "%s", festivalcommand);
		fflush(fs);
		fclose(fs);
	}
	
	/* Write to cache and then pass it down */
	if (writecache == 1) {
		ast_debug(1, "Writing result to cache...\n");
		while ((strln = read(fd, buffer, 16384)) != 0) {
			if (write(fdesc,buffer,strln) < 0) {
				ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
			}
		}
		close(fd);
		close(fdesc);
		fd = open(cachefile, O_RDWR);
		lseek(fd, seekpos, SEEK_SET);
	}
	
	ast_debug(1, "Passing data to channel...\n");

	/* Read back info from server */
	/* This assumes only one waveform will come back, also LP is unlikely */
	do {
		int read_data;
		for (n = 0; n < 3; ) {
			read_data = read(fd, ack + n, 3 - n);
			/* this avoids falling in infinite loop
			 * in case that festival server goes down
			 */
			if (read_data == -1) {
				ast_log(LOG_WARNING, "Unable to read from cache/festival fd\n");
				close(fd);
				ast_config_destroy(cfg);
				return -1;
			}
			n += read_data;
		}
		ack[3] = '\0';
		if (strcmp(ack, "WV\n") == 0) {         /* receive a waveform */
			ast_debug(1, "Festival WV command\n");
			if ((waveform = socket_receive_file_to_buff(fd, &filesize))) {
				res = send_waveform_to_channel(chan, waveform, filesize, args.interrupt);
				ast_free(waveform);
			}
			break;
		} else if (strcmp(ack, "LP\n") == 0) {   /* receive an s-expr */
			ast_debug(1, "Festival LP command\n");
			if ((waveform = socket_receive_file_to_buff(fd, &filesize))) {
				waveform[filesize] = '\0';
				ast_log(LOG_WARNING, "Festival returned LP : %s\n", waveform);
				ast_free(waveform);
			}
		} else if (strcmp(ack, "ER\n") == 0) {    /* server got an error */
			ast_log(LOG_WARNING, "Festival returned ER\n");
			res = -1;
			break;
		}
	} while (strcmp(ack, "OK\n") != 0);
	close(fd);
	ast_config_destroy(cfg);
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(FESTIVAL_CONFIG, config_flags);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", FESTIVAL_CONFIG);
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " FESTIVAL_CONFIG " is in an invalid format.  Aborting.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_config_destroy(cfg);
	return ast_register_application_xml(app, festival_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Simple Festival Interface");
