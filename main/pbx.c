/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>

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
#define	SAY_STUBS	/* generate declarations and stubs for say methods */
#include "asterisk/say.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"
#include "asterisk/stringfields.h"
#include "asterisk/threadstorage.h"
#include "asterisk/astobj2.h"

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
	AST_APP_OPTION_ARG('m', WAITEXTEN_MOH, 0),
});

struct ast_context;

AST_THREADSTORAGE(switch_data, switch_data_init);

/*!
   \brief ast_exten: An extension
	The dialplan is saved as a linked list with each context
	having it's own linked list of extensions - one item per
	priority.
*/
struct ast_exten {
	char *exten;			/*!< Extension name */
	int matchcid;			/*!< Match caller id ? */
	const char *cidmatch;		/*!< Caller id to match for this extension */
	int priority;			/*!< Priority */
	const char *label;		/*!< Label */
	struct ast_context *parent;	/*!< The context this extension belongs to  */
	const char *app; 		/*!< Application to execute */
	void *data;			/*!< Data to use (arguments) */
	void (*datad)(void *);		/*!< Data destructor */
	struct ast_exten *peer;		/*!< Next higher priority with our extension */
	const char *registrar;		/*!< Registrar */
	struct ast_exten *next;		/*!< Extension with a greater ID */
	char stuff[0];
};

/*! \brief ast_include: include= support in extensions.conf */
struct ast_include {
	const char *name;
	const char *rname;			/*!< Context to include */
	const char *registrar;			/*!< Registrar */
	int hastime;				/*!< If time construct exists */
	struct ast_timing timing;               /*!< time construct */
	struct ast_include *next;		/*!< Link them together */
	char stuff[0];
};

/*! \brief ast_sw: Switch statement in extensions.conf */
struct ast_sw {
	char *name;
	const char *registrar;			/*!< Registrar */
	char *data;				/*!< Data load */
	int eval;
	AST_LIST_ENTRY(ast_sw) list;
	char stuff[0];
};

/*! \brief ast_ignorepat: Ignore patterns in dial plan */
struct ast_ignorepat {
	const char *registrar;
	struct ast_ignorepat *next;
	const char pattern[0];
};

/*! \brief ast_context: An extension context */
struct ast_context {
	ast_mutex_t lock; 			/*!< A lock to prevent multiple threads from clobbering the context */
	struct ast_exten *root;			/*!< The root of the list of extensions */
	struct ast_context *next;		/*!< Link them together */
	struct ast_include *includes;		/*!< Include other contexts */
	struct ast_ignorepat *ignorepats;	/*!< Patterns for which to continue playing dialtone */
	const char *registrar;			/*!< Registrar */
	AST_LIST_HEAD_NOLOCK(, ast_sw) alts;	/*!< Alternative switches */
	ast_mutex_t macrolock;			/*!< A lock to implement "exclusive" macros - held whilst a call is executing in the macro */
	char name[0];				/*!< Name of the context */
};


/*! \brief ast_app: A registered application */
struct ast_app {
	int (*execute)(struct ast_channel *chan, void *data);
	const char *synopsis;			/*!< Synopsis text for 'show applications' */
	const char *description;		/*!< Description (help text) for 'show application &lt;name&gt;' */
	AST_LIST_ENTRY(ast_app) list;		/*!< Next app in list */
	struct module *module;			/*!< Module this app belongs to */
	char name[0];				/*!< Name of the application */
};

/*! \brief ast_state_cb: An extension state notify register item */
struct ast_state_cb {
	int id;
	void *data;
	ast_state_cb_type callback;
	struct ast_state_cb *next;
};

/*! \brief Structure for dial plan hints

  \note Hints are pointers from an extension in the dialplan to one or
  more devices (tech/name) */
struct ast_hint {
	struct ast_exten *exten;	/*!< Extension */
	int laststate; 			/*!< Last known state */
	struct ast_state_cb *callbacks;	/*!< Callback list for this extension */
};

static const struct cfextension_states {
	int extension_state;
	const char * const text;
} extension_states[] = {
	{ AST_EXTENSION_NOT_INUSE,                     "Idle" },
	{ AST_EXTENSION_INUSE,                         "InUse" },
	{ AST_EXTENSION_BUSY,                          "Busy" },
	{ AST_EXTENSION_UNAVAILABLE,                   "Unavailable" },
	{ AST_EXTENSION_RINGING,                       "Ringing" },
	{ AST_EXTENSION_INUSE | AST_EXTENSION_RINGING, "InUse&Ringing" },
	{ AST_EXTENSION_ONHOLD,                        "Hold" },
	{ AST_EXTENSION_INUSE | AST_EXTENSION_ONHOLD,  "InUse&Hold" }
};

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

AST_MUTEX_DEFINE_STATIC(globalslock);
static struct varshead globals = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

static int autofallthrough = 1;

AST_MUTEX_DEFINE_STATIC(maxcalllock);
static int countcalls;

static AST_LIST_HEAD_STATIC(acf_root, ast_custom_function);

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
	"Asterisk will wait this number of milliseconds before returning to\n"
	"the dialplan after answering the call.\n"
	},

	{ "BackGround", pbx_builtin_background,
	"Play an audio file while waiting for digits of an extension to go to.",
	"  Background(filename1[&filename2...][|options[|langoverride][|context]]):\n"
	"This application will play the given list of files (do not put extension)\n"
	"while waiting for an extension to be dialed by the calling channel. To\n"
	"continue waiting for digits after this application has finished playing\n"
	"files, the WaitExten application should be used. The 'langoverride' option\n"
	"explicitly specifies which language to attempt to use for the requested sound\n"
	"files. If a 'context' is specified, this is the dialplan context that this\n"
	"application will use when exiting to a dialed extension."
	"  If one of the requested sound files does not exist, call processing will be\n"
	"terminated.\n"
	"  Options:\n"
	"    s - Causes the playback of the message to be skipped\n"
	"          if the channel is not in the 'up' state (i.e. it\n"
	"          hasn't been answered yet). If this happens, the\n"
	"          application will return immediately.\n"
	"    n - Don't answer the channel before playing the files.\n"
	"    m - Only break if a digit hit matches a one digit\n"
	"          extension in the destination context.\n"
	"See Also: Playback (application) -- Play sound file(s) to the channel,\n"
    "                                    that cannot be interrupted\n"
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
	"  Congestion([timeout]): This application will indicate the congestion\n"
	"condition to the calling channel. If the optional timeout is specified, the\n"
	"calling channel will be hung up after the specified number of seconds.\n"
	"Otherwise, this application will wait until the calling channel hangs up.\n"
	},

	{ "Goto", pbx_builtin_goto,
	"Jump to a particular priority, extension, or context",
	"  Goto([[context|]extension|]priority): This application will set the current\n"
	"context, extension, and priority in the channel structure. After it completes, the\n"
	"pbx engine will continue dialplan execution at the specified location.\n"
	"If no specific extension, or extension and context, are specified, then this\n"
	"application will just set the specified priority of the current extension.\n"
	"  At least a priority is required as an argument, or the goto will return a -1,\n"
	"and the channel and call will be terminated.\n"
	"  If the location that is put into the channel information is bogus, and asterisk cannot\n"
        "find that location in the dialplan,\n"
	"then the execution engine will try to find and execute the code in the 'i' (invalid)\n"
	"extension in the current context. If that does not exist, it will try to execute the\n"
	"'h' extension. If either or neither the 'h' or 'i' extensions have been defined, the\n"
	"channel is hung up, and the execution of instructions on the channel is terminated.\n"
	"What this means is that, for example, you specify a context that does not exist, then\n"
	"it will not be possible to find the 'h' or 'i' extensions, and the call will terminate!\n"
	},

	{ "GotoIf", pbx_builtin_gotoif,
	"Conditional goto",
	"  GotoIf(condition?[labeliftrue]:[labeliffalse]): This application will set the current\n"
	"context, extension, and priority in the channel structure based on the evaluation of\n"
	"the given condition. After this application completes, the\n"
	"pbx engine will continue dialplan execution at the specified location in the dialplan.\n"
	"The channel will continue at\n"
	"'labeliftrue' if the condition is true, or 'labeliffalse' if the condition is\n"
	"false. The labels are specified with the same syntax as used within the Goto\n"
	"application.  If the label chosen by the condition is omitted, no jump is\n"
	"performed, and the execution passes to the next instruction.\n"
	"If the target location is bogus, and does not exist, the execution engine will try \n"
	"to find and execute the code in the 'i' (invalid)\n"
	"extension in the current context. If that does not exist, it will try to execute the\n"
	"'h' extension. If either or neither the 'h' or 'i' extensions have been defined, the\n"
	"channel is hung up, and the execution of instructions on the channel is terminated.\n"
	"Remember that this command can set the current context, and if the context specified\n"
	"does not exist, then it will not be able to find any 'h' or 'i' extensions there, and\n"
	"the channel and call will both be terminated!\n"
	},

	{ "GotoIfTime", pbx_builtin_gotoiftime,
	"Conditional Goto based on the current time",
	"  GotoIfTime(<times>|<weekdays>|<mdays>|<months>?[[context|]exten|]priority):\n"
	"This application will set the context, extension, and priority in the channel structure\n"
	"if the current time matches the given time specification. Otherwise, nothing is done.\n"
        "Further information on the time specification can be found in examples\n"
        "illustrating how to do time-based context includes in the dialplan.\n" 
	"If the target jump location is bogus, the same actions would be taken as for Goto.\n"
	},

	{ "ExecIfTime", pbx_builtin_execiftime,
	"Conditional application execution based on the current time",
	"  ExecIfTime(<times>|<weekdays>|<mdays>|<months>?appname[|appargs]):\n"
	"This application will execute the specified dialplan application, with optional\n"
	"arguments, if the current time matches the given time specification.\n"
	},

	{ "Hangup", pbx_builtin_hangup,
	"Hang up the calling channel",
	"  Hangup([causecode]): This application will hang up the calling channel.\n"
	"If a causecode is given the channel's hangup cause will be set to the given\n"
	"value.\n"
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
	"  SetAMAFlags([flag]): This application will set the channel's AMA Flags for\n"
 	"  billing purposes.\n"
	},

	{ "SetGlobalVar", pbx_builtin_setglobalvar,
	"Set a global variable to a given value",
	"  SetGlobalVar(variable=value): This application sets a given global variable to\n"
	"the specified value.\n"
	"\n\nThis application is deprecated in favor of Set(GLOBAL(var)=value)\n"
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
	"\n\nThe use of Set to set multiple variables at once and the g flag have both\n"
	"been deprecated.  Please use multiple Set calls and the GLOBAL() dialplan\n"
	"function instead.\n"
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
	"See Also: Playback(application), Background(application).\n"
	},

};

static struct ast_context *contexts;
/*!\brief Lock for the ast_context list
 * This lock MUST be recursive, or a deadlock on reload may result.  See
 * https://issues.asterisk.org/view.php?id=17643
 */
AST_MUTEX_DEFINE_STATIC(conlock);

static AST_LIST_HEAD_STATIC(apps, ast_app);

static AST_LIST_HEAD_STATIC(switches, ast_switch);

static int stateid = 1;

/* WARNING:
   When holding this container's lock, do _not_ do anything that will cause conlock
   to be taken, unless you _already_ hold it. The ast_merge_contexts_and_delete
   function will take the locks in conlock/hints order, so any other
   paths that require both locks must also take them in that order.
*/
static struct ao2_container *hints;

/* XXX TODO Convert this to an astobj2 container, too. */
struct ast_state_cb *statecbs;

/*
   \note This function is special. It saves the stack so that no matter
   how many times it is called, it returns to the same place */
int pbx_exec(struct ast_channel *c, 		/*!< Channel */
	     struct ast_app *app,		/*!< Application */
	     void *data)			/*!< Data for execution */
{
	int res;

	const char *saved_c_appl;
	const char *saved_c_data;

	if (c->cdr && !ast_check_hangup(c))
		ast_cdr_setapp(c->cdr, app->name, data);

	/* save channel values */
	saved_c_appl= c->appl;
	saved_c_data= c->data;

	c->appl = app->name;
	c->data = data;
	/* XXX remember what to to when we have linked apps to modules */
	if (app->module) {
		/* XXX LOCAL_USER_ADD(app->module) */
	}
	res = app->execute(c, S_OR(data, ""));
	if (app->module) {
		/* XXX LOCAL_USER_REMOVE(app->module) */
	}
	/* restore channel values */
	c->appl = saved_c_appl;
	c->data = saved_c_data;
	return res;
}


/*! Go no deeper than this through includes (not counting loops) */
#define AST_PBX_MAX_STACK	128

/*! \brief Find application handle in linked list
 */
struct ast_app *pbx_findapp(const char *app)
{
	struct ast_app *tmp;

	AST_LIST_LOCK(&apps);
	AST_LIST_TRAVERSE(&apps, tmp, list) {
		if (!strcasecmp(tmp->name, app))
			break;
	}
	AST_LIST_UNLOCK(&apps);

	return tmp;
}

static struct ast_switch *pbx_findswitch(const char *sw)
{
	struct ast_switch *asw;

	AST_LIST_LOCK(&switches);
	AST_LIST_TRAVERSE(&switches, asw, list) {
		if (!strcasecmp(asw->name, sw))
			break;
	}
	AST_LIST_UNLOCK(&switches);

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

/*
 * Special characters used in patterns:
 *	'_'	underscore is the leading character of a pattern.
 *		In other position it is treated as a regular char.
 *	' ' '-'	space and '-' are separator and ignored. Why? so
 *	        patterns like NXX-XXX-XXXX or NXX XXX XXXX will work.
 *	.	one or more of any character. Only allowed at the end of
 *		a pattern.
 *	!	zero or more of anything. Also impacts the result of CANMATCH
 *		and MATCHMORE. Only allowed at the end of a pattern.
 *		In the core routine, ! causes a match with a return code of 2.
 *		In turn, depending on the search mode: (XXX check if it is implemented)
 *		- E_MATCH retuns 1 (does match)
 *		- E_MATCHMORE returns 0 (no match)
 *		- E_CANMATCH returns 1 (does match)
 *
 *	/	should not appear as it is considered the separator of the CID info.
 *		XXX at the moment we may stop on this char.
 *
 *	X Z N	match ranges 0-9, 1-9, 2-9 respectively.
 *	[	denotes the start of a set of character. Everything inside
 *		is considered literally. We can have ranges a-d and individual
 *		characters. A '[' and '-' can be considered literally if they
 *		are just before ']'.
 *		XXX currently there is no way to specify ']' in a range, nor \ is
 *		considered specially.
 *
 * When we compare a pattern with a specific extension, all characters in the extension
 * itself are considered literally with the only exception of '-' which is considered
 * as a separator and thus ignored.
 * XXX do we want to consider space as a separator as well ?
 * XXX do we want to consider the separators in non-patterns as well ?
 */

/*!
 * \brief helper functions to sort extensions and patterns in the desired way,
 * so that more specific patterns appear first.
 *
 * ext_cmp1 compares individual characters (or sets of), returning
 * an int where bits 0-7 are the ASCII code of the first char in the set,
 * while bit 8-15 are the cardinality of the set minus 1.
 * This way more specific patterns (smaller cardinality) appear first.
 * Wildcards have a special value, so that we can directly compare them to
 * sets by subtracting the two values. In particular:
 * 	0x000xx		one character, xx
 * 	0x0yyxx		yy character set starting with xx
 * 	0x10000		'.' (one or more of anything)
 * 	0x20000		'!' (zero or more of anything)
 * 	0x30000		NUL (end of string)
 * 	0x40000		error in set.
 * The pointer to the string is advanced according to needs.
 * NOTES:
 *	1. the empty set is equivalent to NUL.
 *	2. given that a full set has always 0 as the first element,
 *	   we could encode the special cases as 0xffXX where XX
 *	   is 1, 2, 3, 4 as used above.
 */
static int ext_cmp1(const char **p, unsigned char *bitwise)
{
	int c, cmin = 0xff, count = 0;
	const char *end;

	/* load value and advance pointer */
	while ( (c = *(*p)++) && (c == ' ' || c == '-') )
		;	/* ignore some characters */

	/* always return unless we have a set of chars */
	switch (c) {
	default:	/* ordinary character */
		bitwise[c / 8] = 1 << (c % 8);
		return 0x0100 | (c & 0xff);

	case 'N':	/* 2..9 */
		bitwise[6] = 0xfc;
		bitwise[7] = 0x03;
		return 0x0800 | '2';

	case 'X':	/* 0..9 */
		bitwise[6] = 0xff;
		bitwise[7] = 0x03;
		return 0x0A00 | '0';

	case 'Z':	/* 1..9 */
		bitwise[6] = 0xfe;
		bitwise[7] = 0x03;
		return 0x0900 | '1';

	case '.':	/* wildcard */
		return 0x10000;

	case '!':	/* earlymatch */
		return 0x20000;	/* less specific than NULL */

	case '\0':	/* empty string */
		*p = NULL;
		return 0x30000;

	case '[':	/* pattern */
		break;
	}
	/* locate end of set */
	end = strchr(*p, ']');	

	if (end == NULL) {
		ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
		return 0x40000;	/* XXX make this entry go last... */
	}

	for (; *p < end  ; (*p)++) {
		unsigned char c1, c2;	/* first-last char in range */
		c1 = (unsigned char)((*p)[0]);
		if (*p + 2 < end && (*p)[1] == '-') { /* this is a range */
			c2 = (unsigned char)((*p)[2]);
			*p += 2;    /* skip a total of 3 chars */
		} else {        /* individual character */
			c2 = c1;
		}
		if (c1 < cmin) {
			cmin = c1;
		}
		for (; c1 <= c2; c1++) {
			unsigned char mask = 1 << (c1 % 8);
			/*!\note If two patterns score the same, the one with the lowest
			 * ascii values will compare as coming first. */
			/* Flag the character as included (used) and count it. */
			if (!(bitwise[ c1 / 8 ] & mask)) {
				bitwise[ c1 / 8 ] |= mask;
				count += 0x100;
			}
		}
	}
	(*p)++;
	return count == 0 ? 0x30000 : (count | cmin);
}

/*!
 * \brief the full routine to compare extensions in rules.
 */
static int ext_cmp(const char *a, const char *b)
{
	/* make sure non-patterns come first.
	 * If a is not a pattern, it either comes first or
	 * we do a more complex pattern comparison.
	 */
	int ret = 0;

	if (a[0] != '_')
		return (b[0] == '_') ? -1 : strcmp(a, b);

	/* Now we know a is a pattern; if b is not, a comes first */
	if (b[0] != '_')
		return 1;

	/* ok we need full pattern sorting routine.
	 * skip past the underscores */
	++a; ++b;
	do {
		unsigned char bitwise[2][32] = { { 0, } };
		ret = ext_cmp1(&a, bitwise[0]) - ext_cmp1(&b, bitwise[1]);
		if (ret == 0) {
			/* Are the classes different, even though they score the same? */
			ret = memcmp(bitwise[0], bitwise[1], 32);
		}
	} while (!ret && a && b);
	if (ret == 0) {
		return 0;
	} else {
		return (ret > 0) ? 1 : -1;
	}
}

/*!
 * When looking up extensions, we can have different requests
 * identified by the 'action' argument, as follows.
 * Note that the coding is such that the low 4 bits are the
 * third argument to extension_match_core.
 */
enum ext_match_t {
	E_MATCHMORE = 	0x00,	/* extension can match but only with more 'digits' */
	E_CANMATCH =	0x01,	/* extension can match with or without more 'digits' */
	E_MATCH =	0x02,	/* extension is an exact match */
	E_MATCH_MASK =	0x03,	/* mask for the argument to extension_match_core() */
	E_SPAWN =	0x12,	/* want to spawn an extension. Requires exact match */
	E_FINDLABEL =	0x22	/* returns the priority for a given label. Requires exact match */
};

/*
 * Internal function for ast_extension_{match|close}
 * return 0 on no-match, 1 on match, 2 on early match.
 * mode is as follows:
 *	E_MATCH		success only on exact match
 *	E_MATCHMORE	success only on partial match (i.e. leftover digits in pattern)
 *	E_CANMATCH	either of the above.
 */

static int _extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	mode &= E_MATCH_MASK;	/* only consider the relevant bits */

	if ( (mode == E_MATCH) && (pattern[0] == '_') && (strcasecmp(pattern,data)==0) ) /* note: if this test is left out, then _x. will not match _x. !!! */
		return 1;

	if (pattern[0] != '_') { /* not a pattern, try exact or partial match */
		int ld = strlen(data), lp = strlen(pattern);

		if (lp < ld)		/* pattern too short, cannot match */
			return 0;
		/* depending on the mode, accept full or partial match or both */
		if (mode == E_MATCH)
			return !strcmp(pattern, data); /* 1 on match, 0 on fail */
		if (ld == 0 || !strncasecmp(pattern, data, ld)) /* partial or full match */
			return (mode == E_MATCHMORE) ? lp > ld : 1; /* XXX should consider '!' and '/' ? */
		else
			return 0;
	}
	pattern++; /* skip leading _ */
	/*
	 * XXX below we stop at '/' which is a separator for the CID info. However we should
	 * not store '/' in the pattern at all. When we insure it, we can remove the checks.
	 */
	while (*data && *pattern && *pattern != '/') {
		const char *end;

		if (*data == '-') { /* skip '-' in data (just a separator) */
			data++;
			continue;
		}
		switch (toupper(*pattern)) {
		case '[':	/* a range */
			end = strchr(pattern+1, ']'); /* XXX should deal with escapes ? */
			if (end == NULL) {
				ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
				return 0;	/* unconditional failure */
			}
			for (pattern++; pattern != end; pattern++) {
				if (pattern+2 < end && pattern[1] == '-') { /* this is a range */
					if (*data >= pattern[0] && *data <= pattern[2])
						break;	/* match found */
					else {
						pattern += 2; /* skip a total of 3 chars */
						continue;
					}
				} else if (*data == pattern[0])
					break;	/* match found */
			}
			if (pattern == end)
				return 0;
			pattern = end;	/* skip and continue */
			break;
		case 'N':
			if (*data < '2' || *data > '9')
				return 0;
			break;
		case 'X':
			if (*data < '0' || *data > '9')
				return 0;
			break;
		case 'Z':
			if (*data < '1' || *data > '9')
				return 0;
			break;
		case '.':	/* Must match, even with more digits */
			return 1;
		case '!':	/* Early match */
			return 2;
		case ' ':
		case '-':	/* Ignore these in patterns */
			data--; /* compensate the final data++ */
			break;
		default:
			if (*data != *pattern)
				return 0;
		}
		data++;
		pattern++;
	}
	if (*data)			/* data longer than pattern, no match */
		return 0;
	/*
	 * match so far, but ran off the end of the data.
	 * Depending on what is next, determine match or not.
	 */
	if (*pattern == '\0' || *pattern == '/')	/* exact match */
		return (mode == E_MATCHMORE) ? 0 : 1;	/* this is a failure for E_MATCHMORE */
	else if (*pattern == '!')			/* early match */
		return 2;
	else						/* partial match */
		return (mode == E_MATCH) ? 0 : 1;	/* this is a failure for E_MATCH */
}

/*
 * Wrapper around _extension_match_core() to do performance measurement
 * using the profiling code.
 */
static int extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	int i;
	static int prof_id = -2;	/* marker for 'unallocated' id */
	if (prof_id == -2)
		prof_id = ast_add_profile("ext_match", 0);
	ast_mark(prof_id, 1);
	i = _extension_match_core(pattern, data, mode);
	ast_mark(prof_id, 0);
	return i;
}

int ast_extension_match(const char *pattern, const char *data)
{
	return extension_match_core(pattern, data, E_MATCH);
}

int ast_extension_close(const char *pattern, const char *data, int needmore)
{
	if (needmore != E_MATCHMORE && needmore != E_CANMATCH)
		ast_log(LOG_WARNING, "invalid argument %d\n", needmore);
	return extension_match_core(pattern, data, needmore);
}

struct ast_context *ast_context_find(const char *name)
{
	struct ast_context *tmp = NULL;

	ast_rdlock_contexts();

	while ( (tmp = ast_walk_contexts(tmp)) ) {
		if (!name || !strcasecmp(name, tmp->name))
			break;
	}

	ast_unlock_contexts();

	return tmp;
}

#define STATUS_NO_CONTEXT	1
#define STATUS_NO_EXTENSION	2
#define STATUS_NO_PRIORITY	3
#define STATUS_NO_LABEL		4
#define STATUS_SUCCESS		5

static int matchcid(const char *cidpattern, const char *callerid)
{
	/* If the Caller*ID pattern is empty, then we're matching NO Caller*ID, so
	   failing to get a number should count as a match, otherwise not */

	if (ast_strlen_zero(callerid))
		return ast_strlen_zero(cidpattern) ? 1 : 0;

	return ast_extension_match(cidpattern, callerid);
}

/* request and result for pbx_find_extension */
struct pbx_find_info {
#if 0
	const char *context;
	const char *exten;
	int priority;
#endif

	char *incstack[AST_PBX_MAX_STACK];      /* filled during the search */
	int stacklen;                   /* modified during the search */
	int status;                     /* set on return */
	struct ast_switch *swo;         /* set on return */
	const char *data;               /* set on return */
	const char *foundcontext;       /* set on return */
};

static struct ast_exten *pbx_find_extension(struct ast_channel *chan,
	struct ast_context *bypass, struct pbx_find_info *q,
	const char *context, const char *exten, int priority,
	const char *label, const char *callerid, enum ext_match_t action)
{
	int x, res;
	struct ast_context *tmp;
	struct ast_exten *e, *eroot;
	struct ast_include *i;
	struct ast_sw *sw;
	char *tmpdata = NULL;

	/* Initialize status if appropriate */
	if (q->stacklen == 0) {
		q->status = STATUS_NO_CONTEXT;
		q->swo = NULL;
		q->data = NULL;
		q->foundcontext = NULL;
	}
	/* Check for stack overflow */
	if (q->stacklen >= AST_PBX_MAX_STACK) {
		ast_log(LOG_WARNING, "Maximum PBX stack exceeded\n");
		return NULL;
	}
	/* Check first to see if we've already been checked */
	for (x = 0; x < q->stacklen; x++) {
		if (!strcasecmp(q->incstack[x], context))
			return NULL;
	}
	if (bypass)	/* bypass means we only look there */
		tmp = bypass;
	else {	/* look in contexts */
		tmp = NULL;
		while ((tmp = ast_walk_contexts(tmp)) ) {
			if (!strcmp(tmp->name, context))
				break;
		}
		if (!tmp)
			return NULL;
	}
	if (q->status < STATUS_NO_EXTENSION)
		q->status = STATUS_NO_EXTENSION;

	/* scan the list trying to match extension and CID */
	eroot = NULL;
	while ( (eroot = ast_walk_context_extensions(tmp, eroot)) ) {
		int match = extension_match_core(eroot->exten, exten, action);
		/* 0 on fail, 1 on match, 2 on earlymatch */

		if (!match || (eroot->matchcid && !matchcid(eroot->cidmatch, callerid)))
			continue;	/* keep trying */
		if (match == 2 && action == E_MATCHMORE) {
			/* We match an extension ending in '!'.
			 * The decision in this case is final and is NULL (no match).
			 */
			return NULL;
		}
		/* found entry, now look for the right priority */
		if (q->status < STATUS_NO_PRIORITY)
			q->status = STATUS_NO_PRIORITY;
		e = NULL;
		while ( (e = ast_walk_extension_priorities(eroot, e)) ) {
			/* Match label or priority */
			if (action == E_FINDLABEL) {
				if (q->status < STATUS_NO_LABEL)
					q->status = STATUS_NO_LABEL;
				if (label && e->label && !strcmp(label, e->label))
					break;	/* found it */
			} else if (e->priority == priority) {
				break;	/* found it */
			} /* else keep searching */
		}
		if (e) {	/* found a valid match */
			q->status = STATUS_SUCCESS;
			q->foundcontext = context;
			return e;
		}
	}
	/* Check alternative switches */
	AST_LIST_TRAVERSE(&tmp->alts, sw, list) {
		struct ast_switch *asw = pbx_findswitch(sw->name);
		ast_switch_f *aswf = NULL;
		char *datap;

		if (!asw) {
			ast_log(LOG_WARNING, "No such switch '%s'\n", sw->name);
			continue;
		}
		/* Substitute variables now */
		if (sw->eval) {
			if (!(tmpdata = ast_threadstorage_get(&switch_data, 512))) {
				ast_log(LOG_WARNING, "Can't evaluate switch?!");
				continue;
			}
			pbx_substitute_variables_helper(chan, sw->data, tmpdata, 512);
		}

		/* equivalent of extension_match_core() at the switch level */
		if (action == E_CANMATCH)
			aswf = asw->canmatch;
		else if (action == E_MATCHMORE)
			aswf = asw->matchmore;
		else /* action == E_MATCH */
			aswf = asw->exists;
		datap = sw->eval ? tmpdata : sw->data;
		if (!aswf)
			res = 0;
		else {
			if (chan)
				ast_autoservice_start(chan);
			res = aswf(chan, context, exten, priority, callerid, datap);
			if (chan)
				ast_autoservice_stop(chan);
		}
		if (res) {	/* Got a match */
			q->swo = asw;
			q->data = datap;
			q->foundcontext = context;
			/* XXX keep status = STATUS_NO_CONTEXT ? */
			return NULL;
		}
	}
	q->incstack[q->stacklen++] = tmp->name;	/* Setup the stack */
	/* Now try any includes we have in this context */
	for (i = tmp->includes; i; i = i->next) {
		if (include_valid(i)) {
			if ((e = pbx_find_extension(chan, bypass, q, i->rname, exten, priority, label, callerid, action)))
				return e;
			if (q->swo)
				return NULL;
		}
	}
	return NULL;
}

/*! \brief extract offset:length from variable name.
 * Returns 1 if there is a offset:length part, which is
 * trimmed off (values go into variables)
 */
static int parse_variable_name(char *var, int *offset, int *length, int *isfunc)
{
	int parens=0;

	*offset = 0;
	*length = INT_MAX;
	*isfunc = 0;
	for (; *var; var++) {
		if (*var == '(') {
			(*isfunc)++;
			parens++;
		} else if (*var == ')') {
			parens--;
		} else if (*var == ':' && parens == 0) {
			*var++ = '\0';
			sscanf(var, "%30d:%30d", offset, length);
			return 1; /* offset:length valid */
		}
	}
	return 0;
}

/*! \brief takes a substring. It is ok to call with value == workspace.
 *
 * offset < 0 means start from the end of the string and set the beginning
 *   to be that many characters back.
 * length is the length of the substring.  A value less than 0 means to leave
 * that many off the end.
 * Always return a copy in workspace.
 */
static char *substring(const char *value, int offset, int length, char *workspace, size_t workspace_len)
{
	char *ret = workspace;
	int lr;	/* length of the input string after the copy */

	ast_copy_string(workspace, value, workspace_len); /* always make a copy */

	lr = strlen(ret); /* compute length after copy, so we never go out of the workspace */

	/* Quick check if no need to do anything */
	if (offset == 0 && length >= lr)	/* take the whole string */
		return ret;

	if (offset < 0)	{	/* translate negative offset into positive ones */
		offset = lr + offset;
		if (offset < 0) /* If the negative offset was greater than the length of the string, just start at the beginning */
			offset = 0;
	}

	/* too large offset result in empty string so we know what to return */
	if (offset >= lr)
		return ret + lr;	/* the final '\0' */

	ret += offset;		/* move to the start position */
	if (length >= 0 && length < lr - offset)	/* truncate if necessary */
		ret[length] = '\0';
	else if (length < 0) {
		if (lr > offset - length) /* After we remove from the front and from the rear, is there anything left? */
			ret[lr + length - offset] = '\0';
		else
			ret[0] = '\0';
	}

	return ret;
}

/*! \brief  pbx_retrieve_variable: Support for Asterisk built-in variables
  ---*/
void pbx_retrieve_variable(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp)
{
	const char not_found = '\0';
	char *tmpvar;
	const char *s;	/* the result */
	int offset, length;
	int i, need_substring;
	struct varshead *places[2] = { headp, &globals };	/* list of places where we may look */

	if (c) {
		ast_channel_lock(c);
		places[0] = &c->varshead;
	}
	/*
	 * Make a copy of var because parse_variable_name() modifies the string.
	 * Then if called directly, we might need to run substring() on the result;
	 * remember this for later in 'need_substring', 'offset' and 'length'
	 */
	tmpvar = ast_strdupa(var);	/* parse_variable_name modifies the string */
	need_substring = parse_variable_name(tmpvar, &offset, &length, &i /* ignored */);

	/*
	 * Look first into predefined variables, then into variable lists.
	 * Variable 's' points to the result, according to the following rules:
	 * s == &not_found (set at the beginning) means that we did not find a
	 *	matching variable and need to look into more places.
	 * If s != &not_found, s is a valid result string as follows:
	 * s = NULL if the variable does not have a value;
	 *	you typically do this when looking for an unset predefined variable.
	 * s = workspace if the result has been assembled there;
	 *	typically done when the result is built e.g. with an snprintf(),
	 *	so we don't need to do an additional copy.
	 * s != workspace in case we have a string, that needs to be copied
	 *	(the ast_copy_string is done once for all at the end).
	 *	Typically done when the result is already available in some string.
	 */
	s = &not_found;	/* default value */
	if (c) {	/* This group requires a valid channel */
		/* Names with common parts are looked up a piece at a time using strncmp. */
		if (!strncmp(var, "CALL", 4)) {
			if (!strncmp(var + 4, "ING", 3)) {
				if (!strcmp(var + 7, "PRES")) {			/* CALLINGPRES */
					snprintf(workspace, workspacelen, "%d", c->cid.cid_pres);
					s = workspace;
				} else if (!strcmp(var + 7, "ANI2")) {		/* CALLINGANI2 */
					snprintf(workspace, workspacelen, "%d", c->cid.cid_ani2);
					s = workspace;
				} else if (!strcmp(var + 7, "TON")) {		/* CALLINGTON */
					snprintf(workspace, workspacelen, "%d", c->cid.cid_ton);
					s = workspace;
				} else if (!strcmp(var + 7, "TNS")) {		/* CALLINGTNS */
					snprintf(workspace, workspacelen, "%d", c->cid.cid_tns);
					s = workspace;
				}
			}
		} else if (!strcmp(var, "HINT")) {
			s = ast_get_hint(workspace, workspacelen, NULL, 0, c, c->context, c->exten) ? workspace : NULL;
		} else if (!strcmp(var, "HINTNAME")) {
			s = ast_get_hint(NULL, 0, workspace, workspacelen, c, c->context, c->exten) ? workspace : NULL;
		} else if (!strcmp(var, "EXTEN")) {
			s = c->exten;
		} else if (!strcmp(var, "CONTEXT")) {
			s = c->context;
		} else if (!strcmp(var, "PRIORITY")) {
			snprintf(workspace, workspacelen, "%d", c->priority);
			s = workspace;
		} else if (!strcmp(var, "CHANNEL")) {
			s = c->name;
		} else if (!strcmp(var, "UNIQUEID")) {
			s = c->uniqueid;
		} else if (!strcmp(var, "HANGUPCAUSE")) {
			snprintf(workspace, workspacelen, "%d", c->hangupcause);
			s = workspace;
		}
	}
	if (s == &not_found) { /* look for more */
		if (!strcmp(var, "EPOCH")) {
			snprintf(workspace, workspacelen, "%u",(int)time(NULL));
			s = workspace;
		} else if (!strcmp(var, "SYSTEMNAME")) {
			s = ast_config_AST_SYSTEM_NAME;
		}
	}
	/* if not found, look into chanvars or global vars */
	for (i = 0; s == &not_found && i < (sizeof(places) / sizeof(places[0])); i++) {
		struct ast_var_t *variables;
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_mutex_lock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (strcasecmp(ast_var_name(variables), var)==0) {
				s = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_mutex_unlock(&globalslock);
	}
	if (s == &not_found || s == NULL)
		*ret = NULL;
	else {
		if (s != workspace)
			ast_copy_string(workspace, s, workspacelen);
		*ret = workspace;
		if (need_substring)
			*ret = substring(*ret, offset, length, workspace, workspacelen);
	}

	if (c)
		ast_channel_unlock(c);
}

/*! \brief CLI function to show installed custom functions
    \addtogroup CLI_functions
 */
static int handle_show_functions_deprecated(int fd, int argc, char *argv[])
{
	struct ast_custom_function *acf;
	int count_acf = 0;
	int like = 0;

	if (argc == 4 && (!strcmp(argv[2], "like")) ) {
		like = 1;
	} else if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}

	ast_cli(fd, "%s Custom Functions:\n--------------------------------------------------------------------------------\n", like ? "Matching" : "Installed");

	AST_LIST_LOCK(&acf_root);
	AST_LIST_TRAVERSE(&acf_root, acf, acflist) {
		if (!like || strstr(acf->name, argv[3])) {
			count_acf++;
			ast_cli(fd, "%-20.20s  %-35.35s  %s\n", acf->name, acf->syntax, acf->synopsis);
		}
	}
	AST_LIST_UNLOCK(&acf_root);

	ast_cli(fd, "%d %scustom functions installed.\n", count_acf, like ? "matching " : "");

	return RESULT_SUCCESS;
}
static int handle_show_functions(int fd, int argc, char *argv[])
{
	struct ast_custom_function *acf;
	int count_acf = 0;
	int like = 0;

	if (argc == 5 && (!strcmp(argv[3], "like")) ) {
		like = 1;
	} else if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	ast_cli(fd, "%s Custom Functions:\n--------------------------------------------------------------------------------\n", like ? "Matching" : "Installed");

	AST_LIST_LOCK(&acf_root);
	AST_LIST_TRAVERSE(&acf_root, acf, acflist) {
		if (!like || strstr(acf->name, argv[4])) {
			count_acf++;
			ast_cli(fd, "%-20.20s  %-35.35s  %s\n", acf->name, acf->syntax, acf->synopsis);
		}
	}
	AST_LIST_UNLOCK(&acf_root);

	ast_cli(fd, "%d %scustom functions installed.\n", count_acf, like ? "matching " : "");

	return RESULT_SUCCESS;
}

static int handle_show_function_deprecated(int fd, int argc, char *argv[])
{
	struct ast_custom_function *acf;
	/* Maximum number of characters added by terminal coloring is 22 */
	char infotitle[64 + AST_MAX_APP + 22], syntitle[40], destitle[40];
	char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL;
	char stxtitle[40], *syntax = NULL;
	int synopsis_size, description_size, syntax_size;

	if (argc < 3)
		return RESULT_SHOWUSAGE;

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

static int handle_show_function(int fd, int argc, char *argv[])
{
	struct ast_custom_function *acf;
	/* Maximum number of characters added by terminal coloring is 22 */
	char infotitle[64 + AST_MAX_APP + 22], syntitle[40], destitle[40];
	char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL;
	char stxtitle[40], *syntax = NULL;
	int synopsis_size, description_size, syntax_size;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (!(acf = ast_custom_function_find(argv[3]))) {
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

static char *complete_show_function(const char *line, const char *word, int pos, int state)
{
	struct ast_custom_function *acf;
	char *ret = NULL;
	int which = 0;
	int wordlen = strlen(word);

	/* case-insensitive for convenience in this 'complete' function */
	AST_LIST_LOCK(&acf_root);
	AST_LIST_TRAVERSE(&acf_root, acf, acflist) {
 		if (!strncasecmp(word, acf->name, wordlen) && ++which > state) {
 			ret = strdup(acf->name);
			break;
		}
	}
	AST_LIST_UNLOCK(&acf_root);

	return ret;
}

struct ast_custom_function *ast_custom_function_find(const char *name)
{
	struct ast_custom_function *acf = NULL;

	AST_LIST_LOCK(&acf_root);
	AST_LIST_TRAVERSE(&acf_root, acf, acflist) {
		if (!strcmp(name, acf->name))
			break;
	}
	AST_LIST_UNLOCK(&acf_root);

	return acf;
}

int ast_custom_function_unregister(struct ast_custom_function *acf)
{
	struct ast_custom_function *cur;

	if (!acf)
		return -1;

	AST_LIST_LOCK(&acf_root);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&acf_root, cur, acflist) {
		if (cur == acf) {
			AST_LIST_REMOVE_CURRENT(&acf_root, acflist);
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Unregistered custom function %s\n", acf->name);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&acf_root);

	return acf ? 0 : -1;
}

int ast_custom_function_register(struct ast_custom_function *acf)
{
	struct ast_custom_function *cur;

	if (!acf)
		return -1;

	AST_LIST_LOCK(&acf_root);

	if (ast_custom_function_find(acf->name)) {
		ast_log(LOG_ERROR, "Function %s already registered.\n", acf->name);
		AST_LIST_UNLOCK(&acf_root);
		return -1;
	}

	/* Store in alphabetical order */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&acf_root, cur, acflist) {
		if (strcasecmp(acf->name, cur->name) < 0) {
			AST_LIST_INSERT_BEFORE_CURRENT(&acf_root, acf, acflist);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	if (!cur)
		AST_LIST_INSERT_TAIL(&acf_root, acf, acflist);

	AST_LIST_UNLOCK(&acf_root);

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered custom function %s\n", acf->name);

	return 0;
}

/*! \brief return a pointer to the arguments of the function,
 * and terminates the function name with '\\0'
 */
static char *func_args(char *function)
{
	char *args = strchr(function, '(');

	if (!args)
		ast_log(LOG_WARNING, "Function '%s' doesn't contain parentheses.  Assuming null argument.\n", function);
	else {
		char *p;
		*args++ = '\0';
		if ((p = strrchr(args, ')')) )
			*p = '\0';
		else
			ast_log(LOG_WARNING, "Can't find trailing parenthesis for function '%s(%s'?\n", function, args);
	}
	return args;
}

int ast_func_read(struct ast_channel *chan, char *function, char *workspace, size_t len)
{
	char *args = func_args(function);
	struct ast_custom_function *acfptr = ast_custom_function_find(function);

	if (acfptr == NULL)
		ast_log(LOG_ERROR, "Function %s not registered\n", function);
	else if (!acfptr->read)
		ast_log(LOG_ERROR, "Function %s cannot be read\n", function);
	else
		return acfptr->read(chan, function, args, workspace, len);
	return -1;
}

int ast_func_write(struct ast_channel *chan, char *function, const char *value)
{
	char *args = func_args(function);
	struct ast_custom_function *acfptr = ast_custom_function_find(function);

	if (acfptr == NULL)
		ast_log(LOG_ERROR, "Function %s not registered\n", function);
	else if (!acfptr->write)
		ast_log(LOG_ERROR, "Function %s cannot be written to\n", function);
	else
		return acfptr->write(chan, function, args, value);

	return -1;
}

static void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int count)
{
	/* Substitutes variables into cp2, based on string cp1, cp2 NO LONGER NEEDS TO BE ZEROED OUT!!!!  */
	char *cp4;
	const char *tmp, *whereweare;
	int length, offset, offset2, isfunction;
	char *workspace = NULL;
	char *ltmp = NULL, *var = NULL;
	char *nextvar, *nextexp, *nextthing;
	char *vars, *vare;
	int pos, brackets, needsub, len;

	*cp2 = 0; /* just in case nothing ends up there */
	whereweare=tmp=cp1;
	while (!ast_strlen_zero(whereweare) && count) {
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
			default:
				pos = 1;
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
			*cp2 = 0;
		}

		if (nextvar) {
			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
				} else if (vare[0] == '{') {
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '['))
					needsub++;
				vare++;
			}
			if (brackets)
				ast_log(LOG_WARNING, "Error in extension logic (missing '}')\n");
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
				if (c || !headp)
					cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
				else {
					struct varshead old;
					struct ast_channel *c = ast_channel_alloc(0, 0, "", "", "", "", "", 0, "Bogus/%p", vars);
					if (c) {
						memcpy(&old, &c->varshead, sizeof(old));
						memcpy(&c->varshead, headp, sizeof(c->varshead));
						cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
						/* Don't deallocate the varshead that was passed in */
						memcpy(&c->varshead, &old, sizeof(c->varshead));
						ast_channel_free(c);
					} else
						ast_log(LOG_ERROR, "Unable to allocate bogus channel for variable substitution.  Function results may be blank.\n");
				}

				if (option_debug)
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
				*cp2 = 0;
			}
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
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
				ast_log(LOG_WARNING, "Error in extension logic (missing ']')\n");
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

				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1);
				vars = ltmp;
			} else {
				vars = var;
			}

			length = ast_expr(vars, cp2, count);

			if (length) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Expression result is '%s'\n", cp2);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		}
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
	if (e->data && !strchr(e->data, '$') && !strstr(e->data,"${") && !strstr(e->data,"$[") && !strstr(e->data,"$(")) {
		ast_copy_string(passdata, e->data, datalen);
		return;
	}

	pbx_substitute_variables_helper(c, e->data, passdata, datalen - 1);
}

/*! 
 * \brief The return value depends on the action:
 *
 * E_MATCH, E_CANMATCH, E_MATCHMORE require a real match,
 *	and return 0 on failure, -1 on match;
 * E_FINDLABEL maps the label to a priority, and returns
 *	the priority on success, ... XXX
 * E_SPAWN, spawn an application,
 *	and return 0 on success, -1 on failure.
 *
 * \note The channel is auto-serviced in this function, because doing an extension
 * match may block for a long time.  For example, if the lookup has to use a network
 * dialplan switch, such as DUNDi or IAX2, it may take a while.  However, the channel
 * auto-service code will queue up any important signalling frames to be processed
 * after this is done.
 */
static int pbx_extension_helper(struct ast_channel *c, struct ast_context *con,
	const char *context, const char *exten, int priority,
	const char *label, const char *callerid, enum ext_match_t action)
{
	struct ast_exten *e;
	struct ast_app *app;
	int res;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	char passdata[EXT_DATA_SIZE];

	int matching_action = (action == E_MATCH || action == E_CANMATCH || action == E_MATCHMORE);

	ast_rdlock_contexts();
	e = pbx_find_extension(c, con, &q, context, exten, priority, label, callerid, action);
	if (e) {
		if (matching_action) {
			ast_unlock_contexts();
			return -1;	/* success, we found it */
		} else if (action == E_FINDLABEL) { /* map the label to a priority */
			res = e->priority;
			ast_unlock_contexts();
			return res;	/* the priority we were looking for */
		} else {	/* spawn */
			app = pbx_findapp(e->app);
			ast_unlock_contexts();
			if (!app) {
				ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
				return -1;
			}
			if (c->context != context)
				ast_copy_string(c->context, context, sizeof(c->context));
			if (c->exten != exten)
				ast_copy_string(c->exten, exten, sizeof(c->exten));
			c->priority = priority;
			pbx_substitute_variables(passdata, sizeof(passdata), c, e);
			if (option_debug) {
				ast_log(LOG_DEBUG, "Launching '%s'\n", app->name);
			}
			if (option_verbose > 2) {
				char tmp[80], tmp2[80], tmp3[EXT_DATA_SIZE];
				ast_verbose( VERBOSE_PREFIX_3 "Executing [%s@%s:%d] %s(\"%s\", \"%s\") %s\n",
					exten, context, priority,
					term_color(tmp, app->name, COLOR_BRCYAN, 0, sizeof(tmp)),
					term_color(tmp2, c->name, COLOR_BRMAGENTA, 0, sizeof(tmp2)),
					term_color(tmp3, passdata, COLOR_BRMAGENTA, 0, sizeof(tmp3)),
					"in new stack");
			}
			manager_event(EVENT_FLAG_CALL, "Newexten",
					"Channel: %s\r\n"
					"Context: %s\r\n"
					"Extension: %s\r\n"
					"Priority: %d\r\n"
					"Application: %s\r\n"
					"AppData: %s\r\n"
					"Uniqueid: %s\r\n",
					c->name, c->context, c->exten, c->priority, app->name, passdata, c->uniqueid);
			return pbx_exec(c, app, passdata);	/* 0 on success, -1 on failure */
		}
	} else if (q.swo) {	/* not found here, but in another switch */
		ast_unlock_contexts();
		if (matching_action) {
			return -1;
		} else {
			if (!q.swo->exec) {
				ast_log(LOG_WARNING, "No execution engine for switch %s\n", q.swo->name);
				res = -1;
			}
			return q.swo->exec(c, q.foundcontext ? q.foundcontext : context, exten, priority, callerid, q.data);
		}
	} else {	/* not found anywhere, see what happened */
		ast_unlock_contexts();
		/* Using S_OR here because Solaris doesn't like NULL being passed to ast_log */
		switch (q.status) {
		case STATUS_NO_CONTEXT:
			if (!matching_action)
				ast_log(LOG_NOTICE, "Cannot find extension context '%s'\n", S_OR(context, ""));
			break;
		case STATUS_NO_EXTENSION:
			if (!matching_action)
				ast_log(LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, S_OR(context, ""));
			break;
		case STATUS_NO_PRIORITY:
			if (!matching_action)
				ast_log(LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, S_OR(context, ""));
			break;
		case STATUS_NO_LABEL:
			if (context)
				ast_log(LOG_NOTICE, "No such label '%s' in extension '%s' in context '%s'\n", label, exten, S_OR(context, ""));
			break;
		default:
			if (option_debug)
				ast_log(LOG_DEBUG, "Shouldn't happen!\n");
		}

		return (matching_action) ? 0 : -1;
	}
}

/*! \brief  ast_hint_extension: Find hint for given extension in context */
static struct ast_exten *ast_hint_extension(struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is set in pbx_find_context */

	ast_rdlock_contexts();
	e = pbx_find_extension(c, NULL, &q, context, exten, PRIORITY_HINT, NULL, "", E_MATCH);
	ast_unlock_contexts();

	return e;
}

enum ast_extension_states ast_devstate_to_extenstate(enum ast_device_state devstate)
{
	switch (devstate) {
	case AST_DEVICE_ONHOLD:
		return AST_EXTENSION_ONHOLD;
	case AST_DEVICE_BUSY:
		return AST_EXTENSION_BUSY;
	case AST_DEVICE_UNKNOWN:
		return AST_EXTENSION_NOT_INUSE;
	case AST_DEVICE_UNAVAILABLE:
	case AST_DEVICE_INVALID:
		return AST_EXTENSION_UNAVAILABLE;
	case AST_DEVICE_RINGINUSE:
		return (AST_EXTENSION_INUSE | AST_EXTENSION_RINGING);
	case AST_DEVICE_RINGING:
		return AST_EXTENSION_RINGING;
	case AST_DEVICE_INUSE:
		return AST_EXTENSION_INUSE;
	case AST_DEVICE_NOT_INUSE:
		return AST_EXTENSION_NOT_INUSE;
	case AST_DEVICE_TOTAL: /* not a device state, included for completeness */
		break;
	}

	return AST_EXTENSION_NOT_INUSE;
}

/*! \brief  ast_extensions_state2: Check state of extension by using hints */
static int ast_extension_state2(struct ast_exten *e)
{
	char *hint;
	char *cur, *rest;
	struct ast_devstate_aggregate agg;

	ast_devstate_aggregate_init(&agg);

	if (!e)
		return -1;

	hint = ast_strdupa(ast_get_extension_app(e));

	rest = hint;	/* One or more devices separated with a & character */
	while ( (cur = strsep(&rest, "&")) ) {
		int res = ast_device_state(cur);
		ast_devstate_aggregate_add(&agg, res);
	}
	return ast_devstate_to_extenstate(ast_devstate_aggregate_result(&agg));
}

/*! \brief  ast_extension_state2str: Return extension_state as string */
const char *ast_extension_state2str(int extension_state)
{
	int i;

	for (i = 0; (i < (sizeof(extension_states) / sizeof(extension_states[0]))); i++) {
		if (extension_states[i].extension_state == extension_state)
			return extension_states[i].text;
	}
	return "Unknown";
}

/*! \brief  ast_extension_state: Check extension state for an extension by using hint */
int ast_extension_state(struct ast_channel *c, const char *context, const char *exten)
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
	struct ast_dynamic_str *str;
	struct ao2_iterator i;

	if (!(str = ast_dynamic_str_create(1024))) {
		return;
	}

	i = ao2_iterator_init(hints, 0);
	for (hint = ao2_iterator_next(&i); hint; ao2_ref(hint, -1), hint = ao2_iterator_next(&i)) {
		struct ast_state_cb *cblist;
		char *cur, *parse;
		int state;

		ast_dynamic_str_set(&str, 0, "%s", ast_get_extension_app(hint->exten));
		parse = str->str;

		while ( (cur = strsep(&parse, "&")) ) {
			if (!strcasecmp(cur, device)) {
				break;
			}
		}

		if (!cur) {
			continue;
		}

		/* Get device state for this hint */
		state = ast_extension_state2(hint->exten);

		if ((state == -1) || (state == hint->laststate)) {
			continue;
		}

		/* Device state changed since last check - notify the watchers */

		ast_rdlock_contexts();
		ao2_lock(hints);
		ao2_lock(hint);

		if (hint->exten == NULL) {
			/* the extension has been destroyed */
			ao2_unlock(hint);
			ao2_unlock(hints);
			ast_unlock_contexts();
			continue;
		}

		/* For general callbacks */
		for (cblist = statecbs; cblist; cblist = cblist->next) {
			cblist->callback(hint->exten->parent->name, hint->exten->exten, state, cblist->data);
		}

		/* For extension callbacks */
		for (cblist = hint->callbacks; cblist; cblist = cblist->next) {
			cblist->callback(hint->exten->parent->name, hint->exten->exten, state, cblist->data);
		}

		hint->laststate = state;	/* record we saw the change */
		ao2_unlock(hint);
		ao2_unlock(hints);
		ast_unlock_contexts();
	}

	ao2_iterator_destroy(&i);
	ast_free(str);
}

/*! \brief  ast_extension_state_add: Add watcher for extension states */
int ast_extension_state_add(const char *context, const char *exten,
			    ast_state_cb_type callback, void *data)
{
	struct ast_hint *hint;
	struct ast_state_cb *cblist;
	struct ast_exten *e;

	/* If there's no context and extension:  add callback to statecbs list */
	if (!context && !exten) {
		ao2_lock(hints);

		for (cblist = statecbs; cblist; cblist = cblist->next) {
			if (cblist->callback == callback) {
				cblist->data = data;
				ao2_unlock(hints);
				return 0;
			}
		}

		/* Now insert the callback */
		if (!(cblist = ast_calloc(1, sizeof(*cblist)))) {
			ao2_unlock(hints);
			return -1;
		}
		cblist->id = 0;
		cblist->callback = callback;
		cblist->data = data;

		cblist->next = statecbs;
		statecbs = cblist;

		ao2_unlock(hints);
		return 0;
	}

	if (!context || !exten)
		return -1;

	/* This callback type is for only one hint, so get the hint */
	e = ast_hint_extension(NULL, context, exten);
	if (!e) {
		return -1;
	}

	hint = ao2_find(hints, e, 0);

	if (!hint) {
		return -1;
	}

	/* Now insert the callback in the callback list  */
	if (!(cblist = ast_calloc(1, sizeof(*cblist)))) {
		ao2_ref(hint, -1);
		return -1;
	}
	cblist->id = stateid++;		/* Unique ID for this callback */
	cblist->callback = callback;	/* Pointer to callback routine */
	cblist->data = data;		/* Data for the callback */

	ao2_lock(hint);
	cblist->next = hint->callbacks;
	hint->callbacks = cblist;
	ao2_unlock(hint);

	ao2_ref(hint, -1);

	return cblist->id;
}

static int find_hint_by_cb_id(void *obj, void *arg, int flags)
{
	const struct ast_hint *hint = obj;
	int *id = arg;
	struct ast_state_cb *cb;

	for (cb = hint->callbacks; cb; cb = cb->next) {
		if (cb->id == *id) {
			return CMP_MATCH | CMP_STOP;
		}
	}

	return 0;
}

/*! \brief  ast_extension_state_del: Remove a watcher from the callback list */
int ast_extension_state_del(int id, ast_state_cb_type callback)
{
	struct ast_state_cb **p_cur = NULL;	/* address of pointer to us */
	int ret = -1;

	if (!id && !callback) {
		return -1;
	}

	if (!id) {	/* id == 0 is a callback without extension */
		ao2_lock(hints);
		for (p_cur = &statecbs; *p_cur; p_cur = &(*p_cur)->next) {
			if ((*p_cur)->callback == callback) {
				break;
			}
		}
		if (p_cur && *p_cur) {
			struct ast_state_cb *cur = *p_cur;
			*p_cur = cur->next;
			free(cur);
			ret = 0;
		}
		ao2_unlock(hints);
	} else { /* callback with extension, find the callback based on ID */
		struct ast_hint *hint;

		hint = ao2_callback(hints, 0, find_hint_by_cb_id, &id);

		if (hint) {
			ao2_lock(hint);
			for (p_cur = &hint->callbacks; *p_cur; p_cur = &(*p_cur)->next) {
				if ((*p_cur)->id == id) {
					break;
				}
			}
			if (p_cur && *p_cur) {
				struct ast_state_cb *cur = *p_cur;
				*p_cur = cur->next;
				free(cur);
				ret = 0;
			}
			ao2_unlock(hint);
			ao2_ref(hint, -1);
		}
	}

	return ret;
}

static void ast_hint_destroy(void *obj)
{
	/* ast_remove_hint() takes care of most things before object destruction */
}

/*! \brief  ast_add_hint: Add hint to hint list, check initial extension state */
static int ast_add_hint(struct ast_exten *e)
{
	struct ast_hint *hint;

	if (!e) {
		return -1;
	}

	hint = ao2_find(hints, e, 0);

	if (hint) {
		if (option_debug > 1)
			ast_log(LOG_DEBUG, "HINTS: Not re-adding existing hint %s: %s\n", ast_get_extension_name(e), ast_get_extension_app(e));
		ao2_ref(hint, -1);
		return -1;
	}

	if (option_debug > 1)
		ast_log(LOG_DEBUG, "HINTS: Adding hint %s: %s\n", ast_get_extension_name(e), ast_get_extension_app(e));

	if (!(hint = ao2_alloc(sizeof(*hint), ast_hint_destroy))) {
		return -1;
	}

	/* Initialize and insert new item at the top */
	hint->exten = e;
	hint->laststate = ast_extension_state2(e);

	ao2_link(hints, hint);

	ao2_ref(hint, -1);

	return 0;
}

/*! \brief  ast_change_hint: Change hint for an extension */
static int ast_change_hint(struct ast_exten *oe, struct ast_exten *ne)
{
	struct ast_hint *hint;

	hint = ao2_find(hints, oe, 0);

	if (!hint) {
		return -1;
	}

	ao2_lock(hint);
	hint->exten = ne;
	ao2_unlock(hint);
	ao2_ref(hint, -1);

	return 0;
}

/*! \brief  ast_remove_hint: Remove hint from extension */
static int ast_remove_hint(struct ast_exten *e)
{
	/* Cleanup the Notifys if hint is removed */
	struct ast_hint *hint;
	struct ast_state_cb *cblist, *cbprev;

	if (!e) {
		return -1;
	}

	hint = ao2_find(hints, e, 0);

	if (!hint) {
		return -1;
	}
	ao2_lock(hint);

	cbprev = NULL;
	cblist = hint->callbacks;
	while (cblist) {
		cbprev = cblist;
		cblist = cblist->next;
		cbprev->callback(hint->exten->parent->name, hint->exten->exten, AST_EXTENSION_DEACTIVATED, cbprev->data);
		ast_free(cbprev);
	}
	hint->callbacks = NULL;
	hint->exten = NULL;
	ao2_unlink(hints, hint);
	ao2_unlock(hint);
	ao2_ref(hint, -1);

	return 0;
}


/*! \brief  ast_get_hint: Get hint for channel */
int ast_get_hint(char *hint, int hintsize, char *name, int namesize, struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e = ast_hint_extension(c, context, exten);

	if (e) {
		if (hint)
			ast_copy_string(hint, ast_get_extension_app(e), hintsize);
		if (name) {
			const char *tmp = ast_get_extension_app_data(e);
			if (tmp)
				ast_copy_string(name, tmp, namesize);
		}
		return -1;
	}
	return 0;
}

int ast_exists_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_MATCH);
}

int ast_findlabel_extension(struct ast_channel *c, const char *context, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, 0, label, callerid, E_FINDLABEL);
}

int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, con, NULL, exten, 0, label, callerid, E_FINDLABEL);
}

int ast_canmatch_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_CANMATCH);
}

int ast_matchmore_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_MATCHMORE);
}

int ast_spawn_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_SPAWN);
}

/* helper function to set extension and priority */
static void set_ext_pri(struct ast_channel *c, const char *exten, int pri)
{
	ast_channel_lock(c);
	ast_copy_string(c->exten, exten, sizeof(c->exten));
	c->priority = pri;
	ast_channel_unlock(c);
}

/*!
 * \brief collect digits from the channel into the buffer,
 * return -1 on error, 0 on timeout or done.
 */
static int collect_digits(struct ast_channel *c, int waittime, char *buf, int buflen, int pos)
{
	int digit;

	buf[pos] = '\0';	/* make sure it is properly terminated */
	while (ast_matchmore_extension(c, c->context, buf, 1, c->cid.cid_num)) {
		/* As long as we're willing to wait, and as long as it's not defined,
		   keep reading digits until we can't possibly get a right answer anymore.  */
		digit = ast_waitfordigit(c, waittime * 1000);
		if (c->_softhangup & AST_SOFTHANGUP_ASYNCGOTO) {
			ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
		} else {
			if (!digit)	/* No entry */
				break;
			if (digit < 0)	/* Error, maybe a  hangup */
				return -1;
			if (pos < buflen - 1) {	/* XXX maybe error otherwise ? */
				buf[pos++] = digit;
				buf[pos] = '\0';
			}
			waittime = c->pbx->dtimeout;
		}
	}
	return 0;
}

static int __ast_pbx_run(struct ast_channel *c)
{
	int found = 0;	/* set if we find at least one match */
	int res = 0;
	int autoloopflag;
	int error = 0;		/* set an error conditions */
	const char *emc;

	/* A little initial setup here */
	if (c->pbx) {
		ast_log(LOG_WARNING, "%s already has PBX structure??\n", c->name);
		/* XXX and now what ? */
		free(c->pbx);
	}
	if (!(c->pbx = ast_calloc(1, sizeof(*c->pbx))))
		return -1;
	/* Set reasonable defaults */
	c->pbx->rtimeout = 10;
	c->pbx->dtimeout = 5;

	autoloopflag = ast_test_flag(c, AST_FLAG_IN_AUTOLOOP);	/* save value to restore at the end */
	ast_set_flag(c, AST_FLAG_IN_AUTOLOOP);

	/* Start by trying whatever the channel is set to */
	if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
		/* If not successful fall back to 's' */
		if (option_verbose > 1)
			ast_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d failed so falling back to exten 's'\n", c->name, c->context, c->exten, c->priority);
		/* XXX the original code used the existing priority in the call to
		 * ast_exists_extension(), and reset it to 1 afterwards.
		 * I believe the correct thing is to set it to 1 immediately.
		 */
		set_ext_pri(c, "s", 1);
		if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			/* JK02: And finally back to default if everything else failed */
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d still failed so falling back to context 'default'\n", c->name, c->context, c->exten, c->priority);
			ast_copy_string(c->context, "default", sizeof(c->context));
		}
	}
	if (c->cdr) {
		/* allow CDR variables that have been collected after channel was created to be visible during call */
		ast_cdr_update(c);
	}
	for (;;) {
		char dst_exten[256];	/* buffer to accumulate digits */
		int pos = 0;		/* XXX should check bounds */
		int digit = 0;

		/* loop on priorities in this context/exten */
		while (ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			found = 1;
			if ((res = ast_spawn_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))) {
				/* Something bad happened, or a hangup has been requested. */
				if (strchr("0123456789ABCDEF*#", res)) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
					pos = 0;
					dst_exten[pos++] = digit = res;
					dst_exten[pos] = '\0';
					break;
				}
				if (res == AST_PBX_KEEPALIVE) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
					if (option_verbose > 1)
						ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
					error = 1;
					break;
				}
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				if (c->_softhangup & AST_SOFTHANGUP_ASYNCGOTO) {
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
				} else if (c->_softhangup & AST_SOFTHANGUP_TIMEOUT) {
					/* atimeout, nothing bad */
				} else {
					if (c->cdr)
						ast_cdr_update(c);
					error = 1;
					break;
				}
			}
			if (c->_softhangup & AST_SOFTHANGUP_ASYNCGOTO) {
				ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
			} else if (c->_softhangup & AST_SOFTHANGUP_TIMEOUT && ast_exists_extension(c,c->context,"T",1,c->cid.cid_num)) {
				set_ext_pri(c, "T", 0); /* 0 will become 1 with the c->priority++; at the end */
				/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
				c->whentohangup = 0;
				ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
			} else if (c->_softhangup) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Extension %s, priority %d returned normally even though call was hung up\n",
						c->exten, c->priority);
				error = 1;
				break;
			}
			c->priority++;
		} /* end while  - from here on we can use 'break' to go out */
		if (error)
			break;

		/* XXX we get here on non-existing extension or a keypress or hangup ? */

		if (!ast_exists_extension(c, c->context, c->exten, 1, c->cid.cid_num)) {
			/* If there is no match at priority 1, it is not a valid extension anymore.
			 * Try to continue at "i", 1 or exit if the latter does not exist.
			 */
			if (ast_exists_extension(c, c->context, "i", 1, c->cid.cid_num)) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Sent into invalid extension '%s' in context '%s' on %s\n", c->exten, c->context, c->name);
				pbx_builtin_setvar_helper(c, "INVALID_EXTEN", c->exten);
				set_ext_pri(c, "i", 1);
			} else {
				ast_log(LOG_WARNING, "Channel '%s' sent into invalid extension '%s' in context '%s', but no invalid handler\n",
					c->name, c->exten, c->context);
				error = 1; /* we know what to do with it */
				break;
			}
		} else if (c->_softhangup & AST_SOFTHANGUP_TIMEOUT) {
			/* If we get this far with AST_SOFTHANGUP_TIMEOUT, then we know that the "T" extension is next. */
			ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
		} else {	/* keypress received, get more digits for a full extension */
			int waittime = 0;
			if (digit)
				waittime = c->pbx->dtimeout;
			else if (!autofallthrough)
				waittime = c->pbx->rtimeout;
			if (!waittime) {
				const char *status = pbx_builtin_getvar_helper(c, "DIALSTATUS");
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
				error = 1; /* XXX disable message */
				break;	/* exit from the 'for' loop */
			}

			if (collect_digits(c, waittime, dst_exten, sizeof(dst_exten), pos))
				break;
			if (ast_exists_extension(c, c->context, dst_exten, 1, c->cid.cid_num)) /* Prepare the next cycle */
				set_ext_pri(c, dst_exten, 1);
			else {
				/* No such extension */
				if (!ast_strlen_zero(dst_exten)) {
					/* An invalid extension */
					if (ast_exists_extension(c, c->context, "i", 1, c->cid.cid_num)) {
						if (option_verbose > 2)
							ast_verbose( VERBOSE_PREFIX_3 "Invalid extension '%s' in context '%s' on %s\n", dst_exten, c->context, c->name);
						pbx_builtin_setvar_helper(c, "INVALID_EXTEN", dst_exten);
						set_ext_pri(c, "i", 1);
					} else {
						ast_log(LOG_WARNING, "Invalid extension '%s', but no rule 'i' in context '%s'\n", dst_exten, c->context);
						found = 1; /* XXX disable message */
						break;
					}
				} else {
					/* A simple timeout */
					if (ast_exists_extension(c, c->context, "t", 1, c->cid.cid_num)) {
						if (option_verbose > 2)
							ast_verbose( VERBOSE_PREFIX_3 "Timeout on %s\n", c->name);
						set_ext_pri(c, "t", 1);
					} else {
						ast_log(LOG_WARNING, "Timeout, but no rule 't' in context '%s'\n", c->context);
						found = 1; /* XXX disable message */
						break;
					}
				}
			}
			if (c->cdr) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_2 "CDR updated on %s\n",c->name);
				ast_cdr_update(c);
			}
		}
	}
	if (!found && !error)
		ast_log(LOG_WARNING, "Don't know what to do with '%s'\n", c->name);
	if (res != AST_PBX_KEEPALIVE) {
		ast_softhangup(c, AST_SOFTHANGUP_APPUNLOAD);
	}
	ast_channel_lock(c);
	if ((emc = pbx_builtin_getvar_helper(c, "EXIT_MACRO_CONTEXT"))) {
		emc = ast_strdupa(emc);
	}
	ast_channel_unlock(c);
	if ((res != AST_PBX_KEEPALIVE) && !ast_test_flag(c, AST_FLAG_BRIDGE_HANGUP_RUN) &&
			((emc && ast_exists_extension(c, emc, "h", 1, c->cid.cid_num)) ||
			 (ast_exists_extension(c, c->context, "h", 1, c->cid.cid_num) && (emc = c->context)))) {
		ast_copy_string(c->context, emc, sizeof(c->context));
		set_ext_pri(c, "h", 1);
		if (c->cdr && ast_opt_end_cdr_before_h_exten) {
			ast_cdr_end(c->cdr);
		}
		while(ast_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)) {
			if ((res = ast_spawn_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))) {
				/* Something bad happened, or a hangup has been requested. */
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				break;
			}
			c->priority++;
		}
	}
	ast_set2_flag(c, autoloopflag, AST_FLAG_IN_AUTOLOOP);
	ast_clear_flag(c, AST_FLAG_BRIDGE_HANGUP_RUN); /* from one round to the next, make sure this gets cleared */
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

static void destroy_exten(struct ast_exten *e)
{
	if (e->priority == PRIORITY_HINT)
		ast_remove_hint(e);

	if (e->datad)
		e->datad(e->data);
	free(e);
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
		pthread_attr_destroy(&attr);
		decrease_call_count();
		return AST_PBX_FAILED;
	}
	pthread_attr_destroy(&attr);

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
	int oldval = autofallthrough;
	autofallthrough = newval;
	return oldval;
}

/* lookup for a context with a given name,
 * return with conlock held if found, NULL if not found
 */
static struct ast_context *find_context_locked(const char *context)
{
	struct ast_context *c = NULL;

	ast_rdlock_contexts();
	while ( (c = ast_walk_contexts(c)) ) {
		if (!strcmp(ast_get_context_name(c), context))
			return c;
	}
	ast_unlock_contexts();

	return NULL;
}

/*
 * This function locks contexts list by &conlist, search for the right context
 * structure, leave context list locked and call ast_context_remove_include2
 * which removes include, unlock contexts list and return ...
 */
int ast_context_remove_include(const char *context, const char *include, const char *registrar)
{
	int ret = -1;
	struct ast_context *c = find_context_locked(context);

	if (c) {
		/* found, remove include from this context ... */
		ret = ast_context_remove_include2(c, include, registrar);
		ast_unlock_contexts();
	}
	return ret;
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
	int ret = -1;

	ast_mutex_lock(&con->lock);

	/* find our include */
	for (i = con->includes; i; pi = i, i = i->next) {
		if (!strcmp(i->name, include) &&
				(!registrar || !strcmp(i->registrar, registrar))) {
			/* remove from list */
			if (pi)
				pi->next = i->next;
			else
				con->includes = i->next;
			/* free include and return */
			free(i);
			ret = 0;
			break;
		}
	}

	ast_mutex_unlock(&con->lock);
	return ret;
}

/*!
 * \note This function locks contexts list by &conlist, search for the rigt context
 * structure, leave context list locked and call ast_context_remove_switch2
 * which removes switch, unlock contexts list and return ...
 */
int ast_context_remove_switch(const char *context, const char *sw, const char *data, const char *registrar)
{
	int ret = -1; /* default error return */
	struct ast_context *c = find_context_locked(context);

	if (c) {
		/* remove switch from this context ... */
		ret = ast_context_remove_switch2(c, sw, data, registrar);
		ast_unlock_contexts();
	}
	return ret;
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
	struct ast_sw *i;
	int ret = -1;

	ast_mutex_lock(&con->lock);

	/* walk switches */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&con->alts, i, list) {
		if (!strcmp(i->name, sw) && !strcmp(i->data, data) &&
			(!registrar || !strcmp(i->registrar, registrar))) {
			/* found, remove from list */
			AST_LIST_REMOVE_CURRENT(&con->alts, list);
			free(i); /* free switch and return */
			ret = 0;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END

	ast_mutex_unlock(&con->lock);

	return ret;
}

/*
 * \note This functions lock contexts list, search for the right context,
 * call ast_context_remove_extension2, unlock contexts list and return.
 * In this function we are using
 */
int ast_context_remove_extension(const char *context, const char *extension, int priority, const char *registrar)
{
	return ast_context_remove_extension_callerid(context, extension, priority, NULL, 0, registrar);
}

int ast_context_remove_extension_callerid(const char *context, const char *extension, int priority, const char *callerid, int matchcid, const char *registrar)
{
	int ret = -1; /* default error return */
	struct ast_context *c = find_context_locked(context);

	if (c) { /* ... remove extension ... */
		ret = ast_context_remove_extension_callerid2(c, extension, priority, callerid, matchcid, registrar);
		ast_unlock_contexts();
	}
	return ret;
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
	return ast_context_remove_extension_callerid2(con, extension, priority, NULL, 0, registrar);
}

int ast_context_remove_extension_callerid2(struct ast_context *con, const char *extension, int priority, const char *callerid, int matchcid, const char *registrar)
{
	struct ast_exten *exten, *prev_exten = NULL;
	struct ast_exten *peer;
	struct ast_exten *previous_peer = NULL;
	struct ast_exten *next_peer = NULL;
	int found = 0;

	ast_mutex_lock(&con->lock);

	/* scan the extension list to find first matching extension-registrar */
	for (exten = con->root; exten; prev_exten = exten, exten = exten->next) {
		if (!strcmp(exten->exten, extension) &&
			(!registrar || !strcmp(exten->registrar, registrar)))
			break;
	}
	if (!exten) {
		/* we can't find right extension */
		ast_mutex_unlock(&con->lock);
		return -1;
	}

	/* scan the priority list to remove extension with exten->priority == priority */
	for (peer = exten, next_peer = exten->peer ? exten->peer : exten->next;
			peer && !strcmp(peer->exten, extension);
			peer = next_peer, next_peer = next_peer ? (next_peer->peer ? next_peer->peer : next_peer->next) : NULL) {
		if ((priority == 0 || peer->priority == priority) &&
				(!callerid || !matchcid || (matchcid && !strcmp(peer->cidmatch, callerid))) &&
				(!registrar || !strcmp(peer->registrar, registrar) )) {
			found = 1;

			/* we are first priority extension? */
			if (!previous_peer) {
				/*
				 * We are first in the priority chain, so must update the extension chain.
				 * The next node is either the next priority or the next extension
				 */
				struct ast_exten *next_node = peer->peer ? peer->peer : peer->next;

				if (!prev_exten) {	/* change the root... */
					con->root = next_node;
				} else {
					prev_exten->next = next_node; /* unlink */
				}
				if (peer->peer)	{ /* update the new head of the pri list */
					peer->peer->next = peer->next;
				}
			} else { /* easy, we are not first priority in extension */
				previous_peer->peer = peer->peer;
			}

			/* now, free whole priority extension */
			destroy_exten(peer);
		} else {
			previous_peer = peer;
		}
	}
	ast_mutex_unlock(&con->lock);
	return found ? 0 : -1;
}


/*!
 * \note This function locks contexts list by &conlist, searches for the right context
 * structure, and locks the macrolock mutex in that context.
 * macrolock is used to limit a macro to be executed by one call at a time.
 */
int ast_context_lockmacro(const char *context)
{
	struct ast_context *c = NULL;
	int ret = -1;

	ast_rdlock_contexts();

	while ((c = ast_walk_contexts(c))) {
		if (!strcmp(ast_get_context_name(c), context)) {
			ret = 0;
			break;
		}
	}

	ast_unlock_contexts();

	/* if we found context, lock macrolock */
	if (ret == 0) 
		ret = ast_mutex_lock(&c->macrolock);

	return ret;
}

/*!
 * \note This function locks contexts list by &conlist, searches for the right context
 * structure, and unlocks the macrolock mutex in that context.
 * macrolock is used to limit a macro to be executed by one call at a time.
 */
int ast_context_unlockmacro(const char *context)
{
	struct ast_context *c = NULL;
	int ret = -1;

	ast_rdlock_contexts();

	while ((c = ast_walk_contexts(c))) {
		if (!strcmp(ast_get_context_name(c), context)) {
			ret = 0;
			break;
		}
	}

	ast_unlock_contexts();

	/* if we found context, unlock macrolock */
	if (ret == 0) 
		ret = ast_mutex_unlock(&c->macrolock);

	return ret;
}

/*! \brief Dynamically register a new dial plan application */
int ast_register_application(const char *app, int (*execute)(struct ast_channel *, void *), const char *synopsis, const char *description)
{
	struct ast_app *tmp, *cur = NULL;
	char tmps[80];
	int length;

	AST_LIST_LOCK(&apps);
	AST_LIST_TRAVERSE(&apps, tmp, list) {
		if (!strcasecmp(app, tmp->name)) {
			ast_log(LOG_WARNING, "Already have an application '%s'\n", app);
			AST_LIST_UNLOCK(&apps);
			return -1;
		}
	}

	length = sizeof(*tmp) + strlen(app) + 1;

	if (!(tmp = ast_calloc(1, length))) {
		AST_LIST_UNLOCK(&apps);
		return -1;
	}

	strcpy(tmp->name, app);
	tmp->execute = execute;
	tmp->synopsis = synopsis;
	tmp->description = description;

	/* Store in alphabetical order */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&apps, cur, list) {
		if (strcasecmp(tmp->name, cur->name) < 0) {
			AST_LIST_INSERT_BEFORE_CURRENT(&apps, tmp, list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	if (!cur)
		AST_LIST_INSERT_TAIL(&apps, tmp, list);

	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered application '%s'\n", term_color(tmps, tmp->name, COLOR_BRCYAN, 0, sizeof(tmps)));

	AST_LIST_UNLOCK(&apps);

	return 0;
}

/*
 * Append to the list. We don't have a tail pointer because we need
 * to scan the list anyways to check for duplicates during insertion.
 */
int ast_register_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp;

	AST_LIST_LOCK(&switches);
	AST_LIST_TRAVERSE(&switches, tmp, list) {
		if (!strcasecmp(tmp->name, sw->name)) {
			AST_LIST_UNLOCK(&switches);
			ast_log(LOG_WARNING, "Switch '%s' already found\n", sw->name);
			return -1;
		}
	}
	AST_LIST_INSERT_TAIL(&switches, sw, list);
	AST_LIST_UNLOCK(&switches);

	return 0;
}

void ast_unregister_switch(struct ast_switch *sw)
{
	AST_LIST_LOCK(&switches);
	AST_LIST_REMOVE(&switches, sw, list);
	AST_LIST_UNLOCK(&switches);
}

/*
 * Help for CLI commands ...
 */

#ifdef AST_DEVMODE
static char show_device2extenstate_help[] =
"Usage: core show device2extenstate\n"
"       Lists device state to extension state combinations.\n";
#endif

static char show_applications_help[] =
"Usage: core show applications [{like|describing} <text>]\n"
"       List applications which are currently available.\n"
"       If 'like', <text> will be a substring of the app name\n"
"       If 'describing', <text> will be a substring of the description\n";

static char show_functions_help[] =
"Usage: core show functions [like <text>]\n"
"       List builtin functions, optionally only those matching a given string\n";

static char show_switches_help[] =
"Usage: core show switches\n"
"       List registered switches\n";

static char show_hints_help[] =
"Usage: core show hints\n"
"       List registered hints\n";

static char show_globals_help[] =
"Usage: core show globals\n"
"       List current global dialplan variables and their values\n";

static char show_application_help[] =
"Usage: core show application <application> [<application> [<application> [...]]]\n"
"       Describes a particular application.\n";

static char show_function_help[] =
"Usage: core show function <function>\n"
"       Describe a particular dialplan function.\n";

static char show_dialplan_help[] =
"Usage: dialplan show [exten@][context]\n"
"       Show dialplan\n";

static char set_global_help[] =
"Usage: core set global <name> <value>\n"
"       Set global dialplan variable <name> to <value>\n";


/*
 * \brief 'show application' CLI command implementation functions ...
 */

/*
 * There is a possibility to show informations about more than one
 * application at one time. You can type 'show application Dial Echo' and
 * you will see informations about these two applications ...
 */
static char *complete_show_application(const char *line, const char *word, int pos, int state)
{
	struct ast_app *a;
	char *ret = NULL;
	int which = 0;
	int wordlen = strlen(word);

	/* return the n-th [partial] matching entry */
	AST_LIST_LOCK(&apps);
	AST_LIST_TRAVERSE(&apps, a, list) {
		if (!strncasecmp(word, a->name, wordlen) && ++which > state) {
			ret = strdup(a->name);
			break;
		}
	}
	AST_LIST_UNLOCK(&apps);

	return ret;
}

static int handle_show_application_deprecated(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	int app, no_registered_app = 1;

	if (argc < 3)
		return RESULT_SHOWUSAGE;

	/* ... go through all applications ... */
	AST_LIST_LOCK(&apps);
	AST_LIST_TRAVERSE(&apps, a, list) {
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
	}
	AST_LIST_UNLOCK(&apps);

	/* we found at least one app? no? */
	if (no_registered_app) {
		ast_cli(fd, "Your application(s) is (are) not registered\n");
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int handle_show_application(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	int app, no_registered_app = 1;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	/* ... go through all applications ... */
	AST_LIST_LOCK(&apps);
	AST_LIST_TRAVERSE(&apps, a, list) {
		/* ... compare this application name with all arguments given
		 * to 'show application' command ... */
		for (app = 3; app < argc; app++) {
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
	}
	AST_LIST_UNLOCK(&apps);

	/* we found at least one app? no? */
	if (no_registered_app) {
		ast_cli(fd, "Your application(s) is (are) not registered\n");
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

/*! \brief  handle_show_hints: CLI support for listing registered dial plan hints */
static int handle_show_hints(int fd, int argc, char *argv[])
{
	struct ast_hint *hint;
	int num = 0;
	int watchers;
	struct ast_state_cb *watcher;
	struct ao2_iterator i;

	if (ao2_container_count(hints) == 0) {
		ast_cli(fd, "There are no registered dialplan hints\n");
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "\n    -= Registered Asterisk Dial Plan Hints =-\n");

	i = ao2_iterator_init(hints, 0);
	for (hint = ao2_iterator_next(&i); hint; ao2_ref(hint, -1), hint = ao2_iterator_next(&i)) {
		watchers = 0;
		for (watcher = hint->callbacks; watcher; watcher = watcher->next) {
			watchers++;
		}
		ast_cli(fd, "   %20s@%-20.20s: %-20.20s  State:%-15.15s Watchers %2d\n",
			ast_get_extension_name(hint->exten),
			ast_get_context_name(ast_get_extension_context(hint->exten)),
			ast_get_extension_app(hint->exten),
			ast_extension_state2str(hint->laststate), watchers);
		num++;
	}
	ao2_iterator_destroy(&i);

	ast_cli(fd, "----------------\n");
	ast_cli(fd, "- %d hints registered\n", num);

	return RESULT_SUCCESS;
}

/*! \brief  handle_show_switches: CLI support for listing registered dial plan switches */
static int handle_show_switches(int fd, int argc, char *argv[])
{
	struct ast_switch *sw;

	AST_LIST_LOCK(&switches);

	if (AST_LIST_EMPTY(&switches)) {
		AST_LIST_UNLOCK(&switches);
		ast_cli(fd, "There are no registered alternative switches\n");
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "\n    -= Registered Asterisk Alternative Switches =-\n");
	AST_LIST_TRAVERSE(&switches, sw, list)
		ast_cli(fd, "%s: %s\n", sw->name, sw->description);

	AST_LIST_UNLOCK(&switches);

	return RESULT_SUCCESS;
}

/*
 * 'show applications' CLI command implementation functions ...
 */
static int handle_show_applications_deprecated(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	int like = 0, describing = 0;
	int total_match = 0; 	/* Number of matches in like clause */
	int total_apps = 0; 	/* Number of apps registered */

	AST_LIST_LOCK(&apps);

	if (AST_LIST_EMPTY(&apps)) {
		ast_cli(fd, "There are no registered applications\n");
		AST_LIST_UNLOCK(&apps);
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

	AST_LIST_TRAVERSE(&apps, a, list) {
		int printapp = 0;
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
				for (i = 3; i < argc; i++) {
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

	AST_LIST_UNLOCK(&apps);

	return RESULT_SUCCESS;
}
static int handle_show_applications(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	int like = 0, describing = 0;
	int total_match = 0; 	/* Number of matches in like clause */
	int total_apps = 0; 	/* Number of apps registered */

	AST_LIST_LOCK(&apps);

	if (AST_LIST_EMPTY(&apps)) {
		ast_cli(fd, "There are no registered applications\n");
		AST_LIST_UNLOCK(&apps);
		return -1;
	}

	/* core list applications like <keyword> */
	if ((argc == 5) && (!strcmp(argv[3], "like"))) {
		like = 1;
	} else if ((argc > 4) && (!strcmp(argv[3], "describing"))) {
		describing = 1;
	}

	/* core list applications describing <keyword1> [<keyword2>] [...] */
	if ((!like) && (!describing)) {
		ast_cli(fd, "    -= Registered Asterisk Applications =-\n");
	} else {
		ast_cli(fd, "    -= Matching Asterisk Applications =-\n");
	}

	AST_LIST_TRAVERSE(&apps, a, list) {
		int printapp = 0;
		total_apps++;
		if (like) {
			if (strcasestr(a->name, argv[4])) {
				printapp = 1;
				total_match++;
			}
		} else if (describing) {
			if (a->description) {
				/* Match all words on command line */
				int i;
				printapp = 1;
				for (i = 4; i < argc; i++) {
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

	AST_LIST_UNLOCK(&apps);

	return RESULT_SUCCESS;
}

static char *complete_show_applications_deprecated(const char *line, const char *word, int pos, int state)
{
	static char* choices[] = { "like", "describing", NULL };

	return (pos != 2) ? NULL : ast_cli_complete(word, choices, state);
}

static char *complete_show_applications(const char *line, const char *word, int pos, int state)
{
	static char* choices[] = { "like", "describing", NULL };

	return (pos != 3) ? NULL : ast_cli_complete(word, choices, state);
}

/*
 * 'show dialplan' CLI command implementation functions ...
 */
static char *complete_show_dialplan_context(const char *line, const char *word, int pos,
	int state)
{
	struct ast_context *c = NULL;
	char *ret = NULL;
	int which = 0;
	int wordlen;

	/* we are do completion of [exten@]context on second position only */
	if (pos != 2)
		return NULL;

	ast_rdlock_contexts();

	wordlen = strlen(word);

	/* walk through all contexts and return the n-th match */
	while ( (c = ast_walk_contexts(c)) ) {
		if (!strncasecmp(word, ast_get_context_name(c), wordlen) && ++which > state) {
			ret = ast_strdup(ast_get_context_name(c));
			break;
		}
	}

	ast_unlock_contexts();

	return ret;
}

struct dialplan_counters {
	int total_context;
	int total_exten;
	int total_prio;
	int context_existence;
	int extension_existence;
};

/*! \brief helper function to print an extension */
static void print_ext(struct ast_exten *e, char * buf, int buflen)
{
	int prio = ast_get_extension_priority(e);
	if (prio == PRIORITY_HINT) {
		snprintf(buf, buflen, "hint: %s",
			ast_get_extension_app(e));
	} else {
		snprintf(buf, buflen, "%d. %s(%s)",
			prio, ast_get_extension_app(e),
			(!ast_strlen_zero(ast_get_extension_app_data(e)) ? (char *)ast_get_extension_app_data(e) : ""));
	}
}

/* XXX not verified */
static int show_dialplan_helper(int fd, const char *context, const char *exten, struct dialplan_counters *dpc, struct ast_include *rinclude, int includecount, const char *includes[])
{
	struct ast_context *c = NULL;
	int res = 0, old_total_exten = dpc->total_exten;

	ast_rdlock_contexts();

	/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		struct ast_exten *e;
		struct ast_include *i;
		struct ast_ignorepat *ip;
		char buf[256], buf2[256];
		int context_info_printed = 0;

		if (context && strcmp(ast_get_context_name(c), context))
			continue;	/* skip this one, name doesn't match */

		dpc->context_existence = 1;

		ast_lock_context(c);

		/* are we looking for exten too? if yes, we print context
		 * only if we find our extension.
		 * Otherwise print context even if empty ?
		 * XXX i am not sure how the rinclude is handled.
		 * I think it ought to go inside.
		 */
		if (!exten) {
			dpc->total_context++;
			ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
				ast_get_context_name(c), ast_get_context_registrar(c));
			context_info_printed = 1;
		}

		/* walk extensions ... */
		e = NULL;
		while ( (e = ast_walk_context_extensions(c, e)) ) {
			struct ast_exten *p;

			if (exten && !ast_extension_match(ast_get_extension_name(e), exten))
				continue;	/* skip, extension match failed */

			dpc->extension_existence = 1;

			/* may we print context info? */
			if (!context_info_printed) {
				dpc->total_context++;
				if (rinclude) { /* TODO Print more info about rinclude */
					ast_cli(fd, "[ Included context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
				} else {
					ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
				}
				context_info_printed = 1;
			}
			dpc->total_prio++;

			/* write extension name and first peer */
			if (e->matchcid)
				snprintf(buf, sizeof(buf), "'%s' (CID match '%s') => ", ast_get_extension_name(e), e->cidmatch);
			else
				snprintf(buf, sizeof(buf), "'%s' =>", ast_get_extension_name(e));

			print_ext(e, buf2, sizeof(buf2));

			ast_cli(fd, "  %-17s %-45s [%s]\n", buf, buf2,
				ast_get_extension_registrar(e));

			dpc->total_exten++;
			/* walk next extension peers */
			p = e;	/* skip the first one, we already got it */
			while ( (p = ast_walk_extension_priorities(e, p)) ) {
				const char *el = ast_get_extension_label(p);
				dpc->total_prio++;
				if (el)
					snprintf(buf, sizeof(buf), "   [%s]", el);
				else
					buf[0] = '\0';
				print_ext(p, buf2, sizeof(buf2));

				ast_cli(fd,"  %-17s %-45s [%s]\n", buf, buf2,
					ast_get_extension_registrar(p));
			}
		}

		/* walk included and write info ... */
		i = NULL;
		while ( (i = ast_walk_context_includes(c, i)) ) {
			snprintf(buf, sizeof(buf), "'%s'", ast_get_include_name(i));
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
						includes[includecount] = ast_get_include_name(i);
						show_dialplan_helper(fd, ast_get_include_name(i), exten, dpc, i, includecount + 1, includes);
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
		ip = NULL;
		while ( (ip = ast_walk_context_ignorepats(c, ip)) ) {
			const char *ipname = ast_get_ignorepat_name(ip);
			char ignorepat[AST_MAX_EXTENSION];
			snprintf(buf, sizeof(buf), "'%s'", ipname);
			snprintf(ignorepat, sizeof(ignorepat), "_%s.", ipname);
			if (!exten || ast_extension_match(ignorepat, exten)) {
				ast_cli(fd, "  Ignore pattern => %-45s [%s]\n",
					buf, ast_get_ignorepat_registrar(ip));
			}
		}
		if (!rinclude) {
			struct ast_sw *sw = NULL;
			while ( (sw = ast_walk_context_switches(c, sw)) ) {
				snprintf(buf, sizeof(buf), "'%s/%s'",
					ast_get_switch_name(sw),
					ast_get_switch_data(sw));
				ast_cli(fd, "  Alt. Switch =>    %-45s [%s]\n",
					buf, ast_get_switch_registrar(sw));
			}
		}

		ast_unlock_context(c);

		/* if we print something in context, make an empty line */
		if (context_info_printed)
			ast_cli(fd, "\n");
	}
	ast_unlock_contexts();

	return (dpc->total_exten == old_total_exten) ? -1 : res;
}

static int handle_show_dialplan(int fd, int argc, char *argv[])
{
	char *exten = NULL, *context = NULL;
	/* Variables used for different counters */
	struct dialplan_counters counters;

	const char *incstack[AST_PBX_MAX_STACK];
	memset(&counters, 0, sizeof(counters));

	if (argc != 2 && argc != 3)
		return RESULT_SHOWUSAGE;

	/* we obtain [exten@]context? if yes, split them ... */
	if (argc == 3) {
		if (strchr(argv[2], '@')) {	/* split into exten & context */
			context = ast_strdupa(argv[2]);
			exten = strsep(&context, "@");
			/* change empty strings to NULL */
			if (ast_strlen_zero(exten))
				exten = NULL;
		} else { /* no '@' char, only context given */
			context = argv[2];
		}
		if (ast_strlen_zero(context))
			context = NULL;
	}
	/* else Show complete dial plan, context and exten are NULL */
	show_dialplan_helper(fd, context, exten, &counters, NULL, 0, incstack);

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

/*! \brief CLI support for listing global variables in a parseable way */
static int handle_show_globals(int fd, int argc, char *argv[])
{
	int i = 0;
	struct ast_var_t *newvariable;

	ast_mutex_lock(&globalslock);
	AST_LIST_TRAVERSE (&globals, newvariable, entries) {
		i++;
		ast_cli(fd, "   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_mutex_unlock(&globalslock);
	ast_cli(fd, "\n    -- %d variables\n", i);

	return RESULT_SUCCESS;
}

/*! \brief  CLI support for setting global variables */
static int handle_set_global_deprecated(int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	pbx_builtin_setvar_helper(NULL, argv[2], argv[3]);
	ast_cli(fd, "\n    -- Global variable %s set to %s\n", argv[2], argv[3]);

	return RESULT_SUCCESS;
}


static int handle_set_global(int fd, int argc, char *argv[])
{
	if (argc != 5)
		return RESULT_SHOWUSAGE;

	pbx_builtin_setvar_helper(NULL, argv[3], argv[4]);
	ast_cli(fd, "\n    -- Global variable %s set to %s\n", argv[3], argv[4]);

	return RESULT_SUCCESS;
}
#ifdef AST_DEVMODE
static int handle_show_device2extenstate(int fd, int argc, char *argv[])
{
	struct ast_devstate_aggregate agg;
	int i, j, exten, combined;

	for (i = 0; i < AST_DEVICE_TOTAL; i++) {
		for (j = 0; j < AST_DEVICE_TOTAL; j++) {
			ast_devstate_aggregate_init(&agg);
			ast_devstate_aggregate_add(&agg, i);
			ast_devstate_aggregate_add(&agg, j);
			combined = ast_devstate_aggregate_result(&agg);
			exten = ast_devstate_to_extenstate(combined);
			ast_cli(fd, "\n Exten:%14s  CombinedDevice:%12s  Dev1:%12s  Dev2:%12s", ast_extension_state2str(exten), devstate2str(combined), devstate2str(j), devstate2str(i));
		}
	}
	ast_cli(fd, "\n");
	return RESULT_SUCCESS;
}
#endif

/*
 * CLI entries for upper commands ...
 */
static struct ast_cli_entry cli_show_applications_deprecated = {
	{ "show", "applications", NULL },
	handle_show_applications_deprecated, NULL,
	NULL, complete_show_applications_deprecated };

static struct ast_cli_entry cli_show_functions_deprecated = {
	{ "show", "functions", NULL },
	handle_show_functions_deprecated, NULL,
        NULL };

static struct ast_cli_entry cli_show_switches_deprecated = {
	{ "show", "switches", NULL },
	handle_show_switches, NULL,
        NULL };

static struct ast_cli_entry cli_show_hints_deprecated = {
	{ "show", "hints", NULL },
	handle_show_hints, NULL,
        NULL };

static struct ast_cli_entry cli_show_globals_deprecated = {
	{ "show", "globals", NULL },
	handle_show_globals, NULL,
        NULL };

static struct ast_cli_entry cli_show_function_deprecated = {
	{ "show" , "function", NULL },
	handle_show_function_deprecated, NULL,
        NULL, complete_show_function };

static struct ast_cli_entry cli_show_application_deprecated = {
	{ "show", "application", NULL },
	handle_show_application_deprecated, NULL,
        NULL, complete_show_application };

static struct ast_cli_entry cli_show_dialplan_deprecated = {
	{ "show", "dialplan", NULL },
	handle_show_dialplan, NULL,
        NULL, complete_show_dialplan_context };

static struct ast_cli_entry cli_set_global_deprecated = {
	{ "set", "global", NULL },
	handle_set_global_deprecated, NULL,
        NULL };

static struct ast_cli_entry pbx_cli[] = {
	{ { "core", "show", "applications", NULL },
	handle_show_applications, "Shows registered dialplan applications",
	show_applications_help, complete_show_applications, &cli_show_applications_deprecated },

	{ { "core", "show", "functions", NULL },
	handle_show_functions, "Shows registered dialplan functions",
	show_functions_help, NULL, &cli_show_functions_deprecated },

	{ { "core", "show", "switches", NULL },
	handle_show_switches, "Show alternative switches",
	show_switches_help, NULL, &cli_show_switches_deprecated },

	{ { "core", "show", "hints", NULL },
	handle_show_hints, "Show dialplan hints",
	show_hints_help, NULL, &cli_show_hints_deprecated },

	{ { "core", "show", "globals", NULL },
	handle_show_globals, "Show global dialplan variables",
	show_globals_help, NULL, &cli_show_globals_deprecated },

#ifdef AST_DEVMODE
	{ { "core", "show", "device2extenstate", NULL },
	handle_show_device2extenstate, "Show expected exten state from multiple device states",
	show_device2extenstate_help, NULL, NULL },
#endif

	{ { "core", "show" , "function", NULL },
	handle_show_function, "Describe a specific dialplan function",
	show_function_help, complete_show_function, &cli_show_function_deprecated },

	{ { "core", "show", "application", NULL },
	handle_show_application, "Describe a specific dialplan application",
	show_application_help, complete_show_application, &cli_show_application_deprecated },

	{ { "core", "set", "global", NULL },
	handle_set_global, "Set global dialplan variable",
	set_global_help, NULL, &cli_set_global_deprecated },

	{ { "dialplan", "show", NULL },
	handle_show_dialplan, "Show dialplan",
	show_dialplan_help, complete_show_dialplan_context, &cli_show_dialplan_deprecated },
};

int ast_unregister_application(const char *app)
{
	struct ast_app *tmp;

	AST_LIST_LOCK(&apps);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&apps, tmp, list) {
		if (!strcasecmp(app, tmp->name)) {
			AST_LIST_REMOVE_CURRENT(&apps, list);
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Unregistered application '%s'\n", tmp->name);
			free(tmp);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&apps);

	return tmp ? 0 : -1;
}

static struct ast_context *__ast_context_create(struct ast_context **extcontexts, const char *name, const char *registrar, int existsokay)
{
	struct ast_context *tmp, **local_contexts;
	int length = sizeof(struct ast_context) + strlen(name) + 1;

	if (!extcontexts) {
		ast_rdlock_contexts();
		local_contexts = &contexts;
	} else
		local_contexts = extcontexts;

	for (tmp = *local_contexts; tmp; tmp = tmp->next) {
		if (!strcasecmp(tmp->name, name)) {
			if (!existsokay) {
				ast_log(LOG_WARNING, "Tried to register context '%s', already in use\n", name);
				tmp = NULL;
			}
			if (!extcontexts)
				ast_unlock_contexts();
			return tmp;
		}
	}
	
	if (!extcontexts)
		ast_unlock_contexts();

	if ((tmp = ast_calloc(1, length))) {
		ast_mutex_init(&tmp->lock);
		ast_mutex_init(&tmp->macrolock);
		strcpy(tmp->name, name);
		tmp->registrar = registrar;
		if (!extcontexts)
			ast_wrlock_contexts();
		tmp->next = *local_contexts;
		*local_contexts = tmp;
		if (!extcontexts)
			ast_unlock_contexts();
		if (option_debug)
			ast_log(LOG_DEBUG, "Registered context '%s'\n", tmp->name);
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Registered extension context '%s'\n", tmp->name);
	}

	return tmp;
}

struct ast_context *ast_context_create(struct ast_context **extcontexts, const char *name, const char *registrar)
{
	return __ast_context_create(extcontexts, name, registrar, 0);
}

struct ast_context *ast_context_find_or_create(struct ast_context **extcontexts, const char *name, const char *registrar)
{
	return __ast_context_create(extcontexts, name, registrar, 1);
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

/* XXX this does not check that multiple contexts are merged */
void ast_merge_contexts_and_delete(struct ast_context **extcontexts, const char *registrar)
{
	struct ast_context *tmp, *lasttmp = NULL;
	struct store_hints store = AST_LIST_HEAD_INIT_VALUE;
	struct store_hint *this;
	struct ast_hint *hint;
	struct ast_exten *exten;
	int length;
	struct ast_state_cb *thiscb, *prevcb;
	struct ao2_iterator i;

	/* it is very important that this function hold the hint list lock _and_ the conlock
	   during its operation; not only do we need to ensure that the list of contexts
	   and extensions does not change, but also that no hint callbacks (watchers) are
	   added or removed during the merge/delete process

	   in addition, the locks _must_ be taken in this order, because there are already
	   other code paths that use this order
	*/
	ast_wrlock_contexts();
	ao2_lock(hints);

	/* preserve all watchers for hints associated with this registrar */
	i = ao2_iterator_init(hints, AO2_ITERATOR_DONTLOCK);
	for (hint = ao2_iterator_next(&i); hint; ao2_ref(hint, -1), hint = ao2_iterator_next(&i)) {
		if (hint->callbacks && !strcmp(registrar, hint->exten->parent->registrar)) {
			length = strlen(hint->exten->exten) + strlen(hint->exten->parent->name) + 2 + sizeof(*this);
			if (!(this = ast_calloc(1, length))) {
				continue;
			}
			ao2_lock(hint);

			if (hint->exten == NULL) {
				ao2_unlock(hint);
				continue;
			}

			this->callbacks = hint->callbacks;
			hint->callbacks = NULL;
			this->laststate = hint->laststate;
			ao2_unlock(hint);
			this->context = this->data;
			strcpy(this->data, hint->exten->parent->name);
			this->exten = this->data + strlen(this->context) + 1;
			strcpy(this->exten, hint->exten->exten);
			AST_LIST_INSERT_HEAD(&store, this, list);
		}
	}

	tmp = *extcontexts;
	if (registrar) {
		/* XXX remove previous contexts from same registrar */
		if (option_debug)
			ast_log(LOG_DEBUG, "must remove any reg %s\n", registrar);
		__ast_context_destroy(NULL,registrar);
		while (tmp) {
			lasttmp = tmp;
			tmp = tmp->next;
		}
	} else {
		/* XXX remove contexts with the same name */
		while (tmp) {
			ast_log(LOG_WARNING, "must remove %s  reg %s\n", tmp->name, tmp->registrar);
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

	/* restore the watchers for hints that can be found; notify those that
	   cannot be restored
	*/
	while ((this = AST_LIST_REMOVE_HEAD(&store, list))) {
		struct pbx_find_info q = { .stacklen = 0 };
		exten = pbx_find_extension(NULL, NULL, &q, this->context, this->exten, PRIORITY_HINT, NULL, "", E_MATCH);
		hint = ao2_find(hints, exten, 0);
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
			while (thiscb->next) {
				thiscb = thiscb->next;
			}
			ao2_lock(hint);
			thiscb->next = hint->callbacks;
			hint->callbacks = this->callbacks;
			hint->laststate = this->laststate;
			ao2_unlock(hint);
		}
		if (hint) {
			ao2_ref(hint, -1);
		}
		free(this);
	}

	ao2_unlock(hints);
	ast_unlock_contexts();

	return;
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int ast_context_add_include(const char *context, const char *include, const char *registrar)
{
	int ret = -1;
	struct ast_context *c = find_context_locked(context);

	if (c) {
		ret = ast_context_add_include2(c, include, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

/*! \brief Helper for get_range.
 * return the index of the matching entry, starting from 1.
 * If names is not supplied, try numeric values.
 */
static int lookup_name(const char *s, char *const names[], int max)
{
	int i;

	if (names) {
		for (i = 0; names[i]; i++) {
			if (!strcasecmp(s, names[i]))
				return i+1;
		}
	} else if (sscanf(s, "%30d", &i) == 1 && i >= 1 && i <= max) {
		return i;
	}
	return 0; /* error return */
}

/*! \brief helper function to return a range up to max (7, 12, 31 respectively).
 * names, if supplied, is an array of names that should be mapped to numbers.
 */
static unsigned get_range(char *src, int max, char *const names[], const char *msg)
{
	int s, e; /* start and ending position */
	unsigned int mask = 0;

	/* Check for whole range */
	if (ast_strlen_zero(src) || !strcmp(src, "*")) {
		s = 0;
		e = max - 1;
	} else {
		/* Get start and ending position */
		char *c = strchr(src, '-');
		if (c)
			*c++ = '\0';
		/* Find the start */
		s = lookup_name(src, names, max);
		if (!s) {
			ast_log(LOG_WARNING, "Invalid %s '%s', assuming none\n", msg, src);
			return 0;
		}
		s--;
		if (c) { /* find end of range */
			e = lookup_name(c, names, max);
			if (!e) {
				ast_log(LOG_WARNING, "Invalid end %s '%s', assuming none\n", msg, c);
				return 0;
			}
			e--;
		} else
			e = s;
	}
	/* Fill the mask. Remember that ranges are cyclic */
	mask = 1 << e;	/* initialize with last element */
	while (s != e) {
		if (s >= max) {
			s = 0;
			mask |= (1 << s);
		} else {
			mask |= (1 << s);
			s++;
		}
	}
	return mask;
}

/*! \brief store a bitmask of valid times, one bit each 2 minute */
static void get_timerange(struct ast_timing *i, char *times)
{
	char *e;
	int x;
	int s1, s2;
	int e1, e2;
	/*	int cth, ctm; */

	/* start disabling all times, fill the fields with 0's, as they may contain garbage */
	memset(i->minmask, 0, sizeof(i->minmask));

	/* 2-minutes per bit, since the mask has only 32 bits :( */
	/* Star is all times */
	if (ast_strlen_zero(times) || !strcmp(times, "*")) {
		for (x=0; x<24; x++)
			i->minmask[x] = 0x3fffffff; /* 30 bits */
		return;
	}
	/* Otherwise expect a range */
	e = strchr(times, '-');
	if (!e) {
		ast_log(LOG_WARNING, "Time range is not valid. Assuming no restrictions based on time.\n");
		return;
	}
	*e++ = '\0';
	/* XXX why skip non digits ? */
	while (*e && !isdigit(*e))
		e++;
	if (!*e) {
		ast_log(LOG_WARNING, "Invalid time range.  Assuming no restrictions based on time.\n");
		return;
	}
	if (sscanf(times, "%2d:%2d", &s1, &s2) != 2) {
		ast_log(LOG_WARNING, "%s isn't a time.  Assuming no restrictions based on time.\n", times);
		return;
	}
	if (sscanf(e, "%2d:%2d", &e1, &e2) != 2) {
		ast_log(LOG_WARNING, "%s isn't a time.  Assuming no restrictions based on time.\n", e);
		return;
	}
	/* XXX this needs to be optimized */
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
	NULL,
};

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
	NULL,
};

int ast_build_timing(struct ast_timing *i, const char *info_in)
{
	char info_save[256];
	char *info;

	/* Check for empty just in case */
	if (ast_strlen_zero(info_in))
		return 0;
	/* make a copy just in case we were passed a static string */
	ast_copy_string(info_save, info_in, sizeof(info_save));
	info = info_save;
	/* Assume everything except time */
	i->monthmask = 0xfff;	/* 12 bits */
	i->daymask = 0x7fffffffU; /* 31 bits */
	i->dowmask = 0x7f; /* 7 bits */
	/* on each call, use strsep() to move info to the next argument */
	get_timerange(i, strsep(&info, "|"));
	if (info)
		i->dowmask = get_range(strsep(&info, "|"), 7, days, "day of week");
	if (info)
		i->daymask = get_range(strsep(&info, "|"), 31, NULL, "day");
	if (info)
		i->monthmask = get_range(strsep(&info, "|"), 12, months, "month");
	return 1;
}

int ast_check_timing(const struct ast_timing *i)
{
	struct tm tm;
	time_t t = time(NULL);

	ast_localtime(&t, &tm, NULL);

	/* If it's not the right month, return */
	if (!(i->monthmask & (1 << tm.tm_mon)))
		return 0;

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
	if (!(new_include = ast_calloc(1, length)))
		return -1;
	/* Fill in this structure. Use 'p' for assignments, as the fields
	 * in the structure are 'const char *'
	 */
	p = new_include->stuff;
	new_include->name = p;
	strcpy(p, value);
	p += strlen(value) + 1;
	new_include->rname = p;
	strcpy(p, value);
	/* Strip off timing info, and process if it is there */
	if ( (c = strchr(p, '|')) ) {
		*c++ = '\0';
	        new_include->hastime = ast_build_timing(&(new_include->timing), c);
	}
	new_include->next      = NULL;
	new_include->registrar = registrar;

	ast_mutex_lock(&con->lock);

	/* ... go to last include and check if context is already included too... */
	for (i = con->includes; i; i = i->next) {
		if (!strcasecmp(i->name, new_include->name)) {
			free(new_include);
			ast_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		il = i;
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
	int ret = -1;
	struct ast_context *c = find_context_locked(context);

	if (c) { /* found, add switch to this context */
		ret = ast_context_add_switch2(c, sw, data, eval, registrar);
		ast_unlock_contexts();
	}
	return ret;
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
	struct ast_sw *i;
	int length;
	char *p;

	length = sizeof(struct ast_sw);
	length += strlen(value) + 1;
	if (data)
		length += strlen(data);
	length++;

	/* allocate new sw structure ... */
	if (!(new_sw = ast_calloc(1, length)))
		return -1;
	/* ... fill in this structure ... */
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
	new_sw->eval	  = eval;
	new_sw->registrar = registrar;

	/* ... try to lock this context ... */
	ast_mutex_lock(&con->lock);

	/* ... go to last sw and check if context is already swd too... */
	AST_LIST_TRAVERSE(&con->alts, i, list) {
		if (!strcasecmp(i->name, new_sw->name) && !strcasecmp(i->data, new_sw->data)) {
			free(new_sw);
			ast_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
	}

	/* ... sw new context into context list, unlock, return */
	AST_LIST_INSERT_TAIL(&con->alts, new_sw, list);

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
	int ret = -1;
	struct ast_context *c = find_context_locked(context);

	if (c) {
		ret = ast_context_remove_ignorepat2(c, ignorepat, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_context_remove_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar)
{
	struct ast_ignorepat *ip, *ipl = NULL;

	ast_mutex_lock(&con->lock);

	for (ip = con->ignorepats; ip; ip = ip->next) {
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
		ipl = ip;
	}

	ast_mutex_unlock(&con->lock);
	errno = EINVAL;
	return -1;
}

/*
 * EBUSY - can't lock
 * ENOENT - there is no existence of context
 */
int ast_context_add_ignorepat(const char *context, const char *value, const char *registrar)
{
	int ret = -1;
	struct ast_context *c = find_context_locked(context);

	if (c) {
		ret = ast_context_add_ignorepat2(c, value, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	struct ast_ignorepat *ignorepat, *ignorepatc, *ignorepatl = NULL;
	int length;
	char *pattern;
	length = sizeof(struct ast_ignorepat);
	length += strlen(value) + 1;
	if (!(ignorepat = ast_calloc(1, length)))
		return -1;
	/* The cast to char * is because we need to write the initial value.
	 * The field is not supposed to be modified otherwise.  Also, gcc 4.2
	 * sees the cast as dereferencing a type-punned pointer and warns about
	 * it.  This is the workaround (we're telling gcc, yes, that's really
	 * what we wanted to do).
	 */
	pattern = (char *) ignorepat->pattern;
	strcpy(pattern, value);
	ignorepat->next = NULL;
	ignorepat->registrar = registrar;
	ast_mutex_lock(&con->lock);
	for (ignorepatc = con->ignorepats; ignorepatc; ignorepatc = ignorepatc->next) {
		ignorepatl = ignorepatc;
		if (!strcasecmp(ignorepatc->pattern, value)) {
			/* Already there */
			ast_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
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
	struct ast_context *con = ast_context_find(context);
	if (con) {
		struct ast_ignorepat *pat;
		for (pat = con->ignorepats; pat; pat = pat->next) {
			if (ast_extension_match(pat->pattern, pattern))
				return 1;
		}
	}

	return 0;
}

/*
 * EBUSY   - can't lock
 * ENOENT  - no existence of context
 *
 */
int ast_add_extension(const char *context, int replace, const char *extension,
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar)
{
	int ret = -1;
	struct ast_context *c = find_context_locked(context);

	if (c) {
		ret = ast_add_extension2(c, replace, extension, priority, label, callerid,
			application, data, datad, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_explicit_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	if (!chan)
		return -1;

	ast_channel_lock(chan);

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

	ast_channel_unlock(chan);

	return 0;
}

int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	int res = 0;

	ast_channel_lock(chan);

	if (chan->pbx) { /* This channel is currently in the PBX */
		ast_explicit_goto(chan, context, exten, priority);
		ast_softhangup_nolock(chan, AST_SOFTHANGUP_ASYNCGOTO);
	} else {
		/* In order to do it when the channel doesn't really exist within
		   the PBX, we have to make a new channel, masquerade, and start the PBX
		   at the new location */
		struct ast_channel *tmpchan = ast_channel_alloc(0, chan->_state, 0, 0, chan->accountcode, chan->exten, chan->context, chan->amaflags, "AsyncGoto/%s", chan->name);
		if (!tmpchan) {
			res = -1;
		} else {
			if (chan->cdr) {
				ast_cdr_discard(tmpchan->cdr);
				tmpchan->cdr = ast_cdr_dup(chan->cdr);  /* share the love */
			}
			/* Make formats okay */
			tmpchan->readformat = chan->readformat;
			tmpchan->writeformat = chan->writeformat;
			/* Setup proper location */
			ast_explicit_goto(tmpchan,
				S_OR(context, chan->context), S_OR(exten, chan->exten), priority);

			/* Masquerade into temp channel */
			if (ast_channel_masquerade(tmpchan, chan)) {
				/* Failed to set up the masquerade.  It's probably chan_local
				 * in the middle of optimizing itself out.  Sad. :( */
				ast_hangup(tmpchan);
				tmpchan = NULL;
				res = -1;
			} else {
				/* Grab the locks and get going */
				ast_channel_lock(tmpchan);
				ast_do_masquerade(tmpchan);
				ast_channel_unlock(tmpchan);
				/* Start the PBX going on our stolen channel */
				if (ast_pbx_start(tmpchan)) {
					ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmpchan->name);
					ast_hangup(tmpchan);
					res = -1;
				}
			}
		}
	}
	ast_channel_unlock(chan);
	return res;
}

int ast_async_goto_by_name(const char *channame, const char *context, const char *exten, int priority)
{
	struct ast_channel *chan;
	int res = -1;

	chan = ast_get_channel_by_name_locked(channame);
	if (chan) {
		res = ast_async_goto(chan, context, exten, priority);
		ast_channel_unlock(chan);
	}
	return res;
}

/*! \brief copy a string skipping whitespace */
static int ext_strncpy(char *dst, const char *src, int len)
{
	int count=0;

	while (*src && (count < len - 1)) {
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

/*! \brief add the extension in the priority chain.
 * returns 0 on success, -1 on failure
 */
static int add_pri(struct ast_context *con, struct ast_exten *tmp,
	struct ast_exten *el, struct ast_exten *e, int replace)
{
	struct ast_exten *ep;

	for (ep = NULL; e ; ep = e, e = e->peer) {
		if (e->priority >= tmp->priority)
			break;
	}
	if (!e) {	/* go at the end, and ep is surely set because the list is not empty */
		ep->peer = tmp;
		return 0;	/* success */
	}
	if (e->priority == tmp->priority) {
		/* Can't have something exactly the same.  Is this a
		   replacement?  If so, replace, otherwise, bonk. */
		if (!replace) {
			ast_log(LOG_WARNING, "Unable to register extension '%s', priority %d in '%s', already in use\n", tmp->exten, tmp->priority, con->name);
			if (tmp->datad)
				tmp->datad(tmp->data);
			free(tmp);
			return -1;
		}
		/* we are replacing e, so copy the link fields and then update
		 * whoever pointed to e to point to us
		 */
		tmp->next = e->next;	/* not meaningful if we are not first in the peer list */
		tmp->peer = e->peer;	/* always meaningful */
		if (ep)			/* We're in the peer list, just insert ourselves */
			ep->peer = tmp;
		else if (el)		/* We're the first extension. Take over e's functions */
			el->next = tmp;
		else			/* We're the very first extension.  */
			con->root = tmp;
		if (tmp->priority == PRIORITY_HINT)
			ast_change_hint(e,tmp);
		/* Destroy the old one */
		if (e->datad)
			e->datad(e->data);
		free(e);
	} else {	/* Slip ourselves in just before e */
		tmp->peer = e;
		tmp->next = e->next;	/* extension chain, or NULL if e is not the first extension */
		if (ep)			/* Easy enough, we're just in the peer list */
			ep->peer = tmp;
		else {			/* we are the first in some peer list, so link in the ext list */
			if (el)
				el->next = tmp;	/* in the middle... */
			else
				con->root = tmp; /* ... or at the head */
			e->next = NULL;	/* e is no more at the head, so e->next must be reset */
		}
		/* And immediately return success. */
		if (tmp->priority == PRIORITY_HINT)
			 ast_add_hint(tmp);
	}
	return 0;
}

/*! \brief
 * Main interface to add extensions to the list for out context.
 *
 * We sort extensions in order of matching preference, so that we can
 * stop the search as soon as we find a suitable match.
 * This ordering also takes care of wildcards such as '.' (meaning
 * "one or more of any character") and '!' (which is 'earlymatch',
 * meaning "zero or more of any character" but also impacts the
 * return value from CANMATCH and EARLYMATCH.
 *
 * The extension match rules defined in the devmeeting 2006.05.05 are
 * quite simple: WE SELECT THE LONGEST MATCH.
 * In detail, "longest" means the number of matched characters in
 * the extension. In case of ties (e.g. _XXX and 333) in the length
 * of a pattern, we give priority to entries with the smallest cardinality
 * (e.g, [5-9] comes before [2-8] before the former has only 5 elements,
 * while the latter has 7, etc.
 * In case of same cardinality, the first element in the range counts.
 * If we still have a tie, any final '!' will make this as a possibly
 * less specific pattern.
 *
 * EBUSY - can't lock
 * EEXIST - extension with the same priority exist and no replace is set
 *
 */
int ast_add_extension2(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar)
{
	/*
	 * Sort extensions (or patterns) according to the rules indicated above.
	 * These are implemented by the function ext_cmp()).
	 * All priorities for the same ext/pattern/cid are kept in a list,
	 * using the 'peer' field  as a link field..
	 */
	struct ast_exten *tmp, *e, *el = NULL;
	int res;
	int length;
	char *p;
	char expand_buf[VAR_BUF_SIZE] = { 0, };

	/* if we are adding a hint, and there are global variables, and the hint
	   contains variable references, then expand them
	*/
	ast_mutex_lock(&globalslock);
	if (priority == PRIORITY_HINT && AST_LIST_FIRST(&globals) && strstr(application, "${")) {
		pbx_substitute_variables_varshead(&globals, application, expand_buf, sizeof(expand_buf));
		application = expand_buf;
	}
	ast_mutex_unlock(&globalslock);

	length = sizeof(struct ast_exten);
	length += strlen(extension) + 1;
	length += strlen(application) + 1;
	if (label)
		length += strlen(label) + 1;
	if (callerid)
		length += strlen(callerid) + 1;
	else
		length ++;	/* just the '\0' */

	/* Be optimistic:  Build the extension structure first */
	if (!(tmp = ast_calloc(1, length)))
		return -1;

	/* use p as dst in assignments, as the fields are const char * */
	p = tmp->stuff;
	if (label) {
		tmp->label = p;
		strcpy(p, label);
		p += strlen(label) + 1;
	}
	tmp->exten = p;
	p += ext_strncpy(p, extension, strlen(extension) + 1) + 1;
	tmp->priority = priority;
	tmp->cidmatch = p;	/* but use p for assignments below */
	if (callerid) {
		p += ext_strncpy(p, callerid, strlen(callerid) + 1) + 1;
		tmp->matchcid = 1;
	} else {
		*p++ = '\0';
		tmp->matchcid = 0;
	}
	tmp->app = p;
	strcpy(p, application);
	tmp->parent = con;
	tmp->data = data;
	tmp->datad = datad;
	tmp->registrar = registrar;

	ast_mutex_lock(&con->lock);
	res = 0; /* some compilers will think it is uninitialized otherwise */
	for (e = con->root; e; el = e, e = e->next) {   /* scan the extension list */
		res = ext_cmp(e->exten, tmp->exten);
		if (res == 0) { /* extension match, now look at cidmatch */
			if (!e->matchcid && !tmp->matchcid)
				res = 0;
			else if (tmp->matchcid && !e->matchcid)
				res = 1;
			else if (e->matchcid && !tmp->matchcid)
				res = -1;
			else
				res = ext_cmp(e->cidmatch, tmp->cidmatch);
		}
		if (res >= 0)
			break;
	}
	if (e && res == 0) { /* exact match, insert in the pri chain */
		res = add_pri(con, tmp, el, e, replace);
		ast_mutex_unlock(&con->lock);
		if (res < 0) {
			errno = EEXIST;	/* XXX do we care ? */
			return 0; /* XXX should we return -1 maybe ? */
		}
	} else {
		/*
		 * not an exact match, this is the first entry with this pattern,
		 * so insert in the main list right before 'e' (if any)
		 */
		tmp->next = e;
		if (el)
			el->next = tmp;
		else
			con->root = tmp;
		ast_mutex_unlock(&con->lock);
		if (tmp->priority == PRIORITY_HINT)
			ast_add_hint(tmp);
	}
	if (option_debug) {
		if (tmp->matchcid) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Added extension '%s' priority %d (CID match '%s') to %s\n",
					tmp->exten, tmp->priority, tmp->cidmatch, con->name);
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "Added extension '%s' priority %d to %s\n",
					tmp->exten, tmp->priority, con->name);
		}
	}
	if (option_verbose > 2) {
		if (tmp->matchcid) {
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d (CID match '%s')to %s\n",
				tmp->exten, tmp->priority, tmp->cidmatch, con->name);
		} else {
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d to %s\n",
				tmp->exten, tmp->priority, con->name);
		}
	}
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

	while (timeout && (chan->_state != AST_STATE_UP)) {
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
			    (f->subclass == AST_CONTROL_CONGESTION) ) {
				ast_frfree(f);
				break;
			}
		}
		ast_frfree(f);
	}
	if (chan->_state == AST_STATE_UP) {
		if (!ast_strlen_zero(as->app)) {
			app = pbx_findapp(as->app);
			if (app) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Launching %s(%s) on %s\n", as->app, as->appdata, chan->name);
				pbx_exec(chan, app, as->appdata);
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

/*! Function to post an empty cdr after a spool call fails.
 *
 *  This function posts an empty cdr for a failed spool call
 *
 */
static int ast_pbx_outgoing_cdr_failed(void)
{
	/* allocate a channel */
	struct ast_channel *chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "%s", "");

	if (!chan)
		return -1;  /* failure */

	if (!chan->cdr) {
		/* allocation of the cdr failed */
		ast_channel_free(chan);   /* free the channel */
		return -1;                /* return failure */
	}

	/* allocation of the cdr was successful */
	ast_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
	ast_cdr_start(chan->cdr);       /* record the start and stop time */
	ast_cdr_end(chan->cdr);
	ast_cdr_failed(chan->cdr);      /* set the status to failed */
	ast_cdr_detach(chan->cdr);      /* post and free the record */
	chan->cdr = NULL;
	ast_channel_free(chan);         /* free the channel */

	return 0;  /* success */
}

int ast_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, struct ast_variable *vars, const char *account, struct ast_channel **channel)
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
				ast_channel_lock(chan);
		}
		if (chan) {
			if (chan->_state == AST_STATE_UP) {
					res = 0;
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);

				if (sync > 1) {
					if (channel)
						ast_channel_unlock(chan);
					if (ast_pbx_run(chan)) {
						ast_log(LOG_ERROR, "Unable to run PBX on %s\n", chan->name);
						if (channel)
							*channel = NULL;
						ast_hangup(chan);
						chan = NULL;
						res = -1;
					}
				} else {
					if (ast_pbx_start(chan)) {
						ast_log(LOG_ERROR, "Unable to start PBX on %s\n", chan->name);
						if (channel) {
							*channel = NULL;
							ast_channel_unlock(chan);
						}
						ast_hangup(chan);
						res = -1;
					}
					chan = NULL;
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

				if (channel) {
					*channel = NULL;
					ast_channel_unlock(chan);
				}
				ast_hangup(chan);
				chan = NULL;
			}
		}

		if (res < 0) { /* the call failed for some reason */
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
				chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "OutgoingSpoolFailed");
				if (chan) {
					char failed_reason[4] = "";
					if (!ast_strlen_zero(context))
						ast_copy_string(chan->context, context, sizeof(chan->context));
					set_ext_pri(chan, "failed", 1);
					ast_set_variables(chan, vars);
					snprintf(failed_reason, sizeof(failed_reason), "%d", *reason);
					pbx_builtin_setvar_helper(chan, "REASON", failed_reason);
					if (account)
						ast_cdr_setaccount(chan, account);
					if (ast_pbx_run(chan)) {
						ast_log(LOG_ERROR, "Unable to run PBX on %s\n", chan->name);
						ast_hangup(chan);
					}
					chan = NULL;
				}
			}
		}
	} else {
		if (!(as = ast_calloc(1, sizeof(*as)))) {
			res = -1;
			goto outgoing_exten_cleanup;
		}
		chan = ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
		if (channel) {
			*channel = chan;
			if (chan)
				ast_channel_lock(chan);
		}
		if (!chan) {
			free(as);
			res = -1;
			goto outgoing_exten_cleanup;
		}
		as->chan = chan;
		ast_copy_string(as->context, context, sizeof(as->context));
		set_ext_pri(as->chan,  exten, priority);
		as->timeout = timeout;
		ast_set_variables(chan, vars);
		if (account)
			ast_cdr_setaccount(chan, account);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&as->p, &attr, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			free(as);
			if (channel) {
				*channel = NULL;
				ast_channel_unlock(chan);
			}
			ast_hangup(chan);
			res = -1;
			pthread_attr_destroy(&attr);
			goto outgoing_exten_cleanup;
		}
		pthread_attr_destroy(&attr);
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

/*! \brief run the application and free the descriptor once done */
static void *ast_pbx_run_app(void *data)
{
	struct app_tmp *tmp = data;
	struct ast_app *app;
	app = pbx_findapp(tmp->app);
	if (app) {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4 "Launching %s(%s) on %s\n", tmp->app, tmp->data, tmp->chan->name);
		pbx_exec(tmp->chan, app, tmp->data);
	} else
		ast_log(LOG_WARNING, "No such application '%s'\n", tmp->app);
	ast_hangup(tmp->chan);
	free(tmp);
	return NULL;
}

int ast_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, struct ast_variable *vars, const char *account, struct ast_channel **locked_channel)
{
	struct ast_channel *chan;
	struct app_tmp *tmp;
	int res = -1, cdr_res = -1;
	struct outgoing_helper oh;
	pthread_attr_t attr;

	memset(&oh, 0, sizeof(oh));
	oh.vars = vars;
	oh.account = account;

	if (locked_channel)
		*locked_channel = NULL;
	if (ast_strlen_zero(app)) {
		res = -1;
		goto outgoing_app_cleanup;
	}
	if (sync) {
		chan = __ast_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
		if (chan) {
			ast_set_variables(chan, vars);
			if (account)
				ast_cdr_setaccount(chan, account);
			if (chan->_state == AST_STATE_UP) {
				res = 0;
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);
				tmp = ast_calloc(1, sizeof(*tmp));
				if (!tmp)
					res = -1;
				else {
					ast_copy_string(tmp->app, app, sizeof(tmp->app));
					if (appdata)
						ast_copy_string(tmp->data, appdata, sizeof(tmp->data));
					tmp->chan = chan;
					if (sync > 1) {
						if (locked_channel)
							ast_channel_unlock(chan);
						ast_pbx_run_app(tmp);
					} else {
						pthread_attr_init(&attr);
						pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						if (locked_channel)
							ast_channel_lock(chan);
						if (ast_pthread_create(&tmp->t, &attr, ast_pbx_run_app, tmp)) {
							ast_log(LOG_WARNING, "Unable to spawn execute thread on %s: %s\n", chan->name, strerror(errno));
							free(tmp);
							if (locked_channel)
								ast_channel_unlock(chan);
							ast_hangup(chan);
							res = -1;
						} else {
							if (locked_channel)
								*locked_channel = chan;
						}
						pthread_attr_destroy(&attr);
					}
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
		struct async_stat *as;
		if (!(as = ast_calloc(1, sizeof(*as)))) {
			res = -1;
			goto outgoing_app_cleanup;
		}
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
		if (account)
			ast_cdr_setaccount(chan, account);
		/* Start a new thread, and get something handling this channel. */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (locked_channel)
			ast_channel_lock(chan);
		if (ast_pthread_create(&as->p, &attr, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			free(as);
			if (locked_channel)
				ast_channel_unlock(chan);
			ast_hangup(chan);
			res = -1;
			pthread_attr_destroy(&attr);
			goto outgoing_app_cleanup;
		} else {
			if (locked_channel)
				*locked_channel = chan;
		}
		pthread_attr_destroy(&attr);
		res = 0;
	}
outgoing_app_cleanup:
	ast_variables_destroy(vars);
	return res;
}

void __ast_context_destroy(struct ast_context *con, const char *registrar)
{
	struct ast_context *tmp, *tmpl=NULL;
	struct ast_include *tmpi;
	struct ast_sw *sw;
	struct ast_exten *e, *el, *en;
	struct ast_ignorepat *ipi;

	for (tmp = contexts; tmp; ) {
		struct ast_context *next;	/* next starting point */
		for (; tmp; tmpl = tmp, tmp = tmp->next) {
			if (option_debug)
				ast_log(LOG_DEBUG, "check ctx %s %s\n", tmp->name, tmp->registrar);
			if ( (!registrar || !strcasecmp(registrar, tmp->registrar)) &&
			     (!con || !strcasecmp(tmp->name, con->name)) )
				break;	/* found it */
		}
		if (!tmp)	/* not found, we are done */
			break;
		ast_mutex_lock(&tmp->lock);
		if (option_debug)
			ast_log(LOG_DEBUG, "delete ctx %s %s\n", tmp->name, tmp->registrar);
		next = tmp->next;
		if (tmpl)
			tmpl->next = next;
		else
			contexts = next;
		/* Okay, now we're safe to let it go -- in a sense, we were
		   ready to let it go as soon as we locked it. */
		ast_mutex_unlock(&tmp->lock);
		for (tmpi = tmp->includes; tmpi; ) { /* Free includes */
			struct ast_include *tmpil = tmpi;
			tmpi = tmpi->next;
			free(tmpil);
		}
		for (ipi = tmp->ignorepats; ipi; ) { /* Free ignorepats */
			struct ast_ignorepat *ipl = ipi;
			ipi = ipi->next;
			free(ipl);
		}
		while ((sw = AST_LIST_REMOVE_HEAD(&tmp->alts, list)))
			free(sw);
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
		/* if we have a specific match, we are done, otherwise continue */
		tmp = con ? NULL : next;
	}
}

void ast_context_destroy(struct ast_context *con, const char *registrar)
{
	ast_wrlock_contexts();
	__ast_context_destroy(con,registrar);
	ast_unlock_contexts();
}

static void wait_for_hangup(struct ast_channel *chan, void *data)
{
	int res;
	struct ast_frame *f;
	int waittime;

	if (ast_strlen_zero(data) || (sscanf(data, "%30d", &waittime) != 1) || (waittime < 0))
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
	/* Don't change state of an UP channel, just indicate
	   busy in audio */
	if (chan->_state != AST_STATE_UP) {
		ast_setstate(chan, AST_STATE_BUSY);
		ast_cdr_busy(chan->cdr);
	}
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_congestion(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	/* Don't change state of an UP channel, just indicate
	   congestion in audio */
	if (chan->_state != AST_STATE_UP)
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
	ast_cdr_setamaflags(chan, data ? data : "");
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_hangup(struct ast_channel *chan, void *data)
{
	if (!ast_strlen_zero(data)) {
		int cause;
		char *endptr;

		if ((cause = ast_str2cause(data)) > -1) {
			chan->hangupcause = cause;
			return -1;
		}
		
		cause = strtol((const char *) data, &endptr, 10);
		if (cause != 0 || (data != endptr)) {
			chan->hangupcause = cause;
			return -1;
		}
			
		ast_log(LOG_NOTICE, "Invalid cause given to Hangup(): \"%s\"\n", (char *) data);
	}

	if (!chan->hangupcause) {
		chan->hangupcause = AST_CAUSE_NORMAL_CLEARING;
	}

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

	ts = s = ast_strdupa(data);

	/* Separate the Goto path */
	strsep(&ts,"?");

	/* struct ast_include include contained garbage here, fixed by zeroing it on get_timerange */
	if (ast_build_timing(&timing, s) && ast_check_timing(&timing))
		res = pbx_builtin_goto(chan, ts);
	
	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_execiftime(struct ast_channel *chan, void *data)
{
	char *s, *appname;
	struct ast_timing timing;
	struct ast_app *app;
	static const char *usage = "ExecIfTime requires an argument:\n  <time range>|<days of week>|<days of month>|<months>?<appname>[|<appargs>]";

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s\n", usage);
		return -1;
	}

	appname = ast_strdupa(data);

	s = strsep(&appname,"?");	/* Separate the timerange and application name/data */
	if (!appname) {	/* missing application */
		ast_log(LOG_WARNING, "%s\n", usage);
		return -1;
	}

	if (!ast_build_timing(&timing, s)) {
		ast_log(LOG_WARNING, "Invalid Time Spec: %s\nCorrect usage: %s\n", s, usage);
		return -1;
	}

	if (!ast_check_timing(&timing))	/* outside the valid time window, just return */
		return 0;

	/* now split appname|appargs */
	if ((s = strchr(appname, '|')))
		*s++ = '\0';

	if ((app = pbx_findapp(appname))) {
		return pbx_exec(chan, app, S_OR(s, ""));
	} else {
		ast_log(LOG_WARNING, "Cannot locate application %s\n", appname);
		return -1;
	}
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_wait(struct ast_channel *chan, void *data)
{
	double s;
	int ms;

	/* Wait for "n" seconds */
	if (data && (s = atof(data)) > 0) {
		ms = s * 1000.0;
		return ast_safe_sleep(chan, ms);
	}
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_waitexten(struct ast_channel *chan, void *data)
{
	int ms, res;
	double sec;
	struct ast_flags flags = {0};
	char *opts[1] = { NULL };
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
	);

	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, parse);
	} else
		memset(&args, 0, sizeof(args));

	if (args.options)
		ast_app_parse_options(waitexten_opts, &flags, opts, args.options);
	
	if (ast_test_flag(&flags, WAITEXTEN_MOH) && !opts[0] ) {
		ast_log(LOG_WARNING, "The 'm' option has been specified for WaitExten without a class.\n"); 
	} else if (ast_test_flag(&flags, WAITEXTEN_MOH))
		ast_indicate_data(chan, AST_CONTROL_HOLD, S_OR(opts[0], NULL), strlen(opts[0]));

	/* Wait for "n" seconds */
	if (args.timeout && (sec = atof(args.timeout)) > 0.0)
		ms = 1000 * sec;
	else if (chan->pbx)
		ms = chan->pbx->rtimeout * 1000;
	else
		ms = 10000;
	res = ast_waitfordigit(chan, ms);
	if (!res) {
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 1, chan->cid.cid_num)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Timeout on %s, continuing...\n", chan->name);
		} else if (chan->_softhangup & AST_SOFTHANGUP_TIMEOUT) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Call timeout on %s, checking for 'T'\n", chan->name);
			res = -1;
		} else if (ast_exists_extension(chan, chan->context, "t", 1, chan->cid.cid_num)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Timeout on %s, going to 't'\n", chan->name);
			set_ext_pri(chan, "t", 0); /* 0 will become 1, next time through the loop */
		} else {
			ast_log(LOG_WARNING, "Timeout but no rule 't' in context '%s'\n", chan->context);
			res = -1;
		}
	}

	if (ast_test_flag(&flags, WAITEXTEN_MOH))
		ast_indicate(chan, AST_CONTROL_UNHOLD);

	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_background(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_flags flags = {0};
	char *parse, exten[2] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
		AST_APP_ARG(lang);
		AST_APP_ARG(context);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Background requires an argument (filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.lang))
		args.lang = (char *)chan->language;	/* XXX this is const */

	if (ast_strlen_zero(args.context)) {
		const char *context;
		ast_channel_lock(chan);
		if ((context = pbx_builtin_getvar_helper(chan, "MACRO_CONTEXT"))) {
			args.context = ast_strdupa(context);
		} else {
			args.context = chan->context;
		}
		ast_channel_unlock(chan);
	}

	if (args.options) {
		if (!strcasecmp(args.options, "skip"))
			flags.flags = BACKGROUND_SKIP;
		else if (!strcasecmp(args.options, "noanswer"))
			flags.flags = BACKGROUND_NOANSWER;
		else
			ast_app_parse_options(background_opts, &flags, NULL, args.options);
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
		char *back = args.filename;
		char *front;
		ast_stopstream(chan);		/* Stop anything playing */
		/* Stream the list of files */
		while (!res && (front = strsep(&back, "&")) ) {
			if ( (res = ast_streamfile(chan, front, args.lang)) ) {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char*)data);
				res = 0;
				break;
			}
			if (ast_test_flag(&flags, BACKGROUND_PLAYBACK)) {
				res = ast_waitstream(chan, "");
			} else if (ast_test_flag(&flags, BACKGROUND_MATCHEXTEN)) {
				res = ast_waitstream_exten(chan, args.context);
			} else {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			}
			ast_stopstream(chan);
		}
	}

	/*
	 * If the single digit DTMF is an extension in the specified context, then
	 * go there and signal no DTMF.  Otherwise, we should exit with that DTMF.
	 * If we're in Macro, we'll exit and seek that DTMF as the beginning of an
	 * extension in the Macro's calling context.  If we're not in Macro, then
	 * we'll simply seek that extension in the calling context.  Previously,
	 * someone complained about the behavior as it related to the interior of a
	 * Gosub routine, and the fix (#14011) inadvertently broke FreePBX
	 * (#14940).  This change should fix both of these situations, but with the
	 * possible incompatibility that if a single digit extension does not exist
	 * (but a longer extension COULD have matched), it would have previously
	 * gone immediately to the "i" extension, but will now need to wait for a
	 * timeout.
	 *
	 * Later, we had to add a flag to disable this workaround, because AGI
	 * users can EXEC Background and reasonably expect that the DTMF code will
	 * be returned (see #16434).
	 */
	if (!ast_test_flag(chan, AST_FLAG_DISABLE_WORKAROUNDS) &&
			(exten[0] = res) &&
			ast_canmatch_extension(chan, args.context, exten, 1, chan->cid.cid_num) &&
			!ast_matchmore_extension(chan, args.context, exten, 1, chan->cid.cid_num)) {
		snprintf(chan->exten, sizeof(chan->exten), "%c", res);
		ast_copy_string(chan->context, args.context, sizeof(chan->context));
		chan->priority = 0;
		res = 0;
	}
	return res;
}

/*! Goto
 * \ingroup applications
 */
static int pbx_builtin_goto(struct ast_channel *chan, void *data)
{
	int res = ast_parseable_goto(chan, data);
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

	ast_channel_lock(chan);

	AST_LIST_TRAVERSE(&chan->varshead, variables, entries) {
		if ((var=ast_var_name(variables)) && (val=ast_var_value(variables))
		   /* && !ast_strlen_zero(var) && !ast_strlen_zero(val) */
		   ) {
			if (ast_build_string(&buf, &size, "%s=%s\n", var, val)) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			} else
				total++;
		} else
			break;
	}

	ast_channel_unlock(chan);

	return total;
}

const char *pbx_builtin_getvar_helper(struct ast_channel *chan, const char *name)
{
	struct ast_var_t *variables;
	const char *ret = NULL;
	int i;
	struct varshead *places[2] = { NULL, &globals };

	if (!name)
		return NULL;

	if (chan) {
		ast_channel_lock(chan);
		places[0] = &chan->varshead;
	}

	for (i = 0; i < 2; i++) {
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_mutex_lock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (!strcmp(name, ast_var_name(variables))) {
				ret = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_mutex_unlock(&globalslock);
		if (ret)
			break;
	}

	if (chan)
		ast_channel_unlock(chan);

	return ret;
}

void pbx_builtin_pushvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;

	if (name[strlen(name)-1] == ')') {
		char *function = ast_strdupa(name);

		ast_log(LOG_WARNING, "Cannot push a value onto a function\n");
		ast_func_write(chan, function, value);
		return;
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = &chan->varshead;
	} else {
		ast_mutex_lock(&globalslock);
		headp = &globals;
	}

	if (value) {
		if ((option_verbose > 1) && (headp == &globals))
			ast_verbose(VERBOSE_PREFIX_2 "Setting global variable '%s' to '%s'\n", name, value);
		newvariable = ast_var_assign(name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_mutex_unlock(&globalslock);
}

void pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *nametail = name;

	if (name[strlen(name)-1] == ')') {
		char *function = ast_strdupa(name);

		ast_func_write(chan, function, value);
		return;
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = &chan->varshead;
	} else {
		ast_mutex_lock(&globalslock);
		headp = &globals;
	}

	/* For comparison purposes, we have to strip leading underscores */
	if (*nametail == '_') {
		nametail++;
		if (*nametail == '_')
			nametail++;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(headp, newvariable, entries) {
		if (strcmp(ast_var_name(newvariable), nametail) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE_CURRENT(headp, entries);
			ast_var_delete(newvariable);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (value) {
		if ((option_verbose > 1) && (headp == &globals))
			ast_verbose(VERBOSE_PREFIX_2 "Setting global variable '%s' to '%s'\n", name, value);
		newvariable = ast_var_assign(name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_mutex_unlock(&globalslock);
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
		if (strchr(argv[argc], 'g')) {
			ast_log(LOG_WARNING, "The use of the 'g' flag is deprecated.  Please use Set(GLOBAL(foo)=bar) instead\n");
			global = 1;
		}
	}

	if (argc > 1)
		ast_log(LOG_WARNING, "Setting multiple variables at once within Set is deprecated.  Please separate each name/value pair into its own line.\n");

	for (x = 0; x < argc; x++) {
		name = argv[x];
		if ((value = strchr(name, '='))) {
			*value++ = '\0';
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
	char *channel;
	char tmp[VAR_BUF_SIZE]="";

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}

	value = ast_strdupa(data);
	name = strsep(&value,"=");
	channel = strsep(&value,"|");
	if (channel && value && name) { /*! \todo XXX should do !ast_strlen_zero(..) of the args ? */
		struct ast_channel *chan2 = ast_get_channel_by_name_locked(channel);
		if (chan2) {
			char *s = alloca(strlen(value) + 4);
			if (s) {
				sprintf(s, "${%s}", value);
				pbx_substitute_variables_helper(chan2, s, tmp, sizeof(tmp) - 1);
			}
			ast_channel_unlock(chan2);
		}
		pbx_builtin_setvar_helper(chan, name, tmp);
	}

	return(0);
}

/*! \todo XXX overwrites data ? */
static int pbx_builtin_setglobalvar(struct ast_channel *chan, void *data)
{
	char *name;
	char *stringp = data;
	static int dep_warning = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}

	name = strsep(&stringp, "=");

	if (!dep_warning) {
		dep_warning = 1;
		ast_log(LOG_WARNING, "SetGlobalVar is deprecated.  Please use Set(GLOBAL(%s)=%s) instead.\n", name, stringp);
	}

	/*! \todo XXX watch out, leading whitespace ? */
	pbx_builtin_setvar_helper(NULL, name, stringp);

	return(0);
}

static int pbx_builtin_noop(struct ast_channel *chan, void *data)
{
	return 0;
}

void pbx_builtin_clear_globals(void)
{
	struct ast_var_t *vardata;

	ast_mutex_lock(&globalslock);
	while ((vardata = AST_LIST_REMOVE_HEAD(&globals, entries)))
		ast_var_delete(vardata);
	ast_mutex_unlock(&globalslock);
}

int pbx_checkcondition(const char *condition)
{
	if (ast_strlen_zero(condition))	/* NULL or empty strings are false */
		return 0;
	else if (*condition >= '0' && *condition <= '9')	/* Numbers are evaluated for truth */
		return atoi(condition);
	else	/* Strings are true */
		return 1;
}

static int pbx_builtin_gotoif(struct ast_channel *chan, void *data)
{
	char *condition, *branch1, *branch2, *branch;
	int rc;
	char *stringp;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to check\n");
		return 0;
	}

	stringp = ast_strdupa(data);
	condition = strsep(&stringp,"?");
	branch1 = strsep(&stringp,":");
	branch2 = strsep(&stringp,"");
	branch = pbx_checkcondition(condition) ? branch1 : branch2;

	if (ast_strlen_zero(branch)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Not taking any branch\n");
		return 0;
	}

	rc = pbx_builtin_goto(chan, branch);

	return rc;
}

static int pbx_builtin_saynumber(struct ast_channel *chan, void *data)
{
	char tmp[256];
	char *number = tmp;
	char *options;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayNumber requires an argument (number)\n");
		return -1;
	}
	ast_copy_string(tmp, data, sizeof(tmp));
	strsep(&number, "|");
	options = strsep(&number, "|");
	if (options) {
		if ( strcasecmp(options, "f") && strcasecmp(options,"m") &&
			strcasecmp(options, "c") && strcasecmp(options, "n") ) {
			ast_log(LOG_WARNING, "SayNumber gender option is either 'f', 'm', 'c' or 'n'\n");
			return -1;
		}
	}

	if (ast_say_number(chan, atoi(tmp), "", chan->language, options)) {
		ast_log(LOG_WARNING, "We were unable to say the number %s, is it too large?\n", tmp);
	}

	return 0;
}

static int pbx_builtin_saydigits(struct ast_channel *chan, void *data)
{
	int res = 0;

	if (data)
		res = ast_say_digit_str(chan, data, "", chan->language);
	return res;
}

static int pbx_builtin_saycharacters(struct ast_channel *chan, void *data)
{
	int res = 0;

	if (data)
		res = ast_say_character_str(chan, data, "", chan->language);
	return res;
}

static int pbx_builtin_sayphonetic(struct ast_channel *chan, void *data)
{
	int res = 0;

	if (data)
		res = ast_say_phonetic_str(chan, data, "", chan->language);
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
	ast_cli_register_multiple(pbx_cli, sizeof(pbx_cli) / sizeof(struct ast_cli_entry));

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

int ast_rdlock_contexts(void)
{
	return ast_mutex_lock(&conlock);
}

int ast_wrlock_contexts(void)
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

struct ast_context *ast_get_extension_context(struct ast_exten *exten)
{
	return exten ? exten->parent : NULL;
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
	return con ? con->next : contexts;
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
		return con ? AST_LIST_FIRST(&con->alts) : NULL;
	else
		return AST_LIST_NEXT(sw, list);
}

struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority)
{
	return priority ? priority->peer : exten;
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
	struct ast_include *inc = NULL;
	int res = 0;

	while ( (inc = ast_walk_context_includes(con, inc)) ) {
		if (ast_context_find(inc->rname))
			continue;

		res = -1;
		ast_log(LOG_WARNING, "Context '%s' tries to include nonexistent context '%s'\n",
			ast_get_context_name(con), inc->rname);
		break;
	}

	return res;
}


static int __ast_goto_if_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, int async)
{
	int (*goto_func)(struct ast_channel *chan, const char *context, const char *exten, int priority);

	if (!chan)
		return -2;

	if (context == NULL)
		context = chan->context;
	if (exten == NULL)
		exten = chan->exten;

	goto_func = (async) ? ast_async_goto : ast_explicit_goto;
	if (ast_exists_extension(chan, context, exten, priority, chan->cid.cid_num))
		return goto_func(chan, context, exten, priority);
	else
		return -3;
}

int ast_goto_if_exists(struct ast_channel *chan, const char* context, const char *exten, int priority)
{
	return __ast_goto_if_exists(chan, context, exten, priority, 0);
}

int ast_async_goto_if_exists(struct ast_channel *chan, const char * context, const char *exten, int priority)
{
	return __ast_goto_if_exists(chan, context, exten, priority, 1);
}

int ast_parseable_goto(struct ast_channel *chan, const char *goto_string)
{
	char *exten, *pri, *context;
	char *stringp;
	int ipri;
	int mode = 0;

	if (ast_strlen_zero(goto_string)) {
		ast_log(LOG_WARNING, "Goto requires an argument (optional context|optional extension|priority)\n");
		return -1;
	}
	stringp = ast_strdupa(goto_string);
	context = strsep(&stringp, "|");	/* guaranteed non-null */
	exten = strsep(&stringp, "|");
	pri = strsep(&stringp, "|");
	if (!exten) {	/* Only a priority in this one */
		pri = context;
		exten = NULL;
		context = NULL;
	} else if (!pri) {	/* Only an extension and priority in this one */
		pri = exten;
		exten = context;
		context = NULL;
	}
	if (*pri == '+') {
		mode = 1;
		pri++;
	} else if (*pri == '-') {
		mode = -1;
		pri++;
	}
	if (sscanf(pri, "%30d", &ipri) != 1) {
		if ((ipri = ast_findlabel_extension(chan, context ? context : chan->context, exten ? exten : chan->exten,
			pri, chan->cid.cid_num)) < 1) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0, or valid label\n", pri);
			return -1;
		} else
			mode = 0;
	}
	/* At this point we have a priority and maybe an extension and a context */

	if (mode)
		ipri = chan->priority + (ipri * mode);

	ast_explicit_goto(chan, context, exten, ipri);
	return 0;

}

static int hint_hash(const void *hint, const int flags)
{
	/* Only 1 bucket, not important. */
	return 0;
}

static int hint_cmp(void *obj, void *arg, int flags)
{
	const struct ast_hint *hint = obj;
	const struct ast_exten *exten = arg;

	return (hint->exten == exten) ? CMP_MATCH | CMP_STOP : 0;
}

int ast_pbx_init(void)
{
	hints = ao2_container_alloc(1, hint_hash, hint_cmp);

	return hints ? 0 : -1;
}
