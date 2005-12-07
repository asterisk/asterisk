/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Core PBX routines.
 * 
 */

#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/cdr.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/ast_expr.h"
#include "asterisk/linkedlists.h"
#include "asterisk/say.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"
#include "asterisk/devicestate.h"
#include "asterisk/compat.h"

/*!
 * \note I M P O R T A N T :
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

#define SWITCH_DATA_LENGTH 256

#define VAR_BUF_SIZE 4096

#define	VAR_NORMAL		1
#define	VAR_SOFTTRAN	2
#define	VAR_HARDTRAN	3

#define BACKGROUND_SKIP		(1 << 0)
#define BACKGROUND_NOANSWER	(1 << 1)
#define BACKGROUND_MATCHEXTEN	(1 << 2)
#define BACKGROUND_PLAYBACK	(1 << 3)

AST_APP_OPTIONS(background_opts, {
	AST_APP_OPTION('s', BACKGROUND_SKIP),
	AST_APP_OPTION('n', BACKGROUND_NOANSWER),
	AST_APP_OPTION('m', BACKGROUND_MATCHEXTEN),
	AST_APP_OPTION('p', BACKGROUND_PLAYBACK),
});

#define WAITEXTEN_MOH		(1 << 0)

AST_APP_OPTIONS(waitexten_opts, {
	AST_APP_OPTION_ARG('m', WAITEXTEN_MOH, 1),
});

struct ast_context;

/*!\brief ast_exten: An extension 
	The dialplan is saved as a linked list with each context
	having it's own linked list of extensions - one item per
	priority.
*/
struct ast_exten {
	char *exten;			/* Extension name */
	int matchcid;			/* Match caller id ? */
	char *cidmatch;			/* Caller id to match for this extension */
	int priority;			/* Priority */
	char *label;			/* Label */
	struct ast_context *parent;	/* The context this extension belongs to  */
	char *app; 			/* Application to execute */
	void *data;			/* Data to use (arguments) */
	void (*datad)(void *);		/* Data destructor */
	struct ast_exten *peer;		/* Next higher priority with our extension */
	const char *registrar;		/* Registrar */
	struct ast_exten *next;		/* Extension with a greater ID */
	char stuff[0];
};

/*! \brief ast_include: include= support in extensions.conf */
struct ast_include {
	char *name;		
	char *rname;		/* Context to include */
	const char *registrar;			/* Registrar */
	int hastime;				/* If time construct exists */
	struct ast_timing timing;               /* time construct */
	struct ast_include *next;		/* Link them together */
	char stuff[0];
};

/*! \brief ast_sw: Switch statement in extensions.conf */
struct ast_sw {
	char *name;
	const char *registrar;			/* Registrar */
	char *data;				/* Data load */
	int eval;
	struct ast_sw *next;			/* Link them together */
	char *tmpdata;
	char stuff[0];
};

/*! \brief ast_ignorepat: Ignore patterns in dial plan */
struct ast_ignorepat {
	const char *registrar;
	struct ast_ignorepat *next;
	char pattern[0];
};

/*! \brief ast_context: An extension context */
struct ast_context {
	ast_mutex_t lock; 			/*!< A lock to prevent multiple threads from clobbering the context */
	struct ast_exten *root;			/*!< The root of the list of extensions */
	struct ast_context *next;		/*!< Link them together */
	struct ast_include *includes;		/*!< Include other contexts */
	struct ast_ignorepat *ignorepats;	/*!< Patterns for which to continue playing dialtone */
	const char *registrar;			/*!< Registrar */
	struct ast_sw *alts;			/*!< Alternative switches */
	char name[0];				/*!< Name of the context */
};


/*! \brief ast_app: A registered application */
struct ast_app {
	int (*execute)(struct ast_channel *chan, void *data);
	const char *synopsis;			/* Synopsis text for 'show applications' */
	const char *description;		/* Description (help text) for 'show application <name>' */
	struct ast_app *next;			/* Next app in list */
	char name[0];				/* Name of the application */
};

/*! \brief ast_state_cb: An extension state notify register item */
struct ast_state_cb {
	int id;
	void *data;
	ast_state_cb_type callback;
	struct ast_state_cb *next;
};
	    
/*! \brief Structure for dial plan hints

  Hints are pointers from an extension in the dialplan to one or
  more devices (tech/name) */
struct ast_hint {
	struct ast_exten *exten;	/*!< Extension */
	int laststate; 			/*!< Last known state */
	struct ast_state_cb *callbacks;	/*!< Callback list for this extension */
	struct ast_hint *next;		/*!< Pointer to next hint in list */
};

int ast_pbx_outgoing_cdr_failed(void);

static int pbx_builtin_answer(struct ast_channel *, void *);
static int pbx_builtin_goto(struct ast_channel *, void *);
static int pbx_builtin_hangup(struct ast_channel *, void *);
static int pbx_builtin_background(struct ast_channel *, void *);
static int pbx_builtin_wait(struct ast_channel *, void *);
static int pbx_builtin_waitexten(struct ast_channel *, void *);
static int pbx_builtin_resetcdr(struct ast_channel *, void *);
static int pbx_builtin_setamaflags(struct ast_channel *, void *);
static int pbx_builtin_ringing(struct ast_channel *, void *);
static int pbx_builtin_progress(struct ast_channel *, void *);
static int pbx_builtin_congestion(struct ast_channel *, void *);
static int pbx_builtin_busy(struct ast_channel *, void *);
static int pbx_builtin_setglobalvar(struct ast_channel *, void *);
static int pbx_builtin_noop(struct ast_channel *, void *);
static int pbx_builtin_gotoif(struct ast_channel *, void *);
static int pbx_builtin_gotoiftime(struct ast_channel *, void *);
static int pbx_builtin_execiftime(struct ast_channel *, void *);
static int pbx_builtin_saynumber(struct ast_channel *, void *);
static int pbx_builtin_saydigits(struct ast_channel *, void *);
static int pbx_builtin_saycharacters(struct ast_channel *, void *);
static int pbx_builtin_sayphonetic(struct ast_channel *, void *);
int pbx_builtin_setvar(struct ast_channel *, void *);
static int pbx_builtin_importvar(struct ast_channel *, void *);

static struct varshead globals;

static int autofallthrough = 0;

AST_MUTEX_DEFINE_STATIC(maxcalllock);
static int countcalls = 0;

AST_MUTEX_DEFINE_STATIC(acflock); 		/*!< Lock for the custom function list */
static struct ast_custom_function *acf_root = NULL;

/*! \brief Declaration of builtin applications */
static struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, void *data);
	char *synopsis;
	char *description;
} builtins[] = 
{
	/* These applications are built into the PBX core and do not
	   need separate modules */

	{ "Answer", pbx_builtin_answer, 
	"Answer a channel if ringing", 
	"  Answer([delay]): If the call has not been answered, this application will\n"
	"answer it. Otherwise, it has no effect on the call. If a delay is specified,\n"
	"Asterisk will wait this number of milliseconds before answering the call.\n"
	},

	{ "BackGround", pbx_builtin_background,
	"Play a file while awaiting extension",
	"  Background(filename1[&filename2...][|options[|langoverride][|context]]):\n"
	"This application will play the given list of files while waiting for an\n"
	"extension to be dialed by the calling channel. To continue waiting for digits\n"
	"after this application has finished playing files, the WaitExten application\n"
	"should be used. The 'langoverride' option explicity specifies which language\n"
	"to attempt to use for the requested sound files. If a 'context' is specified,\n"
	"this is the dialplan context that this application will use when exiting to a\n"
	"dialed extension."
	"  If one of the requested sound files does not exist, call processing will be\n"
	"terminated.\n"
	"  Options:\n"
	"    s - causes the playback of the message to be skipped\n"
	"          if the channel is not in the 'up' state (i.e. it\n"
	"          hasn't been answered yet.) If this happens, the\n"
	"          application will return immediately.\n"
	"    n - don't answer the channel before playing the files\n"
	"    m - only break if a digit hit matches a one digit\n"
	"          extension in the destination context\n"
	},

	{ "Busy", pbx_builtin_busy,
	"Indicate the Busy condition",
	"  Busy([timeout]): This application will indicate the busy condition to\n"
	"the calling channel. If the optional timeout is specified, the calling channel\n"
	"will be hung up after the specified number of seconds. Otherwise, this\n"
	"application will wait until the calling channel hangs up.\n"
	},

	{ "Congestion", pbx_builtin_congestion,
	"Indicate the Congestion condition",
	"  Congestion([timeout]): This application will indicate the congenstion\n"
	"condition to the calling channel. If the optional timeout is specified, the\n"
	"calling channel will be hung up after the specified number of seconds.\n"
	"Otherwise, this application will wait until the calling channel hangs up.\n"
	},

	{ "Goto", pbx_builtin_goto, 
	"Jump to a particular priority, extension, or context",
	"  Goto([[context|]extension|]priority): This application will cause the\n"
	"calling channel to continue dialplan execution at the specified priority.\n"
	"If no specific extension, or extension and context, are specified, then this\n"
	"application will jump to the specified priority of the current extension.\n"
	"  If the attempt to jump to another location in the dialplan is not successful,\n"
	"then the channel will continue at the next priority of the current extension.\n"
	},

	{ "GotoIf", pbx_builtin_gotoif,
	"Conditional goto",
	"  GotoIf(Condition?[label1]:[label2]): This application will cause the calling\n"
	"channel to jump to the speicifed location in the dialplan based on the\n"
	"evaluation of the given condition. The channel will continue at 'label1' if the\n"
	"condition is true, or 'label2' if the condition is false. The labels are\n"
	"specified in the same syntax that is used with the Goto application.\n"
	},

	{ "GotoIfTime", pbx_builtin_gotoiftime,
	"Conditional Goto based on the current time",
	"  GotoIfTime(<times>|<weekdays>|<mdays>|<months>?[[context|]exten|]priority):\n"
	"This application will have the calling channel jump to the speicified location\n"
	"int the dialplan if the current time matches the given time specification.\n"
	"Further information on the time specification can be found in examples\n"
	"illustrating how to do time-based context includes in the dialplan.\n" 
	},

	{ "ExecIfTime", pbx_builtin_execiftime,
	"Conditional application execution based on the current time",
	"  ExecIfTime(<times>|<weekdays>|<mdays>|<months>?appname[|appargs]):\n"
	"This application will execute the specified dialplan application, with optional\n"
	"arguments, if the current time matches the given time specification. Further\n"
	"information on the time speicification can be found in examples illustrating\n"
	"how to do time-based context includes in the dialplan.\n"
	},
	
	{ "Hangup", pbx_builtin_hangup,
	"Hang up the calling channel",
	"  Hangup(): This application will hang up the calling channel.\n"
	},

	{ "NoOp", pbx_builtin_noop,
	"Do Nothing",
	"  NoOp(): This applicatiion does nothing. However, it is useful for debugging\n"
	"purposes. Any text that is provided as arguments to this application can be\n"
	"viewed at the Asterisk CLI. This method can be used to see the evaluations of\n"
	"variables or functions without having any effect." 
	},

	{ "Progress", pbx_builtin_progress,
	"Indicate progress",
	"  Progress(): This application will request that in-band progress information\n"
	"be provided to the calling channel.\n"
	},

	{ "ResetCDR", pbx_builtin_resetcdr,
	"Resets the Call Data Record",
	"  ResetCDR([options]):  This application causes the Call Data Record to be\n"
	"reset.\n"
	"  Options:\n"
	"    w -- Store the current CDR record before resetting it.\n"
	"    a -- Store any stacked records.\n"
	"    v -- Save CDR variables.\n"
	},

	{ "Ringing", pbx_builtin_ringing,
	"Indicate ringing tone",
	"  Ringing(): This application will request that the channel indicate a ringing\n"
	"tone to the user.\n"
	},

	{ "SayNumber", pbx_builtin_saynumber,
	"Say Number",
	"  SayNumber(digits[,gender]): This application will play the sounds that\n"
	"correspond to the given number. Optionally, a gender may be specified.\n"
	"This will use the language that is currently set for the channel. See the\n"
	"LANGUAGE function for more information on setting the language for the channel.\n"	
	},

	{ "SayDigits", pbx_builtin_saydigits,
	"Say Digits",
	"  SayDigits(digits): This application will play the sounds that correspond\n"
	"to the digits of the given number. This will use the language that is currently\n"
	"set for the channel. See the LANGUAGE function for more information on setting\n"
	"the language for the channel.\n"
	},

	{ "SayAlpha", pbx_builtin_saycharacters,
	"Say Alpha",
	"  SayAlpha(string): This application will play the sounds that correspond to\n"
	"the letters of the given string.\n" 
	},

	{ "SayPhonetic", pbx_builtin_sayphonetic,
	"Say Phonetic",
	"  SayPhonetic(string): This application will play the sounds from the phonetic\n"
	"alphabet that correspond to the letters in the given string.\n"
	},

	{ "SetAMAFlags", pbx_builtin_setamaflags,
	"Set the AMA Flags",
	"  SetAMAFlags([flag]): This channel will set the channel's AMA Flags for billing\n"
	"purposes.\n"
	},

	{ "SetGlobalVar", pbx_builtin_setglobalvar,
	"Set a global variable to a given value",
	"  SetGlobalVar(variable=value): This application sets a given global variable to\n"
	"the specified value.\n"
	},

	{ "Set", pbx_builtin_setvar,
	"Set channel variable(s) or function value(s)",
	"  Set(name1=value1|name2=value2|..[|options])\n"
	"This function can be used to set the value of channel variables or dialplan\n"
	"functions. It will accept up to 24 name/value pairs. When setting variables,\n"
	"if the variable name is prefixed with _, the variable will be inherited into\n"
	"channels created from the current channel. If the variable name is prefixed\n"
	"with __, the variable will be inherited into channels created from the current\n"
	"channel and all children channels.\n"
	"  Options:\n" 
	"    g - Set variable globally instead of on the channel\n"
	"        (applies only to variables, not functions)\n"
	},

	{ "ImportVar", pbx_builtin_importvar,
	"Import a variable from a channel into a new variable",
	"  ImportVar(newvar=channelname|variable): This application imports a variable\n"
	"from the specified channel (as opposed to the current one) and stores it as\n"
	"a variable in the current channel (the channel that is calling this\n"
	"application). Variables created by this application have the same inheritance\n"
	"properties as those created with the Set application. See the documentation for\n"
	"Set for more information.\n"
	},

	{ "Wait", pbx_builtin_wait, 
	"Waits for some time", 
	"  Wait(seconds): This application waits for a specified number of seconds.\n"
	"Then, dialplan execution will continue at the next priority.\n"
	"  Note that the seconds can be passed with fractions of a second. For example,\n"
	"'1.5' will ask the application to wait for 1.5 seconds.\n" 
	},

	{ "WaitExten", pbx_builtin_waitexten, 
	"Waits for an extension to be entered", 
	"  WaitExten([seconds][|options]): This application waits for the user to enter\n"
	"a new extension for a specified number of seconds.\n"
	"  Note that the seconds can be passed with fractions of a second. For example,\n"
	"'1.5' will ask the application to wait for 1.5 seconds.\n" 
	"  Options:\n"
	"    m[(x)] - Provide music on hold to the caller while waiting for an extension.\n"
	"               Optionally, specify the class for music on hold within parenthesis.\n"
	},

};

static struct ast_context *contexts = NULL;
AST_MUTEX_DEFINE_STATIC(conlock); 		/* Lock for the ast_context list */
static struct ast_app *apps = NULL;
AST_MUTEX_DEFINE_STATIC(applock); 		/* Lock for the application list */

struct ast_switch *switches = NULL;
AST_MUTEX_DEFINE_STATIC(switchlock);		/* Lock for switches */

AST_MUTEX_DEFINE_STATIC(hintlock);		/* Lock for extension state notifys */
static int stateid = 1;
struct ast_hint *hints = NULL;
struct ast_state_cb *statecbs = NULL;

/* 
   \note This function is special. It saves the stack so that no matter
   how many times it is called, it returns to the same place */
int pbx_exec(struct ast_channel *c, 		/*!< Channel */
		struct ast_app *app,		/*!< Application */
		void *data,			/*!< Data for execution */
		int newstack)			/*!< Force stack increment */
{
	int res;
	
	char *saved_c_appl;
	char *saved_c_data;
	
	int (*execute)(struct ast_channel *chan, void *data) = app->execute; 

	if (newstack) {
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
		return res;
	} else
		ast_log(LOG_WARNING, "You really didn't want to call this function with newstack set to 0\n");
	return -1;
}


/*! Go no deeper than this through includes (not counting loops) */
#define AST_PBX_MAX_STACK	128

#define HELPER_EXISTS 0
#define HELPER_SPAWN 1
#define HELPER_EXEC 2
#define HELPER_CANMATCH 3
#define HELPER_MATCHMORE 4
#define HELPER_FINDLABEL 5

/*! \brief Find application handle in linked list
 */
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
		case '!':\
			/* Early match */\
			return 2;\
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
	/* If we ran off the end of the data and the pattern ends in '!', match */\
	if (match && !*data && (*pattern == '!'))\
		return 2;\
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

int ast_extension_close(const char *pattern, const char *data, int needmore)
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
	/* If there's more or we don't care about more, or if it's a possible early match, 
	   return non-zero; otherwise it's a miss */
	if (!needmore || *pattern || match == 2) {
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

#define STATUS_NO_CONTEXT	1
#define STATUS_NO_EXTENSION	2
#define STATUS_NO_PRIORITY	3
#define STATUS_NO_LABEL		4
#define STATUS_SUCCESS		5

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

static struct ast_exten *pbx_find_extension(struct ast_channel *chan, struct ast_context *bypass, const char *context, const char *exten, int priority, const char *label, const char *callerid, int action, char *incstack[], int *stacklen, int *status, struct ast_switch **swo, char **data, const char **foundcontext)
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
	for (x=0; x<*stacklen; x++) {
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
			struct ast_exten *earlymatch = NULL;

			if (*status < STATUS_NO_EXTENSION)
				*status = STATUS_NO_EXTENSION;
			for (eroot = tmp->root; eroot; eroot=eroot->next) {
				int match = 0;
				/* Match extension */
				if ((((action != HELPER_MATCHMORE) && ast_extension_match(eroot->exten, exten)) ||
				     ((action == HELPER_CANMATCH) && (ast_extension_close(eroot->exten, exten, 0))) ||
				     ((action == HELPER_MATCHMORE) && (match = ast_extension_close(eroot->exten, exten, 1)))) &&
				    (!eroot->matchcid || matchcid(eroot->cidmatch, callerid))) {

					if (action == HELPER_MATCHMORE && match == 2 && !earlymatch) {
						/* It matched an extension ending in a '!' wildcard
						   So ignore it for now, unless there's a better match */
						earlymatch = eroot;
					} else {
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
									*foundcontext = context;
									return e;
								}
							} else if (e->priority == priority) {
								*status = STATUS_SUCCESS;
								*foundcontext = context;
								return e;
							}
							e = e->peer;
						}
					}
				}
			}
			if (earlymatch) {
				/* Bizarre logic for HELPER_MATCHMORE. We return zero to break out 
				   of the loop waiting for more digits, and _then_ match (normally)
				   the extension we ended up with. We got an early-matching wildcard
				   pattern, so return NULL to break out of the loop. */
				return NULL;
			}
			/* Check alternative switches */
			sw = tmp->alts;
			while(sw) {
				if ((asw = pbx_findswitch(sw->name))) {
					/* Substitute variables now */
					if (sw->eval) 
						pbx_substitute_variables_helper(chan, sw->data, sw->tmpdata, SWITCH_DATA_LENGTH - 1);
					if (action == HELPER_CANMATCH)
						res = asw->canmatch ? asw->canmatch(chan, context, exten, priority, callerid, sw->eval ? sw->tmpdata : sw->data) : 0;
					else if (action == HELPER_MATCHMORE)
						res = asw->matchmore ? asw->matchmore(chan, context, exten, priority, callerid, sw->eval ? sw->tmpdata : sw->data) : 0;
					else
						res = asw->exists ? asw->exists(chan, context, exten, priority, callerid, sw->eval ? sw->tmpdata : sw->data) : 0;
					if (res) {
						/* Got a match */
						*swo = asw;
						*data = sw->eval ? sw->tmpdata : sw->data;
						*foundcontext = context;
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
					if ((e = pbx_find_extension(chan, bypass, i->rname, exten, priority, label, callerid, action, incstack, stacklen, status, swo, data, foundcontext))) 
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

/* Note that it's negative -- that's important later. */
#define DONT_HAVE_LENGTH	0x80000000

static int parse_variable_name(char *var, int *offset, int *length, int *isfunc)
{
	char *varchar, *offsetchar = NULL;
	int parens=0;

	*offset = 0;
	*length = DONT_HAVE_LENGTH;
	*isfunc = 0;
	for (varchar=var; *varchar; varchar++) {
		switch (*varchar) {
		case '(':
			(*isfunc)++;
			parens++;
			break;
		case ')':
			parens--;
			break;
		case ':':
			if (parens == 0) {
				offsetchar = varchar + 1;
				*varchar = '\0';
				goto pvn_endfor;
			}
		}
	}
pvn_endfor:
	if (offsetchar) {
		sscanf(offsetchar, "%d:%d", offset, length);
		return 1;
	} else {
		return 0;
	}
}

static char *substring(char *value, int offset, int length, char *workspace, size_t workspace_len)
{
	char *ret = workspace;

	/* No need to do anything */
	if (offset == 0 && length==-1) {
		return value;
	}

	ast_copy_string(workspace, value, workspace_len);

	if (abs(offset) > strlen(ret)) {	/* Offset beyond string */
		if (offset >= 0) 
			offset = strlen(ret);
		else 
			offset =- strlen(ret);	
	}

	/* Detect too-long length */
	if ((offset < 0 && length > -offset) || (offset >= 0 && offset+length > strlen(ret))) {
		if (offset >= 0) 
			length = strlen(ret)-offset;
		else 
			length = strlen(ret)+offset;
	}

	/* Bounce up to the right offset */
	if (offset >= 0)
		ret += offset;
	else
		ret += strlen(ret)+offset;

	/* Chop off at the requisite length */
	if (length >= 0)
		ret[length] = '\0';

	return ret;
}

/*! \brief  pbx_retrieve_variable: Support for Asterisk built-in variables and
      functions in the dialplan
  ---*/
void pbx_retrieve_variable(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp)
{
	char tmpvar[80];
	time_t thistime;
	struct tm brokentime;
	int offset, offset2, isfunc;
	struct ast_var_t *variables;

	if (c) 
		headp=&c->varshead;
	*ret=NULL;
	ast_copy_string(tmpvar, var, sizeof(tmpvar));
	if (parse_variable_name(tmpvar, &offset, &offset2, &isfunc)) {
		pbx_retrieve_variable(c, tmpvar, ret, workspace, workspacelen, headp);
		if (!(*ret)) 
			return;
		*ret = substring(*ret, offset, offset2, workspace, workspacelen);
	} else if (c && !strncmp(var, "CALL", 4)) {
		if (!strncmp(var + 4, "ER", 2)) {
			if (!strncmp(var + 6, "ID", 2)) {
				if (!var[8]) { 			/* CALLERID */
					if (c->cid.cid_num) {
						if (c->cid.cid_name) {
							snprintf(workspace, workspacelen, "\"%s\" <%s>", c->cid.cid_name, c->cid.cid_num);
						} else {
							ast_copy_string(workspace, c->cid.cid_num, workspacelen);
						}
						*ret = workspace;
					} else if (c->cid.cid_name) {
						ast_copy_string(workspace, c->cid.cid_name, workspacelen);
						*ret = workspace;
					} else
						*ret = NULL;
				} else if (!strcmp(var + 8, "NUM")) {
					/* CALLERIDNUM */
					if (c->cid.cid_num) {
						ast_copy_string(workspace, c->cid.cid_num, workspacelen);
						*ret = workspace;
					} else
						*ret = NULL;
				} else if (!strcmp(var + 8, "NAME")) {
					/* CALLERIDNAME */
					if (c->cid.cid_name) {
						ast_copy_string(workspace, c->cid.cid_name, workspacelen);
						*ret = workspace;
					} else
						*ret = NULL;
				}
			} else if (!strcmp(var + 6, "ANI")) {
				/* CALLERANI */
				if (c->cid.cid_ani) {
					ast_copy_string(workspace, c->cid.cid_ani, workspacelen);
					*ret = workspace;
				} else
					*ret = NULL;
			} else
				goto icky;
		} else if (!strncmp(var + 4, "ING", 3)) {
			if (!strcmp(var + 7, "PRES")) {
				/* CALLINGPRES */
				snprintf(workspace, workspacelen, "%d", c->cid.cid_pres);
				*ret = workspace;
			} else if (!strcmp(var + 7, "ANI2")) {
				/* CALLINGANI2 */
				snprintf(workspace, workspacelen, "%d", c->cid.cid_ani2);
				*ret = workspace;
			} else if (!strcmp(var + 7, "TON")) {
				/* CALLINGTON */
				snprintf(workspace, workspacelen, "%d", c->cid.cid_ton);
				*ret = workspace;
			} else if (!strcmp(var + 7, "TNS")) {
				/* CALLINGTNS */
				snprintf(workspace, workspacelen, "%d", c->cid.cid_tns);
				*ret = workspace;
			} else
				goto icky;
		} else
			goto icky;
	} else if (c && !strcmp(var, "DNID")) {
		if (c->cid.cid_dnid) {
			ast_copy_string(workspace, c->cid.cid_dnid, workspacelen);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "HINT")) {
		if (!ast_get_hint(workspace, workspacelen, NULL, 0, c, c->context, c->exten))
			*ret = NULL;
		else
			*ret = workspace;
	} else if (c && !strcmp(var, "HINTNAME")) {
		if (!ast_get_hint(NULL, 0, workspace, workspacelen, c, c->context, c->exten))
			*ret = NULL;
		else
			*ret = workspace;
	} else if (c && !strcmp(var, "EXTEN")) {
		ast_copy_string(workspace, c->exten, workspacelen);
		*ret = workspace;
	} else if (c && !strcmp(var, "RDNIS")) {
		if (c->cid.cid_rdnis) {
			ast_copy_string(workspace, c->cid.cid_rdnis, workspacelen);
			*ret = workspace;
		} else
			*ret = NULL;
	} else if (c && !strcmp(var, "CONTEXT")) {
		ast_copy_string(workspace, c->context, workspacelen);
		*ret = workspace;
	} else if (c && !strcmp(var, "PRIORITY")) {
		snprintf(workspace, workspacelen, "%d", c->priority);
		*ret = workspace;
	} else if (c && !strcmp(var, "CHANNEL")) {
		ast_copy_string(workspace, c->name, workspacelen);
		*ret = workspace;
	} else if (!strcmp(var, "EPOCH")) {
		snprintf(workspace, workspacelen, "%u",(int)time(NULL));
		*ret = workspace;
	} else if (!strcmp(var, "DATETIME")) {
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
	} else if (!strcmp(var, "TIMESTAMP")) {
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
		snprintf(workspace, workspacelen, "%d", c->hangupcause);
		*ret = workspace;
	} else if (c && !strcmp(var, "ACCOUNTCODE")) {
		ast_copy_string(workspace, c->accountcode, workspacelen);
		*ret = workspace;
	} else if (c && !strcmp(var, "LANGUAGE")) {
		ast_copy_string(workspace, c->language, workspacelen);
		*ret = workspace;
	} else {
icky:
		if (headp) {
			AST_LIST_TRAVERSE(headp,variables,entries) {
#if 0
				ast_log(LOG_WARNING,"Comparing variable '%s' with '%s'\n",var,ast_var_name(variables));
#endif
				if (strcasecmp(ast_var_name(variables),var)==0) {
					const char *s = ast_var_value(variables);
					if (s) {
						ast_copy_string(workspace, s, workspacelen);
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
					const char *s = ast_var_value(variables);
					if (s) {
						ast_copy_string(workspace, s, workspacelen);
						*ret = workspace;
					}
				}
			}
		}
	}
}

/*! \brief CLI function to show installed custom functions 
    \addtogroup CLI_functions
 */
static int handle_show_functions(int fd, int argc, char *argv[])
{
	struct ast_custom_function *acf;
	int count_acf = 0;
	int print_acf = 0;
	int like = 0;

	if (argc == 4 && (!strcmp(argv[2], "like")) ) {
		like = 1;
	} else if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}

	ast_cli(fd, "%s Custom Functions:\n--------------------------------------------------------------------------------\n", like ? "Matching" : "Installed");
	
	for (acf = acf_root ; acf; acf = acf->next) {
		print_acf = 0;
		if (like) {
			if (strstr(acf->name, argv[3])) {
				print_acf = 1;
				count_acf++;
			}
		} else {
			print_acf = 1;
			count_acf++;
		} 

		if (print_acf) {
			ast_cli(fd, "%-20.20s  %-35.35s  %s\n", acf->name, acf->syntax, acf->synopsis);
		}
	}

	ast_cli(fd, "%d %scustom functions installed.\n", count_acf, like ? "matching " : "");

	return 0;
}

static int handle_show_function(int fd, int argc, char *argv[])
{
	struct ast_custom_function *acf;
	/* Maximum number of characters added by terminal coloring is 22 */
	char infotitle[64 + AST_MAX_APP + 22], syntitle[40], destitle[40];
	char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL;
	char stxtitle[40], *syntax = NULL;
	int synopsis_size, description_size, syntax_size;

	if (argc < 3) return RESULT_SHOWUSAGE;

	if (!(acf = ast_custom_function_find(argv[2]))) {
		ast_cli(fd, "No function by that name registered.\n");
		return RESULT_FAILURE;

	}

	if (acf->synopsis)
		synopsis_size = strlen(acf->synopsis) + 23;
	else
		synopsis_size = strlen("Not available") + 23;
	synopsis = alloca(synopsis_size);
	
	if (acf->desc)
		description_size = strlen(acf->desc) + 23;
	else
		description_size = strlen("Not available") + 23;
	description = alloca(description_size);

	if (acf->syntax)
		syntax_size = strlen(acf->syntax) + 23;
	else
		syntax_size = strlen("Not available") + 23;
	syntax = alloca(syntax_size);

	snprintf(info, 64 + AST_MAX_APP, "\n  -= Info about function '%s' =- \n\n", acf->name);
	term_color(infotitle, info, COLOR_MAGENTA, 0, 64 + AST_MAX_APP + 22);
	term_color(stxtitle, "[Syntax]\n", COLOR_MAGENTA, 0, 40);
	term_color(syntitle, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
	term_color(destitle, "[Description]\n", COLOR_MAGENTA, 0, 40);
	term_color(syntax,
		   acf->syntax ? acf->syntax : "Not available",
		   COLOR_CYAN, 0, syntax_size);
	term_color(synopsis,
		   acf->synopsis ? acf->synopsis : "Not available",
		   COLOR_CYAN, 0, synopsis_size);
	term_color(description,
		   acf->desc ? acf->desc : "Not available",
		   COLOR_CYAN, 0, description_size);
	
	ast_cli(fd,"%s%s%s\n\n%s%s\n\n%s%s\n", infotitle, stxtitle, syntax, syntitle, synopsis, destitle, description);

	return RESULT_SUCCESS;
}

static char *complete_show_function(char *line, char *word, int pos, int state)
{
	struct ast_custom_function *acf;
	int which = 0;

	/* try to lock functions list ... */
	if (ast_mutex_lock(&acflock)) {
		ast_log(LOG_ERROR, "Unable to lock function list\n");
		return NULL;
	}

	acf = acf_root;
	while (acf) {
		if (!strncasecmp(word, acf->name, strlen(word))) {
			if (++which > state) {
				char *ret = strdup(acf->name);
				ast_mutex_unlock(&acflock);
				return ret;
			}
		}
		acf = acf->next; 
	}

	ast_mutex_unlock(&acflock);
	return NULL; 
}

struct ast_custom_function* ast_custom_function_find(char *name) 
{
	struct ast_custom_function *acfptr;

	/* try to lock functions list ... */
	if (ast_mutex_lock(&acflock)) {
		ast_log(LOG_ERROR, "Unable to lock function list\n");
		return NULL;
	}

	for (acfptr = acf_root; acfptr; acfptr = acfptr->next) {
		if (!strcmp(name, acfptr->name)) {
			break;
		}
	}

	ast_mutex_unlock(&acflock);
	
	return acfptr;
}

int ast_custom_function_unregister(struct ast_custom_function *acf) 
{
	struct ast_custom_function *acfptr, *lastacf = NULL;
	int res = -1;

	if (!acf)
		return -1;

	/* try to lock functions list ... */
	if (ast_mutex_lock(&acflock)) {
		ast_log(LOG_ERROR, "Unable to lock function list\n");
		return -1;
	}

	for (acfptr = acf_root; acfptr; acfptr = acfptr->next) {
		if (acfptr == acf) {
			if (lastacf) {
				lastacf->next = acf->next;
			} else {
				acf_root = acf->next;
			}
			res = 0;
			break;
		}
		lastacf = acfptr;
	}

	ast_mutex_unlock(&acflock);

	if (!res && (option_verbose > 1))
		ast_verbose(VERBOSE_PREFIX_2 "Unregistered custom function %s\n", acf->name);

	return res;
}

int ast_custom_function_register(struct ast_custom_function *acf) 
{
	if (!acf)
		return -1;

	/* try to lock functions list ... */
	if (ast_mutex_lock(&acflock)) {
		ast_log(LOG_ERROR, "Unable to lock function list. Failed registering function %s\n", acf->name);
		return -1;
	}

	if (ast_custom_function_find(acf->name)) {
		ast_log(LOG_ERROR, "Function %s already registered.\n", acf->name);
		ast_mutex_unlock(&acflock);
		return -1;
	}

	acf->next = acf_root;
	acf_root = acf;

	ast_mutex_unlock(&acflock);

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered custom function %s\n", acf->name);

	return 0;
}

char *ast_func_read(struct ast_channel *chan, const char *in, char *workspace, size_t len)
{
	char *args = NULL, *function, *p;
	char *ret = "0";
	struct ast_custom_function *acfptr;

	function = ast_strdupa(in);
	if (!function) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return ret;
	}
	if ((args = strchr(function, '('))) {
		*args = '\0';
		args++;
		if ((p = strrchr(args, ')'))) {
			*p = '\0';
		} else {
			ast_log(LOG_WARNING, "Can't find trailing parenthesis?\n");
		}
	} else {
		ast_log(LOG_WARNING, "Function doesn't contain parentheses.  Assuming null argument.\n");
	}

	if ((acfptr = ast_custom_function_find(function))) {
		/* run the custom function */
		if (acfptr->read) {
			return acfptr->read(chan, function, args, workspace, len);
		} else {
			ast_log(LOG_ERROR, "Function %s cannot be read\n", function);
		}
	} else {
		ast_log(LOG_ERROR, "Function %s not registered\n", function);
	}
	return ret;
}

void ast_func_write(struct ast_channel *chan, const char *in, const char *value)
{
	char *args = NULL, *function, *p;
	struct ast_custom_function *acfptr;

	function = ast_strdupa(in);
	if (!function) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return;
	}
	if ((args = strchr(function, '('))) {
		*args = '\0';
		args++;
		if ((p = strrchr(args, ')'))) {
			*p = '\0';
		} else {
			ast_log(LOG_WARNING, "Can't find trailing parenthesis?\n");
		}
	} else {
		ast_log(LOG_WARNING, "Function doesn't contain parentheses.  Assuming null argument.\n");
	}

	if ((acfptr = ast_custom_function_find(function))) {
		/* run the custom function */
		if (acfptr->write) {
			acfptr->write(chan, function, args, value);
		} else {
			ast_log(LOG_ERROR, "Function %s is read-only, it cannot be written to\n", function);
		}
	} else {
		ast_log(LOG_ERROR, "Function %s not registered\n", function);
	}
}

static void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int count)
{
	char *cp4;
	const char *tmp, *whereweare;
	int length, offset, offset2, isfunction;
	char *workspace = NULL;
	char *ltmp = NULL, *var = NULL;
	char *nextvar, *nextexp, *nextthing;
	char *vars, *vare;
	int pos, brackets, needsub, len;
	
	/* Substitutes variables into cp2, based on string cp1, and assuming cp2 to be
	   zero-filled */
	whereweare=tmp=cp1;
	while(!ast_strlen_zero(whereweare) && count) {
		/* Assume we're copying the whole remaining string */
		pos = strlen(whereweare);
		nextvar = NULL;
		nextexp = NULL;
		nextthing = strchr(whereweare, '$');
		if (nextthing) {
			switch(nextthing[1]) {
			case '{':
				nextvar = nextthing;
				pos = nextvar - whereweare;
				break;
			case '[':
				nextexp = nextthing;
				pos = nextexp - whereweare;
				break;
			}
		}

		if (pos) {
			/* Can't copy more than 'count' bytes */
			if (pos > count)
				pos = count;
			
			/* Copy that many bytes */
			memcpy(cp2, whereweare, pos);
			
			count -= pos;
			cp2 += pos;
			whereweare += pos;
		}
		
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

			/* Skip totally over variable string */
			whereweare += (len + 3);

			if (!var)
				var = alloca(VAR_BUF_SIZE);

			/* Store variable name (and truncate) */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				if (!ltmp)
					ltmp = alloca(VAR_BUF_SIZE);

				memset(ltmp, 0, VAR_BUF_SIZE);
				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1);
				vars = ltmp;
			} else {
				vars = var;
			}

			if (!workspace)
				workspace = alloca(VAR_BUF_SIZE);

			workspace[0] = '\0';

			parse_variable_name(vars, &offset, &offset2, &isfunction);
			if (isfunction) {
				/* Evaluate function */
				cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE);

				ast_log(LOG_DEBUG, "Function result is '%s'\n", cp4 ? cp4 : "(null)");
			} else {
				/* Retrieve variable value */
				pbx_retrieve_variable(c, vars, &cp4, workspace, VAR_BUF_SIZE, headp);
			}
			if (cp4) {
				cp4 = substring(cp4, offset, offset2, workspace, VAR_BUF_SIZE);

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
			
			/* Skip totally over expression */
			whereweare += (len + 3);
			
			if (!var)
				var = alloca(VAR_BUF_SIZE);

			/* Store variable name (and truncate) */
			ast_copy_string(var, vars, len + 1);
			
			/* Substitute if necessary */
			if (needsub) {
				if (!ltmp)
					ltmp = alloca(VAR_BUF_SIZE);

				memset(ltmp, 0, VAR_BUF_SIZE);
				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1);
				vars = ltmp;
			} else {
				vars = var;
			}

			length = ast_expr(vars, cp2, count);

			if (length) {
				ast_log(LOG_DEBUG, "Expression result is '%s'\n", cp2);
				count -= length;
				cp2 += length;
			}
		} else
			break;
	}
}

void pbx_substitute_variables_helper(struct ast_channel *c, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(c, (c) ? &c->varshead : NULL, cp1, cp2, count);
}

void pbx_substitute_variables_varshead(struct varshead *headp, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(NULL, headp, cp1, cp2, count);
}

static void pbx_substitute_variables(char *passdata, int datalen, struct ast_channel *c, struct ast_exten *e)
{
	memset(passdata, 0, datalen);
		
	/* No variables or expressions in e->data, so why scan it? */
	if (!strchr(e->data, '$') && !strstr(e->data,"${") && !strstr(e->data,"$[") && !strstr(e->data,"$(")) {
		ast_copy_string(passdata, e->data, datalen);
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
	const char *foundcontext=NULL;
	int newstack = 0;
	int res;
	int status = 0;
	char *incstack[AST_PBX_MAX_STACK];
	char passdata[EXT_DATA_SIZE];
	int stacklen = 0;
	char tmp[80];
	char tmp2[80];
	char tmp3[EXT_DATA_SIZE];
	char atmp[80];
	char atmp2[EXT_DATA_SIZE+100];

	if (ast_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		if ((action == HELPER_EXISTS) || (action == HELPER_CANMATCH) || (action == HELPER_MATCHMORE))
			return 0;
		else
			return -1;
	}
	e = pbx_find_extension(c, con, context, exten, priority, label, callerid, action, incstack, &stacklen, &status, &sw, &data, &foundcontext);
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
					ast_copy_string(c->context, context, sizeof(c->context));
				if (c->exten != exten)
					ast_copy_string(c->exten, exten, sizeof(c->exten));
				c->priority = priority;
				pbx_substitute_variables(passdata, sizeof(passdata), c, e);
				if (option_debug) {
						ast_log(LOG_DEBUG, "Launching '%s'\n", app->name);
						snprintf(atmp, 80, "STACK-%s-%s-%d", context, exten, priority);
						snprintf(atmp2, EXT_DATA_SIZE+100, "%s(\"%s\", \"%s\") %s", app->name, c->name, passdata, (newstack ? "in new stack" : "in same stack"));
						pbx_builtin_setvar_helper(c, atmp, atmp2);
				}
				if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Executing %s(\"%s\", \"%s\") %s\n", 
								term_color(tmp, app->name, COLOR_BRCYAN, 0, sizeof(tmp)),
								term_color(tmp2, c->name, COLOR_BRMAGENTA, 0, sizeof(tmp2)),
								term_color(tmp3, passdata, COLOR_BRMAGENTA, 0, sizeof(tmp3)),
								(newstack ? "in new stack" : "in same stack"));
				manager_event(EVENT_FLAG_CALL, "Newexten", 
					"Channel: %s\r\n"
					"Context: %s\r\n"
					"Extension: %s\r\n"
					"Priority: %d\r\n"
					"Application: %s\r\n"
					"AppData: %s\r\n"
					"Uniqueid: %s\r\n",
					c->name, c->context, c->exten, c->priority, app->name, passdata, c->uniqueid);
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
				res = sw->exec(c, foundcontext ? foundcontext : context, exten, priority, callerid, newstack, data);
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

/*! \brief  ast_hint_extension: Find hint for given extension in context */
static struct ast_exten *ast_hint_extension(struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	struct ast_switch *sw;
	char *data;
	const char *foundcontext = NULL;
	int status = 0;
	char *incstack[AST_PBX_MAX_STACK];
	int stacklen = 0;

	if (ast_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		return NULL;
	}
	e = pbx_find_extension(c, NULL, context, exten, PRIORITY_HINT, NULL, "", HELPER_EXISTS, incstack, &stacklen, &status, &sw, &data, &foundcontext);
	ast_mutex_unlock(&conlock);	
	return e;
}

/*! \brief  ast_extensions_state2: Check state of extension by using hints */
static int ast_extension_state2(struct ast_exten *e)
{
	char hint[AST_MAX_EXTENSION] = "";    
	char *cur, *rest;
	int res = -1;
	int allunavailable = 1, allbusy = 1, allfree = 1;
	int busy = 0, inuse = 0, ring = 0;

	if (!e)
		return -1;

	ast_copy_string(hint, ast_get_extension_app(e), sizeof(hint));

	cur = hint;    	/* On or more devices separated with a & character */
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
			inuse = 1;
			allunavailable = 0;
			allfree = 0;
			break;
		case AST_DEVICE_RINGING:
			ring = 1;
			allunavailable = 0;
			allfree = 0;
			break;
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

	if (!inuse && ring)
		return AST_EXTENSION_RINGING;
	if (inuse && ring)
		return (AST_EXTENSION_INUSE | AST_EXTENSION_RINGING);
	if (inuse)
		return AST_EXTENSION_INUSE;
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

/*! \brief  ast_extension_state2str: Return extension_state as string */
const char *ast_extension_state2str(int extension_state)
{
	int i;

	for (i = 0; (i < (sizeof(extension_states) / sizeof(extension_states[0]))); i++) {
		if (extension_states[i].extension_state == extension_state) {
			return extension_states[i].text;
		}
	}
	return "Unknown";	
}

/*! \brief  ast_extension_state: Check extension state for an extension by using hint */
int ast_extension_state(struct ast_channel *c, char *context, char *exten)
{
	struct ast_exten *e;

	e = ast_hint_extension(c, context, exten);	/* Do we have a hint for this extension ? */ 
	if (!e) 
		return -1;				/* No hint, return -1 */

	return ast_extension_state2(e);    		/* Check all devices in the hint */
}

void ast_hint_state_changed(const char *device)
{
	struct ast_hint *hint;
	struct ast_state_cb *cblist;
	char buf[AST_MAX_EXTENSION];
	char *parse;
	char *cur;
	int state;

	ast_mutex_lock(&hintlock);

	for (hint = hints; hint; hint = hint->next) {
		ast_copy_string(buf, ast_get_extension_app(hint->exten), sizeof(buf));
		parse = buf;
		for (cur = strsep(&parse, "&"); cur; cur = strsep(&parse, "&")) {
			if (strcasecmp(cur, device))
				continue;

			/* Get device state for this hint */
			state = ast_extension_state2(hint->exten);
			
			if ((state == -1) || (state == hint->laststate))
				continue;

			/* Device state changed since last check - notify the watchers */
			
			/* For general callbacks */
			for (cblist = statecbs; cblist; cblist = cblist->next)
				cblist->callback(hint->exten->parent->name, hint->exten->exten, state, cblist->data);
			
			/* For extension callbacks */
			for (cblist = hint->callbacks; cblist; cblist = cblist->next)
				cblist->callback(hint->exten->parent->name, hint->exten->exten, state, cblist->data);
			
			hint->laststate = state;
			break;
		}
	}

	ast_mutex_unlock(&hintlock);
}
			
/*! \brief  ast_extension_state_add: Add watcher for extension states */
int ast_extension_state_add(const char *context, const char *exten, 
			    ast_state_cb_type callback, void *data)
{
	struct ast_hint *list;
	struct ast_state_cb *cblist;
	struct ast_exten *e;

	/* If there's no context and extension:  add callback to statecbs list */
	if (!context && !exten) {
		ast_mutex_lock(&hintlock);

		cblist = statecbs;
		while (cblist) {
			if (cblist->callback == callback) {
				cblist->data = data;
				ast_mutex_unlock(&hintlock);
				return 0;
			}
			cblist = cblist->next;
		}
	
		/* Now insert the callback */
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

	/* This callback type is for only one hint, so get the hint */
	e = ast_hint_extension(NULL, context, exten);    
	if (!e) {
		return -1;
	}

	/* Find the hint in the list of hints */
	ast_mutex_lock(&hintlock);
	list = hints;        

	while (list) {
		if (list->exten == e)
			break;	    
		list = list->next;    
	}

	if (!list) {
		/* We have no hint, sorry */
		ast_mutex_unlock(&hintlock);
		return -1;
	}

	/* Now insert the callback in the callback list  */
	cblist = malloc(sizeof(struct ast_state_cb));
	if (!cblist) {
		ast_mutex_unlock(&hintlock);
		return -1;
	}
	memset(cblist, 0, sizeof(struct ast_state_cb));
	cblist->id = stateid++;		/* Unique ID for this callback */
	cblist->callback = callback;	/* Pointer to callback routine */
	cblist->data = data;		/* Data for the callback */

	cblist->next = list->callbacks;
	list->callbacks = cblist;

	ast_mutex_unlock(&hintlock);
	return cblist->id;
}

/*! \brief  ast_extension_state_del: Remove a watcher from the callback list */
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
	/* Find the callback based on ID */
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

/*! \brief  ast_add_hint: Add hint to hint list, check initial extension state */
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
			if (option_debug > 1)
				ast_log(LOG_DEBUG, "HINTS: Not re-adding existing hint %s: %s\n", ast_get_extension_name(e), ast_get_extension_app(e));
			return -1;
		}
		list = list->next;    
	}

	if (option_debug > 1)
		ast_log(LOG_DEBUG, "HINTS: Adding hint %s: %s\n", ast_get_extension_name(e), ast_get_extension_app(e));

	list = malloc(sizeof(struct ast_hint));
	if (!list) {
		ast_mutex_unlock(&hintlock);
		if (option_debug > 1)
			ast_log(LOG_DEBUG, "HINTS: Out of memory...\n");
		return -1;
	}
	/* Initialize and insert new item at the top */
	memset(list, 0, sizeof(struct ast_hint));
	list->exten = e;
	list->laststate = ast_extension_state2(e);
	list->next = hints;
	hints = list;

	ast_mutex_unlock(&hintlock);
	return 0;
}

/*! \brief  ast_change_hint: Change hint for an extension */
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

/*! \brief  ast_remove_hint: Remove hint from extension */
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
				cbprev->callback(list->exten->parent->name, list->exten->exten, AST_EXTENSION_DEACTIVATED, cbprev->data);
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


/*! \brief  ast_get_hint: Get hint for channel */
int ast_get_hint(char *hint, int hintsize, char *name, int namesize, struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	void *tmp;

	e = ast_hint_extension(c, context, exten);
	if (e) {
		if (hint) 
		    ast_copy_string(hint, ast_get_extension_app(e), hintsize);
		if (name) {
			tmp = ast_get_extension_app_data(e);
			if (tmp)
				ast_copy_string(name, (char *) tmp, namesize);
		}
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

static int __ast_pbx_run(struct ast_channel *c)
{
	int firstpass = 1;
	int digit;
	char exten[256];
	int pos;
	int waittime;
	int res=0;
	int autoloopflag;

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

	autoloopflag = ast_test_flag(c, AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(c, AST_FLAG_IN_AUTOLOOP);

	/* Start by trying whatever the channel is set to */
	if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
		/* If not successful fall back to 's' */
		if (option_verbose > 1)
			ast_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d failed so falling back to exten 's'\n", c->name, c->context, c->exten, c->priority);
		ast_copy_string(c->exten, "s", sizeof(c->exten));
		if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			/* JK02: And finally back to default if everything else failed */
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d still failed so falling back to context 'default'\n", c->name, c->context, c->exten, c->priority);
			ast_copy_string(c->context, "default", sizeof(c->context));
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
				ast_copy_string(c->exten, "T", sizeof(c->exten));
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
				ast_copy_string(c->exten, "i", sizeof(c->exten));
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
					ast_copy_string(c->exten, exten, sizeof(c->exten));
					c->priority = 1;
				} else {
					/* No such extension */
					if (!ast_strlen_zero(exten)) {
						/* An invalid extension */
						if (ast_exists_extension(c, c->context, "i", 1, c->cid.cid_num)) {
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "Invalid extension '%s' in context '%s' on %s\n", exten, c->context, c->name);
							pbx_builtin_setvar_helper(c, "INVALID_EXTEN", exten);
							ast_copy_string(c->exten, "i", sizeof(c->exten));
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
							ast_copy_string(c->exten, "t", sizeof(c->exten));
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
				const char *status;

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
	ast_set2_flag(c, autoloopflag, AST_FLAG_IN_AUTOLOOP);

	pbx_destroy(c->pbx);
	c->pbx = NULL;
	if (res != AST_PBX_KEEPALIVE)
		ast_hangup(c);
	return 0;
}

/* Returns 0 on success, non-zero if call limit was reached */
static int increase_call_count(const struct ast_channel *c)
{
	int failed = 0;
	double curloadavg;
	ast_mutex_lock(&maxcalllock);
	if (option_maxcalls) {
		if (countcalls >= option_maxcalls) {
			ast_log(LOG_NOTICE, "Maximum call limit of %d calls exceeded by '%s'!\n", option_maxcalls, c->name);
			failed = -1;
		}
	}
	if (option_maxload) {
		getloadavg(&curloadavg, 1);
		if (curloadavg >= option_maxload) {
			ast_log(LOG_NOTICE, "Maximum loadavg limit of %f load exceeded by '%s' (currently %f)!\n", option_maxload, c->name, curloadavg);
			failed = -1;
		}
	}
	if (!failed)
		countcalls++;	
	ast_mutex_unlock(&maxcalllock);

	return failed;
}

static void decrease_call_count(void)
{
	ast_mutex_lock(&maxcalllock);
	if (countcalls > 0)
		countcalls--;
	ast_mutex_unlock(&maxcalllock);
}

static void *pbx_thread(void *data)
{
	/* Oh joyeous kernel, we're a new thread, with nothing to do but
	   answer this channel and get it going.
	*/
	/* NOTE:
	   The launcher of this function _MUST_ increment 'countcalls'
	   before invoking the function; it will be decremented when the
	   PBX has finished running on the channel
	 */
	struct ast_channel *c = data;

	__ast_pbx_run(c);
	decrease_call_count();

	pthread_exit(NULL);

	return NULL;
}

enum ast_pbx_result ast_pbx_start(struct ast_channel *c)
{
	pthread_t t;
	pthread_attr_t attr;

	if (!c) {
		ast_log(LOG_WARNING, "Asked to start thread on NULL channel?\n");
		return AST_PBX_FAILED;
	}
	   
	if (increase_call_count(c))
		return AST_PBX_CALL_LIMIT;

	/* Start a new thread, and get something handling this channel. */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (ast_pthread_create(&t, &attr, pbx_thread, c)) {
		ast_log(LOG_WARNING, "Failed to create new channel thread\n");
		return AST_PBX_FAILED;
	}

	return AST_PBX_SUCCESS;
}

enum ast_pbx_result ast_pbx_run(struct ast_channel *c)
{
	enum ast_pbx_result res = AST_PBX_SUCCESS;

	if (increase_call_count(c))
		return AST_PBX_CALL_LIMIT;

	res = __ast_pbx_run(c);
	decrease_call_count();

	return res;
}

int ast_active_calls(void)
{
	return countcalls;
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

/*!
 * \note This function locks contexts list by &conlist, search for the rigt context
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

/*!
 * \brief This function locks given context, removes switch, unlock context and
 * return.
 * \note When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
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
 * \note This functions lock contexts list, search for the right context,
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

/*!
 * \brief This functionc locks given context, search for the right extension and
 * fires out all peer in this extensions with given priority. If priority
 * is set to 0, all peers are removed. After that, unlock context and
 * return.
 * \note When do you want to call this function, make sure that &conlock is locked,
 * because some process can handle with your *con context before you lock
 * it.
 *
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


/*! \brief Dynamically register a new dial plan application */
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

static char show_functions_help[] =
"Usage: show functions [like <text>]\n"
"       List builtin functions, optionally only those matching a given string\n";

static char show_function_help[] =
"Usage: show function <function>\n"
"       Describe a particular dialplan function.\n";

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

static char show_hints_help[] = 
"Usage: show hints\n"
"       Show registered hints\n";


/*
 * IMPLEMENTATION OF CLI FUNCTIONS IS IN THE SAME ORDER AS COMMANDS HELPS
 *
 */

/*
 * \brief 'show application' CLI command implementation functions ...
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
					term_color(syntitle, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
					term_color(destitle, "[Description]\n", COLOR_MAGENTA, 0, 40);
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
						"[Synopsis]\n  %s\n\n"
						"[Description]\n%s\n",
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

/*! \brief  handle_show_hints: CLI support for listing registred dial plan hints */
static int handle_show_hints(int fd, int argc, char *argv[])
{
	struct ast_hint *hint;
	int num = 0;
	int watchers;
	struct ast_state_cb *watcher;

	if (!hints) {
		ast_cli(fd, "There are no registered dialplan hints\n");
		return RESULT_SUCCESS;
	}
	/* ... we have hints ... */
	ast_cli(fd, "\n    -= Registered Asterisk Dial Plan Hints =-\n");
	if (ast_mutex_lock(&hintlock)) {
		ast_log(LOG_ERROR, "Unable to lock hints\n");
		return -1;
	}
	hint = hints;
	while (hint) {
		watchers = 0;
		for (watcher = hint->callbacks; watcher; watcher = watcher->next)
			watchers++;
		ast_cli(fd, "   %-20.20s: %-20.20s  State:%-15.15s Watchers %2d\n",
			ast_get_extension_name(hint->exten), ast_get_extension_app(hint->exten),
			ast_extension_state2str(hint->laststate), watchers);
		num++;
		hint = hint->next;
	}
	ast_cli(fd, "----------------\n");
	ast_cli(fd, "- %d hints registered\n", num);
	ast_mutex_unlock(&hintlock);
	return RESULT_SUCCESS;
}

/*! \brief  handle_show_switches: CLI support for listing registred dial plan switches */
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
	int total_match = 0; 	/* Number of matches in like clause */
	int total_apps = 0; 	/* Number of apps registered */
	
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
		total_apps++;
		if (like) {
			if (strcasestr(a->name, argv[3])) {
				printapp = 1;
				total_match++;
			}
		} else if (describing) {
			if (a->description) {
				/* Match all words on command line */
				int i;
				printapp = 1;
				for (i=3; i<argc; i++) {
					if (!strcasestr(a->description, argv[i])) {
						printapp = 0;
					} else {
						total_match++;
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
	if ((!like) && (!describing)) {
		ast_cli(fd, "    -= %d Applications Registered =-\n",total_apps);
	} else {
		ast_cli(fd, "    -= %d Applications Matching =-\n",total_match);
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

struct dialplan_counters {
	int total_context;
	int total_exten;
	int total_prio;
	int context_existence;
	int extension_existence;
};

static int show_dialplan_helper(int fd, char *context, char *exten, struct dialplan_counters *dpc, struct ast_include *rinclude, int includecount, char *includes[])
{
	struct ast_context *c;
	int res=0, old_total_exten = dpc->total_exten;

	/* try to lock contexts */
	if (ast_lock_contexts()) {
		ast_log(LOG_WARNING, "Failed to lock contexts list\n");
		return -1;
	}

	/* walk all contexts ... */
	for (c = ast_walk_contexts(NULL); c ; c = ast_walk_contexts(c)) {
		/* show this context? */
		if (!context ||
			!strcmp(ast_get_context_name(c), context)) {
			dpc->context_existence = 1;

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
					dpc->total_context++;
					ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
					context_info_printed = 1;
				}

				/* walk extensions ... */
				for (e = ast_walk_context_extensions(c, NULL); e; e = ast_walk_context_extensions(c, e)) {
					struct ast_exten *p;
					int prio;

					/* looking for extension? is this our extension? */
					if (exten &&
						!ast_extension_match(ast_get_extension_name(e), exten))
					{
						/* we are looking for extension and it's not our
 						 * extension, so skip to next extension */
						continue;
					}

					dpc->extension_existence = 1;

					/* may we print context info? */	
					if (!context_info_printed) {
						dpc->total_context++;
						if (rinclude) {
							/* TODO Print more info about rinclude */
							ast_cli(fd, "[ Included context '%s' created by '%s' ]\n",
								ast_get_context_name(c),
								ast_get_context_registrar(c));
						} else {
							ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
								ast_get_context_name(c),
								ast_get_context_registrar(c));
						}
						context_info_printed = 1;
					}
					dpc->total_prio++;

					/* write extension name and first peer */	
					bzero(buf, sizeof(buf));		
					snprintf(buf, sizeof(buf), "'%s' =>",
						ast_get_extension_name(e));

					prio = ast_get_extension_priority(e);
					if (prio == PRIORITY_HINT) {
						snprintf(buf2, sizeof(buf2),
							"hint: %s",
							ast_get_extension_app(e));
					} else {
						snprintf(buf2, sizeof(buf2),
							"%d. %s(%s)",
							prio,
							ast_get_extension_app(e),
							(char *)ast_get_extension_app_data(e));
					}

					ast_cli(fd, "  %-17s %-45s [%s]\n", buf, buf2,
						ast_get_extension_registrar(e));

					dpc->total_exten++;
					/* walk next extension peers */
					for (p=ast_walk_extension_priorities(e, e); p; p=ast_walk_extension_priorities(e, p)) {
						dpc->total_prio++;
						bzero((void *)buf2, sizeof(buf2));
						bzero((void *)buf, sizeof(buf));
						if (ast_get_extension_label(p))
							snprintf(buf, sizeof(buf), "   [%s]", ast_get_extension_label(p));
						prio = ast_get_extension_priority(p);
						if (prio == PRIORITY_HINT) {
							snprintf(buf2, sizeof(buf2),
								"hint: %s",
								ast_get_extension_app(p));
						} else {
							snprintf(buf2, sizeof(buf2),
								"%d. %s(%s)",
								prio,
								ast_get_extension_app(p),
								(char *)ast_get_extension_app_data(p));
						}

						ast_cli(fd,"  %-17s %-45s [%s]\n",
							buf, buf2,
							ast_get_extension_registrar(p));
					}
				}

				/* walk included and write info ... */
				for (i = ast_walk_context_includes(c, NULL); i; i = ast_walk_context_includes(c, i)) {
					bzero(buf, sizeof(buf));
					snprintf(buf, sizeof(buf), "'%s'",
						ast_get_include_name(i));
					if (exten) {
						/* Check all includes for the requested extension */
						if (includecount >= AST_PBX_MAX_STACK) {
							ast_log(LOG_NOTICE, "Maximum include depth exceeded!\n");
						} else {
							int dupe=0;
							int x;
							for (x=0;x<includecount;x++) {
								if (!strcasecmp(includes[x], ast_get_include_name(i))) {
									dupe++;
									break;
								}
							}
							if (!dupe) {
								includes[includecount] = (char *)ast_get_include_name(i);
								show_dialplan_helper(fd, (char *)ast_get_include_name(i), exten, dpc, i, includecount + 1, includes);
							} else {
								ast_log(LOG_WARNING, "Avoiding circular include of %s within %s\n", ast_get_include_name(i), context);
							}
						}
					} else {
						ast_cli(fd, "  Include =>        %-45s [%s]\n",
							buf, ast_get_include_registrar(i));
					}
				}

				/* walk ignore patterns and write info ... */
				for (ip=ast_walk_context_ignorepats(c, NULL); ip; ip=ast_walk_context_ignorepats(c, ip)) {
					const char *ipname = ast_get_ignorepat_name(ip);
					char ignorepat[AST_MAX_EXTENSION];
					snprintf(buf, sizeof(buf), "'%s'", ipname);
					snprintf(ignorepat, sizeof(ignorepat), "_%s.", ipname);
					if ((!exten) || ast_extension_match(ignorepat, exten)) {
						ast_cli(fd, "  Ignore pattern => %-45s [%s]\n",
							buf, ast_get_ignorepat_registrar(ip));
					}
				}
				if (!rinclude) {
					for (sw = ast_walk_context_switches(c, NULL); sw; sw = ast_walk_context_switches(c, sw)) {
						snprintf(buf, sizeof(buf), "'%s/%s'",
							ast_get_switch_name(sw),
							ast_get_switch_data(sw));
						ast_cli(fd, "  Alt. Switch =>    %-45s [%s]\n",
							buf, ast_get_switch_registrar(sw));	
					}
				}
	
				ast_unlock_context(c);

				/* if we print something in context, make an empty line */
				if (context_info_printed) ast_cli(fd, "\r\n");
			}
		}
	}
	ast_unlock_contexts();

	if (dpc->total_exten == old_total_exten) {
		/* Nothing new under the sun */
		return -1;
	} else {
		return res;
	}
}

static int handle_show_dialplan(int fd, int argc, char *argv[])
{
	char *exten = NULL, *context = NULL;
	/* Variables used for different counters */
	struct dialplan_counters counters;
	char *incstack[AST_PBX_MAX_STACK];
	memset(&counters, 0, sizeof(counters));

	if (argc != 2 && argc != 3) 
		return RESULT_SHOWUSAGE;

	/* we obtain [exten@]context? if yes, split them ... */
	if (argc == 3) {
		char *splitter = ast_strdupa(argv[2]);
		/* is there a '@' character? */
		if (splitter && strchr(argv[2], '@')) {
			/* yes, split into exten & context ... */
			exten   = strsep(&splitter, "@");
			context = splitter;

			/* check for length and change to NULL if ast_strlen_zero() */
			if (ast_strlen_zero(exten))
				exten = NULL;
			if (ast_strlen_zero(context))
				context = NULL;
			show_dialplan_helper(fd, context, exten, &counters, NULL, 0, incstack);
		} else {
			/* no '@' char, only context given */
			context = argv[2];
			if (ast_strlen_zero(context))
				context = NULL;
			show_dialplan_helper(fd, context, exten, &counters, NULL, 0, incstack);
		}
	} else {
		/* Show complete dial plan */
		show_dialplan_helper(fd, NULL, NULL, &counters, NULL, 0, incstack);
	}

	/* check for input failure and throw some error messages */
	if (context && !counters.context_existence) {
		ast_cli(fd, "There is no existence of '%s' context\n", context);
		return RESULT_FAILURE;
	}

	if (exten && !counters.extension_existence) {
		if (context)
			ast_cli(fd, "There is no existence of %s@%s extension\n",
				exten, context);
		else
			ast_cli(fd,
				"There is no existence of '%s' extension in all contexts\n",
				exten);
		return RESULT_FAILURE;
	}

	ast_cli(fd,"-= %d %s (%d %s) in %d %s. =-\n",
				counters.total_exten, counters.total_exten == 1 ? "extension" : "extensions",
				counters.total_prio, counters.total_prio == 1 ? "priority" : "priorities",
				counters.total_context, counters.total_context == 1 ? "context" : "contexts");

	/* everything ok */
	return RESULT_SUCCESS;
}

/*
 * CLI entries for upper commands ...
 */
static struct ast_cli_entry pbx_cli[] = {
	{ { "show", "applications", NULL }, handle_show_applications,
	  "Shows registered dialplan applications", show_applications_help, complete_show_applications },
	{ { "show", "functions", NULL }, handle_show_functions,
	  "Shows registered dialplan functions", show_functions_help },
	{ { "show" , "function", NULL }, handle_show_function,
	  "Describe a specific dialplan function", show_function_help, complete_show_function },
	{ { "show", "application", NULL }, handle_show_application,
	  "Describe a specific dialplan application", show_application_help, complete_show_application },
	{ { "show", "dialplan", NULL }, handle_show_dialplan,
	  "Show dialplan", show_dialplan_help, complete_show_dialplan_context },
	{ { "show", "switches", NULL },	handle_show_switches,
	  "Show alternative switches", show_switches_help },
	{ { "show", "hints", NULL }, handle_show_hints,
	  "Show dialplan hints", show_hints_help },
};

int ast_unregister_application(const char *app) 
{
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

struct store_hint {
	char *context;
	char *exten;
	struct ast_state_cb *callbacks;
	int laststate;
	AST_LIST_ENTRY(store_hint) list;
	char data[1];
};

AST_LIST_HEAD(store_hints, store_hint);

void ast_merge_contexts_and_delete(struct ast_context **extcontexts, const char *registrar)
{
	struct ast_context *tmp, *lasttmp = NULL;
	struct store_hints store;
	struct store_hint *this;
	struct ast_hint *hint;
	struct ast_exten *exten;
	int length;
	struct ast_state_cb *thiscb, *prevcb;

	/* preserve all watchers for hints associated with this registrar */
	AST_LIST_HEAD_INIT(&store);
	ast_mutex_lock(&hintlock);
	for (hint = hints; hint; hint = hint->next) {
		if (hint->callbacks && !strcmp(registrar, hint->exten->parent->registrar)) {
			length = strlen(hint->exten->exten) + strlen(hint->exten->parent->name) + 2 + sizeof(*this);
			this = calloc(1, length);
			if (!this) {
				ast_log(LOG_WARNING, "Could not allocate memory to preserve hint\n");
				continue;
			}
			this->callbacks = hint->callbacks;
			hint->callbacks = NULL;
			this->laststate = hint->laststate;
			this->context = this->data;
			strcpy(this->data, hint->exten->parent->name);
			this->exten = this->data + strlen(this->context) + 1;
			strcpy(this->exten, hint->exten->exten);
			AST_LIST_INSERT_HEAD(&store, this, list);
		}
	}
	ast_mutex_unlock(&hintlock);

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

	/* restore the watchers for hints that can be found; notify those that
	   cannot be restored
	*/
	while ((this = AST_LIST_REMOVE_HEAD(&store, list))) {
		exten = ast_hint_extension(NULL, this->context, this->exten);
		/* Find the hint in the list of hints */
		ast_mutex_lock(&hintlock);
		for (hint = hints; hint; hint = hint->next) {
			if (hint->exten == exten)
				break;
		}
		if (!exten || !hint) {
			/* this hint has been removed, notify the watchers */
			prevcb = NULL;
			thiscb = this->callbacks;
			while (thiscb) {
				prevcb = thiscb;	    
				thiscb = thiscb->next;
				prevcb->callback(this->context, this->exten, AST_EXTENSION_REMOVED, prevcb->data);
				free(prevcb);
	    		}
		} else {
			thiscb = this->callbacks;
			while (thiscb->next)
				thiscb = thiscb->next;
			thiscb->next = hint->callbacks;
			hint->callbacks = this->callbacks;
			hint->laststate = this->laststate;
		}
		ast_mutex_unlock(&hintlock);
		free(this);
	}

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
		for (x=0; x<24; x++)
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
	while(*e && !isdigit(*e)) 
		e++;
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
	for (cth=0; cth<24; cth++) {
		/* Initialize masks to blank */
		i->minmask[cth] = 0;
		for (ctm=0; ctm<30; ctm++) {
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

/*! \brief  get_dow: Get day of week */
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
	for (x=s; x!=e; x = (x + 1) % 31) {
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
	ast_copy_string(info_save, info_in, sizeof(info_save));
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
	while(*c && (*c != '|')) 
		c++; 
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
int ast_context_add_switch(const char *context, const char *sw, const char *data, int eval, const char *registrar)
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
			int ret = ast_context_add_switch2(c, sw, data, eval, registrar);
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
	const char *data, int eval, const char *registrar)
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
	if (eval) {
		/* Create buffer for evaluation of variables */
		length += SWITCH_DATA_LENGTH;
		length++;
	}

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
	if (data) {
		strcpy(new_sw->data, data);
		p += strlen(data) + 1;
	} else {
		strcpy(new_sw->data, "");
		p++;
	}
	if (eval) 
		new_sw->tmpdata = p;
	new_sw->next      = NULL;
	new_sw->eval	  = eval;
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

int ast_explicit_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	if (!chan)
		return -1;

	if (!ast_strlen_zero(context))
		ast_copy_string(chan->context, context, sizeof(chan->context));
	if (!ast_strlen_zero(exten))
		ast_copy_string(chan->exten, exten, sizeof(chan->exten));
	if (priority > -1) {
		chan->priority = priority;
		/* see flag description in channel.h for explanation */
		if (ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP))
			chan->priority--;
	}
	
	return 0;
}

int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	int res = 0;

	ast_mutex_lock(&chan->lock);

	if (chan->pbx) {
		/* This channel is currently in the PBX */
		ast_explicit_goto(chan, context, exten, priority);
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
			ast_explicit_goto(tmpchan,
					  (!ast_strlen_zero(context)) ? context : chan->context,
					  (!ast_strlen_zero(exten)) ? exten : chan->exten,
					  priority);

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

	chan = ast_get_channel_by_name_locked(channame);
	if (chan) {
		res = ast_async_goto(chan, context, exten, priority);
		ast_mutex_unlock(&chan->lock);
	}
	return res;
}

static int ext_strncpy(char *dst, const char *src, int len)
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

	return count;
}

static void null_datad(void *foo)
{
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
	if (datad == NULL)
		datad = null_datad;
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
		p += ext_strncpy(tmp->exten, extension, strlen(extension) + 1) + 1;
		tmp->priority = priority;
		tmp->cidmatch = p;
		if (callerid) {
			p += ext_strncpy(tmp->cidmatch, callerid, strlen(callerid) + 1) + 1;
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
	char context[AST_MAX_CONTEXT];
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
					ast_verbose(VERBOSE_PREFIX_3 "Launching %s(%s) on %s\n", as->app, as->appdata, chan->name);
				pbx_exec(chan, app, as->appdata, 1);
			} else
				ast_log(LOG_WARNING, "No such application '%s'\n", as->app);
		} else {
			if (!ast_strlen_zero(as->context))
				ast_copy_string(chan->context, as->context, sizeof(chan->context));
			if (!ast_strlen_zero(as->exten))
				ast_copy_string(chan->exten, as->exten, sizeof(chan->exten));
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
	ast_cdr_detach(chan->cdr);      /* post and free the record */
	ast_channel_free(chan);         /* free the channel */
	
	return 0;  /* success */
}

int ast_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, struct ast_variable *vars, struct ast_channel **channel)
{
	struct ast_channel *chan;
	struct async_stat *as;
	int res = -1, cdr_res = -1;
	struct outgoing_helper oh;
	pthread_attr_t attr;

	if (sync) {
		LOAD_OH(oh);
		chan = __ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
		if (channel) {
			*channel = chan;
			if (chan)
				ast_mutex_lock(&chan->lock);
		}
		if (chan) {
			if(chan->cdr) { /* check if the channel already has a cdr record, if not give it one */
				ast_log(LOG_WARNING, "%s already has a call record??\n", chan->name);
			} else {
				chan->cdr = ast_cdr_alloc();   /* allocate a cdr for the channel */
				if (!chan->cdr) {
					/* allocation of the cdr failed */
					ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
					free(chan->pbx);
					res = -1;
					goto outgoing_exten_cleanup;
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
					if (channel)
						ast_mutex_unlock(&chan->lock);
					if (ast_pbx_run(chan)) {
						ast_log(LOG_ERROR, "Unable to run PBX on %s\n", chan->name);
						if (channel)
							*channel = NULL;
						ast_hangup(chan);
						res = -1;
					}
				} else {
					if (ast_pbx_start(chan)) {
						ast_log(LOG_ERROR, "Unable to start PBX on %s\n", chan->name);
						if (channel)
							*channel = NULL;
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
					if (ast_cdr_disposition(chan->cdr, chan->hangupcause))
						ast_cdr_failed(chan->cdr);
				}
			
				if (channel)
					*channel = NULL;
				ast_hangup(chan);
			}
		}

		if(res < 0) { /* the call failed for some reason */
			if (*reason == 0) { /* if the call failed (not busy or no answer)
				            * update the cdr with the failed message */
				cdr_res = ast_pbx_outgoing_cdr_failed();
				if (cdr_res != 0) {
					res = cdr_res;
					goto outgoing_exten_cleanup;
				}
			}
			
			/* create a fake channel and execute the "failed" extension (if it exists) within the requested context */
			/* check if "failed" exists */
			if (ast_exists_extension(chan, context, "failed", 1, NULL)) {
				chan = ast_channel_alloc(0);
				if (chan) {
					ast_copy_string(chan->name, "OutgoingSpoolFailed", sizeof(chan->name));
					if (!ast_strlen_zero(context))
						ast_copy_string(chan->context, context, sizeof(chan->context));
					ast_copy_string(chan->exten, "failed", sizeof(chan->exten));
					chan->priority = 1;
					ast_set_variables(chan, vars);
					ast_pbx_run(chan);	
				} else 
					ast_log(LOG_WARNING, "Can't allocate the channel structure, skipping execution of extension 'failed'\n");
			}
		}
	} else {
		as = malloc(sizeof(struct async_stat));
		if (!as) {
			res = -1;
			goto outgoing_exten_cleanup;
		}	
		memset(as, 0, sizeof(struct async_stat));
		chan = ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
		if (channel) {
			*channel = chan;
			if (chan)
				ast_mutex_lock(&chan->lock);
		}
		if (!chan) {
			free(as);
			res = -1;
			goto outgoing_exten_cleanup;
		}
		as->chan = chan;
		ast_copy_string(as->context, context, sizeof(as->context));
		ast_copy_string(as->exten,  exten, sizeof(as->exten));
		as->priority = priority;
		as->timeout = timeout;
		ast_set_variables(chan, vars);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&as->p, &attr, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			free(as);
			if (channel)
				*channel = NULL;
			ast_hangup(chan);
			res = -1;
			goto outgoing_exten_cleanup;
		}
		res = 0;
	}
outgoing_exten_cleanup:
	ast_variables_destroy(vars);
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
			ast_verbose(VERBOSE_PREFIX_4 "Launching %s(%s) on %s\n", tmp->app, tmp->data, tmp->chan->name);
		pbx_exec(tmp->chan, app, tmp->data, 1);
	} else
		ast_log(LOG_WARNING, "No such application '%s'\n", tmp->app);
	ast_hangup(tmp->chan);
	free(tmp);
	return NULL;
}

int ast_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, struct ast_variable *vars, struct ast_channel **locked_channel)
{
	struct ast_channel *chan;
	struct async_stat *as;
	struct app_tmp *tmp;
	int res = -1, cdr_res = -1;
	struct outgoing_helper oh;
	pthread_attr_t attr;
	
	memset(&oh, 0, sizeof(oh));
	oh.vars = vars;	

	if (locked_channel) 
		*locked_channel = NULL;
	if (ast_strlen_zero(app)) {
		res = -1;
		goto outgoing_app_cleanup;	
	}
	if (sync) {
		chan = __ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
		if (chan) {
			if (chan->cdr) { /* check if the channel already has a cdr record, if not give it one */
				ast_log(LOG_WARNING, "%s already has a call record??\n", chan->name);
			} else {
				chan->cdr = ast_cdr_alloc();   /* allocate a cdr for the channel */
				if(!chan->cdr) {
					/* allocation of the cdr failed */
					ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
					free(chan->pbx);
					res = -1;
					goto outgoing_app_cleanup;
				}
				/* allocation of the cdr was successful */
				ast_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
				ast_cdr_start(chan->cdr);
			}
			ast_set_variables(chan, vars);
			if (chan->_state == AST_STATE_UP) {
				res = 0;
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);
				tmp = malloc(sizeof(struct app_tmp));
				if (tmp) {
					memset(tmp, 0, sizeof(struct app_tmp));
					ast_copy_string(tmp->app, app, sizeof(tmp->app));
					if (appdata)
						ast_copy_string(tmp->data, appdata, sizeof(tmp->data));
					tmp->chan = chan;
					if (sync > 1) {
						if (locked_channel)
							ast_mutex_unlock(&chan->lock);
						ast_pbx_run_app(tmp);
					} else {
						pthread_attr_init(&attr);
						pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						if (locked_channel) 
							ast_mutex_lock(&chan->lock);
						if (ast_pthread_create(&tmp->t, &attr, ast_pbx_run_app, tmp)) {
							ast_log(LOG_WARNING, "Unable to spawn execute thread on %s: %s\n", chan->name, strerror(errno));
							free(tmp);
							if (locked_channel) 
								ast_mutex_unlock(&chan->lock);
							ast_hangup(chan);
							res = -1;
						} else {
							if (locked_channel) 
								*locked_channel = chan;
						}
					}
				} else {
					ast_log(LOG_ERROR, "Out of memory :(\n");
					res = -1;
				}
			} else {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was never answered.\n", chan->name);
				if (chan->cdr) { /* update the cdr */
					/* here we update the status of the call, which sould be busy.
					 * if that fails then we set the status to failed */
					if (ast_cdr_disposition(chan->cdr, chan->hangupcause))
						ast_cdr_failed(chan->cdr);
				}
				ast_hangup(chan);
			}
		}
		
		if (res < 0) { /* the call failed for some reason */
			if (*reason == 0) { /* if the call failed (not busy or no answer)
				            * update the cdr with the failed message */
				cdr_res = ast_pbx_outgoing_cdr_failed();
				if (cdr_res != 0) {
					res = cdr_res;
					goto outgoing_app_cleanup;
				}
			}
		}

	} else {
		as = malloc(sizeof(struct async_stat));
		if (!as) {
			res = -1;
			goto outgoing_app_cleanup;
		}
		memset(as, 0, sizeof(struct async_stat));
		chan = __ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
		if (!chan) {
			free(as);
			res = -1;
			goto outgoing_app_cleanup;
		}
		as->chan = chan;
		ast_copy_string(as->app, app, sizeof(as->app));
		if (appdata)
			ast_copy_string(as->appdata,  appdata, sizeof(as->appdata));
		as->timeout = timeout;
		ast_set_variables(chan, vars);
		/* Start a new thread, and get something handling this channel. */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (locked_channel) 
			ast_mutex_lock(&chan->lock);
		if (ast_pthread_create(&as->p, &attr, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			free(as);
			if (locked_channel) 
				ast_mutex_unlock(&chan->lock);
			ast_hangup(chan);
			res = -1;
			goto outgoing_app_cleanup;
		} else {
			if (locked_channel)
				*locked_channel = chan;
		}
		res = 0;
	}
outgoing_app_cleanup:
	ast_variables_destroy(vars);
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
	
	if (ast_strlen_zero(data) || (sscanf(data, "%d", &waittime) != 1) || (waittime < 0))
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

/*!
 * \ingroup applications
 */
static int pbx_builtin_progress(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_PROGRESS);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_ringing(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_RINGING);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_busy(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_BUSY);		
	ast_setstate(chan, AST_STATE_BUSY);
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_congestion(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	ast_setstate(chan, AST_STATE_BUSY);
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_answer(struct ast_channel *chan, void *data)
{
	int delay = 0;
	int res;

	if (chan->_state == AST_STATE_UP)
		delay = 0;
	else if (!ast_strlen_zero(data))
		delay = atoi(data);

	res = ast_answer(chan);
	if (res)
		return res;

	if (delay)
		res = ast_safe_sleep(chan, delay);

	return res;
}

AST_APP_OPTIONS(resetcdr_opts, {
	AST_APP_OPTION('w', AST_CDR_FLAG_POSTED),
	AST_APP_OPTION('a', AST_CDR_FLAG_LOCKED),
	AST_APP_OPTION('v', AST_CDR_FLAG_KEEP_VARS),
});

/*!
 * \ingroup applications
 */
static int pbx_builtin_resetcdr(struct ast_channel *chan, void *data)
{
	char *args;
	struct ast_flags flags = { 0 };
	
	if (!ast_strlen_zero(data)) {
		args = ast_strdupa(data);
		if (!args) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			return -1;
		}
		ast_app_parse_options(resetcdr_opts, &flags, NULL, args);
	}

	ast_cdr_reset(chan->cdr, &flags);

	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_setamaflags(struct ast_channel *chan, void *data)
{
	/* Copy the AMA Flags as specified */
	if (data)
		ast_cdr_setamaflags(chan, (char *)data);
	else
		ast_cdr_setamaflags(chan, "");
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_hangup(struct ast_channel *chan, void *data)
{
	/* Just return non-zero and it will hang up */
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_gotoiftime(struct ast_channel *chan, void *data)
{
	int res=0;
	char *s, *ts;
	struct ast_timing timing;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "GotoIfTime requires an argument:\n  <time range>|<days of week>|<days of month>|<months>?[[context|]extension|]priority\n");
		return -1;
	}

	if ((s = ast_strdupa((char *) data))) {
		ts = s;

		/* Separate the Goto path */
		strsep(&ts,"?");

		/* struct ast_include include contained garbage here, fixed by zeroing it on get_timerange */
		if (ast_build_timing(&timing, s) && ast_check_timing(&timing))
			res = pbx_builtin_goto(chan, (void *)ts);
	} else {
		ast_log(LOG_ERROR, "Memory Error!\n");
	}
	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_execiftime(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *ptr1, *ptr2;
	struct ast_timing timing;
	struct ast_app *app;
	const char *usage = "ExecIfTime requires an argument:\n  <time range>|<days of week>|<days of month>|<months>?<appname>[|<appargs>]";

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s\n", usage);	
		return -1;
	}

	ptr1 = ast_strdupa(data);

	if (!ptr1) {
		ast_log(LOG_ERROR, "Out of Memory!\n");
		return -1;	
	}

	ptr2 = ptr1;
	/* Separate the Application data ptr1 is the time spec ptr2 is the app|data */
	strsep(&ptr2,"?");
	if(!ast_build_timing(&timing, ptr1)) {
		ast_log(LOG_WARNING, "Invalid Time Spec: %s\nCorrect usage: %s\n", ptr1, usage);
		res = -1;
	}
		
	if (!res && ast_check_timing(&timing)) {
		if (!ptr2) {
			ast_log(LOG_WARNING, "%s\n", usage);
		}

		/* ptr2 is now the app name 
		   we're done with ptr1 now so recycle it and use it to point to the app args */
		if((ptr1 = strchr(ptr2, '|'))) {
			*ptr1 = '\0';
			ptr1++;
		}
		
		if ((app = pbx_findapp(ptr2))) {
			res = pbx_exec(chan, app, ptr1 ? ptr1 : "", 1);
		} else {
			ast_log(LOG_WARNING, "Cannot locate application %s\n", ptr2);
			res = -1;
		}
	}
	
	return res;
}

/*!
 * \ingroup applications
 */
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

/*!
 * \ingroup applications
 */
static int pbx_builtin_waitexten(struct ast_channel *chan, void *data)
{
	int ms, res, argc;
	char *args;
	char *argv[2];
	char *options = NULL; 
	char *timeout = NULL;
	struct ast_flags flags = {0};
	char *opts[1] = { NULL };

	args = ast_strdupa(data);

	if ((argc = ast_app_separate_args(args, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		if (argc > 0) {
			timeout = argv[0];
			if (argc > 1)
				options = argv[1];
		}
	}

	if (options)
		ast_app_parse_options(waitexten_opts, &flags, opts, options);
	
	if (ast_test_flag(&flags, WAITEXTEN_MOH))
		ast_moh_start(chan, opts[0]);

	/* Wait for "n" seconds */
	if (timeout && atof((char *)timeout)) 
		ms = atof((char *)timeout) * 1000;
	else if (chan->pbx)
		ms = chan->pbx->rtimeout * 1000;
	else
		ms = 10000;
	res = ast_waitfordigit(chan, ms);
	if (!res) {
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 1, chan->cid.cid_num)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Timeout on %s, continuing...\n", chan->name);
		} else if (ast_exists_extension(chan, chan->context, "t", 1, chan->cid.cid_num)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Timeout on %s, going to 't'\n", chan->name);
			ast_copy_string(chan->exten, "t", sizeof(chan->exten));
			chan->priority = 0;
		} else {
			ast_log(LOG_WARNING, "Timeout but no rule 't' in context '%s'\n", chan->context);
			res = -1;
		}
	}

	if (ast_test_flag(&flags, WAITEXTEN_MOH))
		ast_moh_stop(chan);

	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_background(struct ast_channel *chan, void *data)
{
	int res = 0;
	int argc;
	char *parse;
	char *argv[4];
	char *options = NULL; 
	char *filename = NULL;
	char *front = NULL, *back = NULL;
	char *lang = NULL;
	char *context = NULL;
	struct ast_flags flags = {0};

	parse = ast_strdupa(data);

	if ((argc = ast_app_separate_args(parse, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		switch (argc) {
		case 4:
			context = argv[3];
		case 3:
			lang = argv[2];
		case 2:
			options = argv[1];
		case 1:
			filename = argv[0];
			break;
		default:
			ast_log(LOG_WARNING, "Background requires an argument (filename)\n");
			break;
		}
	}

	if (!lang)
		lang = chan->language;

	if (!context)
		context = chan->context;

	if (options) {
		if (!strcasecmp(options, "skip"))
			flags.flags = BACKGROUND_SKIP;
		else if (!strcasecmp(options, "noanswer"))
			flags.flags = BACKGROUND_NOANSWER;
		else
			ast_app_parse_options(background_opts, &flags, NULL, options);
	}

	/* Answer if need be */
	if (chan->_state != AST_STATE_UP) {
		if (ast_test_flag(&flags, BACKGROUND_SKIP)) {
			return 0;
		} else if (!ast_test_flag(&flags, BACKGROUND_NOANSWER)) {
			res = ast_answer(chan);
		}
	}

	if (!res) {
		/* Stop anything playing */
		ast_stopstream(chan);
		/* Stream a file */
		front = filename;
		while(!res && front) {
			if((back = strchr(front, '&'))) {
				*back = '\0';
				back++;
			}
			res = ast_streamfile(chan, front, lang);
			if (!res) {
				if (ast_test_flag(&flags, BACKGROUND_PLAYBACK)) {
					res = ast_waitstream(chan, "");
				} else {
					if (ast_test_flag(&flags, BACKGROUND_MATCHEXTEN)) {
						res = ast_waitstream_exten(chan, context);
					} else {
						res = ast_waitstream(chan, AST_DIGIT_ANY);
					}
				}
				ast_stopstream(chan);
			} else {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char*)data);
				res = 0;
				break;
			}
			front = back;
		}
	}
	if (context != chan->context && res) {
		snprintf(chan->exten, sizeof(chan->exten), "%c", res);
		ast_copy_string(chan->context, context, sizeof(chan->context));
		chan->priority = 0;
		return 0;
	} else {
		return res;
	}
}

/*! Goto
 * \ingroup applications
 */
static int pbx_builtin_goto(struct ast_channel *chan, void *data)
{
	int res;
	res = ast_parseable_goto(chan, (const char *) data);
	if (!res && (option_verbose > 2))
		ast_verbose( VERBOSE_PREFIX_3 "Goto (%s,%s,%d)\n", chan->context,chan->exten, chan->priority+1);
	return res;
}


int pbx_builtin_serialize_variables(struct ast_channel *chan, char *buf, size_t size) 
{
	struct ast_var_t *variables;
	const char *var, *val;
	int total = 0;

	if (!chan)
		return 0;

	memset(buf, 0, size);

	AST_LIST_TRAVERSE(&chan->varshead, variables, entries) {
		if(variables &&
		   (var=ast_var_name(variables)) && (val=ast_var_value(variables)) &&
		   !ast_strlen_zero(var) && !ast_strlen_zero(val)) {
			if (ast_build_string(&buf, &size, "%s=%s\n", var, val)) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			} else
				total++;
		} else 
			break;
	}
	
	return total;
}

const char *pbx_builtin_getvar_helper(struct ast_channel *chan, const char *name) 
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

void pbx_builtin_pushvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;

	if (name[strlen(name)-1] == ')') {
		ast_log(LOG_WARNING, "Cannot push a value onto a function\n");
		return ast_func_write(chan, name, value);
	}

	headp = (chan) ? &chan->varshead : &globals;

	if (value) {
		if ((option_verbose > 1) && (headp == &globals))
			ast_verbose(VERBOSE_PREFIX_2 "Setting global variable '%s' to '%s'\n", name, value);
		newvariable = ast_var_assign(name, value);	
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}
}

void pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *nametail = name;

	if (name[strlen(name)-1] == ')')
		return ast_func_write(chan, name, value);

	headp = (chan) ? &chan->varshead : &globals;

	/* For comparison purposes, we have to strip leading underscores */
	if (*nametail == '_') {
		nametail++;
		if (*nametail == '_') 
			nametail++;
	}

	AST_LIST_TRAVERSE (headp, newvariable, entries) {
		if (strcasecmp(ast_var_name(newvariable), nametail) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE(headp, newvariable, entries);
			ast_var_delete(newvariable);
			break;
		}
	} 

	if (value) {
		if ((option_verbose > 1) && (headp == &globals))
			ast_verbose(VERBOSE_PREFIX_2 "Setting global variable '%s' to '%s'\n", name, value);
		newvariable = ast_var_assign(name, value);	
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}
}

int pbx_builtin_setvar(struct ast_channel *chan, void *data)
{
	char *name, *value, *mydata;
	int argc;
	char *argv[24];		/* this will only support a maximum of 24 variables being set in a single operation */
	int global = 0;
	int x;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Set requires at least one variable name/value pair.\n");
		return 0;
	}

	mydata = ast_strdupa(data);
	argc = ast_app_separate_args(mydata, '|', argv, sizeof(argv) / sizeof(argv[0]));

	/* check for a trailing flags argument */
	if ((argc > 1) && !strchr(argv[argc-1], '=')) {
		argc--;
		if (strchr(argv[argc], 'g'))
			global = 1;
	}

	for (x = 0; x < argc; x++) {
		name = argv[x];
		if ((value = strchr(name, '='))) {
			*value = '\0';
			value++;
			pbx_builtin_setvar_helper((global) ? NULL : chan, name, value);
		} else
			ast_log(LOG_WARNING, "Ignoring entry '%s' with no = (and not last 'options' entry)\n", name);
	}

	return(0);
}

int pbx_builtin_importvar(struct ast_channel *chan, void *data)
{
	char *name;
	char *value;
	char *stringp=NULL;
	char *channel;
	struct ast_channel *chan2;
	char tmp[VAR_BUF_SIZE]="";
	char *s;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}

	stringp = ast_strdupa(data);
	name = strsep(&stringp,"=");
	channel = strsep(&stringp,"|"); 
	value = strsep(&stringp,"\0");
	if (channel && value && name) {
		chan2 = ast_get_channel_by_name_locked(channel);
		if (chan2) {
			s = alloca(strlen(value) + 4);
			if (s) {
				sprintf(s, "${%s}", value);
				pbx_substitute_variables_helper(chan2, s, tmp, sizeof(tmp) - 1);
			}
			ast_mutex_unlock(&chan2->lock);
		}
		pbx_builtin_setvar_helper(chan, name, tmp);
	}

	return(0);
}

static int pbx_builtin_setglobalvar(struct ast_channel *chan, void *data)
{
	char *name;
	char *value;
	char *stringp = NULL;

	if (ast_strlen_zero(data)) {
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
		vardata = AST_LIST_REMOVE_HEAD(&globals, entries);
		ast_var_delete(vardata);
	}
}

static int pbx_checkcondition(char *condition) 
{
	if (condition) {
		if (*condition == '\0') {
			/* Empty strings are false */
			return 0;
		} else if (*condition >= '0' && *condition <= '9') {
			/* Numbers are evaluated for truth */
			return atoi(condition);
		} else {
			/* Strings are true */
			return 1;
		}
	} else {
		/* NULL is also false */
		return 0;
	}
}

static int pbx_builtin_gotoif(struct ast_channel *chan, void *data)
{
	char *condition, *branch1, *branch2, *branch;
	char *s;
	int rc;
	char *stringp=NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to check\n");
		return 0;
	}
	
	s = ast_strdupa(data);
	stringp = s;
	condition = strsep(&stringp,"?");
	branch1 = strsep(&stringp,":");
	branch2 = strsep(&stringp,"");
	branch = pbx_checkcondition(condition) ? branch1 : branch2;
	
	if (ast_strlen_zero(branch)) {
		ast_log(LOG_DEBUG, "Not taking any branch\n");
		return 0;
	}
	
	rc = pbx_builtin_goto(chan, branch);

	return rc;
}           

static int pbx_builtin_saynumber(struct ast_channel *chan, void *data)
{
	int res = 0;
	char tmp[256];
	char *number = (char *) NULL;
	char *options = (char *) NULL;

	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayNumber requires an argument (number)\n");
		return -1;
	}
	ast_copy_string(tmp, (char *) data, sizeof(tmp));
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
	AST_LIST_HEAD_INIT_NOLOCK(&globals);
	ast_cli_register_multiple(pbx_cli, sizeof(pbx_cli) / sizeof(pbx_cli[0]));

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
			ast_log(LOG_WARNING, "Context '%s' tries includes nonexistent context '%s'\n",
					ast_get_context_name(con), inc->rname);
		}
	return res;
}


static int __ast_goto_if_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, int async) 
{
	int (*goto_func)(struct ast_channel *chan, const char *context, const char *exten, int priority);

	if (!chan)
		return -2;

	goto_func = (async) ? ast_async_goto : ast_explicit_goto;
	if (ast_exists_extension(chan, context ? context : chan->context,
				 exten ? exten : chan->exten, priority,
				 chan->cid.cid_num))
		return goto_func(chan, context ? context : chan->context,
				 exten ? exten : chan->exten, priority);
	else 
		return -3;
}

int ast_goto_if_exists(struct ast_channel *chan, const char* context, const char *exten, int priority) {
	return __ast_goto_if_exists(chan, context, exten, priority, 0);
}

int ast_async_goto_if_exists(struct ast_channel *chan, const char * context, const char *exten, int priority) {
	return __ast_goto_if_exists(chan, context, exten, priority, 1);
}

int ast_parseable_goto(struct ast_channel *chan, const char *goto_string) 
{
	char *s;
	char *exten, *pri, *context;
	char *stringp=NULL;
	int ipri;
	int mode = 0;

	if (ast_strlen_zero(goto_string)) {
		ast_log(LOG_WARNING, "Goto requires an argument (optional context|optional extension|priority)\n");
		return -1;
	}
	s = ast_strdupa(goto_string);
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
	if (*pri == '+') {
		mode = 1;
		pri++;
	} else if (*pri == '-') {
		mode = -1;
		pri++;
	}
	if (sscanf(pri, "%d", &ipri) != 1) {
		if ((ipri = ast_findlabel_extension(chan, context ? context : chan->context, (exten && strcasecmp(exten, "BYEXTENSION")) ? exten : chan->exten, 
			pri, chan->cid.cid_num)) < 1) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0, or valid label\n", pri);
			return -1;
		} else
			mode = 0;
	} 
	/* At this point we have a priority and maybe an extension and a context */

	if (exten && !strcasecmp(exten, "BYEXTENSION"))
		exten = NULL;

	if (mode) 
		ipri = chan->priority + (ipri * mode);

	ast_explicit_goto(chan, context, exten, ipri);
	ast_cdr_update(chan);
	return 0;

}
