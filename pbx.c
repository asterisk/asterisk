 /*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Core PBX routines.
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/cli.h>
#include <asterisk/pbx.h>
#include <asterisk/channel.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>
#include <asterisk/file.h>
#include <asterisk/callerid.h>
#include <asterisk/cdr.h>
#include <asterisk/config.h>
#include <asterisk/term.h>
#include <asterisk/manager.h>
#include <asterisk/ast_expr.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/linkedlists.h>
#include <asterisk/say.h>
#include <asterisk/utils.h>
#include <asterisk/causes.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "asterisk.h"

/*
 * I M P O R T A N T :
 *
 *		The speed of extension handling will likely be among the most important
 * aspects of this PBX.  The switching scheme as it exists right now isn't
 * terribly bad (it's O(N+M), where N is the # of extensions and M is the avg #
 * of priorities, but a constant search time here would be great ;-) 
 *
 */

#ifdef LOW_MEMORY
#define EXT_DATA_SIZE 256
#else
#define EXT_DATA_SIZE 8192
#endif

#define	VAR_NORMAL		1
#define	VAR_SOFTTRAN	2
#define	VAR_HARDTRAN	3

struct ast_context;

/* ast_exten: An extension */
struct ast_exten {
	char *exten;		/* Extension name */
	int matchcid;				/* Match caller id ? */
	char *cidmatch;	/* Caller id to match for this extension */
	int priority;				/* Priority */
	char *label;	/* Label */
	struct ast_context *parent;		/* An extension */
	char *app; 		/* Application to execute */
	void *data;				/* Data to use */
	void (*datad)(void *);			/* Data destructor */
	struct ast_exten *peer;			/* Next higher priority with our extension */
	const char *registrar;			/* Registrar */
	struct ast_exten *next;			/* Extension with a greater ID */
	char stuff[0];
};

/* ast_include: include= support in extensions.conf */
struct ast_include {
	char *name;		
	char *rname;		/* Context to include */
	const char *registrar;			/* Registrar */
	int hastime;				/* If time construct exists */
        struct ast_timing timing;               /* time construct */
	struct ast_include *next;		/* Link them together */
	char stuff[0];
};

/* ast_sw: Switch statement in extensions.conf */
struct ast_sw {
	char *name;
	const char *registrar;			/* Registrar */
	char *data;		/* Data load */
	struct ast_sw *next;			/* Link them together */
	char stuff[0];
};

struct ast_ignorepat {
	const char *registrar;
	struct ast_ignorepat *next;
	char pattern[0];
};

/* ast_context: An extension context */
struct ast_context {
	ast_mutex_t lock; 			/* A lock to prevent multiple threads from clobbering the context */
	struct ast_exten *root;			/* The root of the list of extensions */
	struct ast_context *next;		/* Link them together */
	struct ast_include *includes;		/* Include other contexts */
	struct ast_ignorepat *ignorepats;	/* Patterns for which to continue playing dialtone */
	const char *registrar;			/* Registrar */
	struct ast_sw *alts;			/* Alternative switches */
	char name[0];		/* Name of the context */
};


/* ast_app: An application */
struct ast_app {
	int (*execute)(struct ast_channel *chan, void *data);
	const char *synopsis;			/* Synopsis text for 'show applications' */
	const char *description;		/* Description (help text) for 'show application <name>' */
	struct ast_app *next;			/* Next app in list */
	char name[0];			/* Name of the application */
};

/* ast_state_cb: An extension state notify */
struct ast_state_cb {
    int id;
    void *data;
    ast_state_cb_type callback;
    struct ast_state_cb *next;
};
	    
/* ast_state_cb: An extension state notify */
struct ast_devstate_cb {
    void *data;
    ast_devstate_cb_type callback;
    struct ast_devstate_cb *next;
};

static struct ast_devstate_cb *devcbs;

struct ast_hint {
    struct ast_exten *exten;
    int laststate; 
    struct ast_state_cb *callbacks;
    struct ast_hint *next;
};

int ast_pbx_outgoing_cdr_failed(void);

static int pbx_builtin_prefix(struct ast_channel *, void *);
static int pbx_builtin_suffix(struct ast_channel *, void *);
static int pbx_builtin_stripmsd(struct ast_channel *, void *);
static int pbx_builtin_answer(struct ast_channel *, void *);
static int pbx_builtin_goto(struct ast_channel *, void *);
static int pbx_builtin_hangup(struct ast_channel *, void *);
static int pbx_builtin_background(struct ast_channel *, void *);
static int pbx_builtin_dtimeout(struct ast_channel *, void *);
static int pbx_builtin_rtimeout(struct ast_channel *, void *);
static int pbx_builtin_atimeout(struct ast_channel *, void *);
static int pbx_builtin_wait(struct ast_channel *, void *);
static int pbx_builtin_waitexten(struct ast_channel *, void *);
static int pbx_builtin_setlanguage(struct ast_channel *, void *);
static int pbx_builtin_resetcdr(struct ast_channel *, void *);
static int pbx_builtin_setaccount(struct ast_channel *, void *);
static int pbx_builtin_setamaflags(struct ast_channel *, void *);
static int pbx_builtin_ringing(struct ast_channel *, void *);
static int pbx_builtin_progress(struct ast_channel *, void *);
static int pbx_builtin_congestion(struct ast_channel *, void *);
static int pbx_builtin_busy(struct ast_channel *, void *);
static int pbx_builtin_setglobalvar(struct ast_channel *, void *);
static int pbx_builtin_noop(struct ast_channel *, void *);
static int pbx_builtin_gotoif(struct ast_channel *, void *);
static int pbx_builtin_gotoiftime(struct ast_channel *, void *);
static int pbx_builtin_saynumber(struct ast_channel *, void *);
static int pbx_builtin_saydigits(struct ast_channel *, void *);
static int pbx_builtin_saycharacters(struct ast_channel *, void *);
static int pbx_builtin_sayphonetic(struct ast_channel *, void *);
int pbx_builtin_setvar(struct ast_channel *, void *);
void pbx_builtin_setvar_helper(struct ast_channel *chan, char *name, char *value);
char *pbx_builtin_getvar_helper(struct ast_channel *chan, char *name);

static struct varshead globals;

static int autofallthrough = 0;

static struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, void *data);
	char *synopsis;
	char *description;
} builtins[] = 
{
	/* These applications are built into the PBX core and do not
	   need separate modules
	   
	    */

	{ "AbsoluteTimeout", pbx_builtin_atimeout,
	"Set absolute maximum time of call",
	"  AbsoluteTimeout(seconds): Set the absolute maximum amount of time permitted\n"
	"for a call.  A setting of 0 disables the timeout.  Always returns 0.\n" 
	},

	{ "Answer", pbx_builtin_answer, 
	"Answer a channel if ringing", 
	"  Answer(): If the channel is ringing, answer it, otherwise do nothing. \n"
	"Returns 0 unless it tries to answer the channel and fails.\n"   
	},

	{ "BackGround", pbx_builtin_background,
	"Play a file while awaiting extension",
	"  Background(filename[|options[|langoverride]]): Plays a given file, while simultaneously\n"
	"waiting for the user to begin typing an extension. The  timeouts do not\n"
	"count until the last BackGround application has ended.\n" 
	"Options may also be  included following a pipe symbol. The 'skip'\n"
	"option causes the playback of the message to  be  skipped  if  the  channel\n"
	"is not in the 'up' state (i.e. it hasn't been  answered  yet. If 'skip' is \n"
	"specified, the application will return immediately should the channel not be\n"
	"off hook.  Otherwise, unless 'noanswer' is specified, the channel channel will\n"
	"be answered before the sound is played. Not all channels support playing\n"
	"messages while still hook. The 'langoverride' may be a language to use for\n"
	"playing the prompt which differs from the current language of the channel\n"
	"Returns -1 if the channel was hung up, or if the file does not exist. \n"
	"Returns 0 otherwise.\n"
	},

	{ "Busy", pbx_builtin_busy,
	"Indicate busy condition and stop",
	"  Busy([timeout]): Requests that the channel indicate busy condition and\n"
	"then waits for the user to hang up or the optional timeout to expire.\n"
	"Always returns -1." 
	},

	{ "Congestion", pbx_builtin_congestion,
	"Indicate congestion and stop",
	"  Congestion([timeout]): Requests that the channel indicate congestion\n"
	"and then waits for the user to hang up or for the optional timeout to\n"
	"expire.  Always returns -1." 
	},

	{ "DigitTimeout", pbx_builtin_dtimeout,
	"Set maximum timeout between digits",
	"  DigitTimeout(seconds): Set the maximum amount of time permitted between\n"
	"digits when the user is typing in an extension. When this timeout expires,\n"
	"after the user has started to type in an extension, the extension will be\n"
	"considered complete, and will be interpreted. Note that if an extension\n"
	"typed in is valid, it will not have to timeout to be tested, so typically\n"
	"at the expiry of this timeout, the extension will be considered invalid\n"
	"(and thus control would be passed to the 'i' extension, or if it doesn't\n"
	"exist the call would be terminated). Always returns 0.\n" 
	},

	{ "Goto", pbx_builtin_goto, 
	"Goto a particular priority, extension, or context",
	"  Goto([[context|]extension|]priority):  Set the  priority to the specified\n"
	"value, optionally setting the extension and optionally the context as well.\n"
	"The extension BYEXTENSION is special in that it uses the current extension,\n"
	"thus  permitting you to go to a different context, without specifying a\n"
	"specific extension. Always returns 0, even if the given context, extension,\n"
	"or priority is invalid.\n" 
	},

	{ "GotoIf", pbx_builtin_gotoif,
	"Conditional goto",
	"  GotoIf(Condition?label1:label2): Go to label 1 if condition is\n"
	"true, to label2 if condition is false. Either label1 or label2 may be\n"
	"omitted (in that case, we just don't take the particular branch) but not\n"
	"both. Look for the condition syntax in examples or documentation." 
	},

	{ "GotoIfTime", pbx_builtin_gotoiftime,
	"Conditional goto on current time",
	"  GotoIfTime(<times>|<weekdays>|<mdays>|<months>?[[context|]extension|]pri):\n"
	"If the current time matches the specified time, then branch to the specified\n"
	"extension. Each of the elements may be specified either as '*' (for always)\n"
	"or as a range. See the 'include' syntax for details." 
	},
	
	{ "Hangup", pbx_builtin_hangup,
	"Unconditional hangup",
	"  Hangup(): Unconditionally hangs up a given channel by returning -1 always.\n" 
	},

	{ "NoOp", pbx_builtin_noop,
	"No operation",
	"  NoOp(): No-operation; Does nothing." 
	},

	{ "Prefix", pbx_builtin_prefix, 
	"Prepend leading digits",
	"  Prefix(digits): Prepends the digit string specified by digits to the\n"
	"channel's associated extension. For example, the number 1212 when prefixed\n"
	"with '555' will become 5551212. This app always returns 0, and the PBX will\n"
	"continue processing at the next priority for the *new* extension.\n"
	"  So, for example, if priority  3  of 1212 is  Prefix  555, the next step\n"
	"executed will be priority 4 of 5551212. If you switch into an extension\n"
	"which has no first step, the PBX will treat it as though the user dialed an\n"
	"invalid extension.\n" 
	},

	{ "Progress", pbx_builtin_progress,
	"Indicate progress",
	"  Progress(): Request that the channel indicate in-band progress is \n"
	"available to the user.\nAlways returns 0.\n" 
	},

	{ "ResetCDR", pbx_builtin_resetcdr,
	"Resets the Call Data Record",
	"  ResetCDR([options]):  Causes the Call Data Record to be reset, optionally\n"
	"storing the current CDR before zeroing it out (if 'w' option is specifed).\n"
	"record WILL be stored.\nAlways returns 0.\n"  
	},

	{ "ResponseTimeout", pbx_builtin_rtimeout,
	"Set maximum timeout awaiting response",
	"  ResponseTimeout(seconds): Set the maximum amount of time permitted after\n"
	"falling through a series of priorities for a channel in which the user may\n"
	"begin typing an extension. If the user does not type an extension in this\n"
	"amount of time, control will pass to the 't' extension if it exists, and\n"
	"if not the call would be terminated.\nAlways returns 0.\n"  
	},

	{ "Ringing", pbx_builtin_ringing,
	"Indicate ringing tone",
	"  Ringing(): Request that the channel indicate ringing tone to the user.\n"
	"Always returns 0.\n" 
	},

	{ "SayNumber", pbx_builtin_saynumber,
	"Say Number",
	"  SayNumber(digits[,gender]): Says the passed number. SayNumber is using\n" 
	"the current language setting for the channel. (See app SetLanguage).\n"
	},

	{ "SayDigits", pbx_builtin_saydigits,
	"Say Digits",
	"  SayDigits(digits): Says the passed digits. SayDigits is using the\n" 
	"current language setting for the channel. (See app setLanguage)\n"
	},

	{ "SayAlpha", pbx_builtin_saycharacters,
	"Say Alpha",
	"  SayAlpha(string): Spells the passed string\n" 
	},

	{ "SayPhonetic", pbx_builtin_sayphonetic,
	"Say Phonetic",
	"  SayPhonetic(string): Spells the passed string with phonetic alphabet\n" 
	},

	{ "SetAccount", pbx_builtin_setaccount,
	"Sets account code",
	"  SetAccount([account]):  Set  the  channel account code for billing\n"
	"purposes. Always returns 0.\n"  
	},

	{ "SetAMAFlags", pbx_builtin_setamaflags,
	"Sets AMA Flags",
	"  SetAMAFlags([flag]):  Set  the  channel AMA Flags for billing\n"
	"purposes. Always returns 0.\n"  
	},

	{ "SetGlobalVar", pbx_builtin_setglobalvar,
	"Set global variable to value",
	"  SetGlobalVar(#n=value): Sets global variable n to value. Global\n" 
	"variable are available across channels.\n"
	},

	{ "SetLanguage", pbx_builtin_setlanguage,
	"Sets user language",
	"  SetLanguage(language):  Set  the  channel  language to 'language'.  This\n"
	"information is used for the syntax in generation of numbers, and to choose\n"
	"a natural language file when available.\n"
	"  For example, if language is set to 'fr' and the file 'demo-congrats' is \n"
	"requested  to  be  played,  if the file 'fr/demo-congrats' exists, then\n"
	"it will play that file, and if not will play the normal 'demo-congrats'.\n"
	"Always returns 0.\n"  
	},

	{ "SetVar", pbx_builtin_setvar,
	"Set variable to value",
	"  SetVar(#n=value): Sets variable n to value.  If prefixed with _, single\n"
	"inheritance assumed.  If prefixed with __, infinite inheritance is assumed.\n" },

	{ "StripMSD", pbx_builtin_stripmsd,
	"Strip leading digits",
	"  StripMSD(count): Strips the leading 'count' digits from the channel's\n"
	"associated extension. For example, the number 5551212 when stripped with a\n"
	"count of 3 would be changed to 1212. This app always returns 0, and the PBX\n"
	"will continue processing at the next priority for the *new* extension.\n"
	"  So, for example, if priority 3 of 5551212 is StripMSD 3, the next step\n"
	"executed will be priority 4 of 1212. If you switch into an extension which\n"
	"has no first step, the PBX will treat it as though the user dialed an\n"
	"invalid extension.\n" 
	},

	{ "Suffix", pbx_builtin_suffix, 
	"Append trailing digits",
	"  Suffix(digits): Appends the  digit  string  specified  by  digits to the\n"
	"channel's associated extension. For example, the number 555 when  suffixed\n"
	"with '1212' will become 5551212. This app always returns 0, and the PBX will\n"
	"continue processing at the next priority for the *new* extension.\n"
	"  So, for example, if priority  3  of  555 is Suffix 1212, the  next  step\n"
	"executed will be priority 4 of 5551212. If  you  switch  into an  extension\n"
	"which has no first step, the PBX will treat it as though the user dialed an\n"
	"invalid extension.\n" 
	},

	{ "Wait", pbx_builtin_wait, 
	"Waits for some time", 
	"  Wait(seconds): Waits for a specified number of seconds, then returns 0.\n"
	"seconds can be passed with fractions of a second. (eg: 1.5 = 1.5 seconds)\n" 
	},

	{ "WaitExten", pbx_builtin_waitexten, 
	"Waits for some time", 
	"  Wait([seconds]): Waits for the user to enter a new extension for the \n"
	"specified number of seconds, then returns 0.  Seconds can be passed with\n"
	"fractions of a seconds (eg: 1.5 = 1.5 seconds) or if unspecified the\n"
	"default extension timeout will be used.\n" 
	},

};

AST_MUTEX_DEFINE_STATIC(applock); 		/* Lock for the application list */
static struct ast_context *contexts = NULL;
AST_MUTEX_DEFINE_STATIC(conlock); 		/* Lock for the ast_context list */
static struct ast_app *apps = NULL;

AST_MUTEX_DEFINE_STATIC(switchlock);		/* Lock for switches */
struct ast_switch *switches = NULL;

AST_MUTEX_DEFINE_STATIC(hintlock);		/* Lock for extension state notifys */
static int stateid = 1;
struct ast_hint *hints = NULL;
struct ast_state_cb *statecbs = NULL;

int pbx_exec(struct ast_channel *c, 		/* Channel */
		struct ast_app *app,		/* Application */
		void *data,			/* Data for execution */
		int newstack)			/* Force stack increment */
{
	/* This function is special.  It saves the stack so that no matter
	   how many times it is called, it returns to the same place */
	int res;
	
	char *saved_c_appl;
	char *saved_c_data;
	
	int stack = c->stack;
	int (*execute)(struct ast_channel *chan, void *data) = app->execute; 

	if (newstack && stack > AST_CHANNEL_MAX_STACK - 2) {
		/* Don't allow us to go over the max number of stacks we
		   permit saving. */
		ast_log(LOG_WARNING, "Stack overflow, cannot create another stack\n");
		return -1;
	}
	if (newstack && (res = setjmp(c->jmp[++c->stack]))) {
		/* Okay, here's where it gets weird.  If newstack is non-zero, 
		   then we increase the stack increment, but setjmp is not going
		   to return until longjmp is called -- when the application
		   exec'd is finished running. */
		if (res == 1)
			res = 0;
		if (c->stack != stack + 1) 
			ast_log(LOG_WARNING, "Stack returned to an unexpected place!\n");
		else if (c->app[c->stack])
			ast_log(LOG_WARNING, "Application may have forgotten to free its memory\n");
		c->stack = stack;
		return res;
	} else {
		if (c->cdr)
			ast_cdr_setapp(c->cdr, app->name, data);

		/* save channel values */
		saved_c_appl= c->appl;
		saved_c_data= c->data;

		c->appl = app->name;
		c->data = data;		
		res = execute(c, data);
		/* restore channel values */
		c->appl= saved_c_appl;
		c->data= saved_c_data;

		/* Any application that returns, we longjmp back, just in case. */
		if (c->stack != stack + 1)
			ast_log(LOG_WARNING, "Stack is not at expected value\n");
		longjmp(c->jmp[stack+1], res);
		/* Never returns */
	}
}


/* Go no deeper than this through includes (not counting loops) */
#define AST_PBX_MAX_STACK	64

#define HELPER_EXISTS 0
#define HELPER_SPAWN 1
#define HELPER_EXEC 2
#define HELPER_CANMATCH 3
#define HELPER_MATCHMORE 4
#define HELPER_FINDLABEL 5

struct ast_app *pbx_findapp(const char *app) 
{
	struct ast_app *tmp;

	if (ast_mutex_lock(&applock)) {
		ast_log(LOG_WARNING, "Unable to obtain application lock\n");
		return NULL;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(tmp->name, app))
			break;
		tmp = tmp->next;
	}
	ast_mutex_unlock(&applock);
	return tmp;
}

static struct ast_switch *pbx_findswitch(const char *sw)
{
	struct ast_switch *asw;

	if (ast_mutex_lock(&switchlock)) {
		ast_log(LOG_WARNING, "Unable to obtain application lock\n");
		return NULL;
	}
	asw = switches;
	while(asw) {
		if (!strcasecmp(asw->name, sw))
			break;
		asw = asw->next;
	}
	ast_mutex_unlock(&switchlock);
	return asw;
}

static inline int include_valid(struct ast_include *i)
{
	if (!i->hastime)
		return 1;

	return ast_check_timing(&(i->timing));
}

static void pbx_destroy(struct ast_pbx *p)
{
	free(p);
}

#define EXTENSION_MATCH_CORE(data,pattern,match) {\
	/* All patterns begin with _ */\
	if (pattern[0] != '_') \
		return 0;\
	/* Start optimistic */\
	match=1;\
	pattern++;\
	while(match && *data && *pattern && (*pattern != '/')) {\
		while (*data == '-' && (*(data+1) != '\0')) data++;\
		switch(toupper(*pattern)) {\
		case '[': \
		{\
			int i,border=0;\
			char *where;\
			match=0;\
			pattern++;\
			where=strchr(pattern,']');\
			if (where)\
				border=(int)(where-pattern);\
			if (!where || border > strlen(pattern)) {\
				ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");\
				return match;\
			}\
			for (i=0; i<border; i++) {\
				int res=0;\
				if (i+2<border)\
					if (pattern[i+1]=='-') {\
						if (*data >= pattern[i] && *data <= pattern[i+2]) {\
							res=1;\
						} else {\
							i+=2;\
							continue;\
						}\
					}\
				if (res==1 || *data==pattern[i]) {\
					match = 1;\
					break;\
				}\
			}\
			pattern+=border;\
			break;\
		}\
		case 'N':\
			if ((*data < '2') || (*data > '9'))\
				match=0;\
			break;\
		case 'X':\
			if ((*data < '0') || (*data > '9'))\
				match = 0;\
			break;\
		case 'Z':\
			if ((*data < '1') || (*data > '9'))\
				match = 0;\
			break;\
		case '.':\
			/* Must match */\
			return 1;\
		case ' ':\
		case '-':\
			/* Ignore these characters */\
			data--;\
			break;\
		default:\
			if (*data != *pattern)\
				match =0;\
		}\
		data++;\
		pattern++;\
	}\
}

int ast_extension_match(const char *pattern, const char *data)
{
	int match;
	/* If they're the same return */
	if (!strcmp(pattern, data))
		return 1;
	EXTENSION_MATCH_CORE(data,pattern,match);
	/* Must be at the end of both */
	if (*data || (*pattern && (*pattern != '/')))
		match = 0;
	return match;
}

static int extension_close(const char *pattern, const char *data, int needmore)
{
	int match;
	/* If "data" is longer, it can'be a subset of pattern unless
	   pattern is a pattern match */
	if ((strlen(pattern) < strlen(data)) && (pattern[0] != '_'))
		return 0;
	
	if ((ast_strlen_zero((char *)data) || !strncasecmp(pattern, data, strlen(data))) && 
		(!needmore || (strlen(pattern) > strlen(data)))) {
		return 1;
	}
	EXTENSION_MATCH_CORE(data,pattern,match);
	/* If there's more or we don't care about more, return non-zero, otlherwise it's a miss */
	if (!needmore || *pattern) {
		return match;
	} else
		return 0;
}

struct ast_context *ast_context_find(const char *name)
{
	struct ast_context *tmp;
	ast_mutex_lock(&conlock);
	if (name) {
		tmp = contexts;
		while(tmp) {
			if (!strcasecmp(name, tmp->name))
				break;
			tmp = tmp->next;
		}
	} else
		tmp = contexts;
	ast_mutex_unlock(&conlock);
	return tmp;
}

#define STATUS_NO_CONTEXT   1
#define STATUS_NO_EXTENSION 2
#define STATUS_NO_PRIORITY  3
#define STATUS_NO_LABEL		4
#define STATUS_SUCCESS	    5

static int matchcid(const char *cidpattern, const char *callerid)
{
	int failresult;
	
	/* If the Caller*ID pattern is empty, then we're matching NO Caller*ID, so
	   failing to get a number should count as a match, otherwise not */


	if (!ast_strlen_zero(cidpattern))
		failresult = 0;
	else
		failresult = 1;

	if (!callerid)
		return failresult;

	return ast_extension_match(cidpattern, callerid);
}

static struct ast_exten *pbx_find_extension(struct ast_channel *chan, struct ast_context *bypass, const char *context, const char *exten, int priority, const char *label, const char *callerid, int action, char *incstack[], int *stacklen, int *status, struct ast_switch **swo, char **data)
{
	int x, res;
	struct ast_context *tmp;
	struct ast_exten *e, *eroot;
	struct ast_include *i;
	struct ast_sw *sw;
	struct ast_switch *asw;

	/* Initialize status if appropriate */
	if (!*stacklen) {
		*status = STATUS_NO_CONTEXT;
		*swo = NULL;
		*data = NULL;
	}
	/* Check for stack overflow */
	if (*stacklen >= AST_PBX_MAX_STACK) {
		ast_log(LOG_WARNING, "Maximum PBX stack exceeded\n");
		return NULL;
	}
	/* Check first to see if we've already been checked */
	for (x=0;x<*stacklen;x++) {
		if (!strcasecmp(incstack[x], context))
			return NULL;
	}
	if (bypass)
		tmp = bypass;
	else
		tmp = contexts;
	while(tmp) {
		/* Match context */
		if (bypass || !strcmp(tmp->name, context)) {
			if (*status < STATUS_NO_EXTENSION)
				*status = STATUS_NO_EXTENSION;
			eroot = tmp->root;
			while(eroot) {
				/* Match extension */
				if ((((action != HELPER_MATCHMORE) && ast_extension_match(eroot->exten, exten)) ||
						((action == HELPER_CANMATCH) && (extension_close(eroot->exten, exten, 0))) ||
						((action == HELPER_MATCHMORE) && (extension_close(eroot->exten, exten, 1)))) &&
						(!eroot->matchcid || matchcid(eroot->cidmatch, callerid))) {
						e = eroot;
						if (*status < STATUS_NO_PRIORITY)
							*status = STATUS_NO_PRIORITY;
						while(e) {
							/* Match priority */
							if (action == HELPER_FINDLABEL) {
								if (*status < STATUS_NO_LABEL)
									*status = STATUS_NO_LABEL;
							 	if (label && e->label && !strcmp(label, e->label)) {
									*status = STATUS_SUCCESS;
									return e;
								}
							} else if (e->priority == priority) {
								*status = STATUS_SUCCESS;
								return e;
							}
							e = e->peer;
						}
				}
				eroot = eroot->next;
			}
			/* Check alternative switches */
			sw = tmp->alts;
			while(sw) {
				if ((asw = pbx_findswitch(sw->name))) {
					if (action == HELPER_CANMATCH)
						res = asw->canmatch ? asw->canmatch(chan, context, exten, priority, callerid, sw->data) : 0;
					else if (action == HELPER_MATCHMORE)
						res = asw->matchmore ? asw->matchmore(chan, context, exten, priority, callerid, sw->data) : 0;
					else
						res = asw->exists ? asw->exists(chan, context, exten, priority, callerid, sw->data) : 0;
					if (res) {
						/* Got a match */
						*swo = asw;
						*data = sw->data;
						return NULL;
					}
				} else {
					ast_log(LOG_WARNING, "No such switch '%s'\n", sw->name);
				}
				sw = sw->next;
			}
			/* Setup the stack */
			incstack[*stacklen] = tmp->name;
			(*stacklen)++;
			/* Now try any includes we have in this context */
			i = tmp->includes;
			while(i) {
				if (include_valid(i)) {
					if ((e = pbx_find_extension(chan, bypass, i->rname, exten, priority, label, callerid, action, incstack, stacklen, status, swo, data))) 
						return e;
					if (*swo) 
						return NULL;
				}
				i = i->next;
			}
			break;
		}
		tmp = tmp->next;
	}
	return NULL;
}

static void pbx_substitute_variables_temp(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp)
{
	char *first,*second;
	char tmpvar[80] = "";
	time_t thistime;
	struct tm brokentime;
	int offset,offset2;
	struct ast_var_t *variables;

	if (c) 
		headp=&c->varshead;
	*ret=NULL;
	/* Now we have the variable name on cp3 */
	if (!strncasecmp(var,"LEN(",4)) {
		int len=strlen(var);
		int len_len=4;
		if (strrchr(var,')')) {
			char cp3[80];
			strncpy(cp3, var, sizeof(cp3) - 1);
			cp3[len-len_len-1]='\0';
			sprintf(workspace,"%d",(int)strlen(cp3));
			*ret = workspace;
		} else {
			/* length is zero */
			*ret = "0";
		}
	} else if ((first=strchr(var,':'))) {
		strncpy(tmpvar, var, sizeof(tmpvar) - 1);
		first = strchr(tmpvar, ':');
		if (!first)
			first = tmpvar + strlen(tmpvar);
		*first='\0';
		pbx_substitute_variables_temp(c,tmpvar,ret,workspace,workspacelen - 1, headp);
		if (!(*ret)) return;
		offset=atoi(first+1);
	 	if ((second=strchr(first+1,':'))) {
			*second='\0';
			offset2=atoi(second+1);
		} else
			offset2=strlen(*ret)-offset;
		if (abs(offset)>strlen(*ret)) {
			if (offset>=0) 
				offset=strlen(*ret);
			else 
				offset=-strlen(*ret);
		}
		if ((offset<0 && offset2>-offset) || (offset>=0 && offset+offset2>strlen(*ret))) {
			if (offset>=0) 
				offset2=strlen(*ret)-offset;
			else 
				offset2=strlen(*ret)+offset;
		}
		if (offset>=0)
			*ret+=offset;
		else
			*ret+=strlen(*ret)+offset;
		(*ret)[offset2] = '\0';
	} else if (c && !strcmp(var, "CALLERIDNUM")) {
		if (c->cid.cid_num) {
			strncpy(workspace, c->cid.cid_num, workspacelen - 1);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "CALLERANI")) {
		if (c->cid.cid_ani) {
			strncpy(workspace, c->cid.cid_ani, workspacelen - 1);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "CALLERIDNAME")) {
		if (c->cid.cid_name) {
			strncpy(workspace, c->cid.cid_name, workspacelen - 1);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "CALLERID")) {
		if (c->cid.cid_num) {
			if (c->cid.cid_name) {
				snprintf(workspace, workspacelen, "\"%s\" <%s>", c->cid.cid_name, c->cid.cid_num);
			} else {
				strncpy(workspace, c->cid.cid_num, workspacelen - 1);
			}
			*ret = workspace;
		} else if (c->cid.cid_name) {
			strncpy(workspace, c->cid.cid_name, workspacelen - 1);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "DNID")) {
		if (c->cid.cid_dnid) {
			strncpy(workspace, c->cid.cid_dnid, workspacelen - 1);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "HINT")) {
		if (!ast_get_hint(workspace, workspacelen, c, c->context, c->exten))
			*ret = NULL;
		else
			*ret = workspace;
	} else if (c && !strcmp(var, "EXTEN")) {
		strncpy(workspace, c->exten, workspacelen - 1);
		*ret = workspace;
	} else if (c && !strncmp(var, "EXTEN-", strlen("EXTEN-")) && 
		/* XXX Remove me eventually */
		(sscanf(var + strlen("EXTEN-"), "%d", &offset) == 1)) {
		if (offset < 0)
			offset=0;
		if (offset > strlen(c->exten))
			offset = strlen(c->exten);
		strncpy(workspace, c->exten + offset, workspacelen - 1);
		*ret = workspace;
		ast_log(LOG_WARNING, "The use of 'EXTEN-foo' has been deprecated in favor of 'EXTEN:foo'\n");
	} else if (c && !strcmp(var, "RDNIS")) {
		if (c->cid.cid_rdnis) {
			strncpy(workspace, c->cid.cid_rdnis, workspacelen - 1);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "CONTEXT")) {
		strncpy(workspace, c->context, workspacelen - 1);
		*ret = workspace;
	} else if (c && !strcmp(var, "PRIORITY")) {
		snprintf(workspace, workspacelen, "%d", c->priority);
		*ret = workspace;
	} else if (c && !strcmp(var, "CALLINGPRES")) {
		snprintf(workspace, workspacelen, "%d", c->cid.cid_pres);
		*ret = workspace;
	} else if (c && !strcmp(var, "CALLINGANI2")) {
		snprintf(workspace, workspacelen, "%d", c->cid.cid_ani2);
		*ret = workspace;
	} else if (c && !strcmp(var, "CALLINGTON")) {
		snprintf(workspace, workspacelen, "%d", c->cid.cid_ton);
		*ret = workspace;
	} else if (c && !strcmp(var, "CALLINGTNS")) {
		snprintf(workspace, workspacelen, "%d", c->cid.cid_tns);
		*ret = workspace;
	} else if (c && !strcmp(var, "CHANNEL")) {
		strncpy(workspace, c->name, workspacelen - 1);
		*ret = workspace;
	} else if (c && !strcmp(var, "EPOCH")) {
		snprintf(workspace, workspacelen, "%u",(int)time(NULL));
		*ret = workspace;
	} else if (c && !strcmp(var, "DATETIME")) {
		thistime=time(NULL);
		localtime_r(&thistime, &brokentime);
		snprintf(workspace, workspacelen, "%02d%02d%04d-%02d:%02d:%02d",
			brokentime.tm_mday,
			brokentime.tm_mon+1,
			brokentime.tm_year+1900,
			brokentime.tm_hour,
			brokentime.tm_min,
			brokentime.tm_sec
		);
		*ret = workspace;
	} else if (c && !strcmp(var, "TIMESTAMP")) {
		thistime=time(NULL);
		localtime_r(&thistime, &brokentime);
		/* 20031130-150612 */
		snprintf(workspace, workspacelen, "%04d%02d%02d-%02d%02d%02d",
			brokentime.tm_year+1900,
			brokentime.tm_mon+1,
			brokentime.tm_mday,
			brokentime.tm_hour,
			brokentime.tm_min,
			brokentime.tm_sec
		);
		*ret = workspace;
	} else if (c && !strcmp(var, "UNIQUEID")) {
		snprintf(workspace, workspacelen, "%s", c->uniqueid);
		*ret = workspace;
	} else if (c && !strcmp(var, "HANGUPCAUSE")) {
		snprintf(workspace, workspacelen, "%i", c->hangupcause);
		*ret = workspace;
	} else if (c && !strcmp(var, "ACCOUNTCODE")) {
		strncpy(workspace, c->accountcode, workspacelen - 1);
		*ret = workspace;
	} else if (c && !strcmp(var, "LANGUAGE")) {
		strncpy(workspace, c->language, workspacelen - 1);
		*ret = workspace;
	} else {
		if (headp) {
			AST_LIST_TRAVERSE(headp,variables,entries) {
#if 0
				ast_log(LOG_WARNING,"Comparing variable '%s' with '%s'\n",var,ast_var_name(variables));
#endif
				if (strcasecmp(ast_var_name(variables),var)==0) {
					*ret=ast_var_value(variables);
					if (*ret) {
						strncpy(workspace, *ret, workspacelen - 1);
						*ret = workspace;
					}
					break;
				}
			}
		}
		if (!(*ret)) {
			/* Try globals */
			AST_LIST_TRAVERSE(&globals,variables,entries) {
#if 0
				ast_log(LOG_WARNING,"Comparing variable '%s' with '%s'\n",var,ast_var_name(variables));
#endif
				if (strcasecmp(ast_var_name(variables),var)==0) {
					*ret=ast_var_value(variables);
					if (*ret) {
						strncpy(workspace, *ret, workspacelen - 1);
						*ret = workspace;
					}
				}
			}
		}
		if (!(*ret)) {
			int len=strlen(var);
			int len_env=strlen("ENV(");
			if (len > (len_env+1) && !strncasecmp(var,"ENV(",len_env) && !strcmp(var+len-1,")")) {
				char cp3[80] = "";
				strncpy(cp3, var, sizeof(cp3) - 1);
				cp3[len-1]='\0';
				*ret=getenv(cp3+len_env);
				if (*ret) {
					strncpy(workspace, *ret, workspacelen - 1);
					*ret = workspace;
				}
			}
		}
	}
}

static void pbx_substitute_variables_helper_full(struct ast_channel *c, const char *cp1, char *cp2, int count, struct varshead *headp)
{
	char *cp4;
	const char *tmp, *whereweare;
	int length;
	char workspace[4096];
	char ltmp[4096], var[4096];
	char *nextvar, *nextexp;
	char *vars, *vare;
	int pos, brackets, needsub, len;
	
	/* Substitutes variables into cp2, based on string cp1, and assuming cp2 to be
	   zero-filled */
	whereweare=tmp=cp1;
	while(!ast_strlen_zero(whereweare) && count) {
		/* Assume we're copying the whole remaining string */
		pos = strlen(whereweare);

		/* Look for a variable */
		nextvar = strstr(whereweare, "${");
		
		/* Look for an expression */
		nextexp = strstr(whereweare, "$[");
		
		/* Pick the first one only */
		if (nextvar && nextexp) {
			if (nextvar < nextexp)
				nextexp = NULL;
			else
				nextvar = NULL;
		}
		
		/* If there is one, we only go that far */
		if (nextvar)
			pos = nextvar - whereweare;
		else if (nextexp)
			pos = nextexp - whereweare;
		
		/* Can't copy more than 'count' bytes */
		if (pos > count)
			pos = count;
		
		/* Copy that many bytes */
		memcpy(cp2, whereweare, pos);
		
		count -= pos;
		cp2 += pos;
		whereweare += pos;
		
		if (nextvar) {
			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;
			
			/* Find the end of it */
			while(brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '['))
					needsub++;
				vare++;
			}
			if (brackets)
				ast_log(LOG_NOTICE, "Error in extension logic (missing '}')\n");
			len = vare - vars - 1;
			
			/* Skip totally over variable name */
			whereweare += ( len + 3);
			
			/* Store variable name (and truncate) */
			memset(var, 0, sizeof(var));
			strncpy(var, vars, sizeof(var) - 1);
			var[len] = '\0';
			
			/* Substitute if necessary */
			if (needsub) {
				memset(ltmp, 0, sizeof(ltmp));
				pbx_substitute_variables_helper(c, var, ltmp, sizeof(ltmp) - 1);
				vars = ltmp;
			} else {
				vars = var;
			}
			
			/* Retrieve variable value */
			workspace[0] = '\0';
			pbx_substitute_variables_temp(c,vars,&cp4, workspace, sizeof(workspace), headp);
			if (cp4) {
				length = strlen(cp4);
				if (length > count)
					length = count;
				memcpy(cp2, cp4, length);
				count -= length;
				cp2 += length;
			}
			
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;
			
			/* Find the end of it */
			while(brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '[') {
					brackets++;
				} else if (vare[0] == ']') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			if (brackets)
				ast_log(LOG_NOTICE, "Error in extension logic (missing ']')\n");
			len = vare - vars - 1;
			
			/* Skip totally over variable name */
			whereweare += ( len + 3);
			
			/* Store variable name (and truncate) */
			memset(var, 0, sizeof(var));
			strncpy(var, vars, sizeof(var) - 1);
			var[len] = '\0';
			
			/* Substitute if necessary */
			if (needsub) {
				memset(ltmp, 0, sizeof(ltmp));
				pbx_substitute_variables_helper(c, var, ltmp, sizeof(ltmp) - 1);
				vars = ltmp;
			} else {
				vars = var;
			}

			/* Evaluate expression */			
			cp4 = ast_expr(vars);
			
			ast_log(LOG_DEBUG, "Expression is '%s'\n", cp4);
			
			if (cp4) {
				length = strlen(cp4);
				if (length > count)
					length = count;
				memcpy(cp2, cp4, length);
				count -= length;
				cp2 += length;
				free(cp4);
			}
			
		} else
			break;
	}
}

void pbx_substitute_variables_helper(struct ast_channel *c, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(c, cp1, cp2, count, NULL);
}

void pbx_substitute_variables_varshead(struct varshead *headp, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(NULL, cp1, cp2, count, headp);
}

static void pbx_substitute_variables(char *passdata, int datalen, struct ast_channel *c, struct ast_exten *e) {
        
	memset(passdata, 0, datalen);
		
	/* No variables or expressions in e->data, so why scan it? */
	if (!strstr(e->data,"${") && !strstr(e->data,"$[")) {
		strncpy(passdata, e->data, datalen - 1);
		passdata[datalen-1] = '\0';
		return;
	}
	
	pbx_substitute_variables_helper(c, e->data, passdata, datalen - 1);
}		                                                

static int pbx_extension_helper(struct ast_channel *c, struct ast_context *con, const char *context, const char *exten, int priority, const char *label, const char *callerid, int action) 
{
	struct ast_exten *e;
	struct ast_app *app;
	struct ast_switch *sw;
	char *data;
	int newstack = 0;
	int res;
	int status = 0;
	char *incstack[AST_PBX_MAX_STACK];
	char passdata[EXT_DATA_SIZE];
	int stacklen = 0;
	char tmp[80];
	char tmp2[80];
	char tmp3[EXT_DATA_SIZE];

	if (ast_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		if ((action == HELPER_EXISTS) || (action == HELPER_CANMATCH) || (action == HELPER_MATCHMORE))
			return 0;
		else
			return -1;
	}
	e = pbx_find_extension(c, con, context, exten, priority, label, callerid, action, incstack, &stacklen, &status, &sw, &data);
	if (e) {
		switch(action) {
		case HELPER_CANMATCH:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_EXISTS:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_FINDLABEL:
			res = e->priority;
			ast_mutex_unlock(&conlock);
			return res;
		case HELPER_MATCHMORE:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_SPAWN:
			newstack++;
			/* Fall through */
		case HELPER_EXEC:
			app = pbx_findapp(e->app);
			ast_mutex_unlock(&conlock);
			if (app) {
				if (c->context != context)
					strncpy(c->context, context, sizeof(c->context)-1);
				if (c->exten != exten)
					strncpy(c->exten, exten, sizeof(c->exten)-1);
				c->priority = priority;
				pbx_substitute_variables(passdata, sizeof(passdata), c, e);
				if (option_debug)
						ast_log(LOG_DEBUG, "Launching '%s'\n", app->name);
				if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Executing %s(\"%s\", \"%s\") %s\n", 
								term_color(tmp, app->name, COLOR_BRCYAN, 0, sizeof(tmp)),
								term_color(tmp2, c->name, COLOR_BRMAGENTA, 0, sizeof(tmp2)),
								term_color(tmp3, (!ast_strlen_zero(passdata) ? (char *)passdata : ""), COLOR_BRMAGENTA, 0, sizeof(tmp3)),
								(newstack ? "in new stack" : "in same stack"));
				manager_event(EVENT_FLAG_CALL, "Newexten", 
					"Channel: %s\r\n"
					"Context: %s\r\n"
					"Extension: %s\r\n"
					"Priority: %d\r\n"
					"Application: %s\r\n"
					"AppData: %s\r\n"
					"Uniqueid: %s\r\n",
					c->name, c->context, c->exten, c->priority, app->name, passdata ? passdata : "(NULL)", c->uniqueid);
				res = pbx_exec(c, app, passdata, newstack);
				return res;
			} else {
				ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
				return -1;
			}
		default:
			ast_log(LOG_WARNING, "Huh (%d)?\n", action);			return -1;
		}
	} else if (sw) {
		switch(action) {
		case HELPER_CANMATCH:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_EXISTS:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_MATCHMORE:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_FINDLABEL:
			ast_mutex_unlock(&conlock);
			return -1;
		case HELPER_SPAWN:
			newstack++;
			/* Fall through */
		case HELPER_EXEC:
			ast_mutex_unlock(&conlock);
			if (sw->exec)
				res = sw->exec(c, context, exten, priority, callerid, newstack, data);
			else {
				ast_log(LOG_WARNING, "No execution engine for switch %s\n", sw->name);
				res = -1;
			}
			return res;
		default:
			ast_log(LOG_WARNING, "Huh (%d)?\n", action);
			return -1;
		}
	} else {
		ast_mutex_unlock(&conlock);
		switch(status) {
		case STATUS_NO_CONTEXT:
			if ((action != HELPER_EXISTS) && (action != HELPER_MATCHMORE))
				ast_log(LOG_NOTICE, "Cannot find extension context '%s'\n", context);
			break;
		case STATUS_NO_EXTENSION:
			if ((action != HELPER_EXISTS) && (action !=  HELPER_CANMATCH) && (action != HELPER_MATCHMORE))
				ast_log(LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, context);
			break;
		case STATUS_NO_PRIORITY:
			if ((action != HELPER_EXISTS) && (action !=  HELPER_CANMATCH) && (action != HELPER_MATCHMORE))
				ast_log(LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, context);
			break;
		case STATUS_NO_LABEL:
			if (context)
				ast_log(LOG_NOTICE, "No such label '%s' in extension '%s' in context '%s'\n", label, exten, context);
			break;
		default:
			ast_log(LOG_DEBUG, "Shouldn't happen!\n");
		}
		
		if ((action != HELPER_EXISTS) && (action != HELPER_CANMATCH) && (action != HELPER_MATCHMORE))
			return -1;
		else
			return 0;
	}

}

static struct ast_exten *ast_hint_extension(struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	struct ast_switch *sw;
	char *data;
	int status = 0;
	char *incstack[AST_PBX_MAX_STACK];
	int stacklen = 0;

	if (ast_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		return NULL;
	}
	e = pbx_find_extension(c, NULL, context, exten, PRIORITY_HINT, NULL, "", HELPER_EXISTS, incstack, &stacklen, &status, &sw, &data);
	ast_mutex_unlock(&conlock);	
	return e;
}

static int ast_extension_state2(struct ast_exten *e)
{
	char hint[AST_MAX_EXTENSION] = "";    
	char *cur, *rest;
	int res = -1;
	int allunavailable = 1, allbusy = 1, allfree = 1;
	int busy = 0;

	strncpy(hint, ast_get_extension_app(e), sizeof(hint)-1);
    
	cur = hint;    
	do {
		rest = strchr(cur, '&');
		if (rest) {
	    		*rest = 0;
			rest++;
		}
	
		res = ast_device_state(cur);
		switch (res) {
    		case AST_DEVICE_NOT_INUSE:
			allunavailable = 0;
			allbusy = 0;
			break;
    		case AST_DEVICE_INUSE:
			return AST_EXTENSION_INUSE;
    		case AST_DEVICE_BUSY:
			allunavailable = 0;
			allfree = 0;
			busy = 1;
			break;
    		case AST_DEVICE_UNAVAILABLE:
    		case AST_DEVICE_INVALID:
			allbusy = 0;
			allfree = 0;
			break;
    		default:
			allunavailable = 0;
			allbusy = 0;
			allfree = 0;
		}
       		cur = rest;
	} while (cur);

	if (allfree)
		return AST_EXTENSION_NOT_INUSE;
	if (allbusy)
		return AST_EXTENSION_BUSY;
	if (allunavailable)
		return AST_EXTENSION_UNAVAILABLE;
	if (busy) 
		return AST_EXTENSION_INUSE;
	
	return AST_EXTENSION_NOT_INUSE;
}


int ast_extension_state(struct ast_channel *c, char *context, char *exten)
{
	struct ast_exten *e;

	e = ast_hint_extension(c, context, exten);    
	if (!e) 
		return -1;

	return ast_extension_state2(e);    
}

int ast_device_state_changed(const char *fmt, ...) 
{
	struct ast_hint *list;
	struct ast_state_cb *cblist;
	struct ast_devstate_cb *devcb;
	char hint[AST_MAX_EXTENSION] = "";
	char device[AST_MAX_EXTENSION];
	char *cur, *rest;
	int state;

	va_list ap;

	va_start(ap, fmt);
	vsnprintf(device, sizeof(device), fmt, ap);
	va_end(ap);

	rest = strchr(device, '-');
	if (rest) {
		*rest = 0;
	}

	state = ast_device_state(device);

	ast_mutex_lock(&hintlock);

	devcb = devcbs;
	while(devcb) {
		if (devcb->callback)
			devcb->callback(device, state, devcb->data);
		devcb = devcb->next;
	}
	list = hints;

	while (list) {

		strncpy(hint, ast_get_extension_app(list->exten), sizeof(hint) - 1);
		cur = hint;
		do {
			rest = strchr(cur, '&');
			if (rest) {
				*rest = 0;
				rest++;
			}
			
			if (!strcmp(cur, device)) {
				/* Found extension execute callbacks  */
				state = ast_extension_state2(list->exten);
				if ((state != -1) && (state != list->laststate)) {
					/* For general callbacks */
					cblist = statecbs;
					while (cblist) {
						cblist->callback(list->exten->parent->name, list->exten->exten, state, cblist->data);
						cblist = cblist->next;
					}

					/* For extension callbacks */
					cblist = list->callbacks;
					while (cblist) {
						cblist->callback(list->exten->parent->name, list->exten->exten, state, cblist->data);
						cblist = cblist->next;
					}
			
					list->laststate = state;
				}
				break;
			}
			cur = rest;
		} while (cur);
		list = list->next;
	}
	ast_mutex_unlock(&hintlock);
	return 1;
}
			
int ast_devstate_add(ast_devstate_cb_type callback, void *data)
{
	struct ast_devstate_cb *devcb;
	devcb = malloc(sizeof(struct ast_devstate_cb));
	if (devcb) {
		memset(devcb, 0, sizeof(struct ast_devstate_cb));
		ast_mutex_lock(&hintlock);
		devcb->data = data;
		devcb->callback = callback;
		devcb->next = devcbs;
		devcbs = devcb;
		ast_mutex_unlock(&hintlock);
	}
	return 0;
}

void ast_devstate_del(ast_devstate_cb_type callback, void *data)
{
	struct ast_devstate_cb *devcb, *prev = NULL, *next;
	ast_mutex_lock(&hintlock);
	devcb = devcbs;
	while(devcb) {
		next = devcb->next;
		if ((devcb->data == data) && (devcb->callback == callback)) {
			if (prev)
				prev->next = next;
			else
				devcbs = next;
			free(devcb);
		} else
			prev = devcb;
		devcb = next;
	}
	ast_mutex_unlock(&hintlock);
}

int ast_extension_state_add(const char *context, const char *exten, 
			    ast_state_cb_type callback, void *data)
{
	struct ast_hint *list;
	struct ast_state_cb *cblist;
	struct ast_exten *e;

	/* No context and extension add callback to statecbs list */
	if (!context && !exten) {
		ast_mutex_lock(&hintlock);

		cblist = statecbs;
		while (cblist) {
			if (cblist->callback == callback) {
				cblist->data = data;
				ast_mutex_unlock(&hintlock);
			}
			cblist = cblist->next;
		}
	
		/* Now inserts the callback */
		cblist = malloc(sizeof(struct ast_state_cb));
		if (!cblist) {
			ast_mutex_unlock(&hintlock);
			return -1;
		}
		memset(cblist, 0, sizeof(struct ast_state_cb));
		cblist->id = 0;
		cblist->callback = callback;
		cblist->data = data;
	
       		cblist->next = statecbs;
		statecbs = cblist;

		ast_mutex_unlock(&hintlock);
		return 0;
    	}

	if (!context || !exten)
		return -1;

	/* This callback type is for only one hint */
	e = ast_hint_extension(NULL, context, exten);    
	if (!e) {
		return -1;
	}
    
	ast_mutex_lock(&hintlock);
	list = hints;        
    
	while (list) {
		if (list->exten == e)
			break;	    
		list = list->next;    
	}

	if (!list) {
		ast_mutex_unlock(&hintlock);
		return -1;
	}

	/* Now inserts the callback */
	cblist = malloc(sizeof(struct ast_state_cb));
	if (!cblist) {
		ast_mutex_unlock(&hintlock);
		return -1;
	}
	memset(cblist, 0, sizeof(struct ast_state_cb));
	cblist->id = stateid++;
	cblist->callback = callback;
	cblist->data = data;

	cblist->next = list->callbacks;
	list->callbacks = cblist;

	ast_mutex_unlock(&hintlock);
	return cblist->id;
}

int ast_extension_state_del(int id, ast_state_cb_type callback)
{
	struct ast_hint *list;
	struct ast_state_cb *cblist, *cbprev;
    
	if (!id && !callback)
		return -1;
            
	ast_mutex_lock(&hintlock);

	/* id is zero is a callback without extension */
	if (!id) {
		cbprev = NULL;
		cblist = statecbs;
		while (cblist) {
	 		if (cblist->callback == callback) {
				if (!cbprev)
		    			statecbs = cblist->next;
				else
		    			cbprev->next = cblist->next;

				free(cblist);

	        		ast_mutex_unlock(&hintlock);
				return 0;
	    		}
	    		cbprev = cblist;
	    		cblist = cblist->next;
		}

    		ast_mutex_lock(&hintlock);
		return -1;
	}

	/* id greater than zero is a callback with extension */
	list = hints;
	while (list) {
		cblist = list->callbacks;
		cbprev = NULL;
		while (cblist) {
	    		if (cblist->id==id) {
				if (!cbprev)
		    			list->callbacks = cblist->next;		
				else
		    			cbprev->next = cblist->next;
		
				free(cblist);
		
				ast_mutex_unlock(&hintlock);
				return 0;		
	    		}		
    	    		cbprev = cblist;				
	    		cblist = cblist->next;
		}
		list = list->next;
	}
    
	ast_mutex_unlock(&hintlock);
	return -1;
}

static int ast_add_hint(struct ast_exten *e)
{
	struct ast_hint *list;

	if (!e) 
		return -1;
    
	ast_mutex_lock(&hintlock);
	list = hints;        
    
	/* Search if hint exists, do nothing */
	while (list) {
		if (list->exten == e) {
			ast_mutex_unlock(&hintlock);
			return -1;
		}
		list = list->next;    
    	}

	list = malloc(sizeof(struct ast_hint));
	if (!list) {
		ast_mutex_unlock(&hintlock);
		return -1;
	}
	/* Initialize and insert new item */
	memset(list, 0, sizeof(struct ast_hint));
	list->exten = e;
	list->laststate = ast_extension_state2(e);
	list->next = hints;
	hints = list;

	ast_mutex_unlock(&hintlock);
	return 0;
}

static int ast_change_hint(struct ast_exten *oe, struct ast_exten *ne)
{ 
	struct ast_hint *list;

	ast_mutex_lock(&hintlock);
	list = hints;
    
	while(list) {
		if (list->exten == oe) {
	    		list->exten = ne;
			ast_mutex_unlock(&hintlock);	
			return 0;
		}
		list = list->next;
	}
	ast_mutex_unlock(&hintlock);

	return -1;
}

static int ast_remove_hint(struct ast_exten *e)
{
	/* Cleanup the Notifys if hint is removed */
	struct ast_hint *list, *prev = NULL;
	struct ast_state_cb *cblist, *cbprev;

	if (!e) 
		return -1;

	ast_mutex_lock(&hintlock);

	list = hints;    
	while(list) {
		if (list->exten==e) {
			cbprev = NULL;
			cblist = list->callbacks;
			while (cblist) {
				/* Notify with -1 and remove all callbacks */
				cbprev = cblist;	    
				cblist = cblist->next;
				cbprev->callback(list->exten->parent->name, list->exten->exten, -1, cbprev->data);
				free(cbprev);
	    		}
	    		list->callbacks = NULL;

	    		if (!prev)
				hints = list->next;
	    		else
				prev->next = list->next;
	    		free(list);
	    
			ast_mutex_unlock(&hintlock);
			return 0;
		} else {
			prev = list;
			list = list->next;    
		}
    	}

	ast_mutex_unlock(&hintlock);
	return -1;
}


int ast_get_hint(char *hint, int hintsize, struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	e = ast_hint_extension(c, context, exten);
	if (e) {	
	    strncpy(hint, ast_get_extension_app(e), hintsize - 1);
	    return -1;
	}
	return 0;	
}

int ast_exists_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid) 
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_EXISTS);
}

int ast_findlabel_extension(struct ast_channel *c, const char *context, const char *exten, const char *label, const char *callerid) 
{
	return pbx_extension_helper(c, NULL, context, exten, 0, label, callerid, HELPER_FINDLABEL);
}

int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, const char *exten, const char *label, const char *callerid) 
{
	return pbx_extension_helper(c, con, NULL, exten, 0, label, callerid, HELPER_FINDLABEL);
}

int ast_canmatch_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_CANMATCH);
}

int ast_matchmore_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_MATCHMORE);
}

int ast_spawn_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid) 
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_SPAWN);
}

int ast_exec_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid) 
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_EXEC);
}

int ast_pbx_run(struct ast_channel *c)
{
	int firstpass = 1;
	char digit;
	char exten[256];
	int pos;
	int waittime;
	int res=0;

	/* A little initial setup here */
	if (c->pbx)
		ast_log(LOG_WARNING, "%s already has PBX structure??\n", c->name);
	c->pbx = malloc(sizeof(struct ast_pbx));
	if (!c->pbx) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	if (c->amaflags) {
		if (!c->cdr) {
			c->cdr = ast_cdr_alloc();
			if (!c->cdr) {
				ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
				free(c->pbx);
				return -1;
			}
			ast_cdr_init(c->cdr, c);
		}
	}
	memset(c->pbx, 0, sizeof(struct ast_pbx));
	/* Set reasonable defaults */
	c->pbx->rtimeout = 10;
	c->pbx->dtimeout = 5;

	/* Start by trying whatever the channel is set to */
	if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
		/* JK02: If not successfull fall back to 's' */
		if (option_verbose > 1)
			ast_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d failed so falling back to exten 's'\n", c->name, c->context, c->exten, c->priority);
		strncpy(c->exten, "s", sizeof(c->exten)-1);
		if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			/* JK02: And finally back to default if everything else failed */
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d still failed so falling back to context 'default'\n", c->name, c->context, c->exten, c->priority);
			strncpy(c->context, "default", sizeof(c->context)-1);
		}
		c->priority = 1;
	}
	if (c->cdr && !c->cdr->start.tv_sec && !c->cdr->start.tv_usec)
		ast_cdr_start(c->cdr);
	for(;;) {
		pos = 0;
		digit = 0;
		while(ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			memset(exten, 0, sizeof(exten));
			if ((res = ast_spawn_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))) {
				/* Something bad happened, or a hangup has been requested. */
				if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F')) ||
					(res == '*') || (res == '#')) {
					ast_log(LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
					memset(exten, 0, sizeof(exten));
					pos = 0;
					exten[pos++] = digit = res;
					break;
				}
				switch(res) {
				case AST_PBX_KEEPALIVE:
					if (option_debug)
						ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
					else if (option_verbose > 1)
						ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
					goto out;
					break;
				default:
					if (option_debug)
						ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
					else if (option_verbose > 1)
						ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
					if (c->_softhangup == AST_SOFTHANGUP_ASYNCGOTO) {
						c->_softhangup =0;
						break;
					}
					/* atimeout */
					if (c->_softhangup == AST_SOFTHANGUP_TIMEOUT) {
						break;
					}

					if (c->cdr) {
						ast_cdr_update(c);
					}
					goto out;
				}
			}
			if ((c->_softhangup == AST_SOFTHANGUP_TIMEOUT) && (ast_exists_extension(c,c->context,"T",1,c->cid.cid_num))) {
				strncpy(c->exten,"T",sizeof(c->exten) - 1);
				/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
				c->whentohangup = 0;
				c->priority = 0;
				c->_softhangup &= ~AST_SOFTHANGUP_TIMEOUT;
			} else if (c->_softhangup) {
				ast_log(LOG_DEBUG, "Extension %s, priority %d returned normally even though call was hung up\n",
					c->exten, c->priority);
				goto out;
			}
			firstpass = 0;
			c->priority++;
		}
		if (!ast_exists_extension(c, c->context, c->exten, 1, c->cid.cid_num)) {
			/* It's not a valid extension anymore */
			if (ast_exists_extension(c, c->context, "i", 1, c->cid.cid_num)) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Sent into invalid extension '%s' in context '%s' on %s\n", c->exten, c->context, c->name);
				pbx_builtin_setvar_helper(c, "INVALID_EXTEN", c->exten);
				strncpy(c->exten, "i", sizeof(c->exten)-1);
				c->priority = 1;
			} else {
				ast_log(LOG_WARNING, "Channel '%s' sent into invalid extension '%s' in context '%s', but no invalid handler\n",
					c->name, c->exten, c->context);
				goto out;
			}
		} else if (c->_softhangup == AST_SOFTHANGUP_TIMEOUT) {
			/* If we get this far with AST_SOFTHANGUP_TIMEOUT, then we know that the "T" extension is next. */
			c->_softhangup = 0;
		} else {
			/* Done, wait for an extension */
			waittime = 0;
			if (digit)
				waittime = c->pbx->dtimeout;
			else if (!autofallthrough)
				waittime = c->pbx->rtimeout;
			if (waittime) {
				while (ast_matchmore_extension(c, c->context, exten, 1, c->cid.cid_num)) {
					/* As long as we're willing to wait, and as long as it's not defined, 
					   keep reading digits until we can't possibly get a right answer anymore.  */
					digit = ast_waitfordigit(c, waittime * 1000);
					if (c->_softhangup == AST_SOFTHANGUP_ASYNCGOTO) {
						c->_softhangup = 0;
					} else {
						if (!digit)
							/* No entry */
							break;
						if (digit < 0)
							/* Error, maybe a  hangup */
							goto out;
						exten[pos++] = digit;
						waittime = c->pbx->dtimeout;
					}
				}
				if (ast_exists_extension(c, c->context, exten, 1, c->cid.cid_num)) {
					/* Prepare the next cycle */
					strncpy(c->exten, exten, sizeof(c->exten)-1);
					c->priority = 1;
				} else {
					/* No such extension */
					if (!ast_strlen_zero(exten)) {
						/* An invalid extension */
						if (ast_exists_extension(c, c->context, "i", 1, c->cid.cid_num)) {
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "Invalid extension '%s' in context '%s' on %s\n", exten, c->context, c->name);
							pbx_builtin_setvar_helper(c, "INVALID_EXTEN", exten);
							strncpy(c->exten, "i", sizeof(c->exten)-1);
							c->priority = 1;
						} else {
							ast_log(LOG_WARNING, "Invalid extension '%s', but no rule 'i' in context '%s'\n", exten, c->context);
							goto out;
						}
					} else {
						/* A simple timeout */
						if (ast_exists_extension(c, c->context, "t", 1, c->cid.cid_num)) {
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "Timeout on %s\n", c->name);
							strncpy(c->exten, "t", sizeof(c->exten)-1);
							c->priority = 1;
						} else {
							ast_log(LOG_WARNING, "Timeout, but no rule 't' in context '%s'\n", c->context);
							goto out;
						}
					}	
				}
				if (c->cdr) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_2 "CDR updated on %s\n",c->name);	
					ast_cdr_update(c);
			    }
			} else {
				if (option_verbose > 0) {
					char *status;
					status = pbx_builtin_getvar_helper(c, "DIALSTATUS");
					if (!status)
						status = "UNKNOWN";
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_2 "Auto fallthrough, channel '%s' status is '%s'\n", c->name, status);
					if (!strcasecmp(status, "CONGESTION"))
						res = pbx_builtin_congestion(c, "10");
					else if (!strcasecmp(status, "CHANUNAVAIL"))
						res = pbx_builtin_congestion(c, "10");
					else if (!strcasecmp(status, "BUSY"))
						res = pbx_builtin_busy(c, "10");
					goto out;
				}
			}
		}
	}
	if (firstpass) 
		ast_log(LOG_WARNING, "Don't know what to do with '%s'\n", c->name);
out:
	if ((res != AST_PBX_KEEPALIVE) && ast_exists_extension(c, c->context, "h", 1, c->cid.cid_num)) {
		c->exten[0] = 'h';
		c->exten[1] = '\0';
		c->priority = 1;
		while(ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			if ((res = ast_spawn_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))) {
				/* Something bad happened, or a hangup has been requested. */
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				else if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				break;
			}
			c->priority++;
		}
	}

	pbx_destroy(c->pbx);
	c->pbx = NULL;
	if (res != AST_PBX_KEEPALIVE)
		ast_hangup(c);
	return 0;
}

static void *pbx_thread(void *data)
{
	/* Oh joyeous kernel, we're a new thread, with nothing to do but
	   answer this channel and get it going.  The setjmp stuff is fairly
	   confusing, but necessary to get smooth transitions between
	   the execution of different applications (without the use of
	   additional threads) */
	struct ast_channel *c = data;
	ast_pbx_run(c);
	pthread_exit(NULL);
	return NULL;
}

int ast_pbx_start(struct ast_channel *c)
{
	pthread_t t;
	pthread_attr_t attr;
	if (!c) {
		ast_log(LOG_WARNING, "Asked to start thread on NULL channel?\n");
		return -1;
	}
	   
	/* Start a new thread, and get something handling this channel. */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (ast_pthread_create(&t, &attr, pbx_thread, c)) {
		ast_log(LOG_WARNING, "Failed to create new channel thread\n");
		return -1;
	}
	return 0;
}

int pbx_set_autofallthrough(int newval)
{
	int oldval;
	oldval = autofallthrough;
	if (oldval != newval)
		autofallthrough = newval;
	return oldval;
}

/*
 * This function locks contexts list by &conlist, search for the right context
 * structure, leave context list locked and call ast_context_remove_include2
 * which removes include, unlock contexts list and return ...
 */
int ast_context_remove_include(const char *context, const char *include, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) return -1;

	/* walk contexts and search for the right one ...*/
	c = ast_walk_contexts(NULL);
	while (c) {
		/* we found one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret;
			/* remove include from this context ... */	
			ret = ast_context_remove_include2(c, include, registrar);

			ast_unlock_contexts();

			/* ... return results */
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right one context */
	ast_unlock_contexts();
	return -1;
}

/*
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * This function locks given context, removes include, unlock context and
 * return.
 */
int ast_context_remove_include2(struct ast_context *con, const char *include, const char *registrar)
{
	struct ast_include *i, *pi = NULL;

	if (ast_mutex_lock(&con->lock)) return -1;

	/* walk includes */
	i = con->includes;
	while (i) {
		/* find our include */
		if (!strcmp(i->name, include) && 
			(!registrar || !strcmp(i->registrar, registrar))) {
			/* remove from list */
			if (pi)
				pi->next = i->next;
			else
				con->includes = i->next;
			/* free include and return */
			free(i);
			ast_mutex_unlock(&con->lock);
			return 0;
		}
		pi = i;
		i = i->next;
	}

	/* we can't find the right include */
	ast_mutex_unlock(&con->lock);
	return -1;
}

/*
 * This function locks contexts list by &conlist, search for the rigt context
 * structure, leave context list locked and call ast_context_remove_switch2
 * which removes switch, unlock contexts list and return ...
 */
int ast_context_remove_switch(const char *context, const char *sw, const char *data, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) return -1;

	/* walk contexts and search for the right one ...*/
	c = ast_walk_contexts(NULL);
	while (c) {
		/* we found one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret;
			/* remove switch from this context ... */	
			ret = ast_context_remove_switch2(c, sw, data, registrar);

			ast_unlock_contexts();

			/* ... return results */
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right one context */
	ast_unlock_contexts();
	return -1;
}

/*
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * This function locks given context, removes switch, unlock context and
 * return.
 */
int ast_context_remove_switch2(struct ast_context *con, const char *sw, const char *data, const char *registrar)
{
	struct ast_sw *i, *pi = NULL;

	if (ast_mutex_lock(&con->lock)) return -1;

	/* walk switchs */
	i = con->alts;
	while (i) {
		/* find our switch */
		if (!strcmp(i->name, sw) && !strcmp(i->data, data) && 
			(!registrar || !strcmp(i->registrar, registrar))) {
			/* remove from list */
			if (pi)
				pi->next = i->next;
			else
				con->alts = i->next;
			/* free switch and return */
			free(i);
			ast_mutex_unlock(&con->lock);
			return 0;
		}
		pi = i;
		i = i->next;
	}

	/* we can't find the right switch */
	ast_mutex_unlock(&con->lock);
	return -1;
}

/*
 * This functions lock contexts list, search for the right context,
 * call ast_context_remove_extension2, unlock contexts list and return.
 * In this function we are using
 */
int ast_context_remove_extension(const char *context, const char *extension, int priority, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) return -1;

	/* walk contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* ... search for the right one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			/* ... remove extension ... */
			int ret = ast_context_remove_extension2(c, extension, priority,
				registrar);
			/* ... unlock contexts list and return */
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right context */
	ast_unlock_contexts();
	return -1;
}

/*
 * When do you want to call this function, make sure that &conlock is locked,
 * because some process can handle with your *con context before you lock
 * it.
 *
 * This functionc locks given context, search for the right extension and
 * fires out all peer in this extensions with given priority. If priority
 * is set to 0, all peers are removed. After that, unlock context and
 * return.
 */
int ast_context_remove_extension2(struct ast_context *con, const char *extension, int priority, const char *registrar)
{
	struct ast_exten *exten, *prev_exten = NULL;

	if (ast_mutex_lock(&con->lock)) return -1;

	/* go through all extensions in context and search the right one ... */
	exten = con->root;
	while (exten) {

		/* look for right extension */
		if (!strcmp(exten->exten, extension) &&
			(!registrar || !strcmp(exten->registrar, registrar))) {
			struct ast_exten *peer;

			/* should we free all peers in this extension? (priority == 0)? */
			if (priority == 0) {
				/* remove this extension from context list */
				if (prev_exten)
					prev_exten->next = exten->next;
				else
					con->root = exten->next;

				/* fire out all peers */
				peer = exten; 
				while (peer) {
					exten = peer->peer;
					
					if (!peer->priority==PRIORITY_HINT) 
					    ast_remove_hint(peer);

					peer->datad(peer->data);
					free(peer);

					peer = exten;
				}

				ast_mutex_unlock(&con->lock);
				return 0;
			} else {
				/* remove only extension with exten->priority == priority */
				struct ast_exten *previous_peer = NULL;

				peer = exten;
				while (peer) {
					/* is this our extension? */
					if (peer->priority == priority &&
						(!registrar || !strcmp(peer->registrar, registrar) )) {
						/* we are first priority extension? */
						if (!previous_peer) {
							/* exists previous extension here? */
							if (prev_exten) {
								/* yes, so we must change next pointer in
								 * previous connection to next peer
								 */
								if (peer->peer) {
									prev_exten->next = peer->peer;
									peer->peer->next = exten->next;
								} else
									prev_exten->next = exten->next;
							} else {
								/* no previous extension, we are first
								 * extension, so change con->root ...
								 */
								if (peer->peer)
									con->root = peer->peer;
								else
									con->root = exten->next; 
							}
						} else {
							/* we are not first priority in extension */
							previous_peer->peer = peer->peer;
						}

						/* now, free whole priority extension */
						if (peer->priority==PRIORITY_HINT)
						    ast_remove_hint(peer);
						peer->datad(peer->data);
						free(peer);

						ast_mutex_unlock(&con->lock);
						return 0;
					} else {
						/* this is not right extension, skip to next peer */
						previous_peer = peer;
						peer = peer->peer;
					}
				}

				ast_mutex_unlock(&con->lock);
				return -1;
			}
		}

		prev_exten = exten;
		exten = exten->next;
	}

	/* we can't find right extension */
	ast_mutex_unlock(&con->lock);
	return -1;
}


int ast_register_application(const char *app, int (*execute)(struct ast_channel *, void *), const char *synopsis, const char *description)
{
	struct ast_app *tmp, *prev, *cur;
	char tmps[80];
	int length;
	length = sizeof(struct ast_app);
	length += strlen(app) + 1;
	if (ast_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(app, tmp->name)) {
			ast_log(LOG_WARNING, "Already have an application '%s'\n", app);
			ast_mutex_unlock(&applock);
			return -1;
		}
		tmp = tmp->next;
	}
	tmp = malloc(length);
	if (tmp) {
		memset(tmp, 0, length);
		strcpy(tmp->name, app);
		tmp->execute = execute;
		tmp->synopsis = synopsis;
		tmp->description = description;
		/* Store in alphabetical order */
		cur = apps;
		prev = NULL;
		while(cur) {
			if (strcasecmp(tmp->name, cur->name) < 0)
				break;
			prev = cur;
			cur = cur->next;
		}
		if (prev) {
			tmp->next = prev->next;
			prev->next = tmp;
		} else {
			tmp->next = apps;
			apps = tmp;
		}
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		ast_mutex_unlock(&applock);
		return -1;
	}
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered application '%s'\n", term_color(tmps, tmp->name, COLOR_BRCYAN, 0, sizeof(tmps)));
	ast_mutex_unlock(&applock);
	return 0;
}

int ast_register_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp, *prev=NULL;
	if (ast_mutex_lock(&switchlock)) {
		ast_log(LOG_ERROR, "Unable to lock switch lock\n");
		return -1;
	}
	tmp = switches;
	while(tmp) {
		if (!strcasecmp(tmp->name, sw->name))
			break;
		prev = tmp;
		tmp = tmp->next;
	}
	if (tmp) {	
		ast_mutex_unlock(&switchlock);
		ast_log(LOG_WARNING, "Switch '%s' already found\n", sw->name);
		return -1;
	}
	sw->next = NULL;
	if (prev) 
		prev->next = sw;
	else
		switches = sw;
	ast_mutex_unlock(&switchlock);
	return 0;
}

void ast_unregister_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp, *prev=NULL;
	if (ast_mutex_lock(&switchlock)) {
		ast_log(LOG_ERROR, "Unable to lock switch lock\n");
		return;
	}
	tmp = switches;
	while(tmp) {
		if (tmp == sw) {
			if (prev)
				prev->next = tmp->next;
			else
				switches = tmp->next;
			tmp->next = NULL;
			break;			
		}
		prev = tmp;
		tmp = tmp->next;
	}
	ast_mutex_unlock(&switchlock);
}

/*
 * Help for CLI commands ...
 */
static char show_application_help[] = 
"Usage: show application <application> [<application> [<application> [...]]]\n"
"       Describes a particular application.\n";

static char show_applications_help[] =
"Usage: show applications [{like|describing} <text>]\n"
"       List applications which are currently available.\n"
"       If 'like', <text> will be a substring of the app name\n"
"       If 'describing', <text> will be a substring of the description\n";

static char show_dialplan_help[] =
"Usage: show dialplan [exten@][context]\n"
"       Show dialplan\n";

static char show_switches_help[] = 
"Usage: show switches\n"
"       Show registered switches\n";

/*
 * IMPLEMENTATION OF CLI FUNCTIONS IS IN THE SAME ORDER AS COMMANDS HELPS
 *
 */

/*
 * 'show application' CLI command implementation functions ...
 */

/*
 * There is a possibility to show informations about more than one
 * application at one time. You can type 'show application Dial Echo' and
 * you will see informations about these two applications ...
 */
static char *complete_show_application(char *line, char *word,
	int pos, int state)
{
	struct ast_app *a;
	int which = 0;

	/* try to lock applications list ... */
	if (ast_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return NULL;
	}

	/* ... walk all applications ... */
	a = apps; 
	while (a) {
		/* ... check if word matches this application ... */
		if (!strncasecmp(word, a->name, strlen(word))) {
			/* ... if this is right app serve it ... */
			if (++which > state) {
				char *ret = strdup(a->name);
				ast_mutex_unlock(&applock);
				return ret;
			}
		}
		a = a->next; 
	}

	/* no application match */
	ast_mutex_unlock(&applock);
	return NULL; 
}

static int handle_show_application(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	int app, no_registered_app = 1;

	if (argc < 3) return RESULT_SHOWUSAGE;

	/* try to lock applications list ... */
	if (ast_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}

	/* ... go through all applications ... */
	a = apps; 
	while (a) {
		/* ... compare this application name with all arguments given
		 * to 'show application' command ... */
		for (app = 2; app < argc; app++) {
			if (!strcasecmp(a->name, argv[app])) {
				/* Maximum number of characters added by terminal coloring is 22 */
				char infotitle[64 + AST_MAX_APP + 22], syntitle[40], destitle[40];
				char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL;
				int synopsis_size, description_size;

				no_registered_app = 0;

				if (a->synopsis)
					synopsis_size = strlen(a->synopsis) + 23;
				else
					synopsis_size = strlen("Not available") + 23;
				synopsis = alloca(synopsis_size);

				if (a->description)
					description_size = strlen(a->description) + 23;
				else
					description_size = strlen("Not available") + 23;
				description = alloca(description_size);

				if (synopsis && description) {
					snprintf(info, 64 + AST_MAX_APP, "\n  -= Info about application '%s' =- \n\n", a->name);
					term_color(infotitle, info, COLOR_MAGENTA, 0, 64 + AST_MAX_APP + 22);
					term_color(syntitle, "[Synopsis]:\n", COLOR_MAGENTA, 0, 40);
					term_color(destitle, "[Description]:\n", COLOR_MAGENTA, 0, 40);
					term_color(synopsis,
									a->synopsis ? a->synopsis : "Not available",
									COLOR_CYAN, 0, synopsis_size);
					term_color(description,
									a->description ? a->description : "Not available",
									COLOR_CYAN, 0, description_size);

					ast_cli(fd,"%s%s%s\n\n%s%s\n", infotitle, syntitle, synopsis, destitle, description);
				} else {
					/* ... one of our applications, show info ...*/
					ast_cli(fd,"\n  -= Info about application '%s' =- \n\n"
						"[Synopsis]:\n  %s\n\n"
						"[Description]:\n%s\n",
						a->name,
						a->synopsis ? a->synopsis : "Not available",
						a->description ? a->description : "Not available");
				}
			}
		}
		a = a->next; 
	}

	ast_mutex_unlock(&applock);

	/* we found at least one app? no? */
	if (no_registered_app) {
		ast_cli(fd, "Your application(s) is (are) not registered\n");
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int handle_show_switches(int fd, int argc, char *argv[])
{
	struct ast_switch *sw;
	if (!switches) {
		ast_cli(fd, "There are no registered alternative switches\n");
		return RESULT_SUCCESS;
	}
	/* ... we have applications ... */
	ast_cli(fd, "\n    -= Registered Asterisk Alternative Switches =-\n");
	if (ast_mutex_lock(&switchlock)) {
		ast_log(LOG_ERROR, "Unable to lock switches\n");
		return -1;
	}
	sw = switches;
	while (sw) {
		ast_cli(fd, "%s: %s\n", sw->name, sw->description);
		sw = sw->next;
	}
	ast_mutex_unlock(&switchlock);
	return RESULT_SUCCESS;
}

/*
 * 'show applications' CLI command implementation functions ...
 */
static int handle_show_applications(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	int like=0, describing=0;

	/* try to lock applications list ... */
	if (ast_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}

	/* ... have we got at least one application (first)? no? */
	if (!apps) {
		ast_cli(fd, "There are no registered applications\n");
		ast_mutex_unlock(&applock);
		return -1;
	}

	/* show applications like <keyword> */
	if ((argc == 4) && (!strcmp(argv[2], "like"))) {
		like = 1;
	} else if ((argc > 3) && (!strcmp(argv[2], "describing"))) {
		describing = 1;
	}

	/* show applications describing <keyword1> [<keyword2>] [...] */
	if ((!like) && (!describing)) {
		ast_cli(fd, "    -= Registered Asterisk Applications =-\n");
	} else {
		ast_cli(fd, "    -= Matching Asterisk Applications =-\n");
	}

	/* ... go through all applications ... */
	for (a = apps; a; a = a->next) {
		/* ... show informations about applications ... */
		int printapp=0;

		if (like) {
			if (ast_strcasestr(a->name, argv[3])) {
				printapp = 1;
			}
		} else if (describing) {
			if (a->description) {
				/* Match all words on command line */
				int i;
				printapp = 1;
				for (i=3;i<argc;i++) {
					if (! ast_strcasestr(a->description, argv[i])) {
						printapp = 0;
					}
				}
			}
		} else {
			printapp = 1;
		}

		if (printapp) {
			ast_cli(fd,"  %20s: %s\n", a->name, a->synopsis ? a->synopsis : "<Synopsis not available>");
		}
	}

	/* ... unlock and return */
	ast_mutex_unlock(&applock);

	return RESULT_SUCCESS;
}

static char *complete_show_applications(char *line, char *word, int pos, int state)
{
	if (pos == 2) {
		if (ast_strlen_zero(word)) {
			switch (state) {
			case 0:
				return strdup("like");
			case 1:
				return strdup("describing");
			default:
				return NULL;
			}
		} else if (! strncasecmp(word, "like", strlen(word))) {
			if (state == 0) {
				return strdup("like");
			} else {
				return NULL;
			}
		} else if (! strncasecmp(word, "describing", strlen(word))) {
			if (state == 0) {
				return strdup("describing");
			} else {
				return NULL;
			}
		}
	}
	return NULL;
}

/*
 * 'show dialplan' CLI command implementation functions ...
 */
static char *complete_show_dialplan_context(char *line, char *word, int pos,
	int state)
{
	struct ast_context *c;
	int which = 0;

	/* we are do completion of [exten@]context on second position only */
	if (pos != 2) return NULL;

	/* try to lock contexts list ... */
	if (ast_lock_contexts()) {
		ast_log(LOG_ERROR, "Unable to lock context list\n");
		return NULL;
	}

	/* ... walk through all contexts ... */
	c = ast_walk_contexts(NULL);
	while(c) {
		/* ... word matches context name? yes? ... */
		if (!strncasecmp(word, ast_get_context_name(c), strlen(word))) {
			/* ... for serve? ... */
			if (++which > state) {
				/* ... yes, serve this context name ... */
				char *ret = strdup(ast_get_context_name(c));
				ast_unlock_contexts();
				return ret;
			}
		}
		c = ast_walk_contexts(c);
	}

	/* ... unlock and return */
	ast_unlock_contexts();
	return NULL;
}

static int handle_show_dialplan(int fd, int argc, char *argv[])
{
	struct ast_context *c;
	char *exten = NULL, *context = NULL;
	int context_existence = 0, extension_existence = 0;

	if (argc != 3 && argc != 2) return -1;

	/* we obtain [exten@]context? if yes, split them ... */
	if (argc == 3) {
		char *splitter = argv[2];
		/* is there a '@' character? */
		if (strchr(argv[2], '@')) {
			/* yes, split into exten & context ... */
			exten   = strsep(&splitter, "@");
			context = splitter;

			/* check for length and change to NULL if ast_strlen_zero() */
			if (ast_strlen_zero(exten))   exten = NULL;
			if (ast_strlen_zero(context)) context = NULL;
		} else
		{
			/* no '@' char, only context given */
			context = argv[2];
			if (ast_strlen_zero(context)) context = NULL;
		}
	}

	/* try to lock contexts */
	if (ast_lock_contexts()) {
		ast_log(LOG_WARNING, "Failed to lock contexts list\n");
		return RESULT_FAILURE;
	}

	/* walk all contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* show this context? */
		if (!context ||
			!strcmp(ast_get_context_name(c), context)) {
			context_existence = 1;

			/* try to lock context before walking in ... */
			if (!ast_lock_context(c)) {
				struct ast_exten *e;
				struct ast_include *i;
				struct ast_ignorepat *ip;
				struct ast_sw *sw;
				char buf[256], buf2[256];
				int context_info_printed = 0;

				/* are we looking for exten too? if yes, we print context
				 * if we our extension only
				 */
				if (!exten) {
					ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
					context_info_printed = 1;
				}

				/* walk extensions ... */
				e = ast_walk_context_extensions(c, NULL);
				while (e) {
					struct ast_exten *p;

					/* looking for extension? is this our extension? */
					if (exten &&
						strcmp(ast_get_extension_name(e), exten))
					{
						/* we are looking for extension and it's not our
 						 * extension, so skip to next extension */
						e = ast_walk_context_extensions(c, e);
						continue;
					}

					extension_existence = 1;

					/* may we print context info? */	
					if (!context_info_printed) {
						ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
							ast_get_context_name(c),
							ast_get_context_registrar(c));
						context_info_printed = 1;
					}

					/* write extension name and first peer */	
					bzero(buf, sizeof(buf));		
					snprintf(buf, sizeof(buf), "'%s' =>",
						ast_get_extension_name(e));

					snprintf(buf2, sizeof(buf2),
						"%d. %s(%s)",
						ast_get_extension_priority(e),
						ast_get_extension_app(e),
						(char *)ast_get_extension_app_data(e));

					ast_cli(fd, "  %-17s %-45s [%s]\n", buf, buf2,
						ast_get_extension_registrar(e));

					/* walk next extension peers */
					p = ast_walk_extension_priorities(e, e);
					while (p) {
						bzero((void *)buf2, sizeof(buf2));
						bzero((void *)buf, sizeof(buf));
						if (ast_get_extension_label(p))
							snprintf(buf, sizeof(buf), "   [%s]", ast_get_extension_label(p));
						snprintf(buf2, sizeof(buf2),
							"%d. %s(%s)",
							ast_get_extension_priority(p),
							ast_get_extension_app(p),
							(char *)ast_get_extension_app_data(p));

						ast_cli(fd,"  %-17s %-45s [%s]\n",
							buf, buf2,
							ast_get_extension_registrar(p));	

						p = ast_walk_extension_priorities(e, p);
					}
					e = ast_walk_context_extensions(c, e);
				}

				/* include & ignorepat we all printing if we are not
				 * looking for exact extension
				 */
				if (!exten) {
					if (ast_walk_context_extensions(c, NULL))
						ast_cli(fd, "\n");

					/* walk included and write info ... */
					i = ast_walk_context_includes(c, NULL);
					while (i) {
						bzero(buf, sizeof(buf));
						snprintf(buf, sizeof(buf), "'%s'",
							ast_get_include_name(i));
						ast_cli(fd, "  Include =>        %-45s [%s]\n",
							buf, ast_get_include_registrar(i));
						i = ast_walk_context_includes(c, i);
					}

					/* walk ignore patterns and write info ... */
					ip = ast_walk_context_ignorepats(c, NULL);
					while (ip) {
						bzero(buf, sizeof(buf));
						snprintf(buf, sizeof(buf), "'%s'",
							ast_get_ignorepat_name(ip));
						ast_cli(fd, "  Ignore pattern => %-45s [%s]\n",
							buf, ast_get_ignorepat_registrar(ip));	
						ip = ast_walk_context_ignorepats(c, ip);
					}
					sw = ast_walk_context_switches(c, NULL);
					while(sw) {
						bzero(buf, sizeof(buf));
						snprintf(buf, sizeof(buf), "'%s/%s'",
							ast_get_switch_name(sw),
							ast_get_switch_data(sw));
						ast_cli(fd, "  Alt. Switch =>    %-45s [%s]\n",
							buf, ast_get_switch_registrar(sw));	
						sw = ast_walk_context_switches(c, sw);
					}
				}
	
				ast_unlock_context(c);

				/* if we print something in context, make an empty line */
				if (context_info_printed) ast_cli(fd, "\n");
			}
		}
		c = ast_walk_contexts(c);
	}
	ast_unlock_contexts();

	/* check for input failure and throw some error messages */
	if (context && !context_existence) {
		ast_cli(fd, "There is no existence of '%s' context\n",
			context);
		return RESULT_FAILURE;
	}

	if (exten && !extension_existence) {
		if (context)
			ast_cli(fd, "There is no existence of %s@%s extension\n",
				exten, context);
		else
			ast_cli(fd,
				"There is no existence of '%s' extension in all contexts\n",
				exten);
		return RESULT_FAILURE;
	}

	/* everything ok */
	return RESULT_SUCCESS;
}

/*
 * CLI entries for upper commands ...
 */
static struct ast_cli_entry show_applications_cli = 
	{ { "show", "applications", NULL }, 
	handle_show_applications, "Shows registered applications",
	show_applications_help, complete_show_applications };

static struct ast_cli_entry show_application_cli =
	{ { "show", "application", NULL }, 
	handle_show_application, "Describe a specific application",
	show_application_help, complete_show_application };

static struct ast_cli_entry show_dialplan_cli =
	{ { "show", "dialplan", NULL },
		handle_show_dialplan, "Show dialplan",
		show_dialplan_help, complete_show_dialplan_context };

static struct ast_cli_entry show_switches_cli =
	{ { "show", "switches", NULL },
		handle_show_switches, "Show alternative switches",
		show_switches_help, NULL };

int ast_unregister_application(const char *app) {
	struct ast_app *tmp, *tmpl = NULL;
	if (ast_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(app, tmp->name)) {
			if (tmpl)
				tmpl->next = tmp->next;
			else
				apps = tmp->next;
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Unregistered application '%s'\n", tmp->name);
			free(tmp);
			ast_mutex_unlock(&applock);
			return 0;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	ast_mutex_unlock(&applock);
	return -1;
}

struct ast_context *ast_context_create(struct ast_context **extcontexts, const char *name, const char *registrar)
{
	struct ast_context *tmp, **local_contexts;
	int length;
	length = sizeof(struct ast_context);
	length += strlen(name) + 1;
	if (!extcontexts) {
		local_contexts = &contexts;
		ast_mutex_lock(&conlock);
	} else
		local_contexts = extcontexts;

	tmp = *local_contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, name)) {
			ast_mutex_unlock(&conlock);
			ast_log(LOG_WARNING, "Tried to register context '%s', already in use\n", name);
			if (!extcontexts)
				ast_mutex_unlock(&conlock);
			return NULL;
		}
		tmp = tmp->next;
	}
	tmp = malloc(length);
	if (tmp) {
		memset(tmp, 0, length);
		ast_mutex_init(&tmp->lock);
		strcpy(tmp->name, name);
		tmp->root = NULL;
		tmp->registrar = registrar;
		tmp->next = *local_contexts;
		tmp->includes = NULL;
		tmp->ignorepats = NULL;
		*local_contexts = tmp;
		if (option_debug)
			ast_log(LOG_DEBUG, "Registered context '%s'\n", tmp->name);
		else if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Registered extension context '%s'\n", tmp->name);
	} else
		ast_log(LOG_ERROR, "Out of memory\n");
	
	if (!extcontexts)
		ast_mutex_unlock(&conlock);
	return tmp;
}

void __ast_context_destroy(struct ast_context *con, const char *registrar);

void ast_merge_contexts_and_delete(struct ast_context **extcontexts, const char *registrar) {
	struct ast_context *tmp, *lasttmp = NULL;
	tmp = *extcontexts;
	ast_mutex_lock(&conlock);
	if (registrar) {
		__ast_context_destroy(NULL,registrar);
		while (tmp) {
			lasttmp = tmp;
			tmp = tmp->next;
		}
	} else {
		while (tmp) {
			__ast_context_destroy(tmp,tmp->registrar);
			lasttmp = tmp;
			tmp = tmp->next;
		}
	}
	if (lasttmp) {
		lasttmp->next = contexts;
		contexts = *extcontexts;
		*extcontexts = NULL;
	} else 
		ast_log(LOG_WARNING, "Requested contexts didn't get merged\n");
	ast_mutex_unlock(&conlock);
	return;	
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int ast_context_add_include(const char *context, const char *include, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	/* walk contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* ... search for the right one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret = ast_context_add_include2(c, include, registrar);
			/* ... unlock contexts list and return */
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right context */
	ast_unlock_contexts();
	errno = ENOENT;
	return -1;
}

#define FIND_NEXT \
do { \
	c = info; \
	while(*c && (*c != '|')) c++; \
	if (*c) { *c = '\0'; c++; } else c = NULL; \
} while(0)

static void get_timerange(struct ast_timing *i, char *times)
{
	char *e;
	int x;
	int s1, s2;
	int e1, e2;
	/*	int cth, ctm; */

	/* start disabling all times, fill the fields with 0's, as they may contain garbage */
	memset(i->minmask, 0, sizeof(i->minmask));
	
	/* Star is all times */
	if (ast_strlen_zero(times) || !strcmp(times, "*")) {
		for (x=0;x<24;x++)
			i->minmask[x] = (1 << 30) - 1;
		return;
	}
	/* Otherwise expect a range */
	e = strchr(times, '-');
	if (!e) {
		ast_log(LOG_WARNING, "Time range is not valid. Assuming no restrictions based on time.\n");
		return;
	}
	*e = '\0';
	e++;
	while(*e && !isdigit(*e)) e++;
	if (!*e) {
		ast_log(LOG_WARNING, "Invalid time range.  Assuming no restrictions based on time.\n");
		return;
	}
	if (sscanf(times, "%d:%d", &s1, &s2) != 2) {
		ast_log(LOG_WARNING, "%s isn't a time.  Assuming no restrictions based on time.\n", times);
		return;
	}
	if (sscanf(e, "%d:%d", &e1, &e2) != 2) {
		ast_log(LOG_WARNING, "%s isn't a time.  Assuming no restrictions based on time.\n", e);
		return;
	}

#if 1
	s1 = s1 * 30 + s2/2;
	if ((s1 < 0) || (s1 >= 24*30)) {
		ast_log(LOG_WARNING, "%s isn't a valid start time. Assuming no time.\n", times);
		return;
	}
	e1 = e1 * 30 + e2/2;
	if ((e1 < 0) || (e1 >= 24*30)) {
		ast_log(LOG_WARNING, "%s isn't a valid end time. Assuming no time.\n", e);
		return;
	}
	/* Go through the time and enable each appropriate bit */
	for (x=s1;x != e1;x = (x + 1) % (24 * 30)) {
		i->minmask[x/30] |= (1 << (x % 30));
	}
	/* Do the last one */
	i->minmask[x/30] |= (1 << (x % 30));
#else
	for (cth=0;cth<24;cth++) {
		/* Initialize masks to blank */
		i->minmask[cth] = 0;
		for (ctm=0;ctm<30;ctm++) {
			if (
			/* First hour with more than one hour */
			      (((cth == s1) && (ctm >= s2)) &&
			       ((cth < e1)))
			/* Only one hour */
			||    (((cth == s1) && (ctm >= s2)) &&
			       ((cth == e1) && (ctm <= e2)))
			/* In between first and last hours (more than 2 hours) */
			||    ((cth > s1) &&
			       (cth < e1))
			/* Last hour with more than one hour */
			||    ((cth > s1) &&
			       ((cth == e1) && (ctm <= e2)))
			)
				i->minmask[cth] |= (1 << (ctm / 2));
		}
	}
#endif
	/* All done */
	return;
}

static char *days[] =
{
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",
};

static unsigned int get_dow(char *dow)
{
	char *c;
	/* The following line is coincidence, really! */
	int s, e, x;
	unsigned int mask;

	/* Check for all days */
	if (ast_strlen_zero(dow) || !strcmp(dow, "*"))
		return (1 << 7) - 1;
	/* Get start and ending days */
	c = strchr(dow, '-');
	if (c) {
		*c = '\0';
		c++;
	} else
		c = NULL;
	/* Find the start */
	s = 0;
	while((s < 7) && strcasecmp(dow, days[s])) s++;
	if (s >= 7) {
		ast_log(LOG_WARNING, "Invalid day '%s', assuming none\n", dow);
		return 0;
	}
	if (c) {
		e = 0;
		while((e < 7) && strcasecmp(c, days[e])) e++;
		if (e >= 7) {
			ast_log(LOG_WARNING, "Invalid day '%s', assuming none\n", c);
			return 0;
		}
	} else
		e = s;
	mask = 0;
	for (x=s; x != e; x = (x + 1) % 7) {
		mask |= (1 << x);
	}
	/* One last one */
	mask |= (1 << x);
	return mask;
}

static unsigned int get_day(char *day)
{
	char *c;
	/* The following line is coincidence, really! */
	int s, e, x;
	unsigned int mask;

	/* Check for all days */
	if (ast_strlen_zero(day) || !strcmp(day, "*")) {
		mask = (1 << 30)  + ((1 << 30) - 1);
		return mask;
	}
	/* Get start and ending days */
	c = strchr(day, '-');
	if (c) {
		*c = '\0';
		c++;
	}
	/* Find the start */
	if (sscanf(day, "%d", &s) != 1) {
		ast_log(LOG_WARNING, "Invalid day '%s', assuming none\n", day);
		return 0;
	}
	if ((s < 1) || (s > 31)) {
		ast_log(LOG_WARNING, "Invalid day '%s', assuming none\n", day);
		return 0;
	}
	s--;
	if (c) {
		if (sscanf(c, "%d", &e) != 1) {
			ast_log(LOG_WARNING, "Invalid day '%s', assuming none\n", c);
			return 0;
		}
		if ((e < 1) || (e > 31)) {
			ast_log(LOG_WARNING, "Invalid day '%s', assuming none\n", c);
			return 0;
		}
		e--;
	} else
		e = s;
	mask = 0;
	for (x=s;x!=e;x = (x + 1) % 31) {
		mask |= (1 << x);
	}
	mask |= (1 << x);
	return mask;
}

static char *months[] =
{
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",
};

static unsigned int get_month(char *mon)
{
	char *c;
	/* The following line is coincidence, really! */
	int s, e, x;
	unsigned int mask;

	/* Check for all days */
	if (ast_strlen_zero(mon) || !strcmp(mon, "*")) 
		return (1 << 12) - 1;
	/* Get start and ending days */
	c = strchr(mon, '-');
	if (c) {
		*c = '\0';
		c++;
	}
	/* Find the start */
	s = 0;
	while((s < 12) && strcasecmp(mon, months[s])) s++;
	if (s >= 12) {
		ast_log(LOG_WARNING, "Invalid month '%s', assuming none\n", mon);
		return 0;
	}
	if (c) {
		e = 0;
		while((e < 12) && strcasecmp(mon, months[e])) e++;
		if (e >= 12) {
			ast_log(LOG_WARNING, "Invalid month '%s', assuming none\n", c);
			return 0;
		}
	} else
		e = s;
	mask = 0;
	for (x=s; x!=e; x = (x + 1) % 12) {
		mask |= (1 << x);
	}
	/* One last one */
	mask |= (1 << x);
	return mask;
}

int ast_build_timing(struct ast_timing *i, char *info_in)
{
        char info_save[256];
        char *info;
	char *c;

	/* Check for empty just in case */
	if (ast_strlen_zero(info_in))
		return 0;
	/* make a copy just in case we were passed a static string */
	strncpy(info_save, info_in, sizeof(info_save));
	info = info_save;
	/* Assume everything except time */
	i->monthmask = (1 << 12) - 1;
	i->daymask = (1 << 30) - 1 + (1 << 30);
	i->dowmask = (1 << 7) - 1;
	/* Avoid using str tok */
	FIND_NEXT;
	/* Info has the time range, start with that */
	get_timerange(i, info);
	info = c;
	if (!info)
		return 1;
	FIND_NEXT;
	/* Now check for day of week */
	i->dowmask = get_dow(info);

	info = c;
	if (!info)
		return 1;
	FIND_NEXT;
	/* Now check for the day of the month */
	i->daymask = get_day(info);
	info = c;
	if (!info)
		return 1;
	FIND_NEXT;
	/* And finally go for the month */
	i->monthmask = get_month(info);

	return 1;
}

int ast_check_timing(struct ast_timing *i)
{
	struct tm tm;
	time_t t;

	time(&t);
	localtime_r(&t,&tm);

	/* If it's not the right month, return */
	if (!(i->monthmask & (1 << tm.tm_mon))) {
		return 0;
	}

	/* If it's not that time of the month.... */
	/* Warning, tm_mday has range 1..31! */
	if (!(i->daymask & (1 << (tm.tm_mday-1))))
		return 0;

	/* If it's not the right day of the week */
	if (!(i->dowmask & (1 << tm.tm_wday)))
		return 0;

	/* Sanity check the hour just to be safe */
	if ((tm.tm_hour < 0) || (tm.tm_hour > 23)) {
		ast_log(LOG_WARNING, "Insane time...\n");
		return 0;
	}

	/* Now the tough part, we calculate if it fits
	   in the right time based on min/hour */
	if (!(i->minmask[tm.tm_hour] & (1 << (tm.tm_min / 2))))
		return 0;

	/* If we got this far, then we're good */
	return 1;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int ast_context_add_include2(struct ast_context *con, const char *value,
	const char *registrar)
{
	struct ast_include *new_include;
	char *c;
	struct ast_include *i, *il = NULL; /* include, include_last */
	int length;
	char *p;
	
	length = sizeof(struct ast_include);
	length += 2 * (strlen(value) + 1);

	/* allocate new include structure ... */
	if (!(new_include = malloc(length))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	
	/* ... fill in this structure ... */
	memset(new_include, 0, length);
	p = new_include->stuff;
	new_include->name = p;
	strcpy(new_include->name, value);
	p += strlen(value) + 1;
	new_include->rname = p;
	strcpy(new_include->rname, value);
	c = new_include->rname;
	/* Strip off timing info */
	while(*c && (*c != '|')) c++; 
	/* Process if it's there */
	if (*c) {
	        new_include->hastime = ast_build_timing(&(new_include->timing), c+1);
		*c = '\0';
	}
	new_include->next      = NULL;
	new_include->registrar = registrar;

	/* ... try to lock this context ... */
	if (ast_mutex_lock(&con->lock)) {
		free(new_include);
		errno = EBUSY;
		return -1;
	}

	/* ... go to last include and check if context is already included too... */
	i = con->includes;
	while (i) {
		if (!strcasecmp(i->name, new_include->name)) {
			free(new_include);
			ast_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		il = i;
		i = i->next;
	}

	/* ... include new context into context list, unlock, return */
	if (il)
		il->next = new_include;
	else
		con->includes = new_include;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Including context '%s' in context '%s'\n", new_include->name, ast_get_context_name(con)); 
	ast_mutex_unlock(&con->lock);

	return 0;
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int ast_context_add_switch(const char *context, const char *sw, const char *data, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	/* walk contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* ... search for the right one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret = ast_context_add_switch2(c, sw, data, registrar);
			/* ... unlock contexts list and return */
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right context */
	ast_unlock_contexts();
	errno = ENOENT;
	return -1;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int ast_context_add_switch2(struct ast_context *con, const char *value,
	const char *data, const char *registrar)
{
	struct ast_sw *new_sw;
	struct ast_sw *i, *il = NULL; /* sw, sw_last */
	int length;
	char *p;
	
	length = sizeof(struct ast_sw);
	length += strlen(value) + 1;
	if (data)
		length += strlen(data);
	length++;

	/* allocate new sw structure ... */
	if (!(new_sw = malloc(length))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	
	/* ... fill in this structure ... */
	memset(new_sw, 0, length);
	p = new_sw->stuff;
	new_sw->name = p;
	strcpy(new_sw->name, value);
	p += strlen(value) + 1;
	new_sw->data = p;
	if (data)
		strcpy(new_sw->data, data);
	else
		strcpy(new_sw->data, "");
	new_sw->next      = NULL;
	new_sw->registrar = registrar;

	/* ... try to lock this context ... */
	if (ast_mutex_lock(&con->lock)) {
		free(new_sw);
		errno = EBUSY;
		return -1;
	}

	/* ... go to last sw and check if context is already swd too... */
	i = con->alts;
	while (i) {
		if (!strcasecmp(i->name, new_sw->name) && !strcasecmp(i->data, new_sw->data)) {
			free(new_sw);
			ast_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		il = i;
		i = i->next;
	}

	/* ... sw new context into context list, unlock, return */
	if (il)
		il->next = new_sw;
	else
		con->alts = new_sw;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Including switch '%s/%s' in context '%s'\n", new_sw->name, new_sw->data, ast_get_context_name(con)); 
	ast_mutex_unlock(&con->lock);

	return 0;
}

/*
 * EBUSY  - can't lock
 * ENOENT - there is not context existence
 */
int ast_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	c = ast_walk_contexts(NULL);
	while (c) {
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret = ast_context_remove_ignorepat2(c, ignorepat, registrar);
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	ast_unlock_contexts();
	errno = ENOENT;
	return -1;
}

int ast_context_remove_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar)
{
	struct ast_ignorepat *ip, *ipl = NULL;

	if (ast_mutex_lock(&con->lock)) {
		errno = EBUSY;
		return -1;
	}

	ip = con->ignorepats;
	while (ip) {
		if (!strcmp(ip->pattern, ignorepat) &&
			(!registrar || (registrar == ip->registrar))) {
			if (ipl) {
				ipl->next = ip->next;
				free(ip);
			} else {
				con->ignorepats = ip->next;
				free(ip);
			}
			ast_mutex_unlock(&con->lock);
			return 0;
		}
		ipl = ip; ip = ip->next;
	}

	ast_mutex_unlock(&con->lock);
	errno = EINVAL;
	return -1;
}

/*
 * EBUSY - can't lock
 * ENOENT - there is no existence of context
 */
int ast_context_add_ignorepat(const char *con, const char *value, const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	c = ast_walk_contexts(NULL);
	while (c) {
		if (!strcmp(ast_get_context_name(c), con)) {
			int ret = ast_context_add_ignorepat2(c, value, registrar);
			ast_unlock_contexts();
			return ret;
		} 
		c = ast_walk_contexts(c);
	}

	ast_unlock_contexts();
	errno = ENOENT;
	return -1;
}

int ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	struct ast_ignorepat *ignorepat, *ignorepatc, *ignorepatl = NULL;
	int length;
	length = sizeof(struct ast_ignorepat);
	length += strlen(value) + 1;
	ignorepat = malloc(length);
	if (!ignorepat) {
		ast_log(LOG_ERROR, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	memset(ignorepat, 0, length);
	strcpy(ignorepat->pattern, value);
	ignorepat->next = NULL;
	ignorepat->registrar = registrar;
	ast_mutex_lock(&con->lock);
	ignorepatc = con->ignorepats;
	while(ignorepatc) {
		ignorepatl = ignorepatc;
		if (!strcasecmp(ignorepatc->pattern, value)) {
			/* Already there */
			ast_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		ignorepatc = ignorepatc->next;
	}
	if (ignorepatl) 
		ignorepatl->next = ignorepat;
	else
		con->ignorepats = ignorepat;
	ast_mutex_unlock(&con->lock);
	return 0;
	
}

int ast_ignore_pattern(const char *context, const char *pattern)
{
	struct ast_context *con;
	struct ast_ignorepat *pat;

	con = ast_context_find(context);
	if (con) {
		pat = con->ignorepats;
		while (pat) {
			if (ast_extension_match(pat->pattern, pattern))
				return 1;
			pat = pat->next;
		}
	} 
	return 0;
}

/*
 * EBUSY   - can't lock
 * ENOENT  - no existence of context
 *
 */
int ast_add_extension(const char *context, int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	c = ast_walk_contexts(NULL);
	while (c) {
		if (!strcmp(context, ast_get_context_name(c))) {
			int ret = ast_add_extension2(c, replace, extension, priority, label, callerid,
				application, data, datad, registrar);
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	ast_unlock_contexts();
	errno = ENOENT;
	return -1;
}

int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	int res = 0;
	ast_mutex_lock(&chan->lock);

	if (chan->pbx) {
		/* This channel is currently in the PBX */
		if (context && !ast_strlen_zero(context))
			strncpy(chan->context, context, sizeof(chan->context) - 1);
		if (exten && !ast_strlen_zero(exten))
			strncpy(chan->exten, exten, sizeof(chan->context) - 1);
		if (priority)
			chan->priority = priority - 1;
		ast_softhangup_nolock(chan, AST_SOFTHANGUP_ASYNCGOTO);
	} else {
		/* In order to do it when the channel doesn't really exist within
		   the PBX, we have to make a new channel, masquerade, and start the PBX
		   at the new location */
		struct ast_channel *tmpchan;
		tmpchan = ast_channel_alloc(0);
		if (tmpchan) {
			snprintf(tmpchan->name, sizeof(tmpchan->name), "AsyncGoto/%s", chan->name);
			ast_setstate(tmpchan, chan->_state);
			/* Make formats okay */
			tmpchan->readformat = chan->readformat;
			tmpchan->writeformat = chan->writeformat;
			/* Setup proper location */
			if (context && !ast_strlen_zero(context))
				strncpy(tmpchan->context, context, sizeof(tmpchan->context) - 1);
			else
				strncpy(tmpchan->context, chan->context, sizeof(tmpchan->context) - 1);
			if (exten && !ast_strlen_zero(exten))
				strncpy(tmpchan->exten, exten, sizeof(tmpchan->exten) - 1);
			else
				strncpy(tmpchan->exten, chan->exten, sizeof(tmpchan->exten) - 1);
			if (priority)
				tmpchan->priority = priority;
			else
				tmpchan->priority = chan->priority;
			
			/* Masquerade into temp channel */
			ast_channel_masquerade(tmpchan, chan);
		
			/* Grab the locks and get going */
			ast_mutex_lock(&tmpchan->lock);
			ast_do_masquerade(tmpchan);
			ast_mutex_unlock(&tmpchan->lock);
			/* Start the PBX going on our stolen channel */
			if (ast_pbx_start(tmpchan)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmpchan->name);
				ast_hangup(tmpchan);
				res = -1;
			}
		} else {
			res = -1;
		}
	}
	ast_mutex_unlock(&chan->lock);
	return res;
}

int ast_async_goto_by_name(const char *channame, const char *context, const char *exten, int priority)
{
	struct ast_channel *chan;
	int res = -1;

	chan = ast_channel_walk_locked(NULL);
	while(chan) {
		if (!strcasecmp(channame, chan->name))
			break;
		ast_mutex_unlock(&chan->lock);
		chan = ast_channel_walk_locked(chan);
	}
	
	if (chan) {
		res = ast_async_goto(chan, context, exten, priority);
		ast_mutex_unlock(&chan->lock);
	}
	return res;
}

static void ext_strncpy(char *dst, const char *src, int len)
{
	int count=0;

	while(*src && (count < len - 1)) {
		switch(*src) {
		case ' ':
			/*	otherwise exten => [a-b],1,... doesn't work */
			/*		case '-': */
			/* Ignore */
			break;
		default:
			*dst = *src;
			dst++;
		}
		src++;
		count++;
	}
	*dst = '\0';
}

/*
 * EBUSY - can't lock
 * EEXIST - extension with the same priority exist and no replace is set
 *
 */
int ast_add_extension2(struct ast_context *con,
					  int replace, const char *extension, int priority, const char *label, const char *callerid,
					  const char *application, void *data, void (*datad)(void *),
					  const char *registrar)
{

#define LOG do { 	if (option_debug) {\
		if (tmp->matchcid) { \
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d (CID match '%s') to %s\n", tmp->exten, tmp->priority, tmp->cidmatch, con->name); \
		} else { \
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
		} \
	} else if (option_verbose > 2) { \
		if (tmp->matchcid) { \
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d (CID match '%s')to %s\n", tmp->exten, tmp->priority, tmp->cidmatch, con->name); \
		} else {  \
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
		} \
	} } while(0)

	/*
	 * This is a fairly complex routine.  Different extensions are kept
	 * in order by the extension number.  Then, extensions of different
	 * priorities (same extension) are kept in a list, according to the
	 * peer pointer.
	 */
	struct ast_exten *tmp, *e, *el = NULL, *ep = NULL;
	int res;
	int length;
	char *p;
	length = sizeof(struct ast_exten);
	length += strlen(extension) + 1;
	length += strlen(application) + 1;
	if (label)
		length += strlen(label) + 1;
	if (callerid)
		length += strlen(callerid) + 1;
	else
		length ++;

	/* Be optimistic:  Build the extension structure first */
	tmp = malloc(length);
	if (tmp) {
		memset(tmp, 0, length);
		p = tmp->stuff;
		if (label) {
			tmp->label = p;
			strcpy(tmp->label, label);
			p += strlen(label) + 1;
		}
		tmp->exten = p;
		ext_strncpy(tmp->exten, extension, strlen(extension) + 1);
		p += strlen(extension) + 1;
		tmp->priority = priority;
		tmp->cidmatch = p;
		if (callerid) {
			ext_strncpy(tmp->cidmatch, callerid, strlen(callerid) + 1);
			p += strlen(callerid) + 1;
			tmp->matchcid = 1;
		} else {
			tmp->cidmatch[0] = '\0';
			tmp->matchcid = 0;
			p++;
		}
		tmp->app = p;
		strcpy(tmp->app, application);
		tmp->parent = con;
		tmp->data = data;
		tmp->datad = datad;
		tmp->registrar = registrar;
		tmp->peer = NULL;
		tmp->next =  NULL;
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	if (ast_mutex_lock(&con->lock)) {
		free(tmp);
		/* And properly destroy the data */
		datad(data);
		ast_log(LOG_WARNING, "Failed to lock context '%s'\n", con->name);
		errno = EBUSY;
		return -1;
	}
	e = con->root;
	while(e) {
		/* Make sure patterns are always last! */
		if ((e->exten[0] != '_') && (extension[0] == '_'))
			res = -1;
		else if ((e->exten[0] == '_') && (extension[0] != '_'))
			res = 1;
		else
			res= strcmp(e->exten, extension);
		if (!res) {
			if (!e->matchcid && !tmp->matchcid)
				res = 0;
			else if (tmp->matchcid && !e->matchcid)
				res = 1;
			else if (e->matchcid && !tmp->matchcid)
				res = -1;
			else
				res = strcasecmp(e->cidmatch, tmp->cidmatch);
		}
		if (res == 0) {
			/* We have an exact match, now we find where we are
			   and be sure there's no duplicates */
			while(e) {
				if (e->priority == tmp->priority) {
					/* Can't have something exactly the same.  Is this a
					   replacement?  If so, replace, otherwise, bonk. */
					if (replace) {
						if (ep) {
							/* We're in the peer list, insert ourselves */
							ep->peer = tmp;
							tmp->peer = e->peer;
						} else if (el) {
							/* We're the first extension. Take over e's functions */
							el->next = tmp;
							tmp->next = e->next;
							tmp->peer = e->peer;
						} else {
							/* We're the very first extension.  */
							con->root = tmp;
							tmp->next = e->next;
							tmp->peer = e->peer;
						}
						if (tmp->priority == PRIORITY_HINT)
						    ast_change_hint(e,tmp);
						/* Destroy the old one */
						e->datad(e->data);
						free(e);
						ast_mutex_unlock(&con->lock);
						if (tmp->priority == PRIORITY_HINT)
						    ast_change_hint(e, tmp);
						/* And immediately return success. */
						LOG;
						return 0;
					} else {
						ast_log(LOG_WARNING, "Unable to register extension '%s', priority %d in '%s', already in use\n", tmp->exten, tmp->priority, con->name);
						tmp->datad(tmp->data);
						free(tmp);
						ast_mutex_unlock(&con->lock);
						errno = EEXIST;
						return -1;
					}
				} else if (e->priority > tmp->priority) {
					/* Slip ourselves in just before e */
					if (ep) {
						/* Easy enough, we're just in the peer list */
						ep->peer = tmp;
						tmp->peer = e;
					} else if (el) {
						/* We're the first extension in this peer list */
						el->next = tmp;
						tmp->next = e->next;
						e->next = NULL;
						tmp->peer = e;
					} else {
						/* We're the very first extension altogether */
						tmp->next = con->root->next;
						/* Con->root must always exist or we couldn't get here */
						tmp->peer = con->root;
						con->root = tmp;
					}
					ast_mutex_unlock(&con->lock);
					/* And immediately return success. */
					if (tmp->priority == PRIORITY_HINT)
					    ast_add_hint(tmp);
					
					LOG;
					return 0;
				}
				ep = e;
				e = e->peer;
			}
			/* If we make it here, then it's time for us to go at the very end.
			   ep *must* be defined or we couldn't have gotten here. */
			ep->peer = tmp;
			ast_mutex_unlock(&con->lock);
			if (tmp->priority == PRIORITY_HINT)
				ast_add_hint(tmp);
			
			/* And immediately return success. */
			LOG;
			return 0;
				
		} else if (res > 0) {
			/* Insert ourselves just before 'e'.  We're the first extension of
			   this kind */
			tmp->next = e;
			if (el) {
				/* We're in the list somewhere */
				el->next = tmp;
			} else {
				/* We're at the top of the list */
				con->root = tmp;
			}
			ast_mutex_unlock(&con->lock);
			if (tmp->priority == PRIORITY_HINT)
				ast_add_hint(tmp);

			/* And immediately return success. */
			LOG;
			return 0;
		}			
			
		el = e;
		e = e->next;
	}
	/* If we fall all the way through to here, then we need to be on the end. */
	if (el)
		el->next = tmp;
	else
		con->root = tmp;
	ast_mutex_unlock(&con->lock);
	if (tmp->priority == PRIORITY_HINT)
		ast_add_hint(tmp);
	LOG;
	return 0;	
}

struct async_stat {
	pthread_t p;
	struct ast_channel *chan;
	char context[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	int priority;
	int timeout;
	char app[AST_MAX_EXTENSION];
	char appdata[1024];
};

static void *async_wait(void *data) 
{
	struct async_stat *as = data;
	struct ast_channel *chan = as->chan;
	int timeout = as->timeout;
	int res;
	struct ast_frame *f;
	struct ast_app *app;
	
	while(timeout && (chan->_state != AST_STATE_UP)) {
		res = ast_waitfor(chan, timeout);
		if (res < 1) 
			break;
		if (timeout > -1)
			timeout = res;
		f = ast_read(chan);
		if (!f)
			break;
		if (f->frametype == AST_FRAME_CONTROL) {
			if ((f->subclass == AST_CONTROL_BUSY)  ||
				(f->subclass == AST_CONTROL_CONGESTION) )
					break;
		}
		ast_frfree(f);
	}
	if (chan->_state == AST_STATE_UP) {
		if (!ast_strlen_zero(as->app)) {
			app = pbx_findapp(as->app);
			if (app) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Lauching %s(%s) on %s\n", as->app, as->appdata, chan->name);
				pbx_exec(chan, app, as->appdata, 1);
			} else
				ast_log(LOG_WARNING, "No such application '%s'\n", as->app);
		} else {
			if (!ast_strlen_zero(as->context))
				strncpy(chan->context, as->context, sizeof(chan->context) - 1);
			if (!ast_strlen_zero(as->exten))
				strncpy(chan->exten, as->exten, sizeof(chan->exten) - 1);
			if (as->priority > 0)
				chan->priority = as->priority;
			/* Run the PBX */
			if (ast_pbx_run(chan)) {
				ast_log(LOG_ERROR, "Failed to start PBX on %s\n", chan->name);
			} else {
				/* PBX will have taken care of this */
				chan = NULL;
			}
		}
			
	}
	free(as);
	if (chan)
		ast_hangup(chan);
	return NULL;
}

/*! Function to update the cdr after a spool call fails.
 *
 *  This function updates the cdr for a failed spool call
 *  and takes the channel of the failed call as an argument.
 *
 * \param chan the channel for the failed call.
 */
int ast_pbx_outgoing_cdr_failed(void)
{
	/* allocate a channel */
	struct ast_channel *chan = ast_channel_alloc(0);
	if(!chan) {
		/* allocation of the channel failed, let some peeps know */
		ast_log(LOG_WARNING, "Unable to allocate channel structure for CDR record\n");
		return -1;  /* failure */
	}

	chan->cdr = ast_cdr_alloc();   /* allocate a cdr for the channel */

	if(!chan->cdr) {
		/* allocation of the cdr failed */
		ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
		ast_channel_free(chan);   /* free the channel */
		return -1;                /* return failure */
	}
	
	/* allocation of the cdr was successful */
	ast_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
	ast_cdr_start(chan->cdr);       /* record the start and stop time */
	ast_cdr_end(chan->cdr);
	ast_cdr_failed(chan->cdr);      /* set the status to failed */
	ast_cdr_post(chan->cdr);        /* post the record */
	ast_cdr_free(chan->cdr);        /* free the cdr */
	ast_channel_free(chan);         /* free the channel */
	
	return 0;  /* success */
}

int ast_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, const char *variable, const char *account)
{
	struct ast_channel *chan;
	struct async_stat *as;
	int res = -1, cdr_res = -1;
	char *var, *tmp;
	struct outgoing_helper oh;
	pthread_attr_t attr;
		
	if (sync) {
		LOAD_OH(oh);
		chan = __ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
		if (chan) {
			
			if (account)
				ast_cdr_setaccount(chan, account);
			
			if(chan->cdr) { /* check if the channel already has a cdr record, if not give it one */
				ast_log(LOG_WARNING, "%s already has a call record??\n", chan->name);
			} else {
				chan->cdr = ast_cdr_alloc();   /* allocate a cdr for the channel */
				if(!chan->cdr) {
					/* allocation of the cdr failed */
					ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
					free(chan->pbx);
					return -1;  /* return failure */
				}
				/* allocation of the cdr was successful */
				ast_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
				ast_cdr_start(chan->cdr);
			}

			if (chan->_state == AST_STATE_UP) {
					res = 0;
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);

				if (sync > 1) {
					if (ast_pbx_run(chan)) {
						ast_log(LOG_ERROR, "Unable to run PBX on %s\n", chan->name);
						ast_hangup(chan);
						res = -1;
					}
				} else {
					if (ast_pbx_start(chan)) {
						ast_log(LOG_ERROR, "Unable to start PBX on %s\n", chan->name);
						ast_hangup(chan);
						res = -1;
					} 
				}
			} else {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was never answered.\n", chan->name);

				if(chan->cdr) { /* update the cdr */
					/* here we update the status of the call, which sould be busy.
					 * if that fails then we set the status to failed */
					if(ast_cdr_disposition(chan->cdr, chan->hangupcause))
						ast_cdr_failed(chan->cdr);
				}
			
				ast_hangup(chan);
			}
		}

		if(res < 0) { /* the call failed for some reason */
			if(*reason == 0) { /* if the call failed (not busy or no answer)
				            * update the cdr with the failed message */
				cdr_res = ast_pbx_outgoing_cdr_failed();
				if(cdr_res != 0)
					return cdr_res;
			}
			
			/* create a fake channel and execute the "failed" extension (if it exists) within the requested context */
			/* check if "failed" exists */
			if (ast_exists_extension(chan, context, "failed", 1, NULL)) {
				chan = ast_channel_alloc(0);
				if(chan) {
					strncpy(chan->name, "OutgoingSpoolFailed", sizeof(chan->name) - 1);
					if (context && !ast_strlen_zero(context))
						strncpy(chan->context, context, sizeof(chan->context) - 1);
					strncpy(chan->exten, "failed", sizeof(chan->exten) - 1);
					chan->priority = 1;
					if (variable) {
						tmp = ast_strdupa(variable);
						for (var = strtok_r(tmp, "|", &tmp); var; var = strtok_r(NULL, "|", &tmp)) {
							pbx_builtin_setvar( chan, var );
						}
					}
					ast_pbx_run(chan);	
				} else 
					ast_log(LOG_WARNING, "Can't allocate the channel structure, skipping execution of extension 'failed'\n");
			}
		}
	} else {
		as = malloc(sizeof(struct async_stat));
		if (!as)
			return -1;
		memset(as, 0, sizeof(struct async_stat));
		chan = ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
		if (!chan) {
			free(as);
			return -1;
		}
		if (account)
			ast_cdr_setaccount(chan, account);
		as->chan = chan;
		strncpy(as->context, context, sizeof(as->context) - 1);
		strncpy(as->exten,  exten, sizeof(as->exten) - 1);
		as->priority = priority;
		as->timeout = timeout;
		if (variable) {
			tmp = ast_strdupa(variable);
			for (var = strtok_r(tmp, "|", &tmp); var; var = strtok_r(NULL, "|", &tmp))
				pbx_builtin_setvar( chan, var );
		}
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&as->p, &attr, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			free(as);
			ast_hangup(chan);
			return -1;
		}
		res = 0;
	}
	return res;
}

struct app_tmp {
	char app[256];
	char data[256];
	struct ast_channel *chan;
	pthread_t t;
};

static void *ast_pbx_run_app(void *data)
{
	struct app_tmp *tmp = data;
	struct ast_app *app;
	app = pbx_findapp(tmp->app);
	if (app) {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4 "Lauching %s(%s) on %s\n", tmp->app, tmp->data, tmp->chan->name);
		pbx_exec(tmp->chan, app, tmp->data, 1);
	} else
		ast_log(LOG_WARNING, "No such application '%s'\n", tmp->app);
	ast_hangup(tmp->chan);
	free(tmp);
	return NULL;
}

int ast_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, const char *variable, const char *account)
{
	struct ast_channel *chan;
	struct async_stat *as;
	struct app_tmp *tmp;
	char *var, *vartmp;
	int res = -1, cdr_res = -1;
	pthread_attr_t attr;
	
	if (!app || ast_strlen_zero(app))
		return -1;
	if (sync) {
		chan = ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
		if (chan) {
			
			if (account)
				ast_cdr_setaccount(chan, account);
			
			if(chan->cdr) { /* check if the channel already has a cdr record, if not give it one */
				ast_log(LOG_WARNING, "%s already has a call record??\n", chan->name);
			} else {
				chan->cdr = ast_cdr_alloc();   /* allocate a cdr for the channel */
				if(!chan->cdr) {
					/* allocation of the cdr failed */
					ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
					free(chan->pbx);
					return -1;  /* return failure */
				}
				/* allocation of the cdr was successful */
				ast_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
				ast_cdr_start(chan->cdr);
			}
			
			if (variable) {
				vartmp = ast_strdupa(variable);
				for (var = strtok_r(vartmp, "|", &vartmp); var; var = strtok_r(NULL, "|", &vartmp)) {
					pbx_builtin_setvar( chan, var );
				}
			}
			if (chan->_state == AST_STATE_UP) {
				res = 0;
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);
				tmp = malloc(sizeof(struct app_tmp));
				if (tmp) {
					memset(tmp, 0, sizeof(struct app_tmp));
					strncpy(tmp->app, app, sizeof(tmp->app) - 1);
					strncpy(tmp->data, appdata, sizeof(tmp->data) - 1);
					tmp->chan = chan;
					if (sync > 1) {
						ast_pbx_run_app(tmp);
					} else {
						pthread_attr_init(&attr);
						pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						if (ast_pthread_create(&tmp->t, &attr, ast_pbx_run_app, tmp)) {
							ast_log(LOG_WARNING, "Unable to spawn execute thread on %s: %s\n", chan->name, strerror(errno));
							free(tmp);
							ast_hangup(chan);
							res = -1;
						}
					}
				} else {
					ast_log(LOG_ERROR, "Out of memory :(\n");
					res = -1;
				}
			} else {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was never answered.\n", chan->name);
				if(chan->cdr) { /* update the cdr */
					/* here we update the status of the call, which sould be busy.
					 * if that fails then we set the status to failed */
					if(ast_cdr_disposition(chan->cdr, chan->hangupcause))
						ast_cdr_failed(chan->cdr);
				}
				ast_hangup(chan);
			}
		}
		
		if(res < 0) { /* the call failed for some reason */
			if(*reason == 0) { /* if the call failed (not busy or no answer)
				            * update the cdr with the failed message */
				cdr_res = ast_pbx_outgoing_cdr_failed();
				if(cdr_res != 0)
					return cdr_res;
			}
		}

	} else {
		as = malloc(sizeof(struct async_stat));
		if (!as)
			return -1;
		memset(as, 0, sizeof(struct async_stat));
		chan = ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
		if (!chan) {
			free(as);
			return -1;
		}
		if (account)
			ast_cdr_setaccount(chan, account);
		as->chan = chan;
		strncpy(as->app, app, sizeof(as->app) - 1);
		if (appdata)
			strncpy(as->appdata,  appdata, sizeof(as->appdata) - 1);
		as->timeout = timeout;
		if (variable) {
			vartmp = ast_strdupa(variable);
			for (var = strtok_r(vartmp, "|", &vartmp); var; var = strtok_r(NULL, "|", &vartmp))
				pbx_builtin_setvar( chan, var );
		}
		/* Start a new thread, and get something handling this channel. */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&as->p, &attr, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			free(as);
			ast_hangup(chan);
			return -1;
		}
		res = 0;
	}
	return res;
}

static void destroy_exten(struct ast_exten *e)
{
	if (e->priority == PRIORITY_HINT)
		ast_remove_hint(e);

	if (e->datad)
		e->datad(e->data);
	free(e);
}

void __ast_context_destroy(struct ast_context *con, const char *registrar)
{
	struct ast_context *tmp, *tmpl=NULL;
	struct ast_include *tmpi, *tmpil= NULL;
	struct ast_sw *sw, *swl= NULL;
	struct ast_exten *e, *el, *en;
	struct ast_ignorepat *ipi, *ipl = NULL;

	ast_mutex_lock(&conlock);
	tmp = contexts;
	while(tmp) {
		if (((tmp->name && con && con->name && !strcasecmp(tmp->name, con->name)) || !con) &&
		    (!registrar || !strcasecmp(registrar, tmp->registrar))) {
			/* Okay, let's lock the structure to be sure nobody else
			   is searching through it. */
			if (ast_mutex_lock(&tmp->lock)) {
				ast_log(LOG_WARNING, "Unable to lock context lock\n");
				return;
			}
			if (tmpl)
				tmpl->next = tmp->next;
			else
				contexts = tmp->next;
			/* Okay, now we're safe to let it go -- in a sense, we were
			   ready to let it go as soon as we locked it. */
			ast_mutex_unlock(&tmp->lock);
			for (tmpi = tmp->includes; tmpi; ) {
				/* Free includes */
				tmpil = tmpi;
				tmpi = tmpi->next;
				free(tmpil);
			}
			for (ipi = tmp->ignorepats; ipi; ) {
				/* Free ignorepats */
				ipl = ipi;
				ipi = ipi->next;
				free(ipl);
			}
			for (sw = tmp->alts; sw; ) {
				/* Free switches */
				swl = sw;
				sw = sw->next;
				free(swl);
				swl = sw;
			}
			for (e = tmp->root; e;) {
				for (en = e->peer; en;) {
					el = en;
					en = en->peer;
					destroy_exten(el);
				}
				el = e;
				e = e->next;
				destroy_exten(el);
			}
                        ast_mutex_destroy(&tmp->lock);
			free(tmp);
			if (!con) {
				/* Might need to get another one -- restart */
				tmp = contexts;
				tmpl = NULL;
				tmpil = NULL;
				continue;
			}
			ast_mutex_unlock(&conlock);
			return;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	ast_mutex_unlock(&conlock);
}

void ast_context_destroy(struct ast_context *con, const char *registrar)
{
	__ast_context_destroy(con,registrar);
}

static void wait_for_hangup(struct ast_channel *chan, void *data)
{
	int res;
	struct ast_frame *f;
	int waittime;
	
	if (!data || !strlen(data) || (sscanf(data, "%i", &waittime) != 1) || (waittime < 0))
		waittime = -1;
	if (waittime > -1) {
		ast_safe_sleep(chan, waittime * 1000);
	} else do {
		res = ast_waitfor(chan, -1);
		if (res < 0)
			return;
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
	} while(f);
}

static int pbx_builtin_progress(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_PROGRESS);
	return 0;
}

static int pbx_builtin_ringing(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_RINGING);
	return 0;
}

static int pbx_builtin_busy(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_BUSY);		
	wait_for_hangup(chan, data);
	return -1;
}

static int pbx_builtin_congestion(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	wait_for_hangup(chan, data);
	return -1;
}

static int pbx_builtin_answer(struct ast_channel *chan, void *data)
{
	return ast_answer(chan);
}

static int pbx_builtin_setlanguage(struct ast_channel *chan, void *data)
{
	/* Copy the language as specified */
	if (data)
		strncpy(chan->language, (char *)data, sizeof(chan->language)-1);
	return 0;
}

static int pbx_builtin_resetcdr(struct ast_channel *chan, void *data)
{
	int flags = 0;
	/* Reset the CDR as specified */
	if(data) {
		if(strchr((char *)data, 'w'))
			flags |= AST_CDR_FLAG_POSTED;
		if(strchr((char *)data, 'a'))
			flags |= AST_CDR_FLAG_LOCKED;
	}

	ast_cdr_reset(chan->cdr, flags);
	return 0;
}

static int pbx_builtin_setaccount(struct ast_channel *chan, void *data)
{
	/* Copy the account code  as specified */
	if (data)
		ast_cdr_setaccount(chan, (char *)data);
	else
		ast_cdr_setaccount(chan, "");
	return 0;
}

static int pbx_builtin_setamaflags(struct ast_channel *chan, void *data)
{
	/* Copy the AMA Flags as specified */
	if (data)
		ast_cdr_setamaflags(chan, (char *)data);
	else
		ast_cdr_setamaflags(chan, "");
	return 0;
}

static int pbx_builtin_hangup(struct ast_channel *chan, void *data)
{
	/* Just return non-zero and it will hang up */
	return -1;
}

static int pbx_builtin_stripmsd(struct ast_channel *chan, void *data)
{
	char newexten[AST_MAX_EXTENSION] = "";

	if (!data || !atoi(data)) {
		ast_log(LOG_DEBUG, "Ignoring, since number of digits to strip is 0\n");
		return 0;
	}
	if (strlen(chan->exten) > atoi(data)) {
		strncpy(newexten, chan->exten + atoi(data), sizeof(newexten)-1);
	}
	strncpy(chan->exten, newexten, sizeof(chan->exten)-1);
	return 0;
}

static int pbx_builtin_prefix(struct ast_channel *chan, void *data)
{
	char newexten[AST_MAX_EXTENSION] = "";

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_DEBUG, "Ignoring, since there is no prefix to add\n");
		return 0;
	}
	snprintf(newexten, sizeof(newexten), "%s%s", (char *)data, chan->exten);
	strncpy(chan->exten, newexten, sizeof(chan->exten)-1);
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Prepended prefix, new extension is %s\n", chan->exten);
	return 0;
}

static int pbx_builtin_suffix(struct ast_channel *chan, void *data)
{
	char newexten[AST_MAX_EXTENSION] = "";

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_DEBUG, "Ignoring, since there is no suffix to add\n");
		return 0;
	}
	snprintf(newexten, sizeof(newexten), "%s%s", chan->exten, (char *)data);
	strncpy(chan->exten, newexten, sizeof(chan->exten)-1);
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Appended suffix, new extension is %s\n", chan->exten);
	return 0;
}

static int pbx_builtin_gotoiftime(struct ast_channel *chan, void *data)
{
	int res=0;
	char *s, *ts;
	struct ast_timing timing;

	if (!data) {
		ast_log(LOG_WARNING, "GotoIfTime requires an argument:\n  <time range>|<days of week>|<days of month>|<months>?[[context|]extension|]priority\n");
		return -1;
	}

	s = strdup((char *) data);
	ts = s;

	/* Separate the Goto path */
	strsep(&ts,"?");

	/* struct ast_include include contained garbage here, fixed by zeroing it on get_timerange */
	if (ast_build_timing(&timing, s) && ast_check_timing(&timing))
		res = pbx_builtin_goto(chan, (void *)ts);
	free(s);
	return res;
}

static int pbx_builtin_wait(struct ast_channel *chan, void *data)
{
	int ms;

	/* Wait for "n" seconds */
	if (data && atof((char *)data)) {
		ms = atof((char *)data) * 1000;
		return ast_safe_sleep(chan, ms);
	}
	return 0;
}

static int pbx_builtin_waitexten(struct ast_channel *chan, void *data)
{
	int ms;
	int res;
	/* Wait for "n" seconds */
	if (data && atof((char *)data)) 
		ms = atof((char *)data) * 1000;
	else if (chan->pbx)
		ms = chan->pbx->rtimeout * 1000;
	else
		ms = 10000;
	res = ast_waitfordigit(chan, ms);
	if (!res) {
		if (ast_exists_extension(chan, chan->context, "t", 1, chan->cid.cid_num)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Timeout on %s\n", chan->name);
			strncpy(chan->exten, "t", sizeof(chan->exten));
			chan->priority = 0;
		} else {
			ast_log(LOG_WARNING, "Timeout but no rule 't' in context '%s'\n", chan->context);
			res = -1;
		}
	}
	return res;
}

static int pbx_builtin_background(struct ast_channel *chan, void *data)
{
	int res = 0;
	int option_skip = 0;
	int option_noanswer = 0;
	char filename[256] = "";
	char* stringp;
	char* options;
	char *lang = NULL;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Background requires an argument(filename)\n");
		return -1;
	}

	strncpy(filename, (char*)data, sizeof(filename) - 1);
	stringp = filename;
	strsep(&stringp, "|");
	options = strsep(&stringp, "|");
	if (options)
		lang = strsep(&stringp, "|");
	if (!lang)
		lang = chan->language;

	if (options && !strcasecmp(options, "skip"))
		option_skip = 1;
	if (options && !strcasecmp(options, "noanswer"))
		option_noanswer = 1;

	/* Answer if need be */
	if (chan->_state != AST_STATE_UP) {
		if (option_skip) {
			return 0;
		} else if (!option_noanswer) {
			res = ast_answer(chan);
		}
	}

	if (!res) {
		/* Stop anything playing */
		ast_stopstream(chan);
		/* Stream a file */
		res = ast_streamfile(chan, filename, lang);
		if (!res) {
			res = ast_waitstream(chan, AST_DIGIT_ANY);
			ast_stopstream(chan);
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char*)data);
			res = 0;
		}
	}

	return res;
}

static int pbx_builtin_atimeout(struct ast_channel *chan, void *data)
{
	int x = atoi((char *) data);

	/* Set the absolute maximum time how long a call can be connected */
	ast_channel_setwhentohangup(chan,x);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Absolute Timeout to %d\n", x);
	return 0;
}

static int pbx_builtin_rtimeout(struct ast_channel *chan, void *data)
{
	/* Set the timeout for how long to wait between digits */
	chan->pbx->rtimeout = atoi((char *)data);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Response Timeout to %d\n", chan->pbx->rtimeout);
	return 0;
}

static int pbx_builtin_dtimeout(struct ast_channel *chan, void *data)
{
	/* Set the timeout for how long to wait between digits */
	chan->pbx->dtimeout = atoi((char *)data);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Digit Timeout to %d\n", chan->pbx->dtimeout);
	return 0;
}

static int pbx_builtin_goto(struct ast_channel *chan, void *data)
{
	char *s;
	char *exten, *pri, *context;
	char *stringp=NULL;
	int ipri;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Goto requires an argument (optional context|optional extension|priority)\n");
		return -1;
	}
	s = ast_strdupa((void *) data);
	stringp=s;
	context = strsep(&stringp, "|");
	exten = strsep(&stringp, "|");
	if (!exten) {
		/* Only a priority in this one */
		pri = context;
		exten = NULL;
		context = NULL;
	} else {
		pri = strsep(&stringp, "|");
		if (!pri) {
			/* Only an extension and priority in this one */
			pri = exten;
			exten = context;
			context = NULL;
		}
	}
	if (sscanf(pri, "%i", &ipri) != 1) {
		if ((ipri = ast_findlabel_extension(chan, context ? context : chan->context, (exten && strcasecmp(exten, "BYEXTENSION")) ? exten : chan->exten, 
			pri, chan->cid.cid_num)) < 1) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0, or valid label\n", pri);
			return -1;
		}
	} 
	/* At this point we have a priority and maybe an extension and a context */
	chan->priority = ipri - 1;
	if (exten && strcasecmp(exten, "BYEXTENSION"))
		strncpy(chan->exten, exten, sizeof(chan->exten)-1);
	if (context)
		strncpy(chan->context, context, sizeof(chan->context)-1);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Goto (%s,%s,%d)\n", chan->context,chan->exten, chan->priority+1);
	ast_cdr_update(chan);
	return 0;
}

int pbx_builtin_serialize_variables(struct ast_channel *chan, char *buf, size_t size) 
{
	struct ast_var_t *variables;
	struct varshead *headp;
	char *var=NULL ,*val=NULL;
	int total = 0;

	memset(buf,0,size);
	if (chan) {
		headp=&chan->varshead;
		AST_LIST_TRAVERSE(headp,variables,entries) {
			if(chan && variables && (var=ast_var_name(variables)) && (val=ast_var_value(variables)) && !ast_strlen_zero(var) && !ast_strlen_zero(val)) {
				snprintf(buf + strlen(buf), size - strlen(buf), "%s=%s\n", var, val);
				if(strlen(buf) >= size) {
					ast_log(LOG_ERROR,"Data Buffer Size Exceeded!\n");
					break;
				}
				total++;
			} else 
				break;
		}
	}
	
	return total;
}

char *pbx_builtin_getvar_helper(struct ast_channel *chan, char *name) 
{
	struct ast_var_t *variables;
	struct varshead *headp;

	if (chan)
		headp=&chan->varshead;
	else
		headp=&globals;

	if (name) {
		AST_LIST_TRAVERSE(headp,variables,entries) {
			if (!strcmp(name, ast_var_name(variables)))
				return ast_var_value(variables);
		}
		if (headp != &globals) {
			/* Check global variables if we haven't already */
			headp = &globals;
			AST_LIST_TRAVERSE(headp,variables,entries) {
				if (!strcmp(name, ast_var_name(variables)))
					return ast_var_value(variables);
			}
		}
	}
	return NULL;
}

void pbx_builtin_setvar_helper(struct ast_channel *chan, char *name, char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;

	if (chan)
		headp = &chan->varshead;
	else
		headp = &globals;

	AST_LIST_TRAVERSE (headp, newvariable, entries) {
		if (strcasecmp(ast_var_name(newvariable), name) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE(headp, newvariable, ast_var_t, entries);
			ast_var_delete(newvariable);
			break;
		}
	} 

	if (value) {
		if ((option_verbose > 1) && (headp == &globals))
			ast_verbose(VERBOSE_PREFIX_3 "Setting global variable '%s' to '%s'\n", name, value);
		newvariable = ast_var_assign(name, value);	
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}
}

int pbx_builtin_setvar(struct ast_channel *chan, void *data)
{
	char *name;
	char *value;
	char *stringp=NULL;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}

	stringp = data;
	name = strsep(&stringp,"=");
	value = strsep(&stringp,"\0"); 

	pbx_builtin_setvar_helper(chan, name, value);

	return(0);
}

static int pbx_builtin_setglobalvar(struct ast_channel *chan, void *data)
{
	char *name;
	char *value;
	char *stringp = NULL;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}

	stringp = data;
	name = strsep(&stringp, "=");
	value = strsep(&stringp, "\0"); 

	pbx_builtin_setvar_helper(NULL, name, value);

	return(0);
}

static int pbx_builtin_noop(struct ast_channel *chan, void *data)
{
	return 0;
}


void pbx_builtin_clear_globals(void)
{
	struct ast_var_t *vardata;
	while (!AST_LIST_EMPTY(&globals)) {
		vardata = AST_LIST_FIRST(&globals);
		AST_LIST_REMOVE_HEAD(&globals, entries);
		ast_var_delete(vardata);
	}
}

static int pbx_checkcondition(char *condition) 
{
	return condition ? atoi(condition) : 0;
}

static int pbx_builtin_gotoif(struct ast_channel *chan, void *data)
{
	char *condition,*branch1,*branch2,*branch;
	char *s;
	int rc;
	char *stringp=NULL;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to check\n");
		return 0;
	}
	
	s=ast_strdupa(data);
	stringp=s;
	condition=strsep(&stringp,"?");
	branch1=strsep(&stringp,":");
	branch2=strsep(&stringp,"");
	branch = pbx_checkcondition(condition) ? branch1 : branch2;
	
	if ((branch==NULL) || ast_strlen_zero(branch)) {
		ast_log(LOG_DEBUG, "Not taking any branch\n");
		return(0);
	}
	
	rc=pbx_builtin_goto(chan,branch);

	return(rc);
}           

static int pbx_builtin_saynumber(struct ast_channel *chan, void *data)
{
	int res = 0;
	char tmp[256];
	char *number = (char *) NULL;
	char *options = (char *) NULL;

	
	if (!data || ast_strlen_zero((char *)data)) {
                ast_log(LOG_WARNING, "SayNumber requires an argument (number)\n");
                return -1;
        }
        strncpy(tmp, (char *)data, sizeof(tmp)-1);
        number=tmp;
        strsep(&number, "|");
        options = strsep(&number, "|");
        if (options) { 
		if ( strcasecmp(options, "f") && strcasecmp(options,"m") && 
			strcasecmp(options, "c") && strcasecmp(options, "n") ) {
                   ast_log(LOG_WARNING, "SayNumber gender option is either 'f', 'm', 'c' or 'n'\n");
                   return -1;
		}
	}
	return res = ast_say_number(chan, atoi((char *) tmp), "", chan->language, options);
}

static int pbx_builtin_saydigits(struct ast_channel *chan, void *data)
{
	int res = 0;

	if (data)
		res = ast_say_digit_str(chan, (char *)data, "", chan->language);
	return res;
}
	
static int pbx_builtin_saycharacters(struct ast_channel *chan, void *data)
{
	int res = 0;

	if (data)
		res = ast_say_character_str(chan, (char *)data, "", chan->language);
	return res;
}
	
static int pbx_builtin_sayphonetic(struct ast_channel *chan, void *data)
{
	int res = 0;

	if (data)
		res = ast_say_phonetic_str(chan, (char *)data, "", chan->language);
	return res;
}
	
int load_pbx(void)
{
	int x;

	/* Initialize the PBX */
	if (option_verbose) {
		ast_verbose( "Asterisk PBX Core Initializing\n");
		ast_verbose( "Registering builtin applications:\n");
	}
        AST_LIST_HEAD_INIT(&globals);
	ast_cli_register(&show_applications_cli);
	ast_cli_register(&show_application_cli);
	ast_cli_register(&show_dialplan_cli);
	ast_cli_register(&show_switches_cli);

	/* Register builtin applications */
	for (x=0; x<sizeof(builtins) / sizeof(struct pbx_builtin); x++) {
		if (option_verbose)
			ast_verbose( VERBOSE_PREFIX_1 "[%s]\n", builtins[x].name);
		if (ast_register_application(builtins[x].name, builtins[x].execute, builtins[x].synopsis, builtins[x].description)) {
			ast_log(LOG_ERROR, "Unable to register builtin application '%s'\n", builtins[x].name);
			return -1;
		}
	}
	return 0;
}

/*
 * Lock context list functions ...
 */
int ast_lock_contexts()
{
	return ast_mutex_lock(&conlock);
}

int ast_unlock_contexts()
{
	return ast_mutex_unlock(&conlock);
}

/*
 * Lock context ...
 */
int ast_lock_context(struct ast_context *con)
{
	return ast_mutex_lock(&con->lock);
}

int ast_unlock_context(struct ast_context *con)
{
	return ast_mutex_unlock(&con->lock);
}

/*
 * Name functions ...
 */
const char *ast_get_context_name(struct ast_context *con)
{
	return con ? con->name : NULL;
}

const char *ast_get_extension_name(struct ast_exten *exten)
{
	return exten ? exten->exten : NULL;
}

const char *ast_get_extension_label(struct ast_exten *exten)
{
	return exten ? exten->label : NULL;
}

const char *ast_get_include_name(struct ast_include *inc)
{
	return inc ? inc->name : NULL;
}

const char *ast_get_ignorepat_name(struct ast_ignorepat *ip)
{
	return ip ? ip->pattern : NULL;
}

int ast_get_extension_priority(struct ast_exten *exten)
{
	return exten ? exten->priority : -1;
}

/*
 * Registrar info functions ...
 */
const char *ast_get_context_registrar(struct ast_context *c)
{
	return c ? c->registrar : NULL;
}

const char *ast_get_extension_registrar(struct ast_exten *e)
{
	return e ? e->registrar : NULL;
}

const char *ast_get_include_registrar(struct ast_include *i)
{
	return i ? i->registrar : NULL;
}

const char *ast_get_ignorepat_registrar(struct ast_ignorepat *ip)
{
	return ip ? ip->registrar : NULL;
}

int ast_get_extension_matchcid(struct ast_exten *e)
{
	return e ? e->matchcid : 0;
}

const char *ast_get_extension_cidmatch(struct ast_exten *e)
{
	return e ? e->cidmatch : NULL;
}

const char *ast_get_extension_app(struct ast_exten *e)
{
	return e ? e->app : NULL;
}

void *ast_get_extension_app_data(struct ast_exten *e)
{
	return e ? e->data : NULL;
}

const char *ast_get_switch_name(struct ast_sw *sw)
{
	return sw ? sw->name : NULL;
}

const char *ast_get_switch_data(struct ast_sw *sw)
{
	return sw ? sw->data : NULL;
}

const char *ast_get_switch_registrar(struct ast_sw *sw)
{
	return sw ? sw->registrar : NULL;
}

/*
 * Walking functions ...
 */
struct ast_context *ast_walk_contexts(struct ast_context *con)
{
	if (!con)
		return contexts;
	else
		return con->next;
}

struct ast_exten *ast_walk_context_extensions(struct ast_context *con,
	struct ast_exten *exten)
{
	if (!exten)
		return con ? con->root : NULL;
	else
		return exten->next;
}

struct ast_sw *ast_walk_context_switches(struct ast_context *con,
	struct ast_sw *sw)
{
	if (!sw)
		return con ? con->alts : NULL;
	else
		return sw->next;
}

struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority)
{
	if (!priority)
		return exten;
	else
		return priority->peer;
}

struct ast_include *ast_walk_context_includes(struct ast_context *con,
	struct ast_include *inc)
{
	if (!inc)
		return con ? con->includes : NULL;
	else
		return inc->next;
}

struct ast_ignorepat *ast_walk_context_ignorepats(struct ast_context *con,
	struct ast_ignorepat *ip)
{
	if (!ip)
		return con ? con->ignorepats : NULL;
	else
		return ip->next;
}

int ast_context_verify_includes(struct ast_context *con)
{
	struct ast_include *inc;
	int res = 0;

	for (inc = ast_walk_context_includes(con, NULL); inc; inc = ast_walk_context_includes(con, inc))
		if (!ast_context_find(inc->rname)) {
			res = -1;
			ast_log(LOG_WARNING, "Context '%s' tries includes non-existant context '%s'\n",
					ast_get_context_name(con), inc->rname);
		}
	return res;
}

