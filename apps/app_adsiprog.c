/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Program Asterisk ADSI Scripts into phone
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/adsi.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include "../asterisk.h"
#include "../astconf.h"

static char *tdesc = "Asterisk ADSI Programming Application";

static char *app = "ADSIProg";

static char *synopsis = "Load Asterisk ADSI Scripts into phone";

/* #define DUMP_MESSAGES */

static char *descrip =
"  ADSIProg(script): Programs an ADSI Phone with the given script.\n"
"If none is specified, the default is used.  Returns 0 unless CPE\n" 
"is hungup.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

struct adsi_event {
	int id;
	char *name;
};

static struct adsi_event events[] = {
	{ 1, "CALLERID" },
	{ 2, "VMWI" },
	{ 3, "NEARANSWER" },
	{ 4, "FARANSWER" },
	{ 5, "ENDOFRING" },
	{ 6, "IDLE" },
	{ 7, "OFFHOOK" },
	{ 8, "CIDCW" },
	{ 9, "BUSY" },
	{ 10, "FARRING" },
	{ 11, "DIALTONE" },
	{ 12, "RECALL" },
	{ 13, "MESSAGE" },
	{ 14, "REORDER" },
	{ 15, "DISTINCTIVERING" },
	{ 16, "RING" },
	{ 17, "REMINDERRING" },
	{ 18, "SPECIALRING" },
	{ 19, "CODEDRING" },
	{ 20, "TIMER" },
	{ 21, "INUSE" },
	{ 22, "EVENT22" },
	{ 23, "EVENT23" },
	{ 24, "CPEID" },
};

static struct adsi_event justify[] = {
	{ 0, "CENTER" },
	{ 1, "RIGHT" },
	{ 2, "LEFT" },
	{ 3, "INDENT" },
};

#define STATE_NORMAL		0
#define STATE_INKEY		1
#define STATE_INSUB		2
#define STATE_INIF		3

#define MAX_RET_CODE		20
#define MAX_SUB_LEN		255
#define MAX_MAIN_LEN		1600

#define ARG_STRING		(1 << 0)
#define ARG_NUMBER		(1 << 1)

struct adsi_soft_key {
	char vname[40];		/* Which "variable" is associated with it */
	int retstrlen;		/* Length of return string */
	int initlen;		/* initial length */
	int id;
	int defined;
	char retstr[80];	/* Return string data */
};

struct adsi_subscript {
	char vname[40];
	int id;
	int defined;
	int datalen;
	int inscount;
	int ifinscount;
	char *ifdata;
	char data[2048];
};

struct adsi_state {
	char vname[40];
	int id;
};

struct adsi_flag {
	char vname[40];
	int id;
};

struct adsi_display {
	char vname[40];
	int id;
	char data[70];
	int datalen;
};

struct adsi_script {
	int state;
	int numkeys;
	int numsubs;
	int numstates;
	int numdisplays;
	int numflags;
	struct adsi_soft_key *key;
	struct adsi_subscript *sub;
	/* Pre-defined displays */
	struct adsi_display displays[63];
	/* ADSI States 1 (initial) - 254 */
	struct adsi_state states[256];
	/* Keys 2-63 */
	struct adsi_soft_key keys[62];
	/* Subscripts 0 (main) to 127 */
	struct adsi_subscript subs[128];
	/* Flags 1-7 */
	struct adsi_flag flags[7];

	/* Stuff from adsi script */
	char sec[5];
	char desc[19];
	char fdn[5];
	int ver;
};


static int process_token(void *out, char *src, int maxlen, int argtype)
{
	if ((strlen(src) > 1) && src[0] == '\"') {
		/* This is a quoted string */
		if (!(argtype & ARG_STRING))
			return -1;
		src++;
		/* Don't take more than what's there */
		if (maxlen > strlen(src) - 1)
			maxlen = strlen(src) - 1;
		memcpy(out, src, maxlen);
		((char *)out)[maxlen] = '\0';
	} else if (!ast_strlen_zero(src) && (src[0] == '\\')) {
		if (!(argtype & ARG_NUMBER))
			return -1;
		/* Octal value */
		if (sscanf(src, "%o", (int *)out) != 1)
			return -1;
		if (argtype & ARG_STRING) {
			/* Convert */
			*((unsigned int *)out) = htonl(*((unsigned int *)out));
		}
	} else if ((strlen(src) > 2) && (src[0] == '0') && (tolower(src[1]) == 'x')) {
		if (!(argtype & ARG_NUMBER))
			return -1;
		/* Hex value */
		if (sscanf(src + 2, "%x", (unsigned int *)out) != 1)
			return -1;
		if (argtype & ARG_STRING) {
			/* Convert */
			*((unsigned int *)out) = htonl(*((unsigned int *)out));
		}
	} else if ((!ast_strlen_zero(src) && isdigit(src[0]))) {
		if (!(argtype & ARG_NUMBER))
			return -1;
		/* Hex value */
		if (sscanf(src, "%d", (int *)out) != 1)
			return -1;
		if (argtype & ARG_STRING) {
			/* Convert */
			*((unsigned int *)out) = htonl(*((unsigned int *)out));
		}
	} else
		return -1;
	return 0;
}

static char *get_token(char **buf, char *script, int lineno)
{
	char *tmp = *buf;
	char *keyword;
	int quoted = 0;
	/* Advance past any white space */
	while(*tmp && (*tmp < 33))
		tmp++;
	if (!*tmp)
		return NULL;
	keyword = tmp;
	while(*tmp && ((*tmp > 32)  || quoted)) {
		if (*tmp == '\"') {
			quoted = !quoted;
		}
		tmp++;
	}
	if (quoted) {
		ast_log(LOG_WARNING, "Mismatched quotes at line %d of %s\n", lineno, script);
		return NULL;
	}
	*tmp = '\0';
	tmp++;
	while(*tmp && (*tmp < 33))
		tmp++;
	/* Note where we left off */
	*buf = tmp;
	return keyword;
}

static char *validdtmf = "123456789*0#ABCD";

static int send_dtmf(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char dtmfstr[80];
	char *a;
	int bytes=0;
	a = get_token(&args, script, lineno);
	if (!a) {
		ast_log(LOG_WARNING, "Expecting something to send for SENDDTMF at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(dtmfstr, a, sizeof(dtmfstr) - 1, ARG_STRING)) {
		ast_log(LOG_WARNING, "Invalid token for SENDDTMF at line %d of %s\n", lineno, script);
		return 0;
	}
	a = dtmfstr;
	while(*a) {
		if (strchr(validdtmf, *a)) {
			*buf = *a;
			buf++;
			bytes++;
		} else
			ast_log(LOG_WARNING, "'%c' is not a valid DTMF tone at line %d of %s\n", *a, lineno, script);
		a++;
	}
	return bytes;
}

static int goto_line(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *page;
	char *gline;
	int line;
	unsigned char cmd;
	page = get_token(&args, script, lineno);
	gline = get_token(&args, script, lineno);
	if (!page || !gline) {
		ast_log(LOG_WARNING, "Expecting page and line number for GOTOLINE at line %d of %s\n", lineno, script);
		return 0;
	}
	if (!strcasecmp(page, "INFO")) {
		cmd = 0;
	} else if (!strcasecmp(page, "COMM")) {
		cmd = 0x80;
	} else {
		ast_log(LOG_WARNING, "Expecting either 'INFO' or 'COMM' page, got got '%s' at line %d of %s\n", page, lineno, script);
		return 0;
	}
	if (process_token(&line, gline, sizeof(line), ARG_NUMBER)) {
		ast_log(LOG_WARNING, "Invalid line number '%s' at line %d of %s\n", gline, lineno, script);
		return 0;
	}
	cmd |= line;
	buf[0] = 0x8b;
	buf[1] = cmd;
	return 2;
}

static int goto_line_rel(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *dir;
	char *gline;
	int line;
	unsigned char cmd;
	dir = get_token(&args, script, lineno);
	gline = get_token(&args, script, lineno);
	if (!dir || !gline) {
		ast_log(LOG_WARNING, "Expecting direction and number of lines for GOTOLINEREL at line %d of %s\n", lineno, script);
		return 0;
	}
	if (!strcasecmp(dir, "UP")) {
		cmd = 0;
	} else if (!strcasecmp(dir, "DOWN")) {
		cmd = 0x20;
	} else {
		ast_log(LOG_WARNING, "Expecting either 'UP' or 'DOWN' direction, got '%s' at line %d of %s\n", dir, lineno, script);
		return 0;
	}
	if (process_token(&line, gline, sizeof(line), ARG_NUMBER)) {
		ast_log(LOG_WARNING, "Invalid line number '%s' at line %d of %s\n", gline, lineno, script);
		return 0;
	}
	cmd |= line;
	buf[0] = 0x8c;
	buf[1] = cmd;
	return 2;
}

static int send_delay(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *gtime;
	int ms;
	gtime = get_token(&args, script, lineno);
	if (!gtime) {
		ast_log(LOG_WARNING, "Expecting number of milliseconds to wait at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(&ms, gtime, sizeof(ms), ARG_NUMBER)) {
		ast_log(LOG_WARNING, "Invalid delay milliseconds '%s' at line %d of %s\n", gtime, lineno, script);
		return 0;
	}
	buf[0] = 0x90;
	if (id == 11)
		buf[1] = ms / 100;
	else
		buf[1] = ms / 10;
	return 2;
}

static int set_state(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *gstate;
	int state;
	gstate = get_token(&args, script, lineno);
	if (!gstate) {
		ast_log(LOG_WARNING, "Expecting state number at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(&state, gstate, sizeof(state), ARG_NUMBER)) {
		ast_log(LOG_WARNING, "Invalid state number '%s' at line %d of %s\n", gstate, lineno, script);
		return 0;
	}
	buf[0] = id;
	buf[1] = state;
	return 2;
}

static int cleartimer(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *tok;
	tok = get_token(&args, script, lineno);
	if (tok) 
		ast_log(LOG_WARNING, "Clearing timer requires no arguments ('%s') at line %d of %s\n", tok, lineno, script);

	buf[0] = id;
	/* For some reason the clear code is different slightly */
	if (id == 7)
		buf[1] = 0x10;
	else
		buf[1] = 0x00;
	return 2;
}

static struct adsi_flag *getflagbyname(struct adsi_script *state, char *name, char *script, int lineno, int create)
{
	int x;
	for (x=0;x<state->numflags;x++) 
		if (!strcasecmp(state->flags[x].vname, name)) 
			return &state->flags[x];
	/* Return now if we're not allowed to create */
	if (!create)
		return NULL;
	if (state->numflags > 6) {
		ast_log(LOG_WARNING, "No more flag space at line %d of %s\n", lineno, script);
		return NULL;
	}
	strncpy(state->flags[state->numflags].vname, name, sizeof(state->flags[state->numflags].vname) - 1);
	state->flags[state->numflags].id = state->numflags + 1;
	state->numflags++;
	return &state->flags[state->numflags-1];
}

static int setflag(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *tok;
	char sname[80];
	struct adsi_flag *flag;
	tok = get_token(&args, script, lineno);
	if (!tok) {
		ast_log(LOG_WARNING, "Setting flag requires a flag number at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(sname, tok, sizeof(sname) - 1, ARG_STRING)) {
		ast_log(LOG_WARNING, "Invalid flag '%s' at line %d of %s\n", tok, lineno, script);
		return 0;
	}
	flag = getflagbyname(state, sname, script, lineno, 0);
	if (!flag) {
		ast_log(LOG_WARNING, "Flag '%s' is undeclared at line %d of %s\n", sname, lineno, script);
		return 0;
	}
	buf[0] = id;
	buf[1] = ((flag->id & 0x7) << 4) | 1;
	return 2;
}

static int clearflag(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *tok;
	struct adsi_flag *flag;
	char sname[80];
	tok = get_token(&args, script, lineno);
	if (!tok) {
		ast_log(LOG_WARNING, "Clearing flag requires a flag number at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(sname, tok, sizeof(sname) - 1, ARG_STRING)) {
		ast_log(LOG_WARNING, "Invalid flag '%s' at line %d of %s\n", tok, lineno, script);
		return 0;
	}
	flag = getflagbyname(state, sname, script, lineno, 0);
	if (!flag) {
		ast_log(LOG_WARNING, "Flag '%s' is undeclared at line %d of %s\n", sname, lineno, script);
		return 0;
	}
	buf[0] = id;
	buf[1] = ((flag->id & 0x7) << 4);
	return 2;
}

static int starttimer(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *tok;
	int secs;
	tok = get_token(&args, script, lineno);
	if (!tok) {
		ast_log(LOG_WARNING, "Missing number of seconds at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(&secs, tok, sizeof(secs), ARG_NUMBER)) {
		ast_log(LOG_WARNING, "Invalid number of seconds '%s' at line %d of %s\n", tok, lineno, script);
		return 0;
	}
	buf[0] = id;
	buf[1] = 0x1;
	buf[2] = secs;
	return 3;
}

static int geteventbyname(char *name)
{
	int x;
	for (x=0;x<sizeof(events) / sizeof(events[0]); x++) {
		if (!strcasecmp(events[x].name, name))
			return events[x].id;
	}
	return 0;
}

static int getjustifybyname(char *name)
{
	int x;
	for (x=0;x<sizeof(justify) / sizeof(justify[0]); x++) {
		if (!strcasecmp(justify[x].name, name))
			return justify[x].id;
	}
	return -1;
}

static struct adsi_soft_key *getkeybyname(struct adsi_script *state, char *name, char *script, int lineno)
{
	int x;
	for (x=0;x<state->numkeys;x++) 
		if (!strcasecmp(state->keys[x].vname, name)) 
			return &state->keys[x];
	if (state->numkeys > 61) {
		ast_log(LOG_WARNING, "No more key space at line %d of %s\n", lineno, script);
		return NULL;
	}
	strncpy(state->keys[state->numkeys].vname, name, sizeof(state->keys[state->numkeys].vname) - 1);
	state->keys[state->numkeys].id = state->numkeys + 2;
	state->numkeys++;
	return &state->keys[state->numkeys-1];
}

static struct adsi_subscript *getsubbyname(struct adsi_script *state, char *name, char *script, int lineno)
{
	int x;
	for (x=0;x<state->numsubs;x++) 
		if (!strcasecmp(state->subs[x].vname, name)) 
			return &state->subs[x];
	if (state->numsubs > 127) {
		ast_log(LOG_WARNING, "No more subscript space at line %d of %s\n", lineno, script);
		return NULL;
	}
	strncpy(state->subs[state->numsubs].vname, name, sizeof(state->subs[state->numsubs].vname) - 1);
	state->subs[state->numsubs].id = state->numsubs;
	state->numsubs++;
	return &state->subs[state->numsubs-1];
}

static struct adsi_state *getstatebyname(struct adsi_script *state, char *name, char *script, int lineno, int create)
{
	int x;
	for (x=0;x<state->numstates;x++) 
		if (!strcasecmp(state->states[x].vname, name)) 
			return &state->states[x];
	/* Return now if we're not allowed to create */
	if (!create)
		return NULL;
	if (state->numstates > 253) {
		ast_log(LOG_WARNING, "No more state space at line %d of %s\n", lineno, script);
		return NULL;
	}
	strncpy(state->states[state->numstates].vname, name, sizeof(state->states[state->numstates].vname) - 1);
	state->states[state->numstates].id = state->numstates + 1;
	state->numstates++;
	return &state->states[state->numstates-1];
}

static struct adsi_display *getdisplaybyname(struct adsi_script *state, char *name, char *script, int lineno, int create)
{
	int x;
	for (x=0;x<state->numdisplays;x++) 
		if (!strcasecmp(state->displays[x].vname, name)) 
			return &state->displays[x];
	/* Return now if we're not allowed to create */
	if (!create)
		return NULL;
	if (state->numdisplays > 61) {
		ast_log(LOG_WARNING, "No more display space at line %d of %s\n", lineno, script);
		return NULL;
	}
	strncpy(state->displays[state->numdisplays].vname, name, sizeof(state->displays[state->numdisplays].vname) - 1);
	state->displays[state->numdisplays].id = state->numdisplays + 1;
	state->numdisplays++;
	return &state->displays[state->numdisplays-1];
}

static int showkeys(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *tok;
	char newkey[80];
	int bytes;
	unsigned char keyid[6];
	int x;
	int flagid=0;
	struct adsi_soft_key *key;
	struct adsi_flag *flag;

	for (x=0;x<7;x++) {
		/* Up to 6 key arguments */
		tok = get_token(&args, script, lineno);
		if (!tok)
			break;
		if (!strcasecmp(tok, "UNLESS")) {
			/* Check for trailing UNLESS flag */
			tok = get_token(&args, script, lineno);
			if (!tok) {
				ast_log(LOG_WARNING, "Missing argument for UNLESS clause at line %d of %s\n", lineno, script);
			} else if (process_token(newkey, tok, sizeof(newkey) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "Invalid flag name '%s' at line %d of %s\n", tok, lineno, script);
			} else if (!(flag = getflagbyname(state, newkey, script, lineno, 0))) {
				ast_log(LOG_WARNING, "Flag '%s' is undeclared at line %d of %s\n", newkey, lineno, script);
			} else
				flagid = flag->id;
			if ((tok = get_token(&args, script, lineno)))
				ast_log(LOG_WARNING, "Extra arguments after UNLESS clause: '%s' at line %d of %s\n", tok, lineno, script);
			break;
		}
		if (x > 5) {
			ast_log(LOG_WARNING, "Only 6 keys can be defined, ignoring '%s' at line %d of %s\n", tok, lineno, script);
			break;
		}
		if (process_token(newkey, tok, sizeof(newkey) - 1, ARG_STRING)) {
			ast_log(LOG_WARNING, "Invalid token for key name: %s\n", tok);	
			continue;
		}
				   
		key = getkeybyname(state, newkey, script, lineno);
		if (!key)
			break;
		keyid[x] = key->id;
	}
	buf[0] = id;
	buf[1] = (flagid & 0x7) << 3 | (x & 0x7);
	for (bytes=0;bytes<x;bytes++) {
		buf[bytes + 2] = keyid[bytes];
	}
	return 2 + x;
}

static int showdisplay(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *tok;
	char dispname[80];
	int line=0;
	int flag=0;
	int cmd = 3;
	struct adsi_display *disp;

	/* Get display */
	tok = get_token(&args, script, lineno);
	if (!tok || process_token(dispname, tok, sizeof(dispname) - 1, ARG_STRING)) {
		ast_log(LOG_WARNING, "Invalid display name: %s at line %d of %s\n", tok ? tok : "<nothing>", lineno, script);
		return 0;
	}
	disp = getdisplaybyname(state, dispname, script, lineno, 0);
	if (!disp) {
		ast_log(LOG_WARNING, "Display '%s' is undefined at line %d of %s\n", dispname, lineno, script);
		return 0;
	}

	tok = get_token(&args, script, lineno);
	if (!tok || strcasecmp(tok, "AT")) {
		ast_log(LOG_WARNING, "Missing token 'AT' at line %d of %s\n", lineno, script);
		return 0;
	}
	/* Get line number */
	tok = get_token(&args, script, lineno);
	if (!tok || process_token(&line, tok, sizeof(line), ARG_NUMBER)) {
		ast_log(LOG_WARNING, "Invalid line: '%s' at line %d of %s\n", tok ? tok : "<nothing>", lineno, script);
		return 0;
	}
	tok = get_token(&args, script, lineno);
	if (tok && !strcasecmp(tok, "NOUPDATE")) {
		cmd = 1;
		tok = get_token(&args, script, lineno);
	}
	if (tok && !strcasecmp(tok, "UNLESS")) {
		/* Check for trailing UNLESS flag */
		tok = get_token(&args, script, lineno);
		if (!tok) {
			ast_log(LOG_WARNING, "Missing argument for UNLESS clause at line %d of %s\n", lineno, script);
		} else if (process_token(&flag, tok, sizeof(flag), ARG_NUMBER)) {
			ast_log(LOG_WARNING, "Invalid flag number '%s' at line %d of %s\n", tok, lineno, script);
		}
		if ((tok = get_token(&args, script, lineno)))
			ast_log(LOG_WARNING, "Extra arguments after UNLESS clause: '%s' at line %d of %s\n", tok, lineno, script);
	}
				   
	buf[0] = id;
	buf[1] = (cmd << 6) | (disp->id & 0x3f); 
	buf[2] = ((line & 0x1f) << 3) | (flag & 0x7);
	return 3;
}

static int cleardisplay(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *tok;
	tok = get_token(&args, script, lineno);
	if (tok) 
		ast_log(LOG_WARNING, "Clearing display requires no arguments ('%s') at line %d of %s\n", tok, lineno, script);

	buf[0] = id;
	buf[1] = 0x00;
	return 2;
}

static int digitdirect(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *tok;
	tok = get_token(&args, script, lineno);
	if (tok) 
		ast_log(LOG_WARNING, "Digitdirect requires no arguments ('%s') at line %d of %s\n", tok, lineno, script);

	buf[0] = id;
	buf[1] = 0x7;
	return 2;
}

static int clearcbone(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *tok;
	tok = get_token(&args, script, lineno);
	if (tok)
		ast_log(LOG_WARNING, "CLEARCB1 requires no arguments ('%s') at line %d of %s\n", tok, lineno, script);

	buf[0] = id;
	buf[1] = 0;
	return 2;
}

static int digitcollect(char *buf, char *name, int id, char *args, struct adsi_script *istate, char *script, int lineno)
{
	char *tok;
	tok = get_token(&args, script, lineno);
	if (tok) 
		ast_log(LOG_WARNING, "Digitcollect requires no arguments ('%s') at line %d of %s\n", tok, lineno, script);

	buf[0] = id;
	buf[1] = 0xf;
	return 2;
}

static int subscript(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *tok;
	char subscript[80];
	struct adsi_subscript *sub;
	tok = get_token(&args, script, lineno);
	if (!tok) {
		ast_log(LOG_WARNING, "Missing subscript to call at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(subscript, tok, sizeof(subscript) - 1, ARG_STRING)) {
		ast_log(LOG_WARNING, "Invalid number of seconds '%s' at line %d of %s\n", tok, lineno, script);
		return 0;
	}
	sub = getsubbyname(state, subscript, script, lineno);
	if (!sub) 
		return 0;
	buf[0] = 0x9d;
	buf[1] = sub->id;
	return 2;
}

static int onevent(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno)
{
	char *tok;
	char subscript[80];
	char sname[80];
	int sawin=0;
	int event;
	int snums[8];
	int scnt = 0;
	int x;
	struct adsi_subscript *sub;
	tok = get_token(&args, script, lineno);
	if (!tok) {
		ast_log(LOG_WARNING, "Missing event for 'ONEVENT' at line %d of %s\n", lineno, script);
		return 0;
	}
	event = geteventbyname(tok);
	if (event < 1) {
		ast_log(LOG_WARNING, "'%s' is not a valid event name, at line %d of %s\n", args, lineno, script);
		return 0;
	}
	tok = get_token(&args, script, lineno);
	while ((!sawin && !strcasecmp(tok, "IN")) ||
	       (sawin && !strcasecmp(tok, "OR"))) {
		sawin = 1;
		if (scnt > 7) {
			ast_log(LOG_WARNING, "No more than 8 states may be specified for inclusion at line %d of %s\n", lineno, script);
			return 0;
		}
		/* Process 'in' things */
		tok = get_token(&args, script, lineno);
		if (process_token(sname, tok, sizeof(sname), ARG_STRING)) {
			ast_log(LOG_WARNING, "'%s' is not a valid state name at line %d of %s\n", tok, lineno, script);
			return 0;
		}
		if ((snums[scnt] = getstatebyname(state, sname, script, lineno, 0) < 0)) {
			ast_log(LOG_WARNING, "State '%s' not declared at line %d of %s\n", sname, lineno, script);
			return 0;
		}
		scnt++;
		tok = get_token(&args, script, lineno);
		if (!tok)
			break;
	}
	if (!tok || strcasecmp(tok, "GOTO")) {
		if (!tok)
			tok = "<nothing>";
		if (sawin) 
			ast_log(LOG_WARNING, "Got '%s' while looking for 'GOTO' or 'OR' at line %d of %s\n", tok, lineno, script);
		else
			ast_log(LOG_WARNING, "Got '%s' while looking for 'GOTO' or 'IN' at line %d of %s\n", tok, lineno, script);
	}
	tok = get_token(&args, script, lineno);
	if (!tok) {
		ast_log(LOG_WARNING, "Missing subscript to call at line %d of %s\n", lineno, script);
		return 0;
	}
	if (process_token(subscript, tok, sizeof(subscript) - 1, ARG_STRING)) {
		ast_log(LOG_WARNING, "Invalid subscript '%s' at line %d of %s\n", tok, lineno, script);
		return 0;
	}
	sub = getsubbyname(state, subscript, script, lineno);
	if (!sub) 
		return 0;
	buf[0] = 8;
	buf[1] = event;
	buf[2] = sub->id | 0x80;
	for (x=0;x<scnt;x++)
		buf[3 + x] = snums[x];
	return 3 + scnt;
}

struct adsi_key_cmd {
	char *name;
	int id;
	int (*add_args)(char *buf, char *name, int id, char *args, struct adsi_script *state, char *script, int lineno);
};

static struct adsi_key_cmd kcmds[] = {
	{ "SENDDTMF", 0, send_dtmf },
	/* Encoded DTMF would go here */
	{ "ONHOOK", 0x81 },
	{ "OFFHOOK", 0x82 },
	{ "FLASH", 0x83 },
	{ "WAITDIALTONE", 0x84 },
	/* Send line number */
	{ "BLANK", 0x86 },
	{ "SENDCHARS", 0x87 },
	{ "CLEARCHARS", 0x88 },
	{ "BACKSPACE", 0x89 },
	/* Tab column */
	{ "GOTOLINE", 0x8b, goto_line },
	{ "GOTOLINEREL", 0x8c, goto_line_rel },
	{ "PAGEUP", 0x8d },
	{ "PAGEDOWN", 0x8e },
	/* Extended DTMF */
	{ "DELAY", 0x90, send_delay },
	{ "DIALPULSEONE", 0x91 },
	{ "DATAMODE", 0x92 },
	{ "VOICEMODE", 0x93 },
	/* Display call buffer 'n' */
	/* Clear call buffer 'n' */
	{ "CLEARCB1", 0x95, clearcbone },
	{ "DIGITCOLLECT", 0x96, digitcollect },
	{ "DIGITDIRECT", 0x96, digitdirect },
	{ "CLEAR", 0x97 },
	{ "SHOWDISPLAY", 0x98, showdisplay },
	{ "CLEARDISPLAY", 0x98, cleardisplay },
	{ "SHOWKEYS", 0x99, showkeys },
	{ "SETSTATE", 0x9a, set_state },
	{ "TIMERSTART", 0x9b, starttimer },
	{ "TIMERCLEAR", 0x9b, cleartimer },
	{ "SETFLAG", 0x9c, setflag },
	{ "CLEARFLAG", 0x9c, clearflag },
	{ "GOTO", 0x9d, subscript },
	{ "EVENT22", 0x9e },
	{ "EVENT23", 0x9f },
	{ "EXIT", 0xa0 },
};

static struct adsi_key_cmd opcmds[] = {
	
	/* 1 - Branch on event -- handled specially */
	{ "SHOWKEYS", 2, showkeys },
	/* Display Control */
	{ "SHOWDISPLAY", 3, showdisplay },
	{ "CLEARDISPLAY", 3, cleardisplay },
	{ "CLEAR", 5 },
	{ "SETSTATE", 6, set_state },
	{ "TIMERSTART", 7, starttimer },
	{ "TIMERCLEAR", 7, cleartimer },
	{ "ONEVENT", 8, onevent },
	/* 9 - Subroutine label, treated specially */
	{ "SETFLAG", 10, setflag },
	{ "CLEARFLAG", 10, clearflag },
	{ "DELAY", 11, send_delay },
	{ "EXIT", 12 },
};


static int process_returncode(struct adsi_soft_key *key, char *code, char *args, struct adsi_script *state, char *script, int lineno)
{
	int x;
	char *unused;
	int res;
	for (x=0;x<sizeof(kcmds) / sizeof(kcmds[0]);x++) {
		if ((kcmds[x].id > -1) && !strcasecmp(kcmds[x].name, code)) {
			if (kcmds[x].add_args) {
				res = kcmds[x].add_args(key->retstr + key->retstrlen,
						code, kcmds[x].id, args, state, script, lineno);
				if ((key->retstrlen + res - key->initlen) <= MAX_RET_CODE) 
					key->retstrlen += res;
				else 
					ast_log(LOG_WARNING, "No space for '%s' code in key '%s' at line %d of %s\n", kcmds[x].name, key->vname, lineno, script);
			} else {
				if ((unused = get_token(&args, script, lineno))) 
					ast_log(LOG_WARNING, "'%s' takes no arguments at line %d of %s (token is '%s')\n", kcmds[x].name, lineno, script, unused);
				if ((key->retstrlen + 1 - key->initlen) <= MAX_RET_CODE) {
					key->retstr[key->retstrlen] = kcmds[x].id;
					key->retstrlen++;
				} else 
					ast_log(LOG_WARNING, "No space for '%s' code in key '%s' at line %d of %s\n", kcmds[x].name, key->vname, lineno, script);
			}
			return 0;
		}
	}
	return -1;
}

static int process_opcode(struct adsi_subscript *sub, char *code, char *args, struct adsi_script *state, char *script, int lineno)
{
	int x;
	char *unused;
	int res;
	int max = sub->id ? MAX_SUB_LEN : MAX_MAIN_LEN;
	for (x=0;x<sizeof(opcmds) / sizeof(opcmds[0]);x++) {
		if ((opcmds[x].id > -1) && !strcasecmp(opcmds[x].name, code)) {
			if (opcmds[x].add_args) {
				res = opcmds[x].add_args(sub->data + sub->datalen,
						code, opcmds[x].id, args, state, script, lineno);
				if ((sub->datalen + res + 1) <= max) 
					sub->datalen += res;
				else {
					ast_log(LOG_WARNING, "No space for '%s' code in subscript '%s' at line %d of %s\n", opcmds[x].name, sub->vname, lineno, script);
					return -1;
				}
			} else {
				if ((unused = get_token(&args, script, lineno))) 
					ast_log(LOG_WARNING, "'%s' takes no arguments at line %d of %s (token is '%s')\n", opcmds[x].name, lineno, script, unused);
				if ((sub->datalen + 2) <= max) {
					sub->data[sub->datalen] = opcmds[x].id;
					sub->datalen++;
				} else {
					ast_log(LOG_WARNING, "No space for '%s' code in key '%s' at line %d of %s\n", opcmds[x].name, sub->vname, lineno, script);
					return -1;
				}
			}
			/* Separate commands with 0xff */
			sub->data[sub->datalen] = 0xff;
			sub->datalen++;
			sub->inscount++;
			return 0;
		}
	}
	return -1;
}

static int adsi_process(struct adsi_script *state, char *buf, char *script, int lineno)
{
	char *keyword;
	char *args;
	char vname[256];
	char tmp[80];
	char tmp2[80];
	int lrci;
	int wi;
	int event;
	struct adsi_display *disp;
	struct adsi_subscript *newsub;
	/* Find the first keyword */
	keyword = get_token(&buf, script, lineno);
	if (!keyword) 
		return 0;
	switch(state->state) {
	case STATE_NORMAL:
		if (!strcasecmp(keyword, "DESCRIPTION")) {
			args = get_token(&buf, script, lineno);
			if (args) {
				if (process_token(state->desc, args, sizeof(state->desc) - 1, ARG_STRING))
					ast_log(LOG_WARNING, "'%s' is not a valid token for DESCRIPTION at line %d of %s\n", args, lineno, script);
			} else
				ast_log(LOG_WARNING, "Missing argument for DESCRIPTION at line %d of %s\n", lineno, script);
		} else if (!strcasecmp(keyword, "VERSION")) {
			args = get_token(&buf, script, lineno);
			if (args) {
				if (process_token(&state->ver, args, sizeof(state->ver) - 1, ARG_NUMBER))
					ast_log(LOG_WARNING, "'%s' is not a valid token for VERSION at line %d of %s\n", args, lineno, script);
			} else
				ast_log(LOG_WARNING, "Missing argument for VERSION at line %d of %s\n", lineno, script);
		} else if (!strcasecmp(keyword, "SECURITY")) {
			args = get_token(&buf, script, lineno);
			if (args) {
				if (process_token(state->sec, args, sizeof(state->sec) - 1, ARG_STRING | ARG_NUMBER))
					ast_log(LOG_WARNING, "'%s' is not a valid token for SECURITY at line %d of %s\n", args, lineno, script);
			} else
				ast_log(LOG_WARNING, "Missing argument for SECURITY at line %d of %s\n", lineno, script);
		} else if (!strcasecmp(keyword, "FDN")) {
			args = get_token(&buf, script, lineno);
			if (args) {
				if (process_token(state->fdn, args, sizeof(state->fdn) - 1, ARG_STRING | ARG_NUMBER))
					ast_log(LOG_WARNING, "'%s' is not a valid token for FDN at line %d of %s\n", args, lineno, script);
			} else
				ast_log(LOG_WARNING, "Missing argument for FDN at line %d of %s\n", lineno, script);
		} else if (!strcasecmp(keyword, "KEY")) {
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "KEY definition missing name at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(vname, args, sizeof(vname) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "'%s' is not a valid token for a KEY name at line %d of %s\n", args, lineno, script);
				break;
			}
			state->key = getkeybyname(state, vname, script, lineno);
			if (!state->key) {
				ast_log(LOG_WARNING, "Out of key space at line %d of %s\n", lineno, script);
				break;
			}
			if (state->key->defined) {
				ast_log(LOG_WARNING, "Cannot redefine key '%s' at line %d of %s\n", vname, lineno, script);
				break;
			}
			args = get_token(&buf, script, lineno);
			if (!args || strcasecmp(args, "IS")) {
				ast_log(LOG_WARNING, "Expecting 'IS', but got '%s' at line %d of %s\n", args ? args : "<nothing>", lineno, script);
				break;
			}
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "KEY definition missing short name at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(tmp, args, sizeof(tmp) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "'%s' is not a valid token for a KEY short name at line %d of %s\n", args, lineno, script);
				break;
			}
			args = get_token(&buf, script, lineno);
			if (args) {
				if (strcasecmp(args, "OR")) {
					ast_log(LOG_WARNING, "Expecting 'OR' but got '%s' instead at line %d of %s\n", args, lineno, script);
					break;
				}
				args = get_token(&buf, script, lineno);
				if (!args) {
					ast_log(LOG_WARNING, "KEY definition missing optional long name at line %d of %s\n", lineno, script);
					break;
				}
				if (process_token(tmp2, args, sizeof(tmp2) - 1, ARG_STRING)) {
					ast_log(LOG_WARNING, "'%s' is not a valid token for a KEY long name at line %d of %s\n", args, lineno, script);
					break;
				}
			} else {
				strncpy(tmp2, tmp, sizeof(tmp2) - 1);
			}
			if (strlen(tmp2) > 18) {
				ast_log(LOG_WARNING, "Truncating full name to 18 characters at line %d of %s\n", lineno, script);
				tmp2[18] = '\0';
			}
			if (strlen(tmp) > 7) {
				ast_log(LOG_WARNING, "Truncating short name to 7 bytes at line %d of %s\n", lineno, script);
				tmp[7] = '\0';
			}
			/* Setup initial stuff */
			state->key->retstr[0] = 128;
			/* 1 has the length */
			state->key->retstr[2] = state->key->id;
			/* Put the Full name in */
			memcpy(state->key->retstr + 3, tmp2, strlen(tmp2));
			/* Update length */
			state->key->retstrlen = strlen(tmp2) + 3;
			/* Put trailing 0xff */
			state->key->retstr[state->key->retstrlen++] = 0xff;
			/* Put the short name */
			memcpy(state->key->retstr + state->key->retstrlen, tmp, strlen(tmp));
			/* Update length */
			state->key->retstrlen += strlen(tmp);
			/* Put trailing 0xff */
			state->key->retstr[state->key->retstrlen++] = 0xff;
			/* Record initial length */
			state->key->initlen = state->key->retstrlen;
			state->state = STATE_INKEY;
		} else if (!strcasecmp(keyword, "SUB")) {
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "SUB definition missing name at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(vname, args, sizeof(vname) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "'%s' is not a valid token for a KEY name at line %d of %s\n", args, lineno, script);
				break;
			}
			state->sub = getsubbyname(state, vname, script, lineno);
			if (!state->sub) {
				ast_log(LOG_WARNING, "Out of subroutine space at line %d of %s\n", lineno, script);
				break;
			}
			if (state->sub->defined) {
				ast_log(LOG_WARNING, "Cannot redefine subroutine '%s' at line %d of %s\n", vname, lineno, script);
				break;
			}
			/* Setup sub */
			state->sub->data[0] = 130;
			/* 1 is the length */
			state->sub->data[2] = 0x0; /* Clear extensibility bit */
			state->sub->datalen = 3;
			if (state->sub->id) {
				/* If this isn't the main subroutine, make a subroutine label for it */
				state->sub->data[3] = 9;
				state->sub->data[4] = state->sub->id;
				/* 5 is length */
				state->sub->data[6] = 0xff;
				state->sub->datalen = 7;
			}
			args = get_token(&buf, script, lineno);
			if (!args || strcasecmp(args, "IS")) {
				ast_log(LOG_WARNING, "Expecting 'IS', but got '%s' at line %d of %s\n", args ? args : "<nothing>", lineno, script);
				break;
			}
			state->state = STATE_INSUB;
 		} else if (!strcasecmp(keyword, "STATE")) {
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "STATE definition missing name at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(vname, args, sizeof(vname) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "'%s' is not a valid token for a STATE name at line %d of %s\n", args, lineno, script);
				break;
			}
			if (getstatebyname(state, vname, script, lineno, 0)) {
				ast_log(LOG_WARNING, "State '%s' is already defined at line %d of %s\n", vname, lineno, script);
				break;
			}
			getstatebyname(state, vname, script, lineno, 1);
 		} else if (!strcasecmp(keyword, "FLAG")) {
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "FLAG definition missing name at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(vname, args, sizeof(vname) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "'%s' is not a valid token for a FLAG name at line %d of %s\n", args, lineno, script);
				break;
			}
			if (getflagbyname(state, vname, script, lineno, 0)) {
				ast_log(LOG_WARNING, "Flag '%s' is already defined\n", vname);
				break;
			}
			getflagbyname(state, vname, script, lineno, 1);
		} else if (!strcasecmp(keyword, "DISPLAY")) {
			lrci = 0;
			wi = 0;
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "SUB definition missing name at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(vname, args, sizeof(vname) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "'%s' is not a valid token for a KEY name at line %d of %s\n", args, lineno, script);
				break;
			}
			if (getdisplaybyname(state, vname, script, lineno, 0)) {
				ast_log(LOG_WARNING, "State '%s' is already defined\n", vname);
				break;
			}
			disp = getdisplaybyname(state, vname, script, lineno, 1);
			if (!disp)
				break;
			args = get_token(&buf, script, lineno);
			if (!args || strcasecmp(args, "IS")) {
				ast_log(LOG_WARNING, "Missing 'IS' at line %d of %s\n", lineno, script);
				break;
			}
			args = get_token(&buf, script, lineno);
			if (!args) {
				ast_log(LOG_WARNING, "Missing Column 1 text at line %d of %s\n", lineno, script);
				break;
			}
			if (process_token(tmp, args, sizeof(tmp) - 1, ARG_STRING)) {
				ast_log(LOG_WARNING, "Token '%s' is not valid column 1 text at line %d of %s\n", args, lineno, script);
				break;
			}
			if (strlen(tmp) > 20) {
				ast_log(LOG_WARNING, "Truncating column one to 20 characters at line %d of %s\n", lineno, script);
				tmp[20] = '\0';
			}
			memcpy(disp->data + 5, tmp, strlen(tmp));
			disp->datalen = strlen(tmp) + 5;
			disp->data[disp->datalen++] = 0xff;

			args = get_token(&buf, script, lineno);
			if (args && !process_token(tmp, args, sizeof(tmp) - 1, ARG_STRING)) {
				/* Got a column two */
				if (strlen(tmp) > 20) {
					ast_log(LOG_WARNING, "Truncating column two to 20 characters at line %d of %s\n", lineno, script);
					tmp[20] = '\0';
				}
				memcpy(disp->data + disp->datalen, tmp, strlen(tmp));
				disp->datalen += strlen(tmp);
				args = get_token(&buf, script, lineno);
			}
			while(args) {
				if (!strcasecmp(args, "JUSTIFY")) {
					args = get_token(&buf, script, lineno);
					if (!args) {
						ast_log(LOG_WARNING, "Qualifier 'JUSTIFY' requires an argument at line %d of %s\n", lineno, script);
						break;
					}
					lrci = getjustifybyname(args);
					if (lrci < 0) {
						ast_log(LOG_WARNING, "'%s' is not a valid justification at line %d of %s\n", args, lineno, script);
						break;
					}
				} else if (!strcasecmp(args, "WRAP")) {
					wi = 0x80;
				} else {
					ast_log(LOG_WARNING, "'%s' is not a known qualifier at line %d of %s\n", args, lineno, script);
					break;
				}
				args = get_token(&buf, script, lineno);
			}
			if (args) {
				/* Something bad happened */
				break;
			}
			disp->data[0] = 129;
			disp->data[1] = disp->datalen - 2;
			disp->data[2] = ((lrci & 0x3) << 6) | disp->id;
			disp->data[3] = wi;
			disp->data[4] = 0xff;
		} else {
			ast_log(LOG_WARNING, "Invalid or Unknown keyword '%s' in PROGRAM\n", keyword);
		}
		break;
	case STATE_INKEY:
		if (process_returncode(state->key, keyword, buf, state, script, lineno)) {
			if (!strcasecmp(keyword, "ENDKEY")) {
				/* Return to normal operation and increment current key */
				state->state = STATE_NORMAL;
				state->key->defined = 1;
				state->key->retstr[1] = state->key->retstrlen - 2;
				state->key = NULL;
			} else {
				ast_log(LOG_WARNING, "Invalid or Unknown keyword '%s' in SOFTKEY definition at line %d of %s\n", keyword, lineno, script);
			}
		}
		break;
	case STATE_INIF:
		if (process_opcode(state->sub, keyword, buf, state, script, lineno)) {
			if (!strcasecmp(keyword, "ENDIF")) {
				/* Return to normal SUB operation and increment current key */
				state->state = STATE_INSUB;
				state->sub->defined = 1;
				/* Store the proper number of instructions */
				state->sub->ifdata[2] = state->sub->ifinscount;
			} else if (!strcasecmp(keyword, "GOTO")) {
				args = get_token(&buf, script, lineno);
				if (!args) {
					ast_log(LOG_WARNING, "GOTO clause missing Subscript name at line %d of %s\n", lineno, script);
					break;
				}
				if (process_token(tmp, args, sizeof(tmp) - 1, ARG_STRING)) {
					ast_log(LOG_WARNING, "'%s' is not a valid subscript name token at line %d of %s\n", args, lineno, script);
					break;
				}
				newsub = getsubbyname(state, tmp, script, lineno);
				if (!newsub) 
					break;
				/* Somehow you use GOTO to go to another place */
				state->sub->data[state->sub->datalen++] = 0x8;
				state->sub->data[state->sub->datalen++] = state->sub->ifdata[1];
				state->sub->data[state->sub->datalen++] = newsub->id;
				/* Terminate */
				state->sub->data[state->sub->datalen++] = 0xff;
				/* Increment counters */
				state->sub->inscount++;
				state->sub->ifinscount++;
			} else {
				ast_log(LOG_WARNING, "Invalid or Unknown keyword '%s' in IF clause at line %d of %s\n", keyword, lineno, script);
			}
		} else
			state->sub->ifinscount++;
		break;
	case STATE_INSUB:
		if (process_opcode(state->sub, keyword, buf, state, script, lineno)) {
			if (!strcasecmp(keyword, "ENDSUB")) {
				/* Return to normal operation and increment current key */
				state->state = STATE_NORMAL;
				state->sub->defined = 1;
				/* Store the proper length */
				state->sub->data[1] = state->sub->datalen - 2;
				if (state->sub->id) {
					/* if this isn't main, store number of instructions, too */
					state->sub->data[5] = state->sub->inscount;
				}
				state->sub = NULL;
			} else if (!strcasecmp(keyword, "IFEVENT")) {
				args = get_token(&buf, script, lineno);
				if (!args) {
					ast_log(LOG_WARNING, "IFEVENT clause missing Event name at line %d of %s\n", lineno, script);
					break;
				}
				event = geteventbyname(args);
				if (event < 1) {
					ast_log(LOG_WARNING, "'%s' is not a valid event\n", args);
					break;
				}
				args = get_token(&buf, script, lineno);
				if (!args || strcasecmp(args, "THEN")) {
					ast_log(LOG_WARNING, "IFEVENT clause missing 'THEN' at line %d of %s\n", lineno, script);
					break;
				}
				state->sub->ifinscount = 0;
				state->sub->ifdata = state->sub->data + 
						state->sub->datalen;
				/* Reserve header and insert op codes */
				state->sub->ifdata[0] = 0x1;
				state->sub->ifdata[1] = event;
				/* 2 is for the number of instructions */
				state->sub->ifdata[3] = 0xff;
				state->sub->datalen += 4;
				/* Update Subscript instruction count */
				state->sub->inscount++;
				state->state = STATE_INIF;
			} else {
				ast_log(LOG_WARNING, "Invalid or Unknown keyword '%s' in SUB definition at line %d of %s\n", keyword, lineno, script);
			}
		}
		break;
	default:
		ast_log(LOG_WARNING, "Can't process keyword '%s' in weird state %d\n", keyword, state->state);
	}
	return 0;
}

static struct adsi_script *compile_script(char *script)
{
	FILE *f;
	char fn[256];
	char buf[256];
	char *c;
	int lineno=0;
	int x, err;
	struct adsi_script *scr;
	if (script[0] == '/')
		strncpy(fn, script, sizeof(fn) - 1);
	else
		snprintf(fn, sizeof(fn), "%s/%s", (char *)ast_config_AST_CONFIG_DIR, script);
	f = fopen(fn, "r");
	if (!f) {
		ast_log(LOG_WARNING, "Can't open file '%s'\n", fn);
		return NULL;
	}
	scr = malloc(sizeof(struct adsi_script));
	if (!scr) {
		fclose(f);
		ast_log(LOG_WARNING, "Out of memory loading script '%s'\n", fn);
		return NULL;
	}
	memset(scr, 0, sizeof(struct adsi_script));
	/* Create "main" as first subroutine */
	getsubbyname(scr, "main", NULL, 0);
	while(!feof(f)) {
		fgets(buf, sizeof(buf), f);
		if (!feof(f)) {
			lineno++;
			/* Trim off trailing return */
			buf[strlen(buf) - 1] = '\0';
			c = strchr(buf, ';');
			/* Strip comments */
			if (c)
				*c = '\0';
			if (!ast_strlen_zero(buf))
				adsi_process(scr, buf, script, lineno);
		}
	}
	fclose(f);
	/* Make sure we're in the main routine again */
	switch(scr->state) {
	case STATE_NORMAL:
		break;
	case STATE_INSUB:
		ast_log(LOG_WARNING, "Missing ENDSUB at end of file %s\n", script);
		free(scr);
		return NULL;
	case STATE_INKEY:
		ast_log(LOG_WARNING, "Missing ENDKEY at end of file %s\n", script);
		free(scr);
		return NULL;
	}
	err = 0;

	/* Resolve all keys and record their lengths */
	for (x=0;x<scr->numkeys;x++) {
		if (!scr->keys[x].defined) {
			ast_log(LOG_WARNING, "Key '%s' referenced but never defined in file %s\n", scr->keys[x].vname, fn);
			err++;
		}
	}

	/* Resolve all subs */
	for (x=0;x<scr->numsubs;x++) {
		if (!scr->subs[x].defined) {
			ast_log(LOG_WARNING, "Subscript '%s' referenced but never defined in file %s\n", scr->subs[x].vname, fn);
			err++;
		}
		if (x == (scr->numsubs - 1)) {
			/* Clear out extension bit on last message */
			scr->subs[x].data[2] = 0x80;
		}
	}

	if (err) {
		free(scr);
		return NULL;
	}
	return scr;
}

#ifdef DUMP_MESSAGES
static void dump_message(char *type, char *vname, unsigned char *buf, int buflen)
{
	int x;
	printf("%s %s: [ ", type, vname);
	for (x=0;x<buflen;x++)
		printf("%02x ", buf[x]);
	printf("]\n");
}
#endif

static int adsi_prog(struct ast_channel *chan, char *script)
{
	struct adsi_script *scr;
	int x;
	char buf[1024];
	int bytes;
	scr = compile_script(script);
	if (!scr) 
		return -1;

	/* Start an empty ADSI Session */
	if (adsi_load_session(chan, NULL, 0, 1) < 1) 
		return -1;

	/* Now begin the download attempt */
	if (adsi_begin_download(chan, scr->desc, scr->fdn, scr->sec, scr->ver)) {
		/* User rejected us for some reason */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "User rejected download attempt\n");
		ast_log(LOG_NOTICE, "User rejected download on channel %s\n", chan->name);
		free(scr);
		return -1;
	}

	bytes = 0;
	/* Start with key definitions */
	for (x=0;x<scr->numkeys;x++) {
		if (bytes + scr->keys[x].retstrlen > 254) {
			/* Send what we've collected so far */
			if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD)) {
				ast_log(LOG_WARNING, "Unable to send chunk ending at %d\n", x);
				return -1;
			}
			bytes =0;
		}
		memcpy(buf + bytes, scr->keys[x].retstr, scr->keys[x].retstrlen);
		bytes += scr->keys[x].retstrlen;
#ifdef DUMP_MESSAGES
		dump_message("Key", scr->keys[x].vname, scr->keys[x].retstr, scr->keys[x].retstrlen);
#endif
	}
	if (bytes) {
		if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD)) {
			ast_log(LOG_WARNING, "Unable to send chunk ending at %d\n", x);
			return -1;
		}
	}

	bytes = 0;
	/* Continue with the display messages */
	for (x=0;x<scr->numdisplays;x++) {
		if (bytes + scr->displays[x].datalen > 254) {
			/* Send what we've collected so far */
			if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD)) {
				ast_log(LOG_WARNING, "Unable to send chunk ending at %d\n", x);
				return -1;
			}
			bytes =0;
		}
		memcpy(buf + bytes, scr->displays[x].data, scr->displays[x].datalen);
		bytes += scr->displays[x].datalen;
#ifdef DUMP_MESSAGES
		dump_message("Display", scr->displays[x].vname, scr->displays[x].data, scr->displays[x].datalen);
#endif
	}
	if (bytes) {
		if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD)) {
			ast_log(LOG_WARNING, "Unable to send chunk ending at %d\n", x);
			return -1;
		}
	}

	bytes = 0;
	/* Send subroutines */
	for (x=0;x<scr->numsubs;x++) {
		if (bytes + scr->subs[x].datalen > 254) {
			/* Send what we've collected so far */
			if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD)) {
				ast_log(LOG_WARNING, "Unable to send chunk ending at %d\n", x);
				return -1;
			}
			bytes =0;
		}
		memcpy(buf + bytes, scr->subs[x].data, scr->subs[x].datalen);
		bytes += scr->subs[x].datalen;
#ifdef DUMP_MESSAGES
		dump_message("Sub", scr->subs[x].vname, scr->subs[x].data, scr->subs[x].datalen);
#endif
	}
	if (bytes) {
		if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD)) {
			ast_log(LOG_WARNING, "Unable to send chunk ending at %d\n", x);
			return -1;
		}
	}


	bytes = 0;
	bytes += adsi_display(buf, ADSI_INFO_PAGE, 1, ADSI_JUST_LEFT, 0, "Download complete.", "");
	bytes += adsi_set_line(buf, ADSI_INFO_PAGE, 1);
	if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY) < 0)
		return -1;
	if (adsi_end_download(chan)) {
		/* Download failed for some reason */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Download attempt failed\n");
		ast_log(LOG_NOTICE, "Download failed on %s\n", chan->name);
		free(scr);
		return -1;
	}
	free(scr);
	adsi_unload_session(chan);
	return 0;
}

static int adsi_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	if (!data || ast_strlen_zero(data))
		data = "asterisk.adsi";
	LOCAL_USER_ADD(u);
	if (!adsi_available(chan)) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "ADSI Unavailable on CPE.  Not bothering to try.\n");
	} else {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "ADSI Available on CPE.  Attempting Upload.\n");
		res = adsi_prog(chan, data);
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
	return ast_register_application(app, adsi_exec, synopsis, descrip);
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
