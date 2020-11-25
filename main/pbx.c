/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_SYSTEM_NAME */
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#if defined(HAVE_SYSINFO)
#include <sys/sysinfo.h>
#endif
#if defined(SOLARIS)
#include <sys/loadavg.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/cdr.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/time.h"
#include "asterisk/manager.h"
#include "asterisk/ast_expr.h"
#include "asterisk/linkedlists.h"
#define	SAY_STUBS	/* generate declarations and stubs for say methods */
#include "asterisk/say.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"
#include "asterisk/devicestate.h"
#include "asterisk/presencestate.h"
#include "asterisk/hashtab.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/xmldoc.h"
#include "asterisk/astobj2.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/dial.h"
#include "asterisk/vector.h"
#include "pbx_private.h"

/*!
 * \note I M P O R T A N T :
 *
 *		The speed of extension handling will likely be among the most important
 * aspects of this PBX.  The switching scheme as it exists right now isn't
 * terribly bad (it's O(N+M), where N is the # of extensions and M is the avg #
 * of priorities, but a constant search time here would be great ;-)
 *
 * A new algorithm to do searching based on a 'compiled' pattern tree is introduced
 * here, and shows a fairly flat (constant) search time, even for over
 * 10000 patterns.
 *
 * Also, using a hash table for context/priority name lookup can help prevent
 * the find_extension routines from absorbing exponential cpu cycles as the number
 * of contexts/priorities grow. I've previously tested find_extension with red-black trees,
 * which have O(log2(n)) speed. Right now, I'm using hash tables, which do
 * searches (ideally) in O(1) time. While these techniques do not yield much
 * speed in small dialplans, they are worth the trouble in large dialplans.
 *
 */

/*** DOCUMENTATION
	<function name="EXCEPTION" language="en_US">
		<synopsis>
			Retrieve the details of the current dialplan exception.
		</synopsis>
		<syntax>
			<parameter name="field" required="true">
				<para>The following fields are available for retrieval:</para>
				<enumlist>
					<enum name="reason">
						<para>INVALID, ERROR, RESPONSETIMEOUT, ABSOLUTETIMEOUT, or custom
						value set by the RaiseException() application</para>
					</enum>
					<enum name="context">
						<para>The context executing when the exception occurred.</para>
					</enum>
					<enum name="exten">
						<para>The extension executing when the exception occurred.</para>
					</enum>
					<enum name="priority">
						<para>The numeric priority executing when the exception occurred.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Retrieve the details (specified <replaceable>field</replaceable>) of the current dialplan exception.</para>
		</description>
		<see-also>
			<ref type="application">RaiseException</ref>
		</see-also>
	</function>
	<function name="TESTTIME" language="en_US">
		<synopsis>
			Sets a time to be used with the channel to test logical conditions.
		</synopsis>
		<syntax>
			<parameter name="date" required="true" argsep=" ">
				<para>Date in ISO 8601 format</para>
			</parameter>
			<parameter name="time" required="true" argsep=" ">
				<para>Time in HH:MM:SS format (24-hour time)</para>
			</parameter>
			<parameter name="zone" required="false">
				<para>Timezone name</para>
			</parameter>
		</syntax>
		<description>
			<para>To test dialplan timing conditions at times other than the current time, use
			this function to set an alternate date and time.  For example, you may wish to evaluate
			whether a location will correctly identify to callers that the area is closed on Christmas
			Day, when Christmas would otherwise fall on a day when the office is normally open.</para>
		</description>
		<see-also>
			<ref type="application">GotoIfTime</ref>
		</see-also>
	</function>
	<manager name="ShowDialPlan" language="en_US">
		<synopsis>
			Show dialplan contexts and extensions
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Extension">
				<para>Show a specific extension.</para>
			</parameter>
			<parameter name="Context">
				<para>Show a specific context.</para>
			</parameter>
		</syntax>
		<description>
			<para>Show dialplan contexts and extensions. Be aware that showing the full dialplan
			may take a lot of capacity.</para>
		</description>
	</manager>
	<manager name="ExtensionStateList" language="en_US">
		<synopsis>
			List the current known extension states.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>This will list out all known extension states in a
			sequence of <replaceable>ExtensionStatus</replaceable> events.
			When finished, a <replaceable>ExtensionStateListComplete</replaceable> event
			will be emitted.</para>
		</description>
		<see-also>
			<ref type="manager">ExtensionState</ref>
			<ref type="function">HINT</ref>
			<ref type="function">EXTENSION_STATE</ref>
		</see-also>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ExtensionStatus'])" />
			</list-elements>
			<managerEvent name="ExtensionStateListComplete" language="en_US">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>
						Indicates the end of the list the current known extension states.
					</synopsis>
					<syntax>
						<parameter name="EventList">
							<para>Conveys the status of the event list.</para>
						</parameter>
						<parameter name="ListItems">
							<para>Conveys the number of statuses reported.</para>
						</parameter>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
 ***/

#ifdef LOW_MEMORY
#define EXT_DATA_SIZE 256
#else
#define EXT_DATA_SIZE 8192
#endif

#define SWITCH_DATA_LENGTH 256

#define	VAR_NORMAL		1
#define	VAR_SOFTTRAN	2
#define	VAR_HARDTRAN	3

struct ast_context;
struct ast_app;

AST_THREADSTORAGE(switch_data);
AST_THREADSTORAGE(extensionstate_buf);

/*!
   \brief ast_exten: An extension
	The dialplan is saved as a linked list with each context
	having it's own linked list of extensions - one item per
	priority.
*/
struct ast_exten {
	char *exten;			/*!< Clean Extension id */
	char *name;			/*!< Extension name (may include '-' eye candy) */
	int matchcid;			/*!< Match caller id ? */
	const char *cidmatch;		/*!< Caller id to match for this extension */
	const char *cidmatch_display;	/*!< Caller id to match (display version) */
	int priority;			/*!< Priority */
	const char *label;		/*!< Label */
	struct ast_context *parent;	/*!< The context this extension belongs to  */
	const char *app;		/*!< Application to execute */
	struct ast_app *cached_app;     /*!< Cached location of application */
	void *data;			/*!< Data to use (arguments) */
	void (*datad)(void *);		/*!< Data destructor */
	struct ast_exten *peer;		/*!< Next higher priority with our extension */
	struct ast_hashtab *peer_table;    /*!< Priorities list in hashtab form -- only on the head of the peer list */
	struct ast_hashtab *peer_label_table; /*!< labeled priorities in the peers -- only on the head of the peer list */
	const char *registrar;		/*!< Registrar */
	const char *registrar_file;     /*!< File name used to register extension */
	int registrar_line;             /*!< Line number the extension was registered in text */
	struct ast_exten *next;		/*!< Extension with a greater ID */
	char stuff[0];
};

/*! \brief match_char: forms a syntax tree for quick matching of extension patterns */
struct match_char
{
	int is_pattern; /* the pattern started with '_' */
	int deleted;    /* if this is set, then... don't return it */
	int specificity; /* simply the strlen of x, or 10 for X, 9 for Z, and 8 for N; and '.' and '!' will add 11 ? */
	struct match_char *alt_char;
	struct match_char *next_char;
	struct ast_exten *exten; /* attached to last char of a pattern for exten */
	char x[1];       /* the pattern itself-- matches a single char */
};

struct scoreboard  /* make sure all fields are 0 before calling new_find_extension */
{
	int total_specificity;
	int total_length;
	char last_char;   /* set to ! or . if they are the end of the pattern */
	int canmatch;     /* if the string to match was just too short */
	struct match_char *node;
	struct ast_exten *canmatch_exten;
	struct ast_exten *exten;
};

/*! \brief ast_context: An extension context - must remain in sync with fake_context */
struct ast_context {
	ast_rwlock_t lock;			/*!< A lock to prevent multiple threads from clobbering the context */
	struct ast_exten *root;			/*!< The root of the list of extensions */
	struct ast_hashtab *root_table;            /*!< For exact matches on the extensions in the pattern tree, and for traversals of the pattern_tree  */
	struct match_char *pattern_tree;        /*!< A tree to speed up extension pattern matching */
	struct ast_context *next;		/*!< Link them together */
	struct ast_includes includes;		/*!< Include other contexts */
	struct ast_ignorepats ignorepats;	/*!< Patterns for which to continue playing dialtone */
	struct ast_sws alts;			/*!< Alternative switches */
	char *registrar;			/*!< Registrar -- make sure you malloc this, as the registrar may have to survive module unloads */
	int refcount;                   /*!< each module that would have created this context should inc/dec this as appropriate */
	int autohints;                  /*!< Whether autohints support is enabled or not */
	ast_mutex_t macrolock;			/*!< A lock to implement "exclusive" macros - held whilst a call is executing in the macro */
	char name[0];				/*!< Name of the context */
};

/*! \brief ast_state_cb: An extension state notify register item */
struct ast_state_cb {
	/*! Watcher ID returned when registered. */
	int id;
	/*! Arbitrary data passed for callbacks. */
	void *data;
	/*! Flag if this callback is an extended callback containing detailed device status */
	int extended;
	/*! Callback when state changes. */
	ast_state_cb_type change_cb;
	/*! Callback when destroyed so any resources given by the registerer can be freed. */
	ast_state_cb_destroy_type destroy_cb;
	/*! \note Only used by ast_merge_contexts_and_delete */
	AST_LIST_ENTRY(ast_state_cb) entry;
};

/*!
 * \brief Structure for dial plan hints
 *
 * \note Hints are pointers from an extension in the dialplan to
 * one or more devices (tech/name)
 *
 * See \ref AstExtState
 */
struct ast_hint {
	/*!
	 * \brief Hint extension
	 *
	 * \note
	 * Will never be NULL while the hint is in the hints container.
	 */
	struct ast_exten *exten;
	struct ao2_container *callbacks; /*!< Device state callback container for this extension */

	/*! Dev state variables */
	int laststate;			/*!< Last known device state */

	/*! Presence state variables */
	int last_presence_state;     /*!< Last known presence state */
	char *last_presence_subtype; /*!< Last known presence subtype string */
	char *last_presence_message; /*!< Last known presence message string */

	char context_name[AST_MAX_CONTEXT];/*!< Context of destroyed hint extension. */
	char exten_name[AST_MAX_EXTENSION];/*!< Extension of destroyed hint extension. */

	AST_VECTOR(, char *) devices; /*!< Devices associated with the hint */
};

STASIS_MESSAGE_TYPE_DEFN_LOCAL(hint_change_message_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(hint_remove_message_type);

#define HINTDEVICE_DATA_LENGTH 16
AST_THREADSTORAGE(hintdevice_data);

/* --- Hash tables of various objects --------*/
#ifdef LOW_MEMORY
#define HASH_EXTENHINT_SIZE 17
#else
#define HASH_EXTENHINT_SIZE 563
#endif


/*! \brief Container for hint devices */
static struct ao2_container *hintdevices;

/*!
 * \brief Structure for dial plan hint devices
 * \note hintdevice is one device pointing to a hint.
 */
struct ast_hintdevice {
	/*!
	 * \brief Hint this hintdevice belongs to.
	 * \note Holds a reference to the hint object.
	 */
	struct ast_hint *hint;
	/*! Name of the hint device. */
	char hintdevice[1];
};

/*! \brief Container for autohint contexts */
static struct ao2_container *autohints;

/*!
 * \brief Structure for dial plan autohints
 */
struct ast_autohint {
	/*! \brief Name of the registrar */
	char *registrar;
	/*! \brief Name of the context */
	char context[1];
};

/*!
 * \note Using the device for hash
 */
static int hintdevice_hash_cb(const void *obj, const int flags)
{
	const struct ast_hintdevice *ext;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		ext = obj;
		key = ext->hintdevice;
		break;
	default:
		ast_assert(0);
		return 0;
	}

	return ast_str_case_hash(key);
}

/*!
 * \note Devices on hints are not unique so no CMP_STOP is returned
 * Dont use ao2_find against hintdevices container cause there always
 * could be more than one result.
 */
static int hintdevice_cmp_multiple(void *obj, void *arg, int flags)
{
	struct ast_hintdevice *left = obj;
	struct ast_hintdevice *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right->hintdevice;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(left->hintdevice, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		* We could also use a partial key struct containing a length
		* so strlen() does not get called for every comparison instead.
		*/
		cmp = strncmp(left->hintdevice, right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp ? 0 : CMP_MATCH;
}

/*!
 * \note Using the context name for hash
 */
static int autohint_hash_cb(const void *obj, const int flags)
{
	const struct ast_autohint *autohint;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		autohint = obj;
		key = autohint->context;
		break;
	default:
		ast_assert(0);
		return 0;
	}

	return ast_str_case_hash(key);
}

static int autohint_cmp(void *obj, void *arg, int flags)
{
	struct ast_autohint *left = obj;
	struct ast_autohint *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right->context;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(left->context, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		* We could also use a partial key struct containing a length
		* so strlen() does not get called for every comparison instead.
		*/
		cmp = strncmp(left->context, right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp ? 0 : CMP_MATCH | CMP_STOP;
}

/*! \internal \brief \c ao2_callback function to remove hintdevices */
static int hintdevice_remove_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_hintdevice *candidate = obj;
	char *device = arg;
	struct ast_hint *hint = data;

	if (!strcasecmp(candidate->hintdevice, device)
		&& candidate->hint == hint) {
		return CMP_MATCH;
	}
	return 0;
}

static int remove_hintdevice(struct ast_hint *hint)
{
	while (AST_VECTOR_SIZE(&hint->devices) > 0) {
		char *device = AST_VECTOR_GET(&hint->devices, 0);

		ao2_t_callback_data(hintdevices, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA,
			hintdevice_remove_cb, device, hint, "Remove device from container");
		AST_VECTOR_REMOVE_UNORDERED(&hint->devices, 0);
		ast_free(device);
	}

	return 0;
}

static char *parse_hint_device(struct ast_str *hint_args);
/*!
 * \internal
 * \brief Destroy the given hintdevice object.
 *
 * \param obj Hint device to destroy.
 *
 * \return Nothing
 */
static void hintdevice_destroy(void *obj)
{
	struct ast_hintdevice *doomed = obj;

	if (doomed->hint) {
		ao2_ref(doomed->hint, -1);
		doomed->hint = NULL;
	}
}

/*! \brief add hintdevice structure and link it into the container.
 */
static int add_hintdevice(struct ast_hint *hint, const char *devicelist)
{
	struct ast_str *str;
	char *parse;
	char *cur;
	struct ast_hintdevice *device;
	int devicelength;

	if (!hint || !devicelist) {
		/* Trying to add garbage? Don't bother. */
		return 0;
	}
	if (!(str = ast_str_thread_get(&hintdevice_data, 16))) {
		return -1;
	}
	ast_str_set(&str, 0, "%s", devicelist);
	parse = ast_str_buffer(str);

	/* Spit on '&' and ',' to handle presence hints as well */
	while ((cur = strsep(&parse, "&,"))) {
		char *device_name;

		devicelength = strlen(cur);
		if (!devicelength) {
			continue;
		}

		device_name = ast_strdup(cur);
		if (!device_name) {
			return -1;
		}

		device = ao2_t_alloc(sizeof(*device) + devicelength, hintdevice_destroy,
			"allocating a hintdevice structure");
		if (!device) {
			ast_free(device_name);
			return -1;
		}
		strcpy(device->hintdevice, cur);
		ao2_ref(hint, +1);
		device->hint = hint;
		if (AST_VECTOR_APPEND(&hint->devices, device_name)) {
			ast_free(device_name);
			ao2_ref(device, -1);
			return -1;
		}
		ao2_t_link(hintdevices, device, "Linking device into hintdevice container.");
		ao2_t_ref(device, -1, "hintdevice is linked so we can unref");
	}

	return 0;
}


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

struct pbx_exception {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(context);	/*!< Context associated with this exception */
		AST_STRING_FIELD(exten);	/*!< Exten associated with this exception */
		AST_STRING_FIELD(reason);		/*!< The exception reason */
	);

	int priority;				/*!< Priority associated with this exception */
};

static int matchcid(const char *cidpattern, const char *callerid);
#ifdef NEED_DEBUG
static void log_match_char_tree(struct match_char *node, char *prefix); /* for use anywhere */
#endif
static void new_find_extension(const char *str, struct scoreboard *score,
		struct match_char *tree, int length, int spec, const char *callerid,
		const char *label, enum ext_match_t action);
static struct match_char *already_in_tree(struct match_char *current, char *pat, int is_pattern);
static struct match_char *add_exten_to_pattern_tree(struct ast_context *con,
		struct ast_exten *e1, int findonly);
static void create_match_char_tree(struct ast_context *con);
static struct ast_exten *get_canmatch_exten(struct match_char *node);
static void destroy_pattern_tree(struct match_char *pattern_tree);
static int hashtab_compare_extens(const void *ha_a, const void *ah_b);
static int hashtab_compare_exten_numbers(const void *ah_a, const void *ah_b);
static int hashtab_compare_exten_labels(const void *ah_a, const void *ah_b);
static unsigned int hashtab_hash_extens(const void *obj);
static unsigned int hashtab_hash_priority(const void *obj);
static unsigned int hashtab_hash_labels(const void *obj);
static void __ast_internal_context_destroy( struct ast_context *con);
static int ast_add_extension_nolock(const char *context, int replace, const char *extension,
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar);
static int ast_add_extension2_lockopt(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar, const char *registrar_file, int registrar_line,
	int lock_context);
static struct ast_context *find_context_locked(const char *context);
static struct ast_context *find_context(const char *context);
static void get_device_state_causing_channels(struct ao2_container *c);
static unsigned int ext_strncpy(char *dst, const char *src, size_t dst_size, int nofluff);

/*!
 * \internal
 * \brief Character array comparison function for qsort.
 *
 * \param a Left side object.
 * \param b Right side object.
 *
 * \retval <0 if a < b
 * \retval =0 if a = b
 * \retval >0 if a > b
 */
static int compare_char(const void *a, const void *b)
{
	const unsigned char *ac = a;
	const unsigned char *bc = b;

	return *ac - *bc;
}

/* labels, contexts are case sensitive  priority numbers are ints */
int ast_hashtab_compare_contexts(const void *ah_a, const void *ah_b)
{
	const struct ast_context *ac = ah_a;
	const struct ast_context *bc = ah_b;
	if (!ac || !bc) /* safety valve, but it might prevent a crash you'd rather have happen */
		return 1;
	/* assume context names are registered in a string table! */
	return strcmp(ac->name, bc->name);
}

static int hashtab_compare_extens(const void *ah_a, const void *ah_b)
{
	const struct ast_exten *ac = ah_a;
	const struct ast_exten *bc = ah_b;
	int x = strcmp(ac->exten, bc->exten);
	if (x) { /* if exten names are diff, then return */
		return x;
	}

	/* but if they are the same, do the cidmatch values match? */
	/* not sure which side may be using ast_ext_matchcid_types, so check both */
	if (ac->matchcid == AST_EXT_MATCHCID_ANY || bc->matchcid == AST_EXT_MATCHCID_ANY) {
		return 0;
	}
	if (ac->matchcid == AST_EXT_MATCHCID_OFF && bc->matchcid == AST_EXT_MATCHCID_OFF) {
		return 0;
	}
	if (ac->matchcid != bc->matchcid) {
		return 1;
	}
	/* all other cases already disposed of, match now required on callerid string (cidmatch) */
	/* although ast_add_extension2_lockopt() enforces non-zero ptr, caller may not have */
	if (ast_strlen_zero(ac->cidmatch) && ast_strlen_zero(bc->cidmatch)) {
		return 0;
	}
	return strcmp(ac->cidmatch, bc->cidmatch);
}

static int hashtab_compare_exten_numbers(const void *ah_a, const void *ah_b)
{
	const struct ast_exten *ac = ah_a;
	const struct ast_exten *bc = ah_b;
	return ac->priority != bc->priority;
}

static int hashtab_compare_exten_labels(const void *ah_a, const void *ah_b)
{
	const struct ast_exten *ac = ah_a;
	const struct ast_exten *bc = ah_b;
	return strcmp(S_OR(ac->label, ""), S_OR(bc->label, ""));
}

unsigned int ast_hashtab_hash_contexts(const void *obj)
{
	const struct ast_context *ac = obj;
	return ast_hashtab_hash_string(ac->name);
}

static unsigned int hashtab_hash_extens(const void *obj)
{
	const struct ast_exten *ac = obj;
	unsigned int x = ast_hashtab_hash_string(ac->exten);
	unsigned int y = 0;
	if (ac->matchcid == AST_EXT_MATCHCID_ON)
		y = ast_hashtab_hash_string(ac->cidmatch);
	return x+y;
}

static unsigned int hashtab_hash_priority(const void *obj)
{
	const struct ast_exten *ac = obj;
	return ast_hashtab_hash_int(ac->priority);
}

static unsigned int hashtab_hash_labels(const void *obj)
{
	const struct ast_exten *ac = obj;
	return ast_hashtab_hash_string(S_OR(ac->label, ""));
}

static int autofallthrough = 1;
static int extenpatternmatchnew = 0;
static char *overrideswitch = NULL;

/*! \brief Subscription for device state change events */
static struct stasis_subscription *device_state_sub;
/*! \brief Subscription for presence state change events */
static struct stasis_subscription *presence_state_sub;

AST_MUTEX_DEFINE_STATIC(maxcalllock);
static int countcalls;
static int totalcalls;

static struct ast_context *contexts;
static struct ast_hashtab *contexts_table = NULL;

/*!
 * \brief Lock for the ast_context list
 * \note
 * This lock MUST be recursive, or a deadlock on reload may result.  See
 * https://issues.asterisk.org/view.php?id=17643
 */
AST_MUTEX_DEFINE_STATIC(conlock);

/*!
 * \brief Lock to hold off restructuring of hints by ast_merge_contexts_and_delete.
 */
AST_MUTEX_DEFINE_STATIC(context_merge_lock);

static int stateid = 1;
/*!
 * \note When holding this container's lock, do _not_ do
 * anything that will cause conlock to be taken, unless you
 * _already_ hold it.  The ast_merge_contexts_and_delete function
 * will take the locks in conlock/hints order, so any other
 * paths that require both locks must also take them in that
 * order.
 */
static struct ao2_container *hints;

static struct ao2_container *statecbs;

#ifdef CONTEXT_DEBUG

/* these routines are provided for doing run-time checks
   on the extension structures, in case you are having
   problems, this routine might help you localize where
   the problem is occurring. It's kinda like a debug memory
   allocator's arena checker... It'll eat up your cpu cycles!
   but you'll see, if you call it in the right places,
   right where your problems began...
*/

/* you can break on the check_contexts_trouble()
routine in your debugger to stop at the moment
there's a problem */
void check_contexts_trouble(void);

void check_contexts_trouble(void)
{
	int x = 1;
	x = 2;
}

int check_contexts(char *, int);

int check_contexts(char *file, int line )
{
	struct ast_hashtab_iter *t1;
	struct ast_context *c1, *c2;
	int found = 0;
	struct ast_exten *e1, *e2, *e3;
	struct ast_exten ex;

	/* try to find inconsistencies */
	/* is every context in the context table in the context list and vice-versa ? */

	if (!contexts_table) {
		ast_log(LOG_NOTICE,"Called from: %s:%d: No contexts_table!\n", file, line);
		usleep(500000);
	}

	t1 = ast_hashtab_start_traversal(contexts_table);
	while( (c1 = ast_hashtab_next(t1))) {
		for(c2=contexts;c2;c2=c2->next) {
			if (!strcmp(c1->name, c2->name)) {
				found = 1;
				break;
			}
		}
		if (!found) {
			ast_log(LOG_NOTICE,"Called from: %s:%d: Could not find the %s context in the linked list\n", file, line, c1->name);
			check_contexts_trouble();
		}
	}
	ast_hashtab_end_traversal(t1);
	for(c2=contexts;c2;c2=c2->next) {
		c1 = find_context_locked(c2->name);
		if (!c1) {
			ast_log(LOG_NOTICE,"Called from: %s:%d: Could not find the %s context in the hashtab\n", file, line, c2->name);
			check_contexts_trouble();
		} else
			ast_unlock_contexts();
	}

	/* loop thru all contexts, and verify the exten structure compares to the
	   hashtab structure */
	for(c2=contexts;c2;c2=c2->next) {
		c1 = find_context_locked(c2->name);
		if (c1) {
			ast_unlock_contexts();

			/* is every entry in the root list also in the root_table? */
			for(e1 = c1->root; e1; e1=e1->next)
			{
				char dummy_name[1024];
				ex.exten = dummy_name;
				ex.matchcid = e1->matchcid;
				ex.cidmatch = e1->cidmatch;
				ast_copy_string(dummy_name, e1->exten, sizeof(dummy_name));
				e2 = ast_hashtab_lookup(c1->root_table, &ex);
				if (!e2) {
					if (e1->matchcid == AST_EXT_MATCHCID_ON) {
						ast_log(LOG_NOTICE, "Called from: %s:%d: The %s context records "
							"the exten %s (CID match: %s) but it is not in its root_table\n",
							file, line, c2->name, dummy_name, e1->cidmatch_display);
					} else {
						ast_log(LOG_NOTICE, "Called from: %s:%d: The %s context records "
							"the exten %s but it is not in its root_table\n",
							file, line, c2->name, dummy_name);
					}
					check_contexts_trouble();
				}
			}

			/* is every entry in the root_table also in the root list? */
			if (!c2->root_table) {
				if (c2->root) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: No c2->root_table for context %s!\n", file, line, c2->name);
					usleep(500000);
				}
			} else {
				t1 = ast_hashtab_start_traversal(c2->root_table);
				while( (e2 = ast_hashtab_next(t1)) ) {
					for(e1=c2->root;e1;e1=e1->next) {
						if (!strcmp(e1->exten, e2->exten)) {
							found = 1;
							break;
						}
					}
					if (!found) {
						ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context records the exten %s but it is not in its root_table\n", file, line, c2->name, e2->exten);
						check_contexts_trouble();
					}

				}
				ast_hashtab_end_traversal(t1);
			}
		}
		/* is every priority reflected in the peer_table at the head of the list? */

		/* is every entry in the root list also in the root_table? */
		/* are the per-extension peer_tables in the right place? */

		for(e1 = c2->root; e1; e1 = e1->next) {

			for(e2=e1;e2;e2=e2->peer) {
				ex.priority = e2->priority;
				if (e2 != e1 && e2->peer_table) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority has a peer_table entry, and shouldn't!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}

				if (e2 != e1 && e2->peer_label_table) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority has a peer_label_table entry, and shouldn't!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}

				if (e2 == e1 && !e2->peer_table){
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority doesn't have a peer_table!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}

				if (e2 == e1 && !e2->peer_label_table) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority doesn't have a peer_label_table!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}


				e3 = ast_hashtab_lookup(e1->peer_table, &ex);
				if (!e3) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority is not reflected in the peer_table\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}
			}

			if (!e1->peer_table){
				ast_log(LOG_NOTICE,"Called from: %s:%d: No e1->peer_table!\n", file, line);
				usleep(500000);
			}

			/* is every entry in the peer_table also in the peer list? */
			t1 = ast_hashtab_start_traversal(e1->peer_table);
			while( (e2 = ast_hashtab_next(t1)) ) {
				for(e3=e1;e3;e3=e3->peer) {
					if (e3->priority == e2->priority) {
						found = 1;
						break;
					}
				}
				if (!found) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority is not reflected in the peer list\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}
			}
			ast_hashtab_end_traversal(t1);
		}
	}
	return 0;
}
#endif

static void pbx_destroy(struct ast_pbx *p)
{
	ast_free(p);
}

/* form a tree that fully describes all the patterns in a context's extensions
 * in this tree, a "node" represents an individual character or character set
 * meant to match the corresponding character in a dial string. The tree
 * consists of a series of match_char structs linked in a chain
 * via the alt_char pointers. More than one pattern can share the same parts of the
 * tree as other extensions with the same pattern to that point.
 * My first attempt to duplicate the finding of the 'best' pattern was flawed in that
 * I misunderstood the general algorithm. I thought that the 'best' pattern
 * was the one with lowest total score. This was not true. Thus, if you have
 * patterns "1XXXXX" and "X11111", you would be tempted to say that "X11111" is
 * the "best" match because it has fewer X's, and is therefore more specific,
 * but this is not how the old algorithm works. It sorts matching patterns
 * in a similar collating sequence as sorting alphabetic strings, from left to
 * right. Thus, "1XXXXX" comes before "X11111", and would be the "better" match,
 * because "1" is more specific than "X".
 * So, to accomodate this philosophy, I sort the tree branches along the alt_char
 * line so they are lowest to highest in specificity numbers. This way, as soon
 * as we encounter our first complete match, we automatically have the "best"
 * match and can stop the traversal immediately. Same for CANMATCH/MATCHMORE.
 * If anyone would like to resurrect the "wrong" pattern trie searching algorithm,
 * they are welcome to revert pbx to before 1 Apr 2008.
 * As an example, consider these 4 extensions:
 * (a) NXXNXXXXXX
 * (b) 307754XXXX
 * (c) fax
 * (d) NXXXXXXXXX
 *
 * In the above, between (a) and (d), (a) is a more specific pattern than (d), and would win over
 * most numbers. For all numbers beginning with 307754, (b) should always win.
 *
 * These pattern should form a (sorted) tree that looks like this:
 *   { "3" }  --next-->  { "0" }  --next--> { "7" } --next--> { "7" } --next--> { "5" } ... blah ... --> { "X" exten_match: (b) }
 *      |
 *      |alt
 *      |
 *   { "f" }  --next-->  { "a" }  --next--> { "x"  exten_match: (c) }
 *   { "N" }  --next-->  { "X" }  --next--> { "X" } --next--> { "N" } --next--> { "X" } ... blah ... --> { "X" exten_match: (a) }
 *      |                                                        |
 *      |                                                        |alt
 *      |alt                                                     |
 *      |                                                     { "X" } --next--> { "X" } ... blah ... --> { "X" exten_match: (d) }
 *      |
 *     NULL
 *
 *   In the above, I could easily turn "N" into "23456789", but I think that a quick "if( *z >= '2' && *z <= '9' )" might take
 *   fewer CPU cycles than a call to strchr("23456789",*z), where *z is the char to match...
 *
 *   traversal is pretty simple: one routine merely traverses the alt list, and for each matching char in the pattern,  it calls itself
 *   on the corresponding next pointer, incrementing also the pointer of the string to be matched, and passing the total specificity and length.
 *   We pass a pointer to a scoreboard down through, also.
 *   The scoreboard isn't as necessary to the revised algorithm, but I kept it as a handy way to return the matched extension.
 *   The first complete match ends the traversal, which should make this version of the pattern matcher faster
 *   the previous. The same goes for "CANMATCH" or "MATCHMORE"; the first such match ends the traversal. In both
 *   these cases, the reason we can stop immediately, is because the first pattern match found will be the "best"
 *   according to the sort criteria.
 *   Hope the limit on stack depth won't be a problem... this routine should
 *   be pretty lean as far a stack usage goes. Any non-match terminates the recursion down a branch.
 *
 *   In the above example, with the number "3077549999" as the pattern, the traversor could match extensions a, b and d.  All are
 *   of length 10; they have total specificities of  24580, 10246, and 25090, respectively, not that this matters
 *   at all. (b) wins purely because the first character "3" is much more specific (lower specificity) than "N". I have
 *   left the specificity totals in the code as an artifact; at some point, I will strip it out.
 *
 *   Just how much time this algorithm might save over a plain linear traversal over all possible patterns is unknown,
 *   because it's a function of how many extensions are stored in a context. With thousands of extensions, the speedup
 *   can be very noticeable. The new matching algorithm can run several hundreds of times faster, if not a thousand or
 *   more times faster in extreme cases.
 *
 *   MatchCID patterns are also supported, and stored in the tree just as the extension pattern is. Thus, you
 *   can have patterns in your CID field as well.
 *
 * */


static void update_scoreboard(struct scoreboard *board, int length, int spec, struct ast_exten *exten, char last, const char *callerid, int deleted, struct match_char *node)
{
	/* if this extension is marked as deleted, then skip this -- if it never shows
	   on the scoreboard, it will never be found, nor will halt the traversal. */
	if (deleted)
		return;
	board->total_specificity = spec;
	board->total_length = length;
	board->exten = exten;
	board->last_char = last;
	board->node = node;
#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE,"Scoreboarding (LONGER) %s, len=%d, score=%d\n", exten->exten, length, spec);
#endif
}

#ifdef NEED_DEBUG
static void log_match_char_tree(struct match_char *node, char *prefix)
{
	char extenstr[40];
	struct ast_str *my_prefix = ast_str_alloca(1024);

	extenstr[0] = '\0';

	if (node && node->exten)
		snprintf(extenstr, sizeof(extenstr), "(%p)", node->exten);

	if (strlen(node->x) > 1) {
		ast_debug(1, "%s[%s]:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y':'N',
			node->deleted? 'D':'-', node->specificity, node->exten? "EXTEN:":"",
			node->exten ? node->exten->exten : "", extenstr);
	} else {
		ast_debug(1, "%s%s:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y':'N',
			node->deleted? 'D':'-', node->specificity, node->exten? "EXTEN:":"",
			node->exten ? node->exten->exten : "", extenstr);
	}

	ast_str_set(&my_prefix, 0, "%s+       ", prefix);

	if (node->next_char)
		log_match_char_tree(node->next_char, ast_str_buffer(my_prefix));

	if (node->alt_char)
		log_match_char_tree(node->alt_char, prefix);
}
#endif

static void cli_match_char_tree(struct match_char *node, char *prefix, int fd)
{
	char extenstr[40];
	struct ast_str *my_prefix = ast_str_alloca(1024);

	extenstr[0] = '\0';

	if (node->exten) {
		snprintf(extenstr, sizeof(extenstr), "(%p)", node->exten);
	}

	if (strlen(node->x) > 1) {
		ast_cli(fd, "%s[%s]:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y' : 'N',
			node->deleted ? 'D' : '-', node->specificity, node->exten? "EXTEN:" : "",
			node->exten ? node->exten->name : "", extenstr);
	} else {
		ast_cli(fd, "%s%s:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y' : 'N',
			node->deleted ? 'D' : '-', node->specificity, node->exten? "EXTEN:" : "",
			node->exten ? node->exten->name : "", extenstr);
	}

	ast_str_set(&my_prefix, 0, "%s+       ", prefix);

	if (node->next_char)
		cli_match_char_tree(node->next_char, ast_str_buffer(my_prefix), fd);

	if (node->alt_char)
		cli_match_char_tree(node->alt_char, prefix, fd);
}

static struct ast_exten *get_canmatch_exten(struct match_char *node)
{
	/* find the exten at the end of the rope */
	struct match_char *node2 = node;

	for (node2 = node; node2; node2 = node2->next_char) {
		if (node2->exten) {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"CanMatch_exten returns exten %s(%p)\n", node2->exten->exten, node2->exten);
#endif
			return node2->exten;
		}
	}
#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE,"CanMatch_exten returns NULL, match_char=%s\n", node->x);
#endif
	return 0;
}

static struct ast_exten *trie_find_next_match(struct match_char *node)
{
	struct match_char *m3;
	struct match_char *m4;
	struct ast_exten *e3;

	if (node && node->x[0] == '.' && !node->x[1]) { /* dot and ! will ALWAYS be next match in a matchmore */
		return node->exten;
	}

	if (node && node->x[0] == '!' && !node->x[1]) {
		return node->exten;
	}

	if (!node || !node->next_char) {
		return NULL;
	}

	m3 = node->next_char;

	if (m3->exten) {
		return m3->exten;
	}
	for (m4 = m3->alt_char; m4; m4 = m4->alt_char) {
		if (m4->exten) {
			return m4->exten;
		}
	}
	for (m4 = m3; m4; m4 = m4->alt_char) {
		e3 = trie_find_next_match(m3);
		if (e3) {
			return e3;
		}
	}

	return NULL;
}

#ifdef DEBUG_THIS
static char *action2str(enum ext_match_t action)
{
	switch (action) {
	case E_MATCH:
		return "MATCH";
	case E_CANMATCH:
		return "CANMATCH";
	case E_MATCHMORE:
		return "MATCHMORE";
	case E_FINDLABEL:
		return "FINDLABEL";
	case E_SPAWN:
		return "SPAWN";
	default:
		return "?ACTION?";
	}
}

#endif

static const char *candidate_exten_advance(const char *str)
{
	str++;
	while (*str == '-') {
		str++;
	}
	return str;
}

#define MORE(s) (*candidate_exten_advance(s))
#define ADVANCE(s) candidate_exten_advance(s)

static void new_find_extension(const char *str, struct scoreboard *score, struct match_char *tree, int length, int spec, const char *callerid, const char *label, enum ext_match_t action)
{
	struct match_char *p; /* note minimal stack storage requirements */
	struct ast_exten pattern = { .label = label };
#ifdef DEBUG_THIS
	if (tree)
		ast_log(LOG_NOTICE,"new_find_extension called with %s on (sub)tree %s action=%s\n", str, tree->x, action2str(action));
	else
		ast_log(LOG_NOTICE,"new_find_extension called with %s on (sub)tree NULL action=%s\n", str, action2str(action));
#endif
	for (p = tree; p; p = p->alt_char) {
		if (p->is_pattern) {
			if (p->x[0] == 'N') {
				if (p->x[1] == 0 && *str >= '2' && *str <= '9' ) {
#define	NEW_MATCHER_CHK_MATCH	       \
					if (p->exten && !MORE(str)) { /* if a shorter pattern matches along the way, might as well report it */             \
						if (action == E_MATCH || action == E_SPAWN || action == E_FINDLABEL) { /* if in CANMATCH/MATCHMORE, don't let matches get in the way */   \
							update_scoreboard(score, length + 1, spec + p->specificity, p->exten, 0, callerid, p->deleted, p);                 \
							if (!p->deleted) {                                                                                           \
								if (action == E_FINDLABEL) {                                                                             \
									if (ast_hashtab_lookup(score->exten->peer_label_table, &pattern)) {                                  \
										ast_debug(4, "Found label in preferred extension\n");                                            \
										return;                                                                                          \
									}                                                                                                    \
								} else {                                                                                                 \
									ast_debug(4, "returning an exact match-- first found-- %s\n", p->exten->name);                       \
									return; /* the first match, by definition, will be the best, because of the sorted tree */           \
								}                                                                                                        \
							}                                                                                                            \
						}                                                                                                                \
					}

#define	NEW_MATCHER_RECURSE	           \
					if (p->next_char && (MORE(str) || (p->next_char->x[0] == '/' && p->next_char->x[1] == 0)                 \
		                                       || p->next_char->x[0] == '!')) {                                          \
						if (MORE(str) || p->next_char->x[0] == '!') {                                                         \
							new_find_extension(ADVANCE(str), score, p->next_char, length + 1, spec + p->specificity, callerid, label, action); \
							if (score->exten)  {                                                                             \
						        ast_debug(4 ,"returning an exact match-- %s\n", score->exten->name);                         \
								return; /* the first match is all we need */                                                 \
							}												                                                 \
						} else {                                                                                             \
							new_find_extension("/", score, p->next_char, length + 1, spec + p->specificity, callerid, label, action);	 \
							if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {      \
						        ast_debug(4,"returning a (can/more) match--- %s\n", score->exten ? score->exten->name :      \
		                               "NULL");                                                                        \
								return; /* the first match is all we need */                                                 \
							}												                                                 \
						}                                                                                                    \
					} else if ((p->next_char || action == E_CANMATCH) && !MORE(str)) {                                                                  \
						score->canmatch = 1;                                                                                 \
						score->canmatch_exten = get_canmatch_exten(p);                                                       \
						if (action == E_CANMATCH || action == E_MATCHMORE) {                                                 \
					        ast_debug(4, "returning a canmatch/matchmore--- str=%s\n", str);                                  \
							return;                                                                                          \
						}												                                                     \
					}

					NEW_MATCHER_CHK_MATCH;
					NEW_MATCHER_RECURSE;
				}
			} else if (p->x[0] == 'Z') {
				if (p->x[1] == 0 && *str >= '1' && *str <= '9' ) {
					NEW_MATCHER_CHK_MATCH;
					NEW_MATCHER_RECURSE;
				}
			} else if (p->x[0] == 'X') {
				if (p->x[1] == 0 && *str >= '0' && *str <= '9' ) {
					NEW_MATCHER_CHK_MATCH;
					NEW_MATCHER_RECURSE;
				}
			} else if (p->x[0] == '.' && p->x[1] == 0) {
				/* how many chars will the . match against? */
				int i = 0;
				const char *str2 = str;
				while (*str2 && *str2 != '/') {
					str2++;
					i++;
				}
				if (p->exten && *str2 != '/') {
					update_scoreboard(score, length + i, spec + (i * p->specificity), p->exten, '.', callerid, p->deleted, p);
					if (score->exten) {
						ast_debug(4, "return because scoreboard has a match with '/'--- %s\n",
							score->exten->name);
						return; /* the first match is all we need */
					}
				}
				if (p->next_char && p->next_char->x[0] == '/' && p->next_char->x[1] == 0) {
					new_find_extension("/", score, p->next_char, length + i, spec+(p->specificity*i), callerid, label, action);
					if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {
						ast_debug(4, "return because scoreboard has exact match OR "
							"CANMATCH/MATCHMORE & canmatch set--- %s\n",
							score->exten ? score->exten->name : "NULL");
						return; /* the first match is all we need */
					}
				}
			} else if (p->x[0] == '!' && p->x[1] == 0) {
				/* how many chars will the . match against? */
				int i = 1;
				const char *str2 = str;
				while (*str2 && *str2 != '/') {
					str2++;
					i++;
				}
				if (p->exten && *str2 != '/') {
					update_scoreboard(score, length + 1, spec + (p->specificity * i), p->exten, '!', callerid, p->deleted, p);
					if (score->exten) {
						ast_debug(4, "return because scoreboard has a '!' match--- %s\n",
							score->exten->name);
						return; /* the first match is all we need */
					}
				}
				if (p->next_char && p->next_char->x[0] == '/' && p->next_char->x[1] == 0) {
					new_find_extension("/", score, p->next_char, length + i, spec + (p->specificity * i), callerid, label, action);
					if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {
						ast_debug(4, "return because scoreboard has exact match OR "
							"CANMATCH/MATCHMORE & canmatch set with '/' and '!'--- %s\n",
							score->exten ? score->exten->name : "NULL");
						return; /* the first match is all we need */
					}
				}
			} else if (p->x[0] == '/' && p->x[1] == 0) {
				/* the pattern in the tree includes the cid match! */
				if (p->next_char && callerid && *callerid) {
					new_find_extension(callerid, score, p->next_char, length + 1, spec, callerid, label, action);
					if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {
						ast_debug(4, "return because scoreboard has exact match OR "
							"CANMATCH/MATCHMORE & canmatch set with '/'--- %s\n",
							score->exten ? score->exten->name : "NULL");
						return; /* the first match is all we need */
					}
				}
			} else if (strchr(p->x, *str)) {
				ast_debug(4, "Nothing strange about this match\n");
				NEW_MATCHER_CHK_MATCH;
				NEW_MATCHER_RECURSE;
			}
		} else if (strchr(p->x, *str)) {
			ast_debug(4, "Nothing strange about this match\n");
			NEW_MATCHER_CHK_MATCH;
			NEW_MATCHER_RECURSE;
		}
	}
	ast_debug(4, "return at end of func\n");
}

#undef MORE
#undef ADVANCE

/* the algorithm for forming the extension pattern tree is also a bit simple; you
 * traverse all the extensions in a context, and for each char of the extension,
 * you see if it exists in the tree; if it doesn't, you add it at the appropriate
 * spot. What more can I say? At the end of each exten, you cap it off by adding the
 * address of the extension involved. Duplicate patterns will be complained about.
 *
 * Ideally, this would be done for each context after it is created and fully
 * filled. It could be done as a finishing step after extensions.conf or .ael is
 * loaded, or it could be done when the first search is encountered. It should only
 * have to be done once, until the next unload or reload.
 *
 * I guess forming this pattern tree would be analogous to compiling a regex. Except
 * that a regex only handles 1 pattern, really. This trie holds any number
 * of patterns. Well, really, it **could** be considered a single pattern,
 * where the "|" (or) operator is allowed, I guess, in a way, sort of...
 */

static struct match_char *already_in_tree(struct match_char *current, char *pat, int is_pattern)
{
	struct match_char *t;

	if (!current) {
		return 0;
	}

	for (t = current; t; t = t->alt_char) {
		if (is_pattern == t->is_pattern && !strcmp(pat, t->x)) {/* uh, we may want to sort exploded [] contents to make matching easy */
			return t;
		}
	}

	return 0;
}

/* The first arg is the location of the tree ptr, or the
   address of the next_char ptr in the node, so we can mess
   with it, if we need to insert at the beginning of the list */

static void insert_in_next_chars_alt_char_list(struct match_char **parent_ptr, struct match_char *node)
{
	struct match_char *curr, *lcurr;

	/* insert node into the tree at "current", so the alt_char list from current is
	   sorted in increasing value as you go to the leaves */
	if (!(*parent_ptr)) {
		*parent_ptr = node;
		return;
	}

	if ((*parent_ptr)->specificity > node->specificity) {
		/* insert at head */
		node->alt_char = (*parent_ptr);
		*parent_ptr = node;
		return;
	}

	lcurr = *parent_ptr;
	for (curr = (*parent_ptr)->alt_char; curr; curr = curr->alt_char) {
		if (curr->specificity > node->specificity) {
			node->alt_char = curr;
			lcurr->alt_char = node;
			break;
		}
		lcurr = curr;
	}

	if (!curr) {
		lcurr->alt_char = node;
	}

}

struct pattern_node {
	/*! Pattern node specificity */
	int specif;
	/*! Pattern node match characters. */
	char buf[256];
};

static struct match_char *add_pattern_node(struct ast_context *con, struct match_char *current, const struct pattern_node *pattern, int is_pattern, int already, struct match_char **nextcharptr)
{
	struct match_char *m;

	if (!(m = ast_calloc(1, sizeof(*m) + strlen(pattern->buf)))) {
		return NULL;
	}

	/* strcpy is safe here since we know its size and have allocated
	 * just enough space for when we allocated m
	 */
	strcpy(m->x, pattern->buf);

	/* the specificity scores are the same as used in the old
	   pattern matcher. */
	m->is_pattern = is_pattern;
	if (pattern->specif == 1 && is_pattern && pattern->buf[0] == 'N') {
		m->specificity = 0x0832;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == 'Z') {
		m->specificity = 0x0931;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == 'X') {
		m->specificity = 0x0a30;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == '.') {
		m->specificity = 0x18000;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == '!') {
		m->specificity = 0x28000;
	} else {
		m->specificity = pattern->specif;
	}

	if (!con->pattern_tree) {
		insert_in_next_chars_alt_char_list(&con->pattern_tree, m);
	} else {
		if (already) { /* switch to the new regime (traversing vs appending)*/
			insert_in_next_chars_alt_char_list(nextcharptr, m);
		} else {
			insert_in_next_chars_alt_char_list(&current->next_char, m);
		}
	}

	return m;
}

/*!
 * \internal
 * \brief Extract the next exten pattern node.
 *
 * \param node Pattern node to fill.
 * \param src Next source character to read.
 * \param pattern TRUE if the exten is a pattern.
 * \param extenbuf Original exten buffer to use in diagnostic messages.
 *
 * \retval Ptr to next extenbuf pos to read.
 */
static const char *get_pattern_node(struct pattern_node *node, const char *src, int pattern, const char *extenbuf)
{
#define INC_DST_OVERFLOW_CHECK							\
	do {												\
		if (dst - node->buf < sizeof(node->buf) - 1) {	\
			++dst;										\
		} else {										\
			overflow = 1;								\
		}												\
	} while (0)

	node->specif = 0;
	node->buf[0] = '\0';
	while (*src) {
		if (*src == '[' && pattern) {
			char *dst = node->buf;
			const char *src_next;
			int length;
			int overflow = 0;

			/* get past the '[' */
			++src;
			for (;;) {
				if (*src == '\\') {
					/* Escaped character. */
					++src;
					if (*src == '[' || *src == '\\' || *src == '-' || *src == ']') {
						*dst = *src++;
						INC_DST_OVERFLOW_CHECK;
					}
				} else if (*src == '-') {
					unsigned char first;
					unsigned char last;

					src_next = src;
					first = *(src_next - 1);
					last = *++src_next;

					if (last == '\\') {
						/* Escaped character. */
						last = *++src_next;
					}

					/* Possible char range. */
					if (node->buf[0] && last) {
						/* Expand the char range. */
						while (++first <= last) {
							*dst = first;
							INC_DST_OVERFLOW_CHECK;
						}
						src = src_next + 1;
					} else {
						/*
						 * There was no left or right char for the range.
						 * It is just a '-'.
						 */
						*dst = *src++;
						INC_DST_OVERFLOW_CHECK;
					}
				} else if (*src == '\0') {
					ast_log(LOG_WARNING,
						"A matching ']' was not found for '[' in exten pattern '%s'\n",
						extenbuf);
					break;
				} else if (*src == ']') {
					++src;
					break;
				} else {
					*dst = *src++;
					INC_DST_OVERFLOW_CHECK;
				}
			}
			/* null terminate the exploded range */
			*dst = '\0';

			if (overflow) {
				ast_log(LOG_ERROR,
					"Expanded character set too large to deal with in exten pattern '%s'. Ignoring character set.\n",
					extenbuf);
				node->buf[0] = '\0';
				continue;
			}

			/* Sort the characters in character set. */
			length = strlen(node->buf);
			if (!length) {
				ast_log(LOG_WARNING, "Empty character set in exten pattern '%s'. Ignoring.\n",
					extenbuf);
				node->buf[0] = '\0';
				continue;
			}
			qsort(node->buf, length, 1, compare_char);

			/* Remove duplicate characters from character set. */
			dst = node->buf;
			src_next = node->buf;
			while (*src_next++) {
				if (*dst != *src_next) {
					*++dst = *src_next;
				}
			}

			length = strlen(node->buf);
			length <<= 8;
			node->specif = length | (unsigned char) node->buf[0];
			break;
		} else if (*src == '-') {
			/* Skip dashes in all extensions. */
			++src;
		} else {
			if (*src == '\\') {
				/*
				 * XXX The escape character here does not remove any special
				 * meaning to characters except the '[', '\\', and '-'
				 * characters since they are special only in this function.
				 */
				node->buf[0] = *++src;
				if (!node->buf[0]) {
					break;
				}
			} else {
				node->buf[0] = *src;
				if (pattern) {
					/* make sure n,x,z patterns are canonicalized to N,X,Z */
					if (node->buf[0] == 'n') {
						node->buf[0] = 'N';
					} else if (node->buf[0] == 'x') {
						node->buf[0] = 'X';
					} else if (node->buf[0] == 'z') {
						node->buf[0] = 'Z';
					}
				}
			}
			node->buf[1] = '\0';
			node->specif = 1;
			++src;
			break;
		}
	}
	return src;

#undef INC_DST_OVERFLOW_CHECK
}

static struct match_char *add_exten_to_pattern_tree(struct ast_context *con, struct ast_exten *e1, int findonly)
{
	struct match_char *m1 = NULL;
	struct match_char *m2 = NULL;
	struct match_char **m0;
	const char *pos;
	int already;
	int pattern = 0;
	int idx_cur;
	int idx_next;
	char extenbuf[512];
	struct pattern_node pat_node[2];

	if (e1->matchcid) {
		if (sizeof(extenbuf) < strlen(e1->exten) + strlen(e1->cidmatch) + 2) {
			ast_log(LOG_ERROR,
				"The pattern %s/%s is too big to deal with: it will be ignored! Disaster!\n",
				e1->exten, e1->cidmatch);
			return NULL;
		}
		sprintf(extenbuf, "%s/%s", e1->exten, e1->cidmatch);/* Safe.  We just checked. */
	} else {
		ast_copy_string(extenbuf, e1->exten, sizeof(extenbuf));
	}

#ifdef NEED_DEBUG
	ast_debug(1, "Adding exten %s to tree\n", extenbuf);
#endif
	m1 = con->pattern_tree; /* each pattern starts over at the root of the pattern tree */
	m0 = &con->pattern_tree;
	already = 1;

	pos = extenbuf;
	if (*pos == '_') {
		pattern = 1;
		++pos;
	}
	idx_cur = 0;
	pos = get_pattern_node(&pat_node[idx_cur], pos, pattern, extenbuf);
	for (; pat_node[idx_cur].buf[0]; idx_cur = idx_next) {
		idx_next = (idx_cur + 1) % ARRAY_LEN(pat_node);
		pos = get_pattern_node(&pat_node[idx_next], pos, pattern, extenbuf);

		/* See about adding node to tree. */
		m2 = NULL;
		if (already && (m2 = already_in_tree(m1, pat_node[idx_cur].buf, pattern))
			&& m2->next_char) {
			if (!pat_node[idx_next].buf[0]) {
				/*
				 * This is the end of the pattern, but not the end of the tree.
				 * Mark this node with the exten... a shorter pattern might win
				 * if the longer one doesn't match.
				 */
				if (findonly) {
					return m2;
				}
				if (m2->exten) {
					ast_log(LOG_WARNING, "Found duplicate exten. Had %s found %s\n",
						m2->deleted ? "(deleted/invalid)" : m2->exten->name, e1->name);
				}
				m2->exten = e1;
				m2->deleted = 0;
			}
			m1 = m2->next_char; /* m1 points to the node to compare against */
			m0 = &m2->next_char; /* m0 points to the ptr that points to m1 */
		} else { /* not already OR not m2 OR nor m2->next_char */
			if (m2) {
				if (findonly) {
					return m2;
				}
				m1 = m2; /* while m0 stays the same */
			} else {
				if (findonly) {
					return m1;
				}
				m1 = add_pattern_node(con, m1, &pat_node[idx_cur], pattern, already, m0);
				if (!m1) { /* m1 is the node just added */
					return NULL;
				}
				m0 = &m1->next_char;
			}
			if (!pat_node[idx_next].buf[0]) {
				if (m2 && m2->exten) {
					ast_log(LOG_WARNING, "Found duplicate exten. Had %s found %s\n",
						m2->deleted ? "(deleted/invalid)" : m2->exten->name, e1->name);
				}
				m1->deleted = 0;
				m1->exten = e1;
			}

			/* The 'already' variable is a mini-optimization designed to make it so that we
			 * don't have to call already_in_tree when we know it will return false.
			 */
			already = 0;
		}
	}
	return m1;
}

static void create_match_char_tree(struct ast_context *con)
{
	struct ast_hashtab_iter *t1;
	struct ast_exten *e1;
#ifdef NEED_DEBUG
	int biggest_bucket, resizes, numobjs, numbucks;

	ast_debug(1, "Creating Extension Trie for context %s(%p)\n", con->name, con);
	ast_hashtab_get_stats(con->root_table, &biggest_bucket, &resizes, &numobjs, &numbucks);
	ast_debug(1, "This tree has %d objects in %d bucket lists, longest list=%d objects, and has resized %d times\n",
			numobjs, numbucks, biggest_bucket, resizes);
#endif
	t1 = ast_hashtab_start_traversal(con->root_table);
	while ((e1 = ast_hashtab_next(t1))) {
		if (e1->exten) {
			add_exten_to_pattern_tree(con, e1, 0);
		} else {
			ast_log(LOG_ERROR, "Attempt to create extension with no extension name.\n");
		}
	}
	ast_hashtab_end_traversal(t1);
}

static void destroy_pattern_tree(struct match_char *pattern_tree) /* pattern tree is a simple binary tree, sort of, so the proper way to destroy it is... recursively! */
{
	/* destroy all the alternates */
	if (pattern_tree->alt_char) {
		destroy_pattern_tree(pattern_tree->alt_char);
		pattern_tree->alt_char = 0;
	}
	/* destroy all the nexts */
	if (pattern_tree->next_char) {
		destroy_pattern_tree(pattern_tree->next_char);
		pattern_tree->next_char = 0;
	}
	pattern_tree->exten = 0; /* never hurts to make sure there's no pointers laying around */
	ast_free(pattern_tree);
}

/*!
 * \internal
 * \brief Get the length of the exten string.
 *
 * \param str Exten to get length.
 *
 * \retval strlen of exten.
 */
static int ext_cmp_exten_strlen(const char *str)
{
	int len;

	len = 0;
	for (;;) {
		/* Ignore '-' chars as eye candy fluff. */
		while (*str == '-') {
			++str;
		}
		if (!*str) {
			break;
		}
		++str;
		++len;
	}
	return len;
}

/*!
 * \internal
 * \brief Partial comparison of non-pattern extens.
 *
 * \param left Exten to compare.
 * \param right Exten to compare.  Also matches if this string ends first.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp_exten_partial(const char *left, const char *right)
{
	int cmp;

	for (;;) {
		/* Ignore '-' chars as eye candy fluff. */
		while (*left == '-') {
			++left;
		}
		while (*right == '-') {
			++right;
		}

		if (!*right) {
			/*
			 * Right ended first for partial match or both ended at the same
			 * time for a match.
			 */
			cmp = 0;
			break;
		}

		cmp = *left - *right;
		if (cmp) {
			break;
		}
		++left;
		++right;
	}
	return cmp;
}

/*!
 * \internal
 * \brief Comparison of non-pattern extens.
 *
 * \param left Exten to compare.
 * \param right Exten to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp_exten(const char *left, const char *right)
{
	int cmp;

	for (;;) {
		/* Ignore '-' chars as eye candy fluff. */
		while (*left == '-') {
			++left;
		}
		while (*right == '-') {
			++right;
		}

		cmp = *left - *right;
		if (cmp) {
			break;
		}
		if (!*left) {
			/*
			 * Get here only if both strings ended at the same time.  cmp
			 * would be non-zero if only one string ended.
			 */
			break;
		}
		++left;
		++right;
	}
	return cmp;
}

/*
 * Special characters used in patterns:
 *	'_'	underscore is the leading character of a pattern.
 *		In other position it is treated as a regular char.
 *	'-' The '-' is a separator and ignored.  Why?  So patterns like NXX-XXX-XXXX work.
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
 * itself are considered literally.
 * XXX do we want to consider space as a separator as well ?
 * XXX do we want to consider the separators in non-patterns as well ?
 */

/*!
 * \brief helper functions to sort extension patterns in the desired way,
 * so that more specific patterns appear first.
 *
 * \details
 * The function compares individual characters (or sets of), returning
 * an int where bits 0-7 are the ASCII code of the first char in the set,
 * bits 8-15 are the number of characters in the set, and bits 16-20 are
 * for special cases.
 * This way more specific patterns (smaller character sets) appear first.
 * Wildcards have a special value, so that we can directly compare them to
 * sets by subtracting the two values. In particular:
 *  0x001xx     one character, character set starting with xx
 *  0x0yyxx     yy characters, character set starting with xx
 *  0x18000     '.' (one or more of anything)
 *  0x28000     '!' (zero or more of anything)
 *  0x30000     NUL (end of string)
 *  0x40000     error in set.
 * The pointer to the string is advanced according to needs.
 * NOTES:
 *  1. the empty set is ignored.
 *  2. given that a full set has always 0 as the first element,
 *     we could encode the special cases as 0xffXX where XX
 *     is 1, 2, 3, 4 as used above.
 */
static int ext_cmp_pattern_pos(const char **p, unsigned char *bitwise)
{
#define BITS_PER	8	/* Number of bits per unit (byte). */
	unsigned char c;
	unsigned char cmin;
	int count;
	const char *end;

	do {
		/* Get character and advance. (Ignore '-' chars as eye candy fluff.) */
		do {
			c = *(*p)++;
		} while (c == '-');

		/* always return unless we have a set of chars */
		switch (c) {
		default:
			/* ordinary character */
			bitwise[c / BITS_PER] = 1 << ((BITS_PER - 1) - (c % BITS_PER));
			return 0x0100 | c;

		case 'n':
		case 'N':
			/* 2..9 */
			bitwise[6] = 0x3f;
			bitwise[7] = 0xc0;
			return 0x0800 | '2';

		case 'x':
		case 'X':
			/* 0..9 */
			bitwise[6] = 0xff;
			bitwise[7] = 0xc0;
			return 0x0A00 | '0';

		case 'z':
		case 'Z':
			/* 1..9 */
			bitwise[6] = 0x7f;
			bitwise[7] = 0xc0;
			return 0x0900 | '1';

		case '.':
			/* wildcard */
			return 0x18000;

		case '!':
			/* earlymatch */
			return 0x28000;	/* less specific than '.' */

		case '\0':
			/* empty string */
			*p = NULL;
			return 0x30000;

		case '[':
			/* char set */
			break;
		}
		/* locate end of set */
		end = strchr(*p, ']');

		if (!end) {
			ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
			return 0x40000;	/* XXX make this entry go last... */
		}

		count = 0;
		cmin = 0xFF;
		for (; *p < end; ++*p) {
			unsigned char c1;	/* first char in range */
			unsigned char c2;	/* last char in range */

			c1 = (*p)[0];
			if (*p + 2 < end && (*p)[1] == '-') { /* this is a range */
				c2 = (*p)[2];
				*p += 2;    /* skip a total of 3 chars */
			} else {        /* individual character */
				c2 = c1;
			}
			if (c1 < cmin) {
				cmin = c1;
			}
			for (; c1 <= c2; ++c1) {
				unsigned char mask = 1 << ((BITS_PER - 1) - (c1 % BITS_PER));

				/*
				 * Note: If two character sets score the same, the one with the
				 * lowest ASCII values will compare as coming first.  Must fill
				 * in most significant bits for lower ASCII values to accomplish
				 * the desired sort order.
				 */
				if (!(bitwise[c1 / BITS_PER] & mask)) {
					/* Add the character to the set. */
					bitwise[c1 / BITS_PER] |= mask;
					count += 0x100;
				}
			}
		}
		++*p;
	} while (!count);/* While the char set was empty. */
	return count | cmin;
}

/*!
 * \internal
 * \brief Comparison of exten patterns.
 *
 * \param left Pattern to compare.
 * \param right Pattern to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp_pattern(const char *left, const char *right)
{
	int cmp;
	int left_pos;
	int right_pos;

	for (;;) {
		unsigned char left_bitwise[32] = { 0, };
		unsigned char right_bitwise[32] = { 0, };

		left_pos = ext_cmp_pattern_pos(&left, left_bitwise);
		right_pos = ext_cmp_pattern_pos(&right, right_bitwise);
		cmp = left_pos - right_pos;
		if (!cmp) {
			/*
			 * Are the character sets different, even though they score the same?
			 *
			 * Note: Must swap left and right to get the sense of the
			 * comparison correct.  Otherwise, we would need to multiply by
			 * -1 instead.
			 */
			cmp = memcmp(right_bitwise, left_bitwise, ARRAY_LEN(left_bitwise));
		}
		if (cmp) {
			break;
		}
		if (!left) {
			/*
			 * Get here only if both patterns ended at the same time.  cmp
			 * would be non-zero if only one pattern ended.
			 */
			break;
		}
	}
	return cmp;
}

/*!
 * \internal
 * \brief Comparison of dialplan extens for sorting purposes.
 *
 * \param left Exten/pattern to compare.
 * \param right Exten/pattern to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp(const char *left, const char *right)
{
	/* Make sure non-pattern extens come first. */
	if (left[0] != '_') {
		if (right[0] == '_') {
			return -1;
		}
		/* Compare two non-pattern extens. */
		return ext_cmp_exten(left, right);
	}
	if (right[0] != '_') {
		return 1;
	}

	/*
	 * OK, we need full pattern sorting routine.
	 *
	 * Skip past the underscores
	 */
	return ext_cmp_pattern(left + 1, right + 1);
}

static int ext_fluff_count(const char *exten)
{
	int fluff = 0;

	if (*exten != '_') {
		/* not a pattern, simple check. */
		while (*exten) {
			if (*exten == '-') {
				fluff++;
			}
			exten++;
		}

		return fluff;
	}

	/* do pattern check */
	while (*exten) {
		if (*exten == '-') {
			fluff++;
		} else if (*exten == '[') {
			/* skip set, dashes here matter. */
			exten = strchr(exten, ']');

			if (!exten) {
				/* we'll end up warning about this later, don't spam logs */
				return fluff;
			}
		}
		exten++;
	}

	return fluff;
}

int ast_extension_cmp(const char *a, const char *b)
{
	int cmp;

	cmp = ext_cmp(a, b);
	if (cmp < 0) {
		return -1;
	}
	if (cmp > 0) {
		return 1;
	}
	return 0;
}

/*!
 * \internal
 * \brief used ast_extension_{match|close}
 * mode is as follows:
 *	E_MATCH		success only on exact match
 *	E_MATCHMORE	success only on partial match (i.e. leftover digits in pattern)
 *	E_CANMATCH	either of the above.
 * \retval 0 on no-match
 * \retval 1 on match
 * \retval 2 on early match.
 */

static int _extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	mode &= E_MATCH_MASK;	/* only consider the relevant bits */

#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE,"match core: pat: '%s', dat: '%s', mode=%d\n", pattern, data, (int)mode);
#endif

	if (pattern[0] != '_') { /* not a pattern, try exact or partial match */
		int lp = ext_cmp_exten_strlen(pattern);
		int ld = ext_cmp_exten_strlen(data);

		if (lp < ld) {		/* pattern too short, cannot match */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (0) - pattern too short, cannot match\n");
#endif
			return 0;
		}
		/* depending on the mode, accept full or partial match or both */
		if (mode == E_MATCH) {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (!ext_cmp_exten(%s,%s) when mode== E_MATCH)\n", pattern, data);
#endif
			return !ext_cmp_exten(pattern, data); /* 1 on match, 0 on fail */
		}
		if (ld == 0 || !ext_cmp_exten_partial(pattern, data)) { /* partial or full match */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (mode(%d) == E_MATCHMORE ? lp(%d) > ld(%d) : 1)\n", mode, lp, ld);
#endif
			return (mode == E_MATCHMORE) ? lp > ld : 1; /* XXX should consider '!' and '/' ? */
		} else {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (0) when ld(%d) > 0 && pattern(%s) != data(%s)\n", ld, pattern, data);
#endif
			return 0;
		}
	}
	if (mode == E_MATCH && data[0] == '_') {
		/*
		 * XXX It is bad design that we don't know if we should be
		 * comparing data and pattern as patterns or comparing data if
		 * it conforms to pattern when the function is called.  First,
		 * assume they are both patterns.  If they don't match then try
		 * to see if data conforms to the given pattern.
		 *
		 * note: if this test is left out, then _x. will not match _x. !!!
		 */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "Comparing as patterns first. pattern:%s data:%s\n", pattern, data);
#endif
		if (!ext_cmp_pattern(pattern + 1, data + 1)) {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (1) - pattern matches pattern\n");
#endif
			return 1;
		}
	}

	++pattern; /* skip leading _ */
	/*
	 * XXX below we stop at '/' which is a separator for the CID info. However we should
	 * not store '/' in the pattern at all. When we insure it, we can remove the checks.
	 */
	for (;;) {
		const char *end;

		/* Ignore '-' chars as eye candy fluff. */
		while (*data == '-') {
			++data;
		}
		while (*pattern == '-') {
			++pattern;
		}
		if (!*data || !*pattern || *pattern == '/') {
			break;
		}

		switch (*pattern) {
		case '[':	/* a range */
			++pattern;
			end = strchr(pattern, ']'); /* XXX should deal with escapes ? */
			if (!end) {
				ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
				return 0;	/* unconditional failure */
			}
			if (pattern == end) {
				/* Ignore empty character sets. */
				++pattern;
				continue;
			}
			for (; pattern < end; ++pattern) {
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
			if (pattern >= end) {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) when pattern>=end\n");
#endif
				return 0;
			}
			pattern = end;	/* skip and continue */
			break;
		case 'n':
		case 'N':
			if (*data < '2' || *data > '9') {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) N is not matched\n");
#endif
				return 0;
			}
			break;
		case 'x':
		case 'X':
			if (*data < '0' || *data > '9') {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) X is not matched\n");
#endif
				return 0;
			}
			break;
		case 'z':
		case 'Z':
			if (*data < '1' || *data > '9') {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) Z is not matched\n");
#endif
				return 0;
			}
			break;
		case '.':	/* Must match, even with more digits */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE, "return (1) when '.' is matched\n");
#endif
			return 1;
		case '!':	/* Early match */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE, "return (2) when '!' is matched\n");
#endif
			return 2;
		default:
			if (*data != *pattern) {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE, "return (0) when *data(%c) != *pattern(%c)\n", *data, *pattern);
#endif
				return 0;
			}
			break;
		}
		++data;
		++pattern;
	}
	if (*data)			/* data longer than pattern, no match */ {
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "return (0) when data longer than pattern\n");
#endif
		return 0;
	}

	/*
	 * match so far, but ran off the end of data.
	 * Depending on what is next, determine match or not.
	 */
	if (*pattern == '\0' || *pattern == '/') {	/* exact match */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "at end, return (%d) in 'exact match'\n", (mode==E_MATCHMORE) ? 0 : 1);
#endif
		return (mode == E_MATCHMORE) ? 0 : 1;	/* this is a failure for E_MATCHMORE */
	} else if (*pattern == '!')	{		/* early match */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "at end, return (2) when '!' is matched\n");
#endif
		return 2;
	} else {						/* partial match */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "at end, return (%d) which deps on E_MATCH\n", (mode == E_MATCH) ? 0 : 1);
#endif
		return (mode == E_MATCH) ? 0 : 1;	/* this is a failure for E_MATCH */
	}
}

/*
 * Wrapper around _extension_match_core() to do performance measurement
 * using the profiling code.
 */
static int extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	int i;
	static int prof_id = -2;	/* marker for 'unallocated' id */
	if (prof_id == -2) {
		prof_id = ast_add_profile("ext_match", 0);
	}
	ast_mark(prof_id, 1);
	i = _extension_match_core(ast_strlen_zero(pattern) ? "" : pattern, ast_strlen_zero(data) ? "" : data, mode);
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

/* This structure must remain in sync with ast_context for proper hashtab matching */
struct fake_context /* this struct is purely for matching in the hashtab */
{
	ast_rwlock_t lock;
	struct ast_exten *root;
	struct ast_hashtab *root_table;
	struct match_char *pattern_tree;
	struct ast_context *next;
	struct ast_includes includes;
	struct ast_ignorepats ignorepats;
	struct ast_sws alts;
	const char *registrar;
	int refcount;
	int autohints;
	ast_mutex_t macrolock;
	char name[256];
};

struct ast_context *ast_context_find(const char *name)
{
	struct ast_context *tmp;
	struct fake_context item;

	if (!name) {
		return NULL;
	}
	ast_rdlock_contexts();
	if (contexts_table) {
		ast_copy_string(item.name, name, sizeof(item.name));
		tmp = ast_hashtab_lookup(contexts_table, &item);
	} else {
		tmp = NULL;
		while ((tmp = ast_walk_contexts(tmp))) {
			if (!strcasecmp(name, tmp->name)) {
				break;
			}
		}
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

	if (ast_strlen_zero(callerid)) {
		return ast_strlen_zero(cidpattern) ? 1 : 0;
	}

	return ast_extension_match(cidpattern, callerid);
}

struct ast_exten *pbx_find_extension(struct ast_channel *chan,
	struct ast_context *bypass, struct pbx_find_info *q,
	const char *context, const char *exten, int priority,
	const char *label, const char *callerid, enum ext_match_t action)
{
	int x, res;
	struct ast_context *tmp = NULL;
	struct ast_exten *e = NULL, *eroot = NULL;
	struct ast_exten pattern = {NULL, };
	struct scoreboard score = {0, };
	struct ast_str *tmpdata = NULL;
	int idx;

	pattern.label = label;
	pattern.priority = priority;
#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE, "Looking for cont/ext/prio/label/action = %s/%s/%d/%s/%d\n", context, exten, priority, label, (int) action);
#endif

	/* Initialize status if appropriate */
	if (q->stacklen == 0) {
		q->status = STATUS_NO_CONTEXT;
		q->swo = NULL;
		q->data = NULL;
		q->foundcontext = NULL;
	} else if (q->stacklen >= AST_PBX_MAX_STACK) {
		ast_log(LOG_WARNING, "Maximum PBX stack exceeded\n");
		return NULL;
	}

	/* Check first to see if we've already been checked */
	for (x = 0; x < q->stacklen; x++) {
		if (!strcasecmp(q->incstack[x], context))
			return NULL;
	}

	if (bypass) { /* bypass means we only look there */
		tmp = bypass;
	} else {      /* look in contexts */
		tmp = find_context(context);
		if (!tmp) {
			return NULL;
		}
	}

	if (q->status < STATUS_NO_EXTENSION)
		q->status = STATUS_NO_EXTENSION;

	/* Do a search for matching extension */

	eroot = NULL;
	score.total_specificity = 0;
	score.exten = 0;
	score.total_length = 0;
	if (!tmp->pattern_tree && tmp->root_table) {
		create_match_char_tree(tmp);
#ifdef NEED_DEBUG
		ast_debug(1, "Tree Created in context %s:\n", context);
		log_match_char_tree(tmp->pattern_tree," ");
#endif
	}
#ifdef NEED_DEBUG
	ast_log(LOG_NOTICE, "The Trie we are searching in:\n");
	log_match_char_tree(tmp->pattern_tree, "::  ");
#endif

	do {
		if (!ast_strlen_zero(overrideswitch)) {
			char *osw = ast_strdupa(overrideswitch), *name;
			struct ast_switch *asw;
			ast_switch_f *aswf = NULL;
			char *datap;
			int eval = 0;

			name = strsep(&osw, "/");
			asw = pbx_findswitch(name);

			if (!asw) {
				ast_log(LOG_WARNING, "No such switch '%s'\n", name);
				break;
			}

			if (osw && strchr(osw, '$')) {
				eval = 1;
			}

			if (eval && !(tmpdata = ast_str_thread_get(&switch_data, 512))) {
				ast_log(LOG_WARNING, "Can't evaluate overrideswitch?!\n");
				break;
			} else if (eval) {
				/* Substitute variables now */
				pbx_substitute_variables_helper(chan, osw, ast_str_buffer(tmpdata), ast_str_size(tmpdata));
				datap = ast_str_buffer(tmpdata);
			} else {
				datap = osw;
			}

			/* equivalent of extension_match_core() at the switch level */
			if (action == E_CANMATCH)
				aswf = asw->canmatch;
			else if (action == E_MATCHMORE)
				aswf = asw->matchmore;
			else /* action == E_MATCH */
				aswf = asw->exists;
			if (!aswf) {
				res = 0;
			} else {
				if (chan) {
					ast_autoservice_start(chan);
				}
				res = aswf(chan, context, exten, priority, callerid, datap);
				if (chan) {
					ast_autoservice_stop(chan);
				}
			}
			if (res) {	/* Got a match */
				q->swo = asw;
				q->data = datap;
				q->foundcontext = context;
				/* XXX keep status = STATUS_NO_CONTEXT ? */
				return NULL;
			}
		}
	} while (0);

	if (extenpatternmatchnew) {
		new_find_extension(exten, &score, tmp->pattern_tree, 0, 0, callerid, label, action);
		eroot = score.exten;

		if (score.last_char == '!' && action == E_MATCHMORE) {
			/* We match an extension ending in '!'.
			 * The decision in this case is final and is NULL (no match).
			 */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"Returning MATCHMORE NULL with exclamation point.\n");
#endif
			return NULL;
		}

		if (!eroot && (action == E_CANMATCH || action == E_MATCHMORE) && score.canmatch_exten) {
			q->status = STATUS_SUCCESS;
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"Returning CANMATCH exten %s\n", score.canmatch_exten->exten);
#endif
			return score.canmatch_exten;
		}

		if ((action == E_MATCHMORE || action == E_CANMATCH)  && eroot) {
			if (score.node) {
				struct ast_exten *z = trie_find_next_match(score.node);
				if (z) {
#ifdef NEED_DEBUG_HERE
					ast_log(LOG_NOTICE,"Returning CANMATCH/MATCHMORE next_match exten %s\n", z->exten);
#endif
				} else {
					if (score.canmatch_exten) {
#ifdef NEED_DEBUG_HERE
						ast_log(LOG_NOTICE,"Returning CANMATCH/MATCHMORE canmatchmatch exten %s(%p)\n", score.canmatch_exten->exten, score.canmatch_exten);
#endif
						return score.canmatch_exten;
					} else {
#ifdef NEED_DEBUG_HERE
						ast_log(LOG_NOTICE,"Returning CANMATCH/MATCHMORE next_match exten NULL\n");
#endif
					}
				}
				return z;
			}
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE, "Returning CANMATCH/MATCHMORE NULL (no next_match)\n");
#endif
			return NULL;  /* according to the code, complete matches are null matches in MATCHMORE mode */
		}

		if (eroot) {
			/* found entry, now look for the right priority */
			if (q->status < STATUS_NO_PRIORITY)
				q->status = STATUS_NO_PRIORITY;
			e = NULL;
			if (action == E_FINDLABEL && label ) {
				if (q->status < STATUS_NO_LABEL)
					q->status = STATUS_NO_LABEL;
				e = ast_hashtab_lookup(eroot->peer_label_table, &pattern);
			} else {
				e = ast_hashtab_lookup(eroot->peer_table, &pattern);
			}
			if (e) {	/* found a valid match */
				q->status = STATUS_SUCCESS;
				q->foundcontext = context;
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"Returning complete match of exten %s\n", e->exten);
#endif
				return e;
			}
		}
	} else {   /* the old/current default exten pattern match algorithm */

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
			if (action == E_FINDLABEL && label ) {
				if (q->status < STATUS_NO_LABEL)
					q->status = STATUS_NO_LABEL;
				e = ast_hashtab_lookup(eroot->peer_label_table, &pattern);
			} else {
				e = ast_hashtab_lookup(eroot->peer_table, &pattern);
			}
			if (e) {	/* found a valid match */
				q->status = STATUS_SUCCESS;
				q->foundcontext = context;
				return e;
			}
		}
	}

	/* Check alternative switches */
	for (idx = 0; idx < ast_context_switches_count(tmp); idx++) {
		const struct ast_sw *sw = ast_context_switches_get(tmp, idx);
		struct ast_switch *asw = pbx_findswitch(ast_get_switch_name(sw));
		ast_switch_f *aswf = NULL;
		const char *datap;

		if (!asw) {
			ast_log(LOG_WARNING, "No such switch '%s'\n", ast_get_switch_name(sw));
			continue;
		}

		/* Substitute variables now */
		if (ast_get_switch_eval(sw)) {
			if (!(tmpdata = ast_str_thread_get(&switch_data, 512))) {
				ast_log(LOG_WARNING, "Can't evaluate switch?!\n");
				continue;
			}
			pbx_substitute_variables_helper(chan, ast_get_switch_data(sw),
				ast_str_buffer(tmpdata), ast_str_size(tmpdata));
			datap = ast_str_buffer(tmpdata);
		} else {
			datap = ast_get_switch_data(sw);
		}

		/* equivalent of extension_match_core() at the switch level */
		if (action == E_CANMATCH)
			aswf = asw->canmatch;
		else if (action == E_MATCHMORE)
			aswf = asw->matchmore;
		else /* action == E_MATCH */
			aswf = asw->exists;
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
	for (idx = 0; idx < ast_context_includes_count(tmp); idx++) {
		const struct ast_include *i = ast_context_includes_get(tmp, idx);

		if (include_valid(i)) {
			if ((e = pbx_find_extension(chan, bypass, q, include_rname(i), exten, priority, label, callerid, action))) {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"Returning recursive match of %s\n", e->exten);
#endif
				return e;
			}
			if (q->swo)
				return NULL;
		}
	}
	return NULL;
}

static void exception_store_free(void *data)
{
	struct pbx_exception *exception = data;
	ast_string_field_free_memory(exception);
	ast_free(exception);
}

static const struct ast_datastore_info exception_store_info = {
	.type = "EXCEPTION",
	.destroy = exception_store_free,
};

/*!
 * \internal
 * \brief Set the PBX to execute the exception extension.
 *
 * \param chan Channel to raise the exception on.
 * \param reason Reason exception is raised.
 * \param priority Dialplan priority to set.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int raise_exception(struct ast_channel *chan, const char *reason, int priority)
{
	struct ast_datastore *ds = ast_channel_datastore_find(chan, &exception_store_info, NULL);
	struct pbx_exception *exception = NULL;

	if (!ds) {
		ds = ast_datastore_alloc(&exception_store_info, NULL);
		if (!ds)
			return -1;
		if (!(exception = ast_calloc_with_stringfields(1, struct pbx_exception, 128))) {
			ast_datastore_free(ds);
			return -1;
		}
		ds->data = exception;
		ast_channel_datastore_add(chan, ds);
	} else
		exception = ds->data;

	ast_string_field_set(exception, reason, reason);
	ast_string_field_set(exception, context, ast_channel_context(chan));
	ast_string_field_set(exception, exten, ast_channel_exten(chan));
	exception->priority = ast_channel_priority(chan);
	set_ext_pri(chan, "e", priority);
	return 0;
}

static int acf_exception_read(struct ast_channel *chan, const char *name, char *data, char *buf, size_t buflen)
{
	struct ast_datastore *ds = ast_channel_datastore_find(chan, &exception_store_info, NULL);
	struct pbx_exception *exception = NULL;
	if (!ds || !ds->data)
		return -1;
	exception = ds->data;
	if (!strcasecmp(data, "REASON"))
		ast_copy_string(buf, exception->reason, buflen);
	else if (!strcasecmp(data, "CONTEXT"))
		ast_copy_string(buf, exception->context, buflen);
	else if (!strncasecmp(data, "EXTEN", 5))
		ast_copy_string(buf, exception->exten, buflen);
	else if (!strcasecmp(data, "PRIORITY"))
		snprintf(buf, buflen, "%d", exception->priority);
	else
		return -1;
	return 0;
}

static struct ast_custom_function exception_function = {
	.name = "EXCEPTION",
	.read = acf_exception_read,
};

/*!
 * \brief The return value depends on the action:
 *
 * E_MATCH, E_CANMATCH, E_MATCHMORE require a real match,
 *	and return 0 on failure, -1 on match;
 * E_FINDLABEL maps the label to a priority, and returns
 *	the priority on success, ... XXX
 * E_SPAWN, spawn an application,
 *
 * \retval 0 on success.
 * \retval  -1 on failure.
 *
 * \note The channel is auto-serviced in this function, because doing an extension
 * match may block for a long time.  For example, if the lookup has to use a network
 * dialplan switch, such as DUNDi or IAX2, it may take a while.  However, the channel
 * auto-service code will queue up any important signalling frames to be processed
 * after this is done.
 */
static int pbx_extension_helper(struct ast_channel *c, struct ast_context *con,
  const char *context, const char *exten, int priority,
  const char *label, const char *callerid, enum ext_match_t action, int *found, int combined_find_spawn)
{
	struct ast_exten *e;
	struct ast_app *app;
	char *substitute = NULL;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	char passdata[EXT_DATA_SIZE];
	int matching_action = (action == E_MATCH || action == E_CANMATCH || action == E_MATCHMORE);

	ast_rdlock_contexts();

	if (!context) {
		context = con->name;
	}

	if (found)
		*found = 0;

	e = pbx_find_extension(c, con, &q, context, exten, priority, label, callerid, action);
	if (e) {
		if (found)
			*found = 1;
		if (matching_action) {
			ast_unlock_contexts();
			return -1;	/* success, we found it */
		} else if (action == E_FINDLABEL) { /* map the label to a priority */
			int res = e->priority;

			ast_unlock_contexts();

			/* the priority we were looking for */
			return res;
		} else {	/* spawn */
			if (!e->cached_app)
				e->cached_app = pbx_findapp(e->app);
			app = e->cached_app;
			if (ast_strlen_zero(e->data)) {
				*passdata = '\0';
			} else {
				const char *tmp;
				if ((!(tmp = strchr(e->data, '$'))) || (!strstr(tmp, "${") && !strstr(tmp, "$["))) {
					/* no variables to substitute, copy on through */
					ast_copy_string(passdata, e->data, sizeof(passdata));
				} else {
					/* save e->data on stack for later processing after lock released */
					substitute = ast_strdupa(e->data);
				}
			}
			ast_unlock_contexts();
			if (!app) {
				ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
				return -1;
			}
			if (ast_channel_context(c) != context)
				ast_channel_context_set(c, context);
			if (ast_channel_exten(c) != exten)
				ast_channel_exten_set(c, exten);
			ast_channel_priority_set(c, priority);
			if (substitute) {
				pbx_substitute_variables_helper(c, substitute, passdata, sizeof(passdata)-1);
			}
			ast_debug(1, "Launching '%s'\n", app_name(app));
			if (VERBOSITY_ATLEAST(3)) {
				ast_verb(3, "Executing [%s@%s:%d] " COLORIZE_FMT "(\"" COLORIZE_FMT "\", \"" COLORIZE_FMT "\") %s\n",
					exten, context, priority,
					COLORIZE(COLOR_BRCYAN, 0, app_name(app)),
					COLORIZE(COLOR_BRMAGENTA, 0, ast_channel_name(c)),
					COLORIZE(COLOR_BRMAGENTA, 0, passdata),
					"in new stack");
			}
			return pbx_exec(c, app, passdata);	/* 0 on success, -1 on failure */
		}
	} else if (q.swo) {	/* not found here, but in another switch */
		if (found)
			*found = 1;
		ast_unlock_contexts();
		if (matching_action) {
			return -1;
		} else {
			if (!q.swo->exec) {
				ast_log(LOG_WARNING, "No execution engine for switch %s\n", q.swo->name);
				return -1;
			}
			return q.swo->exec(c, q.foundcontext ? q.foundcontext : context, exten, priority, callerid, q.data);
		}
	} else {	/* not found anywhere, see what happened */
		ast_unlock_contexts();
		/* Using S_OR here because Solaris doesn't like NULL being passed to ast_log */
		switch (q.status) {
		case STATUS_NO_CONTEXT:
			if (!matching_action && !combined_find_spawn)
				ast_log(LOG_NOTICE, "Cannot find extension context '%s'\n", S_OR(context, ""));
			break;
		case STATUS_NO_EXTENSION:
			if (!matching_action && !combined_find_spawn)
				ast_log(LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, S_OR(context, ""));
			break;
		case STATUS_NO_PRIORITY:
			if (!matching_action && !combined_find_spawn)
				ast_log(LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, S_OR(context, ""));
			break;
		case STATUS_NO_LABEL:
			if (context && !combined_find_spawn)
				ast_log(LOG_NOTICE, "No such label '%s' in extension '%s' in context '%s'\n", label, exten, S_OR(context, ""));
			break;
		default:
			ast_debug(1, "Shouldn't happen!\n");
		}

		return (matching_action) ? 0 : -1;
	}
}

/*! \brief Find hint for given extension in context */
static struct ast_exten *ast_hint_extension_nolock(struct ast_channel *c, const char *context, const char *exten)
{
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is set in pbx_find_context */
	return pbx_find_extension(c, NULL, &q, context, exten, PRIORITY_HINT, NULL, "", E_MATCH);
}

static struct ast_exten *ast_hint_extension(struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	ast_rdlock_contexts();
	e = ast_hint_extension_nolock(c, context, exten);
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

/*!
 * \internal
 * \brief Parse out the presence portion of the hint string
 */
static char *parse_hint_presence(struct ast_str *hint_args)
{
	char *copy = ast_strdupa(ast_str_buffer(hint_args));
	char *tmp = "";

	if ((tmp = strrchr(copy, ','))) {
		*tmp = '\0';
		tmp++;
	} else {
		return NULL;
	}
	ast_str_set(&hint_args, 0, "%s", tmp);
	return ast_str_buffer(hint_args);
}

/*!
 * \internal
 * \brief Parse out the device portion of the hint string
 */
static char *parse_hint_device(struct ast_str *hint_args)
{
	char *copy = ast_strdupa(ast_str_buffer(hint_args));
	char *tmp;

	if ((tmp = strrchr(copy, ','))) {
		*tmp = '\0';
	}

	ast_str_set(&hint_args, 0, "%s", copy);
	return ast_str_buffer(hint_args);
}

static void device_state_info_dt(void *obj)
{
	struct ast_device_state_info *info = obj;

	ao2_cleanup(info->causing_channel);
}

static struct ao2_container *alloc_device_state_info(void)
{
	return ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
}

static int ast_extension_state3(struct ast_str *hint_app, struct ao2_container *device_state_info)
{
	char *cur;
	char *rest;
	struct ast_devstate_aggregate agg;

	/* One or more devices separated with a & character */
	rest = parse_hint_device(hint_app);

	ast_devstate_aggregate_init(&agg);
	while ((cur = strsep(&rest, "&"))) {
		enum ast_device_state state = ast_device_state(cur);

		ast_devstate_aggregate_add(&agg, state);
		if (device_state_info) {
			struct ast_device_state_info *obj;

			obj = ao2_alloc_options(sizeof(*obj) + strlen(cur), device_state_info_dt, AO2_ALLOC_OPT_LOCK_NOLOCK);
			/* if failed we cannot add this device */
			if (obj) {
				obj->device_state = state;
				strcpy(obj->device_name, cur);
				ao2_link(device_state_info, obj);
				ao2_ref(obj, -1);
			}
		}
	}

	return ast_devstate_to_extenstate(ast_devstate_aggregate_result(&agg));
}

/*! \brief Check state of extension by using hints */
static int ast_extension_state2(struct ast_exten *e, struct ao2_container *device_state_info)
{
	struct ast_str *hint_app = ast_str_thread_get(&extensionstate_buf, 32);

	if (!e || !hint_app) {
		return -1;
	}

	ast_str_set(&hint_app, 0, "%s", ast_get_extension_app(e));
	return ast_extension_state3(hint_app, device_state_info);
}

/*! \brief Return extension_state as string */
const char *ast_extension_state2str(int extension_state)
{
	int i;

	for (i = 0; (i < ARRAY_LEN(extension_states)); i++) {
		if (extension_states[i].extension_state == extension_state)
			return extension_states[i].text;
	}
	return "Unknown";
}

/*!
 * \internal
 * \brief Check extension state for an extension by using hint
 */
static int internal_extension_state_extended(struct ast_channel *c, const char *context, const char *exten,
	struct ao2_container *device_state_info)
{
	struct ast_exten *e;

	if (!(e = ast_hint_extension(c, context, exten))) {  /* Do we have a hint for this extension ? */
		return -1;                   /* No hint, return -1 */
	}

	if (e->exten[0] == '_') {
		/* Create this hint on-the-fly, we explicitly lock hints here to ensure the
		 * same locking order as if this were done through configuration file - that is
		 * hints is locked first and then (if needed) contexts is locked
		 */
		ao2_lock(hints);
		ast_add_extension(e->parent->name, 0, exten, e->priority, e->label,
			e->matchcid ? e->cidmatch : NULL, e->app, ast_strdup(e->data), ast_free_ptr,
			e->registrar);
		ao2_unlock(hints);
		if (!(e = ast_hint_extension(c, context, exten))) {
			/* Improbable, but not impossible */
			return -1;
		}
	}

	return ast_extension_state2(e, device_state_info);  /* Check all devices in the hint */
}

/*! \brief Check extension state for an extension by using hint */
int ast_extension_state(struct ast_channel *c, const char *context, const char *exten)
{
	return internal_extension_state_extended(c, context, exten, NULL);
}

/*! \brief Check extended extension state for an extension by using hint */
int ast_extension_state_extended(struct ast_channel *c, const char *context, const char *exten,
	struct ao2_container **device_state_info)
{
	struct ao2_container *container = NULL;
	int ret;

	if (device_state_info) {
		container = alloc_device_state_info();
	}

	ret = internal_extension_state_extended(c, context, exten, container);
	if (ret < 0 && container) {
		ao2_ref(container, -1);
		container = NULL;
	}

	if (device_state_info) {
		get_device_state_causing_channels(container);
		*device_state_info = container;
	}

	return ret;
}

static int extension_presence_state_helper(struct ast_exten *e, char **subtype, char **message)
{
	struct ast_str *hint_app = ast_str_thread_get(&extensionstate_buf, 32);
	char *presence_provider;
	const char *app;

	if (!e || !hint_app) {
		return -1;
	}

	app = ast_get_extension_app(e);
	if (ast_strlen_zero(app)) {
		return -1;
	}

	ast_str_set(&hint_app, 0, "%s", app);
	presence_provider = parse_hint_presence(hint_app);

	if (ast_strlen_zero(presence_provider)) {
		/* No presence string in the hint */
		return 0;
	}

	return ast_presence_state(presence_provider, subtype, message);
}

int ast_hint_presence_state(struct ast_channel *c, const char *context, const char *exten, char **subtype, char **message)
{
	struct ast_exten *e;

	if (!(e = ast_hint_extension(c, context, exten))) {  /* Do we have a hint for this extension ? */
		return -1;                   /* No hint, return -1 */
	}

	if (e->exten[0] == '_') {
		/* Create this hint on-the-fly */
		ao2_lock(hints);
		ast_add_extension(e->parent->name, 0, exten, e->priority, e->label,
			e->matchcid ? e->cidmatch : NULL, e->app, ast_strdup(e->data), ast_free_ptr,
			e->registrar);
		ao2_unlock(hints);
		if (!(e = ast_hint_extension(c, context, exten))) {
			/* Improbable, but not impossible */
			return -1;
		}
	}

	return extension_presence_state_helper(e, subtype, message);
}

static int execute_state_callback(ast_state_cb_type cb,
	const char *context,
	const char *exten,
	void *data,
	enum ast_state_cb_update_reason reason,
	struct ast_hint *hint,
	struct ao2_container *device_state_info)
{
	int res = 0;
	struct ast_state_cb_info info = { 0, };

	info.reason = reason;

	/* Copy over current hint data */
	if (hint) {
		ao2_lock(hint);
		info.exten_state = hint->laststate;
		info.device_state_info = device_state_info;
		info.presence_state = hint->last_presence_state;
		if (!(ast_strlen_zero(hint->last_presence_subtype))) {
			info.presence_subtype = ast_strdupa(hint->last_presence_subtype);
		} else {
			info.presence_subtype = "";
		}
		if (!(ast_strlen_zero(hint->last_presence_message))) {
			info.presence_message = ast_strdupa(hint->last_presence_message);
		} else {
			info.presence_message = "";
		}
		ao2_unlock(hint);
	} else {
		info.exten_state = AST_EXTENSION_REMOVED;
	}

	res = cb(context, exten, &info, data);

	return res;
}

/*!
 * /internal
 * /brief Identify a channel for every device which is supposedly responsible for the device state.
 *
 * Especially when the device is ringing, the oldest ringing channel is chosen.
 * For all other cases the first encountered channel in the specific state is chosen.
 */
static void get_device_state_causing_channels(struct ao2_container *c)
{
	struct ao2_iterator iter;
	struct ast_device_state_info *info;
	struct ast_channel *chan;

	if (!c || !ao2_container_count(c)) {
		return;
	}
	iter = ao2_iterator_init(c, 0);
	for (; (info = ao2_iterator_next(&iter)); ao2_ref(info, -1)) {
		enum ast_channel_state search_state = 0; /* prevent false uninit warning */
		char match[AST_CHANNEL_NAME];
		struct ast_channel_iterator *chan_iter;
		struct timeval chantime = {0, }; /* prevent false uninit warning */

		switch (info->device_state) {
		case AST_DEVICE_RINGING:
		case AST_DEVICE_RINGINUSE:
			/* find ringing channel */
			search_state = AST_STATE_RINGING;
			break;
		case AST_DEVICE_BUSY:
			/* find busy channel */
			search_state = AST_STATE_BUSY;
			break;
		case AST_DEVICE_ONHOLD:
		case AST_DEVICE_INUSE:
			/* find up channel */
			search_state = AST_STATE_UP;
			break;
		case AST_DEVICE_UNKNOWN:
		case AST_DEVICE_NOT_INUSE:
		case AST_DEVICE_INVALID:
		case AST_DEVICE_UNAVAILABLE:
		case AST_DEVICE_TOTAL /* not a state */:
			/* no channels are of interest */
			continue;
		}

		/* iterate over all channels of the device */
	        snprintf(match, sizeof(match), "%s-", info->device_name);
		chan_iter = ast_channel_iterator_by_name_new(match, strlen(match));
		for (; (chan = ast_channel_iterator_next(chan_iter)); ast_channel_unref(chan)) {
			ast_channel_lock(chan);
			/* this channel's state doesn't match */
			if (search_state != ast_channel_state(chan)) {
				ast_channel_unlock(chan);
				continue;
			}
			/* any non-ringing channel will fit */
			if (search_state != AST_STATE_RINGING) {
				ast_channel_unlock(chan);
				info->causing_channel = chan; /* is kept ref'd! */
				break;
			}
			/* but we need the oldest ringing channel of the device to match with undirected pickup */
			if (!info->causing_channel) {
				chantime = ast_channel_creationtime(chan);
				ast_channel_ref(chan); /* must ref it! */
				info->causing_channel = chan;
			} else if (ast_tvcmp(ast_channel_creationtime(chan), chantime) < 0) {
				chantime = ast_channel_creationtime(chan);
				ast_channel_unref(info->causing_channel);
				ast_channel_ref(chan); /* must ref it! */
				info->causing_channel = chan;
			}
			ast_channel_unlock(chan);
		}
		ast_channel_iterator_destroy(chan_iter);
	}
	ao2_iterator_destroy(&iter);
}

static void device_state_notify_callbacks(struct ast_hint *hint, struct ast_str **hint_app)
{
	struct ao2_iterator cb_iter;
	struct ast_state_cb *state_cb;
	int state;
	int same_state;
	struct ao2_container *device_state_info;
	int first_extended_cb_call = 1;
	char context_name[AST_MAX_CONTEXT];
	char exten_name[AST_MAX_EXTENSION];

	ao2_lock(hint);
	if (!hint->exten) {
		/* The extension has already been destroyed */
		ao2_unlock(hint);
		return;
	}

	/*
	 * Save off strings in case the hint extension gets destroyed
	 * while we are notifying the watchers.
	 */
	ast_copy_string(context_name,
			ast_get_context_name(ast_get_extension_context(hint->exten)),
			sizeof(context_name));
	ast_copy_string(exten_name, ast_get_extension_name(hint->exten),
			sizeof(exten_name));
	ast_str_set(hint_app, 0, "%s", ast_get_extension_app(hint->exten));
	ao2_unlock(hint);

	/*
	 * Get device state for this hint.
	 *
	 * NOTE: We cannot hold any locks while determining the hint
	 * device state or notifying the watchers without causing a
	 * deadlock.  (conlock, hints, and hint)
	 */

	/* Make a container so state3 can fill it if we wish.
	 * If that failed we simply do not provide the extended state info.
	 */
	device_state_info = alloc_device_state_info();

	state = ast_extension_state3(*hint_app, device_state_info);
	same_state = state == hint->laststate;
	if (same_state && (~state & AST_EXTENSION_RINGING)) {
		ao2_cleanup(device_state_info);
		return;
	}

	/* Device state changed since last check - notify the watchers. */
	hint->laststate = state;	/* record we saw the change */

	/* For general callbacks */
	if (!same_state) {
		cb_iter = ao2_iterator_init(statecbs, 0);
		for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			execute_state_callback(state_cb->change_cb,
				context_name,
				exten_name,
				state_cb->data,
				AST_HINT_UPDATE_DEVICE,
				hint,
				NULL);
		}
		ao2_iterator_destroy(&cb_iter);
	}

	/* For extension callbacks */
	/* extended callbacks are called when the state changed or when AST_STATE_RINGING is
	 * included. Normal callbacks are only called when the state changed.
	 */
	cb_iter = ao2_iterator_init(hint->callbacks, 0);
	for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
		if (state_cb->extended && first_extended_cb_call) {
			/* Fill detailed device_state_info now that we know it is used by extd. callback */
			first_extended_cb_call = 0;
			get_device_state_causing_channels(device_state_info);
		}
		if (state_cb->extended || !same_state) {
			execute_state_callback(state_cb->change_cb,
				context_name,
				exten_name,
				state_cb->data,
				AST_HINT_UPDATE_DEVICE,
				hint,
				state_cb->extended ? device_state_info : NULL);
		}
	}
	ao2_iterator_destroy(&cb_iter);

	ao2_cleanup(device_state_info);
}

static void presence_state_notify_callbacks(struct ast_hint *hint, struct ast_str **hint_app,
					    struct ast_presence_state_message *presence_state)
{
	struct ao2_iterator cb_iter;
	struct ast_state_cb *state_cb;
	char context_name[AST_MAX_CONTEXT];
	char exten_name[AST_MAX_EXTENSION];

	ao2_lock(hint);
	if (!hint->exten) {
		/* The extension has already been destroyed */
		ao2_unlock(hint);
		return;
	}

	/*
	 * Save off strings in case the hint extension gets destroyed
	 * while we are notifying the watchers.
	 */
	ast_copy_string(context_name,
			ast_get_context_name(ast_get_extension_context(hint->exten)),
			sizeof(context_name));
	ast_copy_string(exten_name, ast_get_extension_name(hint->exten),
			sizeof(exten_name));
	ast_str_set(hint_app, 0, "%s", ast_get_extension_app(hint->exten));
	ao2_unlock(hint);

	/* Check to see if update is necessary */
	if ((hint->last_presence_state == presence_state->state) &&
	    ((hint->last_presence_subtype && presence_state->subtype &&
	      !strcmp(hint->last_presence_subtype, presence_state->subtype)) ||
	     (!hint->last_presence_subtype && !presence_state->subtype)) &&
	    ((hint->last_presence_message && presence_state->message &&
	      !strcmp(hint->last_presence_message, presence_state->message)) ||
	     (!hint->last_presence_message && !presence_state->message))) {
		/* this update is the same as the last, do nothing */
		return;
	}

	/* update new values */
	ast_free(hint->last_presence_subtype);
	ast_free(hint->last_presence_message);
	hint->last_presence_state = presence_state->state;
	hint->last_presence_subtype = presence_state->subtype ? ast_strdup(presence_state->subtype) : NULL;
	hint->last_presence_message = presence_state->message ? ast_strdup(presence_state->message) : NULL;

	/* For general callbacks */
	cb_iter = ao2_iterator_init(statecbs, 0);
	for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
		execute_state_callback(state_cb->change_cb,
			context_name,
			exten_name,
			state_cb->data,
			AST_HINT_UPDATE_PRESENCE,
			hint,
			NULL);
	}
	ao2_iterator_destroy(&cb_iter);

	/* For extension callbacks */
	cb_iter = ao2_iterator_init(hint->callbacks, 0);
	for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_cleanup(state_cb)) {
		execute_state_callback(state_cb->change_cb,
			context_name,
			exten_name,
			state_cb->data,
			AST_HINT_UPDATE_PRESENCE,
			hint,
			NULL);
	}
	ao2_iterator_destroy(&cb_iter);
}

static int handle_hint_change_message_type(struct stasis_message *msg, enum ast_state_cb_update_reason reason)
{
	struct ast_hint *hint;
	struct ast_str *hint_app;

	if (hint_change_message_type() != stasis_message_type(msg)) {
		return 0;
	}

	if (!(hint_app = ast_str_create(1024))) {
		return -1;
	}

	hint = stasis_message_data(msg);

	switch (reason) {
	case AST_HINT_UPDATE_DEVICE:
		device_state_notify_callbacks(hint, &hint_app);
		break;
	case AST_HINT_UPDATE_PRESENCE:
		{
			char *presence_subtype = NULL;
			char *presence_message = NULL;
			int state;

			state = extension_presence_state_helper(
				hint->exten, &presence_subtype, &presence_message);
			{
				struct ast_presence_state_message presence_state = {
					.state = state > 0 ? state : AST_PRESENCE_INVALID,
					.subtype = presence_subtype,
					.message = presence_message
				};

				presence_state_notify_callbacks(hint, &hint_app, &presence_state);
			}

			ast_free(presence_subtype);
			ast_free(presence_message);
		}
		break;
	}

	ast_free(hint_app);
	return 1;
}

static void device_state_cb(void *unused, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_device_state_message *dev_state;
	struct ast_str *hint_app;
	struct ast_hintdevice *device;
	struct ast_hintdevice *cmpdevice;
	struct ao2_iterator *dev_iter;
	struct ao2_iterator auto_iter;
	struct ast_autohint *autohint;
	char *virtual_device;
	char *type;
	char *device_name;

	if (handle_hint_change_message_type(msg, AST_HINT_UPDATE_DEVICE)) {
		return;
	}

	if (hint_remove_message_type() == stasis_message_type(msg)) {
		/* The extension has already been destroyed */
		struct ast_state_cb *state_cb;
		struct ao2_iterator cb_iter;
		struct ast_hint *hint = stasis_message_data(msg);

		ao2_lock(hint);
		hint->laststate = AST_EXTENSION_DEACTIVATED;
		ao2_unlock(hint);

		cb_iter = ao2_iterator_init(hint->callbacks, 0);
		for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			execute_state_callback(state_cb->change_cb,
			        hint->context_name,
			        hint->exten_name,
			        state_cb->data,
			        AST_HINT_UPDATE_DEVICE,
			        hint,
			        NULL);
		}
		ao2_iterator_destroy(&cb_iter);
		return;
	}

	if (ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	dev_state = stasis_message_data(msg);
	if (dev_state->eid) {
		/* ignore non-aggregate states */
		return;
	}

	if (ao2_container_count(hintdevices) == 0 && ao2_container_count(autohints) == 0) {
		/* There are no hints monitoring devices. */
		return;
	}

	hint_app = ast_str_create(1024);
	if (!hint_app) {
		return;
	}

	cmpdevice = ast_alloca(sizeof(*cmpdevice) + strlen(dev_state->device));
	strcpy(cmpdevice->hintdevice, dev_state->device);

	ast_mutex_lock(&context_merge_lock);/* Hold off ast_merge_contexts_and_delete */

	/* Initially we find all hints for the device and notify them */
	dev_iter = ao2_t_callback(hintdevices,
		OBJ_SEARCH_OBJECT | OBJ_MULTIPLE,
		hintdevice_cmp_multiple,
		cmpdevice,
		"find devices in container");
	if (dev_iter) {
		for (; (device = ao2_iterator_next(dev_iter)); ao2_t_ref(device, -1, "Next device")) {
			if (device->hint) {
				device_state_notify_callbacks(device->hint, &hint_app);
			}
		}
		ao2_iterator_destroy(dev_iter);
	}

	/* Second stage we look for any autohint contexts and if the device is not already in the hints
	 * we create it.
	 */
	type = ast_strdupa(dev_state->device);
	if (ast_strlen_zero(type)) {
		goto end;
	}

	/* Determine if this is a virtual/custom device or a real device */
	virtual_device = strchr(type, ':');
	device_name = strchr(type, '/');
	if (virtual_device && (!device_name || (virtual_device < device_name))) {
		device_name = virtual_device;
	}

	/* Invalid device state name - not a virtual/custom device and not a real device */
	if (ast_strlen_zero(device_name)) {
		goto end;
	}

	*device_name++ = '\0';

	auto_iter = ao2_iterator_init(autohints, 0);
	for (; (autohint = ao2_iterator_next(&auto_iter)); ao2_t_ref(autohint, -1, "Next autohint")) {
		if (ast_get_hint(NULL, 0, NULL, 0, NULL, autohint->context, device_name)) {
			continue;
		}

		/* The device has no hint in the context referenced by this autohint so create one */
		ast_add_extension(autohint->context, 0, device_name,
			PRIORITY_HINT, NULL, NULL, dev_state->device,
			ast_strdup(dev_state->device), ast_free_ptr, autohint->registrar);

		/* Since this hint was just created there are no watchers, so we don't need to notify anyone */
	}
	ao2_iterator_destroy(&auto_iter);

end:
	ast_mutex_unlock(&context_merge_lock);
	ast_free(hint_app);
	return;
}

/*!
 * \internal
 * \brief Destroy the given state callback object.
 *
 * \param doomed State callback to destroy.
 *
 * \return Nothing
 */
static void destroy_state_cb(void *doomed)
{
	struct ast_state_cb *state_cb = doomed;

	if (state_cb->destroy_cb) {
		state_cb->destroy_cb(state_cb->id, state_cb->data);
	}
}

/*!
 * \internal
 * \brief Add watcher for extension states with destructor
 */
static int extension_state_add_destroy(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data, int extended)
{
	struct ast_hint *hint;
	struct ast_state_cb *state_cb;
	struct ast_exten *e;
	int id;

	/* If there's no context and extension:  add callback to statecbs list */
	if (!context && !exten) {
		/* Prevent multiple adds from adding the same change_cb at the same time. */
		ao2_lock(statecbs);

		/* Remove any existing change_cb. */
		ao2_find(statecbs, change_cb, OBJ_UNLINK | OBJ_NODATA);

		/* Now insert the change_cb */
		if (!(state_cb = ao2_alloc(sizeof(*state_cb), destroy_state_cb))) {
			ao2_unlock(statecbs);
			return -1;
		}
		state_cb->id = 0;
		state_cb->change_cb = change_cb;
		state_cb->destroy_cb = destroy_cb;
		state_cb->data = data;
		state_cb->extended = extended;
		ao2_link(statecbs, state_cb);

		ao2_ref(state_cb, -1);
		ao2_unlock(statecbs);
		return 0;
	}

	if (!context || !exten)
		return -1;

	/* This callback type is for only one hint, so get the hint */
	e = ast_hint_extension(NULL, context, exten);
	if (!e) {
		return -1;
	}

	/* If this is a pattern, dynamically create a new extension for this
	 * particular match.  Note that this will only happen once for each
	 * individual extension, because the pattern will no longer match first.
	 */
	if (e->exten[0] == '_') {
		ao2_lock(hints);
		ast_add_extension(e->parent->name, 0, exten, e->priority, e->label,
			e->matchcid ? e->cidmatch : NULL, e->app, ast_strdup(e->data), ast_free_ptr,
			e->registrar);
		ao2_unlock(hints);
		e = ast_hint_extension(NULL, context, exten);
		if (!e || e->exten[0] == '_') {
			return -1;
		}
	}

	/* Find the hint in the hints container */
	ao2_lock(hints);/* Locked to hold off ast_merge_contexts_and_delete */
	hint = ao2_find(hints, e, 0);
	if (!hint) {
		ao2_unlock(hints);
		return -1;
	}

	/* Now insert the callback in the callback list  */
	if (!(state_cb = ao2_alloc(sizeof(*state_cb), destroy_state_cb))) {
		ao2_ref(hint, -1);
		ao2_unlock(hints);
		return -1;
	}
	do {
		id = stateid++;		/* Unique ID for this callback */
		/* Do not allow id to ever be -1 or 0. */
	} while (id == -1 || id == 0);
	state_cb->id = id;
	state_cb->change_cb = change_cb;	/* Pointer to callback routine */
	state_cb->destroy_cb = destroy_cb;
	state_cb->data = data;		/* Data for the callback */
	state_cb->extended = extended;
	ao2_link(hint->callbacks, state_cb);

	ao2_ref(state_cb, -1);
	ao2_ref(hint, -1);
	ao2_unlock(hints);

	return id;
}

int ast_extension_state_add_destroy(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, destroy_cb, data, 0);
}

int ast_extension_state_add(const char *context, const char *exten,
	ast_state_cb_type change_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, NULL, data, 0);
}

int ast_extension_state_add_destroy_extended(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, destroy_cb, data, 1);
}

int ast_extension_state_add_extended(const char *context, const char *exten,
	ast_state_cb_type change_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, NULL, data, 1);
}

/*! \brief Find Hint by callback id */
static int find_hint_by_cb_id(void *obj, void *arg, int flags)
{
	struct ast_state_cb *state_cb;
	const struct ast_hint *hint = obj;
	int *id = arg;

	if ((state_cb = ao2_find(hint->callbacks, id, 0))) {
		ao2_ref(state_cb, -1);
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

int ast_extension_state_del(int id, ast_state_cb_type change_cb)
{
	struct ast_state_cb *p_cur;
	int ret = -1;

	if (!id) {	/* id == 0 is a callback without extension */
		if (!change_cb) {
			return ret;
		}
		p_cur = ao2_find(statecbs, change_cb, OBJ_UNLINK);
		if (p_cur) {
			ret = 0;
			ao2_ref(p_cur, -1);
		}
	} else { /* callback with extension, find the callback based on ID */
		struct ast_hint *hint;

		ao2_lock(hints);/* Locked to hold off ast_merge_contexts_and_delete */
		hint = ao2_callback(hints, 0, find_hint_by_cb_id, &id);
		if (hint) {
			p_cur = ao2_find(hint->callbacks, &id, OBJ_UNLINK);
			if (p_cur) {
				ret = 0;
				ao2_ref(p_cur, -1);
			}
			ao2_ref(hint, -1);
		}
		ao2_unlock(hints);
	}

	return ret;
}

static int hint_id_cmp(void *obj, void *arg, int flags)
{
	const struct ast_state_cb *cb = obj;
	int *id = arg;

	return (cb->id == *id) ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \internal
 * \brief Destroy the given hint object.
 *
 * \param obj Hint to destroy.
 *
 * \return Nothing
 */
static void destroy_hint(void *obj)
{
	struct ast_hint *hint = obj;
	int i;

	ao2_cleanup(hint->callbacks);

	for (i = 0; i < AST_VECTOR_SIZE(&hint->devices); i++) {
		char *device = AST_VECTOR_GET(&hint->devices, i);
		ast_free(device);
	}
	AST_VECTOR_FREE(&hint->devices);
	ast_free(hint->last_presence_subtype);
	ast_free(hint->last_presence_message);
}

/*! \brief Publish a hint removed event  */
static int publish_hint_remove(struct ast_hint *hint)
{
	struct stasis_message *message;

	if (!hint_remove_message_type()) {
		return -1;
	}

	if (!(message = stasis_message_create(hint_remove_message_type(), hint))) {
		ao2_ref(hint, -1);
		return -1;
	}

	stasis_publish(ast_device_state_topic_all(), message);

	ao2_ref(message, -1);

	return 0;
}

/*! \brief Remove hint from extension */
static int ast_remove_hint(struct ast_exten *e)
{
	/* Cleanup the Notifys if hint is removed */
	struct ast_hint *hint;

	if (!e) {
		return -1;
	}

	hint = ao2_find(hints, e, OBJ_UNLINK);
	if (!hint) {
		return -1;
	}

	remove_hintdevice(hint);

	/*
	 * The extension is being destroyed so we must save some
	 * information to notify that the extension is deactivated.
	 */
	ao2_lock(hint);
	ast_copy_string(hint->context_name,
		ast_get_context_name(ast_get_extension_context(hint->exten)),
		sizeof(hint->context_name));
	ast_copy_string(hint->exten_name, ast_get_extension_name(hint->exten),
		sizeof(hint->exten_name));
	hint->exten = NULL;
	ao2_unlock(hint);

	publish_hint_remove(hint);

	ao2_ref(hint, -1);

	return 0;
}

/*! \brief Add hint to hint list, check initial extension state */
static int ast_add_hint(struct ast_exten *e)
{
	struct ast_hint *hint_new;
	struct ast_hint *hint_found;
	char *message = NULL;
	char *subtype = NULL;
	int presence_state;

	if (!e) {
		return -1;
	}

	/*
	 * We must create the hint we wish to add before determining if
	 * it is already in the hints container to avoid possible
	 * deadlock when getting the current extension state.
	 */
	hint_new = ao2_alloc(sizeof(*hint_new), destroy_hint);
	if (!hint_new) {
		return -1;
	}
	AST_VECTOR_INIT(&hint_new->devices, 8);

	/* Initialize new hint. */
	hint_new->callbacks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, hint_id_cmp);
	if (!hint_new->callbacks) {
		ao2_ref(hint_new, -1);
		return -1;
	}
	hint_new->exten = e;
	if (strstr(e->app, "${") && e->exten[0] == '_') {
		/* The hint is dynamic and hasn't been evaluted yet */
		hint_new->laststate = AST_DEVICE_INVALID;
		hint_new->last_presence_state = AST_PRESENCE_INVALID;
	} else {
		hint_new->laststate = ast_extension_state2(e, NULL);
		if ((presence_state = extension_presence_state_helper(e, &subtype, &message)) > 0) {
			hint_new->last_presence_state = presence_state;
			hint_new->last_presence_subtype = subtype;
			hint_new->last_presence_message = message;
		}
	}

	/* Prevent multiple add hints from adding the same hint at the same time. */
	ao2_lock(hints);

	/* Search if hint exists, do nothing */
	hint_found = ao2_find(hints, e, 0);
	if (hint_found) {
		ao2_ref(hint_found, -1);
		ao2_unlock(hints);
		ao2_ref(hint_new, -1);
		ast_debug(2, "HINTS: Not re-adding existing hint %s: %s\n",
			ast_get_extension_name(e), ast_get_extension_app(e));
		return -1;
	}

	/* Add new hint to the hints container */
	ast_debug(2, "HINTS: Adding hint %s: %s\n",
		ast_get_extension_name(e), ast_get_extension_app(e));
	ao2_link(hints, hint_new);
	if (add_hintdevice(hint_new, ast_get_extension_app(e))) {
		ast_log(LOG_WARNING, "Could not add devices for hint: %s@%s.\n",
			ast_get_extension_name(e),
			ast_get_context_name(ast_get_extension_context(e)));
	}

	/* if not dynamic */
	if (!(strstr(e->app, "${") && e->exten[0] == '_')) {
		struct ast_state_cb *state_cb;
		struct ao2_iterator cb_iter;

		/* For general callbacks */
		cb_iter = ao2_iterator_init(statecbs, 0);
		for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			execute_state_callback(state_cb->change_cb,
				ast_get_context_name(ast_get_extension_context(e)),
				ast_get_extension_name(e),
				state_cb->data,
				AST_HINT_UPDATE_DEVICE,
				hint_new,
				NULL);
		}
		ao2_iterator_destroy(&cb_iter);
	}
	ao2_unlock(hints);
	ao2_ref(hint_new, -1);

	return 0;
}

/*! \brief Publish a hint changed event  */
static int publish_hint_change(struct ast_hint *hint, struct ast_exten *ne)
{
	struct stasis_message *message;

	if (!hint_change_message_type()) {
		return -1;
	}

	if (!(message = stasis_message_create(hint_change_message_type(), hint))) {
		ao2_ref(hint, -1);
		return -1;
	}

	stasis_publish(ast_device_state_topic_all(), message);
	stasis_publish(ast_presence_state_topic_all(), message);

	ao2_ref(message, -1);

	return 0;
}

/*! \brief Change hint for an extension */
static int ast_change_hint(struct ast_exten *oe, struct ast_exten *ne)
{
	struct ast_hint *hint;

	if (!oe || !ne) {
		return -1;
	}

	ao2_lock(hints);/* Locked to hold off others while we move the hint around. */

	/*
	 * Unlink the hint from the hints container as the extension
	 * name (which is the hash value) could change.
	 */
	hint = ao2_find(hints, oe, OBJ_UNLINK);
	if (!hint) {
		ao2_unlock(hints);
		ast_mutex_unlock(&context_merge_lock);
		return -1;
	}

	remove_hintdevice(hint);

	/* Update the hint and put it back in the hints container. */
	ao2_lock(hint);
	hint->exten = ne;

	ao2_unlock(hint);

	ao2_link(hints, hint);
	if (add_hintdevice(hint, ast_get_extension_app(ne))) {
		ast_log(LOG_WARNING, "Could not add devices for hint: %s@%s.\n",
			ast_get_extension_name(ne),
			ast_get_context_name(ast_get_extension_context(ne)));
	}
	ao2_unlock(hints);

	publish_hint_change(hint, ne);

	ao2_ref(hint, -1);

	return 0;
}

/*! \brief Get hint for channel */
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

/*! \brief Get hint for channel */
int ast_str_get_hint(struct ast_str **hint, ssize_t hintsize, struct ast_str **name, ssize_t namesize, struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e = ast_hint_extension(c, context, exten);

	if (!e) {
		return 0;
	}

	if (hint) {
		ast_str_set(hint, hintsize, "%s", ast_get_extension_app(e));
	}
	if (name) {
		const char *tmp = ast_get_extension_app_data(e);
		if (tmp) {
			ast_str_set(name, namesize, "%s", tmp);
		}
	}
	return -1;
}

int ast_exists_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_MATCH, 0, 0);
}

int ast_findlabel_extension(struct ast_channel *c, const char *context, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, 0, label, callerid, E_FINDLABEL, 0, 0);
}

int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, con, NULL, exten, 0, label, callerid, E_FINDLABEL, 0, 0);
}

int ast_canmatch_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_CANMATCH, 0, 0);
}

int ast_matchmore_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_MATCHMORE, 0, 0);
}

int ast_spawn_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid, int *found, int combined_find_spawn)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_SPAWN, found, combined_find_spawn);
}

void ast_pbx_h_exten_run(struct ast_channel *chan, const char *context)
{
	int autoloopflag;
	int found;
	int spawn_error;

	ast_channel_lock(chan);

	/*
	 * Make sure that the channel is marked as hungup since we are
	 * going to run the h exten on it.
	 */
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_HANGUP_EXEC);

	/* Set h exten location */
	if (context != ast_channel_context(chan)) {
		ast_channel_context_set(chan, context);
	}
	ast_channel_exten_set(chan, "h");
	ast_channel_priority_set(chan, 1);

	/* Save autoloop flag */
	autoloopflag = ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);
	ast_channel_unlock(chan);

	for (;;) {
		spawn_error = ast_spawn_extension(chan, ast_channel_context(chan),
			ast_channel_exten(chan), ast_channel_priority(chan),
			S_COR(ast_channel_caller(chan)->id.number.valid,
				ast_channel_caller(chan)->id.number.str, NULL), &found, 1);

		ast_channel_lock(chan);
		if (spawn_error) {
			/* The code after the loop needs the channel locked. */
			break;
		}
		ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);
		ast_channel_unlock(chan);
	}
	if (found && spawn_error) {
		/* Something bad happened, or a hangup has been requested. */
		ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n",
			ast_channel_context(chan), ast_channel_exten(chan),
			ast_channel_priority(chan), ast_channel_name(chan));
		ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n",
			ast_channel_context(chan), ast_channel_exten(chan),
			ast_channel_priority(chan), ast_channel_name(chan));
	}

	/* An "h" exten has been run, so indicate that one has been run. */
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_HANGUP_RUN);

	/* Restore autoloop flag */
	ast_set2_flag(ast_channel_flags(chan), autoloopflag, AST_FLAG_IN_AUTOLOOP);
	ast_channel_unlock(chan);
}

/*! helper function to set extension and priority */
void set_ext_pri(struct ast_channel *c, const char *exten, int pri)
{
	ast_channel_lock(c);
	ast_channel_exten_set(c, exten);
	ast_channel_priority_set(c, pri);
	ast_channel_unlock(c);
}

/*!
 * \brief collect digits from the channel into the buffer.
 * \param c, buf, buflen, pos
 * \param waittime is in milliseconds
 * \retval 0 on timeout or done.
 * \retval -1 on error.
*/
static int collect_digits(struct ast_channel *c, int waittime, char *buf, int buflen, int pos)
{
	int digit;

	buf[pos] = '\0';	/* make sure it is properly terminated */
	while (ast_matchmore_extension(c, ast_channel_context(c), buf, 1,
		S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
		/* As long as we're willing to wait, and as long as it's not defined,
		   keep reading digits until we can't possibly get a right answer anymore.  */
		digit = ast_waitfordigit(c, waittime);
		if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_ASYNCGOTO) {
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
			waittime = ast_channel_pbx(c)->dtimeoutms;
		}
	}
	return 0;
}

static enum ast_pbx_result __ast_pbx_run(struct ast_channel *c,
		struct ast_pbx_args *args)
{
	int found = 0;	/* set if we find at least one match */
	int res = 0;
	int autoloopflag;
	int error = 0;		/* set an error conditions */
	struct ast_pbx *pbx;
	ast_callid callid;

	/* A little initial setup here */
	if (ast_channel_pbx(c)) {
		ast_log(LOG_WARNING, "%s already has PBX structure??\n", ast_channel_name(c));
		/* XXX and now what ? */
		ast_free(ast_channel_pbx(c));
	}
	if (!(pbx = ast_calloc(1, sizeof(*pbx)))) {
		return AST_PBX_FAILED;
	}

	callid = ast_read_threadstorage_callid();
	/* If the thread isn't already associated with a callid, we should create that association. */
	if (!callid) {
		/* Associate new PBX thread with the channel call id if it is availble.
		 * If not, create a new one instead.
		 */
		callid = ast_channel_callid(c);
		if (!callid) {
			callid = ast_create_callid();
			if (callid) {
				ast_channel_lock(c);
				ast_channel_callid_set(c, callid);
				ast_channel_unlock(c);
			}
		}
		ast_callid_threadassoc_add(callid);
		callid = 0;
	}

	ast_channel_pbx_set(c, pbx);
	/* Set reasonable defaults */
	ast_channel_pbx(c)->rtimeoutms = 10000;
	ast_channel_pbx(c)->dtimeoutms = 5000;

	ast_channel_lock(c);
	autoloopflag = ast_test_flag(ast_channel_flags(c), AST_FLAG_IN_AUTOLOOP);	/* save value to restore at the end */
	ast_set_flag(ast_channel_flags(c), AST_FLAG_IN_AUTOLOOP);
	ast_channel_unlock(c);

	if (ast_strlen_zero(ast_channel_exten(c))) {
		/* If not successful fall back to 's' - but only if there is no given exten  */
		ast_verb(2, "Starting %s at %s,%s,%d failed so falling back to exten 's'\n", ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c));
		/* XXX the original code used the existing priority in the call to
		 * ast_exists_extension(), and reset it to 1 afterwards.
		 * I believe the correct thing is to set it to 1 immediately.
		*/
		set_ext_pri(c, "s", 1);
	}

	for (;;) {
		char dst_exten[256];	/* buffer to accumulate digits */
		int pos = 0;		/* XXX should check bounds */
		int digit = 0;
		int invalid = 0;
		int timeout = 0;

		/* No digits pressed yet */
		dst_exten[pos] = '\0';

		/* loop on priorities in this context/exten */
		while (!(res = ast_spawn_extension(c, ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c),
			S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL),
			&found, 1))) {

			if (!ast_check_hangup(c)) {
				ast_channel_priority_set(c, ast_channel_priority(c) + 1);
				continue;
			}

			/* Check softhangup flags. */
			if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_ASYNCGOTO) {
				ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
				continue;
			}
			if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_TIMEOUT) {
				if (ast_exists_extension(c, ast_channel_context(c), "T", 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					set_ext_pri(c, "T", 1);
					/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
					memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
					continue;
				} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					raise_exception(c, "ABSOLUTETIMEOUT", 1);
					/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
					memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
					continue;
				}

				/* Call timed out with no special extension to jump to. */
				error = 1;
				break;
			}
			ast_debug(1, "Extension %s, priority %d returned normally even though call was hung up\n",
				ast_channel_exten(c), ast_channel_priority(c));
			error = 1;
			break;
		} /* end while  - from here on we can use 'break' to go out */
		if (found && res) {
			/* Something bad happened, or a hangup has been requested. */
			if (strchr("0123456789ABCDEF*#", res)) {
				ast_debug(1, "Oooh, got something to jump out with ('%c')!\n", res);
				pos = 0;
				dst_exten[pos++] = digit = res;
				dst_exten[pos] = '\0';
			} else if (res == AST_PBX_INCOMPLETE) {
				ast_debug(1, "Spawn extension (%s,%s,%d) exited INCOMPLETE on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
				ast_verb(2, "Spawn extension (%s, %s, %d) exited INCOMPLETE on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));

				/* Don't cycle on incomplete - this will happen if the only extension that matches is our "incomplete" extension */
				if (!ast_matchmore_extension(c, ast_channel_context(c), ast_channel_exten(c), 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					invalid = 1;
				} else {
					ast_copy_string(dst_exten, ast_channel_exten(c), sizeof(dst_exten));
					digit = 1;
					pos = strlen(dst_exten);
				}
			} else {
				ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
				ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
				/* DUB Starts*/
				long int s1_rx_pkt = ast_channel_get_pkt_count(c, 1);
				long int s2_rx_pkt = ast_channel_get_pkt_count(c, 2);
				long int s1_ext_pkt = ast_channel_get_extra_pkt_count(c, 1);
				long int s2_ext_pkt = ast_channel_get_extra_pkt_count(c, 2);

				ast_verb(2, "%s: Stream 1: Rx Packets: %ld\t Extra Packets: %ld\t Total Packets: %ld\n", ast_channel_name(c), s1_rx_pkt, s1_ext_pkt, s1_rx_pkt+s1_ext_pkt);
				ast_verb(2, "%s: Stream 2: Rx Packets: %ld\t Extra Packets: %ld\t Total Packets: %ld\n", ast_channel_name(c), s2_rx_pkt, s2_ext_pkt, s2_rx_pkt+s2_ext_pkt);

				struct timeval rec_end_ts = (ast_tvcmp(ast_channel_get_rec_end_ts(c,1), ast_channel_get_rec_end_ts(c,2))<0)?ast_channel_get_rec_end_ts(c,2):ast_channel_get_rec_end_ts(c,1);
				int64_t rec_duration = ast_tvdiff_sec(rec_end_ts, ast_channel_get_rec_start_time(c));
				ast_verb(2, "%s: Recording Duration (seconds): %ld\n", ast_channel_name(c), ++rec_duration);
				ast_verb(2, "%s: Pause & Resume events: %s\n", ast_channel_name(c), ast_channel_get_pause_resume_events(c));
				/* DUB Ends */

				if ((res == AST_PBX_ERROR)
					&& ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					/* if we are already on the 'e' exten, don't jump to it again */
					if (!strcmp(ast_channel_exten(c), "e")) {
						ast_verb(2, "Spawn extension (%s, %s, %d) exited ERROR while already on 'e' exten on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
						error = 1;
					} else {
						raise_exception(c, "ERROR", 1);
						continue;
					}
				}

				if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_ASYNCGOTO) {
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
					continue;
				}
				if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_TIMEOUT) {
					if (ast_exists_extension(c, ast_channel_context(c), "T", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						set_ext_pri(c, "T", 1);
						/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
						memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
						ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
						continue;
					} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						raise_exception(c, "ABSOLUTETIMEOUT", 1);
						/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
						memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
						ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
						continue;
					}
					/* Call timed out with no special extension to jump to. */
				}
				error = 1;
				break;
			}
		}
		if (error)
			break;

		/*!\note
		 * We get here on a failure of some kind:  non-existing extension or
		 * hangup.  We have options, here.  We can either catch the failure
		 * and continue, or we can drop out entirely. */

		if (invalid
			|| (ast_strlen_zero(dst_exten) &&
				!ast_exists_extension(c, ast_channel_context(c), ast_channel_exten(c), 1,
				S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL)))) {
			/*!\note
			 * If there is no match at priority 1, it is not a valid extension anymore.
			 * Try to continue at "i" (for invalid) or "e" (for exception) or exit if
			 * neither exist.
			 */
			if (ast_exists_extension(c, ast_channel_context(c), "i", 1,
				S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
				ast_verb(3, "Channel '%s' sent to invalid extension: context,exten,priority=%s,%s,%d\n",
					ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c));
				pbx_builtin_setvar_helper(c, "INVALID_EXTEN", ast_channel_exten(c));
				set_ext_pri(c, "i", 1);
			} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
				S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
				raise_exception(c, "INVALID", 1);
			} else {
				ast_log(LOG_WARNING, "Channel '%s' sent to invalid extension but no invalid handler: context,exten,priority=%s,%s,%d\n",
					ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c));
				error = 1; /* we know what to do with it */
				break;
			}
		} else if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_TIMEOUT) {
			/* If we get this far with AST_SOFTHANGUP_TIMEOUT, then we know that the "T" extension is next. */
			ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
		} else {	/* keypress received, get more digits for a full extension */
			int waittime = 0;
			if (digit)
				waittime = ast_channel_pbx(c)->dtimeoutms;
			else if (!autofallthrough)
				waittime = ast_channel_pbx(c)->rtimeoutms;
			if (!waittime) {
				const char *status = pbx_builtin_getvar_helper(c, "DIALSTATUS");
				if (!status)
					status = "UNKNOWN";
				ast_verb(3, "Auto fallthrough, channel '%s' status is '%s'\n", ast_channel_name(c), status);
				if (!strcasecmp(status, "CONGESTION"))
					res = indicate_congestion(c, "10");
				else if (!strcasecmp(status, "CHANUNAVAIL"))
					res = indicate_congestion(c, "10");
				else if (!strcasecmp(status, "BUSY"))
					res = indicate_busy(c, "10");
				error = 1; /* XXX disable message */
				break;	/* exit from the 'for' loop */
			}

			if (collect_digits(c, waittime, dst_exten, sizeof(dst_exten), pos))
				break;
			if (res == AST_PBX_INCOMPLETE && ast_strlen_zero(&dst_exten[pos]))
				timeout = 1;
			if (!timeout
				&& ast_exists_extension(c, ast_channel_context(c), dst_exten, 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) { /* Prepare the next cycle */
				set_ext_pri(c, dst_exten, 1);
			} else {
				/* No such extension */
				if (!timeout && !ast_strlen_zero(dst_exten)) {
					/* An invalid extension */
					if (ast_exists_extension(c, ast_channel_context(c), "i", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						ast_verb(3, "Invalid extension '%s' in context '%s' on %s\n", dst_exten, ast_channel_context(c), ast_channel_name(c));
						pbx_builtin_setvar_helper(c, "INVALID_EXTEN", dst_exten);
						set_ext_pri(c, "i", 1);
					} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						raise_exception(c, "INVALID", 1);
					} else {
						ast_log(LOG_WARNING,
							"Invalid extension '%s', but no rule 'i' or 'e' in context '%s'\n",
							dst_exten, ast_channel_context(c));
						found = 1; /* XXX disable message */
						break;
					}
				} else {
					/* A simple timeout */
					if (ast_exists_extension(c, ast_channel_context(c), "t", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						ast_verb(3, "Timeout on %s\n", ast_channel_name(c));
						set_ext_pri(c, "t", 1);
					} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						raise_exception(c, "RESPONSETIMEOUT", 1);
					} else {
						ast_log(LOG_WARNING,
							"Timeout, but no rule 't' or 'e' in context '%s'\n",
							ast_channel_context(c));
						found = 1; /* XXX disable message */
						break;
					}
				}
			}
		}
	}

	if (!found && !error) {
		ast_log(LOG_WARNING, "Don't know what to do with '%s'\n", ast_channel_name(c));
	}

	if (!args || !args->no_hangup_chan) {
		ast_softhangup(c, AST_SOFTHANGUP_APPUNLOAD);
		if (!ast_test_flag(ast_channel_flags(c), AST_FLAG_BRIDGE_HANGUP_RUN)
			&& ast_exists_extension(c, ast_channel_context(c), "h", 1,
				S_COR(ast_channel_caller(c)->id.number.valid,
					ast_channel_caller(c)->id.number.str, NULL))) {
			ast_pbx_h_exten_run(c, ast_channel_context(c));
		}
		ast_pbx_hangup_handler_run(c);
	}

	ast_channel_lock(c);
	ast_set2_flag(ast_channel_flags(c), autoloopflag, AST_FLAG_IN_AUTOLOOP);
	ast_clear_flag(ast_channel_flags(c), AST_FLAG_BRIDGE_HANGUP_RUN); /* from one round to the next, make sure this gets cleared */
	ast_channel_unlock(c);
	pbx_destroy(ast_channel_pbx(c));
	ast_channel_pbx_set(c, NULL);

	if (!args || !args->no_hangup_chan) {
		ast_hangup(c);
	}

	return AST_PBX_SUCCESS;
}

/*!
 * \brief Increase call count for channel
 * \retval 0 on success
 * \retval non-zero if a configured limit (maxcalls, maxload, minmemfree) was reached
*/
static int increase_call_count(const struct ast_channel *c)
{
	int failed = 0;
	double curloadavg;
#if defined(HAVE_SYSINFO)
	struct sysinfo sys_info;
#endif

	ast_mutex_lock(&maxcalllock);
	if (ast_option_maxcalls) {
		if (countcalls >= ast_option_maxcalls) {
			ast_log(LOG_WARNING, "Maximum call limit of %d calls exceeded by '%s'!\n", ast_option_maxcalls, ast_channel_name(c));
			failed = -1;
		}
	}
	if (ast_option_maxload) {
		getloadavg(&curloadavg, 1);
		if (curloadavg >= ast_option_maxload) {
			ast_log(LOG_WARNING, "Maximum loadavg limit of %f load exceeded by '%s' (currently %f)!\n", ast_option_maxload, ast_channel_name(c), curloadavg);
			failed = -1;
		}
	}
#if defined(HAVE_SYSINFO)
	if (option_minmemfree) {
		/* Make sure that the free system memory is above the configured low watermark */
		if (!sysinfo(&sys_info)) {
			/* Convert the amount of available RAM from mem_units to MB. The calculation
			 * was done this way to avoid overflow problems */
			uint64_t curfreemem = sys_info.freeram + sys_info.bufferram;
			curfreemem *= sys_info.mem_unit;
			curfreemem /= 1024 * 1024;
			if (curfreemem < option_minmemfree) {
				ast_log(LOG_WARNING, "Available system memory (~%" PRIu64 "MB) is below the configured low watermark (%ldMB)\n",
					curfreemem, option_minmemfree);
				failed = -1;
			}
		}
	}
#endif

	if (!failed) {
		countcalls++;
		totalcalls++;
	}
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

	if (e->peer_table)
		ast_hashtab_destroy(e->peer_table,0);
	if (e->peer_label_table)
		ast_hashtab_destroy(e->peer_label_table, 0);
	if (e->datad)
		e->datad(e->data);
	ast_free(e);
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

	__ast_pbx_run(c, NULL);
	decrease_call_count();

	pthread_exit(NULL);

	return NULL;
}

enum ast_pbx_result ast_pbx_start(struct ast_channel *c)
{
	pthread_t t;

	if (!c) {
		ast_log(LOG_WARNING, "Asked to start thread on NULL channel?\n");
		return AST_PBX_FAILED;
	}

	if (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		ast_log(LOG_WARNING, "PBX requires Asterisk to be fully booted\n");
		return AST_PBX_FAILED;
	}

	if (increase_call_count(c))
		return AST_PBX_CALL_LIMIT;

	/* Start a new thread, and get something handling this channel. */
	if (ast_pthread_create_detached(&t, NULL, pbx_thread, c)) {
		ast_log(LOG_WARNING, "Failed to create new channel thread\n");
		decrease_call_count();
		return AST_PBX_FAILED;
	}

	return AST_PBX_SUCCESS;
}

enum ast_pbx_result ast_pbx_run_args(struct ast_channel *c, struct ast_pbx_args *args)
{
	enum ast_pbx_result res = AST_PBX_SUCCESS;

	if (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		ast_log(LOG_WARNING, "PBX requires Asterisk to be fully booted\n");
		return AST_PBX_FAILED;
	}

	if (increase_call_count(c)) {
		return AST_PBX_CALL_LIMIT;
	}

	res = __ast_pbx_run(c, args);

	decrease_call_count();

	return res;
}

enum ast_pbx_result ast_pbx_run(struct ast_channel *c)
{
	return ast_pbx_run_args(c, NULL);
}

int ast_active_calls(void)
{
	return countcalls;
}

int ast_processed_calls(void)
{
	return totalcalls;
}

int pbx_set_autofallthrough(int newval)
{
	int oldval = autofallthrough;
	autofallthrough = newval;
	return oldval;
}

int pbx_set_extenpatternmatchnew(int newval)
{
	int oldval = extenpatternmatchnew;
	extenpatternmatchnew = newval;
	return oldval;
}

void pbx_set_overrideswitch(const char *newval)
{
	if (overrideswitch) {
		ast_free(overrideswitch);
	}
	if (!ast_strlen_zero(newval)) {
		overrideswitch = ast_strdup(newval);
	} else {
		overrideswitch = NULL;
	}
}

/*!
 * \brief lookup for a context with a given name,
 * \retval found context or NULL if not found.
 */
static struct ast_context *find_context(const char *context)
{
	struct fake_context item;

	ast_copy_string(item.name, context, sizeof(item.name));

	return ast_hashtab_lookup(contexts_table, &item);
}

/*!
 * \brief lookup for a context with a given name,
 * \retval with conlock held if found.
 * \retval NULL if not found.
 */
static struct ast_context *find_context_locked(const char *context)
{
	struct ast_context *c;
	struct fake_context item;

	ast_copy_string(item.name, context, sizeof(item.name));

	ast_rdlock_contexts();
	c = ast_hashtab_lookup(contexts_table, &item);
	if (!c) {
		ast_unlock_contexts();
	}

	return c;
}

/*!
 * \brief Remove included contexts.
 * This function locks contexts list by &conlist, search for the right context
 * structure, leave context list locked and call ast_context_remove_include2
 * which removes include, unlock contexts list and return ...
 */
int ast_context_remove_include(const char *context, const char *include, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		/* found, remove include from this context ... */
		ret = ast_context_remove_include2(c, include, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

/*!
 * \brief Locks context, remove included contexts, unlocks context.
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_context_remove_include2(struct ast_context *con, const char *include, const char *registrar)
{
	int ret = -1;
	int idx;

	ast_wrlock_context(con);

	/* find our include */
	for (idx = 0; idx < ast_context_includes_count(con); idx++) {
		struct ast_include *i = AST_VECTOR_GET(&con->includes, idx);

		if (!strcmp(ast_get_include_name(i), include) &&
				(!registrar || !strcmp(ast_get_include_registrar(i), registrar))) {

			/* remove from list */
			ast_verb(3, "Removing inclusion of context '%s' in context '%s; registrar=%s'\n", include, ast_get_context_name(con), registrar);
			AST_VECTOR_REMOVE_ORDERED(&con->includes, idx);

			/* free include and return */
			include_free(i);
			ret = 0;
			break;
		}
	}

	ast_unlock_context(con);

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
	struct ast_context *c;

	c = find_context_locked(context);
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
	int idx;
	int ret = -1;

	ast_wrlock_context(con);

	/* walk switches */
	for (idx = 0; idx < ast_context_switches_count(con); idx++) {
		struct ast_sw *i = AST_VECTOR_GET(&con->alts, idx);

		if (!strcmp(ast_get_switch_name(i), sw) &&
			!strcmp(ast_get_switch_data(i), data) &&
			(!registrar || !strcmp(ast_get_switch_registrar(i), registrar))) {

			/* found, remove from list */
			ast_verb(3, "Removing switch '%s' from context '%s; registrar=%s'\n", sw, ast_get_context_name(con), registrar);
			AST_VECTOR_REMOVE_ORDERED(&con->alts, idx);

			/* free switch and return */
			sw_free(i);
			ret = 0;
			break;
		}
	}

	ast_unlock_context(con);

	return ret;
}

/*! \note This function will lock conlock. */
int ast_context_remove_extension(const char *context, const char *extension, int priority, const char *registrar)
{
	return ast_context_remove_extension_callerid(context, extension, priority, NULL, AST_EXT_MATCHCID_ANY, registrar);
}

int ast_context_remove_extension_callerid(const char *context, const char *extension, int priority, const char *callerid, int matchcallerid, const char *registrar)
{
	int ret = -1; /* default error return */
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) { /* ... remove extension ... */
		ret = ast_context_remove_extension_callerid2(c, extension, priority, callerid,
			matchcallerid, registrar, 0);
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
int ast_context_remove_extension2(struct ast_context *con, const char *extension, int priority, const char *registrar, int already_locked)
{
	return ast_context_remove_extension_callerid2(con, extension, priority, NULL, AST_EXT_MATCHCID_ANY, registrar, already_locked);
}

int ast_context_remove_extension_callerid2(struct ast_context *con, const char *extension, int priority, const char *callerid, int matchcallerid, const char *registrar, int already_locked)
{
	struct ast_exten *exten, *prev_exten = NULL;
	struct ast_exten *peer;
	struct ast_exten ex, *exten2, *exten3;
	char dummy_name[1024];
	char dummy_cid[1024];
	struct ast_exten *previous_peer = NULL;
	struct ast_exten *next_peer = NULL;
	int found = 0;

	if (!already_locked)
		ast_wrlock_context(con);

#ifdef NEED_DEBUG
	ast_verb(3,"Removing %s/%s/%d%s%s from trees, registrar=%s\n", con->name, extension, priority, matchcallerid ? "/" : "", matchcallerid ? callerid : "", registrar);
#endif
#ifdef CONTEXT_DEBUG
	check_contexts(__FILE__, __LINE__);
#endif
	/* find this particular extension */
	ex.exten = dummy_name;
	ext_strncpy(dummy_name, extension, sizeof(dummy_name), 1);
	ex.matchcid = matchcallerid;
	if (callerid) {
		ex.cidmatch = dummy_cid;
		ext_strncpy(dummy_cid, callerid, sizeof(dummy_cid), 1);
	} else {
		ex.cidmatch = NULL;
	}
	exten = ast_hashtab_lookup(con->root_table, &ex);
	if (exten) {
		if (priority == 0) {
			exten2 = ast_hashtab_remove_this_object(con->root_table, exten);
			if (!exten2)
				ast_log(LOG_ERROR,"Trying to delete the exten %s from context %s, but could not remove from the root_table\n", extension, con->name);
			if (con->pattern_tree) {
				struct match_char *x = add_exten_to_pattern_tree(con, exten, 1);

				if (x->exten) { /* this test for safety purposes */
					x->deleted = 1; /* with this marked as deleted, it will never show up in the scoreboard, and therefore never be found */
					x->exten = 0; /* get rid of what will become a bad pointer */
				} else {
					ast_log(LOG_WARNING,"Trying to delete an exten from a context, but the pattern tree node returned isn't a full extension\n");
				}
			}
		} else {
			ex.priority = priority;
			exten2 = ast_hashtab_lookup(exten->peer_table, &ex);
			if (exten2) {
				if (exten2->label) { /* if this exten has a label, remove that, too */
					exten3 = ast_hashtab_remove_this_object(exten->peer_label_table,exten2);
					if (!exten3) {
						ast_log(LOG_ERROR, "Did not remove this priority label (%d/%s) "
							"from the peer_label_table of context %s, extension %s!\n",
							priority, exten2->label, con->name, exten2->name);
					}
				}

				exten3 = ast_hashtab_remove_this_object(exten->peer_table, exten2);
				if (!exten3) {
					ast_log(LOG_ERROR, "Did not remove this priority (%d) from the "
						"peer_table of context %s, extension %s!\n",
						priority, con->name, exten2->name);
				}
				if (exten2 == exten && exten2->peer) {
					exten2 = ast_hashtab_remove_this_object(con->root_table, exten);
					ast_hashtab_insert_immediate(con->root_table, exten2->peer);
				}
				if (ast_hashtab_size(exten->peer_table) == 0) {
					/* well, if the last priority of an exten is to be removed,
					   then, the extension is removed, too! */
					exten3 = ast_hashtab_remove_this_object(con->root_table, exten);
					if (!exten3) {
						ast_log(LOG_ERROR, "Did not remove this exten (%s) from the "
							"context root_table (%s) (priority %d)\n",
							exten->name, con->name, priority);
					}
					if (con->pattern_tree) {
						struct match_char *x = add_exten_to_pattern_tree(con, exten, 1);
						if (x->exten) { /* this test for safety purposes */
							x->deleted = 1; /* with this marked as deleted, it will never show up in the scoreboard, and therefore never be found */
							x->exten = 0; /* get rid of what will become a bad pointer */
						}
					}
				}
			} else {
				ast_log(LOG_ERROR,"Could not find priority %d of exten %s in context %s!\n",
						priority, exten->name, con->name);
			}
		}
	} else {
		/* hmmm? this exten is not in this pattern tree? */
		ast_log(LOG_WARNING,"Cannot find extension %s in root_table in context %s\n",
				extension, con->name);
	}
#ifdef NEED_DEBUG
	if (con->pattern_tree) {
		ast_log(LOG_NOTICE,"match char tree after exten removal:\n");
		log_match_char_tree(con->pattern_tree, " ");
	}
#endif

	/* scan the extension list to find first matching extension-registrar */
	for (exten = con->root; exten; prev_exten = exten, exten = exten->next) {
		if (!strcmp(exten->exten, ex.exten) &&
			(!matchcallerid ||
				(!ast_strlen_zero(ex.cidmatch) && !ast_strlen_zero(exten->cidmatch) && !strcmp(exten->cidmatch, ex.cidmatch)) ||
				(ast_strlen_zero(ex.cidmatch) && ast_strlen_zero(exten->cidmatch)))) {
			break;
		}
	}
	if (!exten) {
		/* we can't find right extension */
		if (!already_locked)
			ast_unlock_context(con);
		return -1;
	}

	/* scan the priority list to remove extension with exten->priority == priority */
	for (peer = exten, next_peer = exten->peer ? exten->peer : exten->next;
		 peer && !strcmp(peer->exten, ex.exten) &&
			(!callerid || (!matchcallerid && !peer->matchcid) || (matchcallerid && peer->matchcid && !strcmp(peer->cidmatch, ex.cidmatch))) ;
			peer = next_peer, next_peer = next_peer ? (next_peer->peer ? next_peer->peer : next_peer->next) : NULL) {

		if ((priority == 0 || peer->priority == priority) &&
				(!registrar || !strcmp(peer->registrar, registrar) )) {
			found = 1;

			/* we are first priority extension? */
			if (!previous_peer) {
				/*
				 * We are first in the priority chain, so must update the extension chain.
				 * The next node is either the next priority or the next extension
				 */
				struct ast_exten *next_node = peer->peer ? peer->peer : peer->next;
				if (peer->peer) {
					/* move the peer_table and peer_label_table down to the next peer, if
					   it is there */
					peer->peer->peer_table = peer->peer_table;
					peer->peer->peer_label_table = peer->peer_label_table;
					peer->peer_table = NULL;
					peer->peer_label_table = NULL;
				}
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
	if (!already_locked)
		ast_unlock_context(con);
	return found ? 0 : -1;
}


/*!
 * \note This function locks contexts list by &conlist, searches for the right context
 * structure, and locks the macrolock mutex in that context.
 * macrolock is used to limit a macro to be executed by one call at a time.
 * \param context The context
 */
int ast_context_lockmacro(const char *context)
{
	struct ast_context *c;
	int ret = -1;

	c = find_context_locked(context);
	if (c) {
		ast_unlock_contexts();

		/* if we found context, lock macrolock */
		ret = ast_mutex_lock(&c->macrolock);
	}

	return ret;
}

/*!
 * \note This function locks contexts list by &conlist, searches for the right context
 * structure, and unlocks the macrolock mutex in that context.
 * macrolock is used to limit a macro to be executed by one call at a time.
 * \param context The context
 */
int ast_context_unlockmacro(const char *context)
{
	struct ast_context *c;
	int ret = -1;

	c = find_context_locked(context);
	if (c) {
		ast_unlock_contexts();

		/* if we found context, unlock macrolock */
		ret = ast_mutex_unlock(&c->macrolock);
	}

	return ret;
}

/*
 * Help for CLI commands ...
 */

/*! \brief  handle_show_hints: CLI support for listing registered dial plan hints */
static char *handle_show_hints(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_hint *hint;
	int num = 0;
	int watchers;
	struct ao2_iterator i;
	char buf[AST_MAX_EXTENSION+AST_MAX_CONTEXT+2];

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hints";
		e->usage =
			"Usage: core show hints\n"
			"       List registered hints.\n"
			"       Hint details are shown in five columns. In order from left to right, they are:\n"
			"       1. Hint extension URI.\n"
			"       2. List of mapped device or presence state identifiers.\n"
			"       3. Current extension state. The aggregate of mapped device states.\n"
			"       4. Current presence state for the mapped presence state provider.\n"
			"       5. Watchers - number of subscriptions and other entities watching this hint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (ao2_container_count(hints) == 0) {
		ast_cli(a->fd, "There are no registered dialplan hints\n");
		return CLI_SUCCESS;
	}
	/* ... we have hints ... */
	ast_cli(a->fd, "\n    -= Registered Asterisk Dial Plan Hints =-\n");

	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}
		watchers = ao2_container_count(hint->callbacks);
		snprintf(buf, sizeof(buf), "%s@%s",
			ast_get_extension_name(hint->exten),
			ast_get_context_name(ast_get_extension_context(hint->exten)));

		ast_cli(a->fd, "%-20.20s: %-20.20s  State:%-15.15s Presence:%-15.15s Watchers %2d\n",
			buf,
			ast_get_extension_app(hint->exten),
			ast_extension_state2str(hint->laststate),
			ast_presence_state2str(hint->last_presence_state),
			watchers);

		ao2_unlock(hint);
		num++;
	}
	ao2_iterator_destroy(&i);

	ast_cli(a->fd, "----------------\n");
	ast_cli(a->fd, "- %d hints registered\n", num);
	return CLI_SUCCESS;
}

/*! \brief autocomplete for CLI command 'core show hint' */
static char *complete_core_show_hint(const char *line, const char *word, int pos, int state)
{
	struct ast_hint *hint;
	char *ret = NULL;
	int which = 0;
	int wordlen;
	struct ao2_iterator i;

	if (pos != 3)
		return NULL;

	wordlen = strlen(word);

	/* walk through all hints */
	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}
		if (!strncasecmp(word, ast_get_extension_name(hint->exten), wordlen) && ++which > state) {
			ret = ast_strdup(ast_get_extension_name(hint->exten));
			ao2_unlock(hint);
			ao2_ref(hint, -1);
			break;
		}
		ao2_unlock(hint);
	}
	ao2_iterator_destroy(&i);

	return ret;
}

/*! \brief  handle_show_hint: CLI support for listing registered dial plan hint */
static char *handle_show_hint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_hint *hint;
	int watchers;
	int num = 0, extenlen;
	struct ao2_iterator i;
	char buf[AST_MAX_EXTENSION+AST_MAX_CONTEXT+2];

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hint";
		e->usage =
			"Usage: core show hint <exten>\n"
			"       List registered hint.\n"
			"       Hint details are shown in five columns. In order from left to right, they are:\n"
			"       1. Hint extension URI.\n"
			"       2. List of mapped device or presence state identifiers.\n"
			"       3. Current extension state. The aggregate of mapped device states.\n"
			"       4. Current presence state for the mapped presence state provider.\n"
			"       5. Watchers - number of subscriptions and other entities watching this hint.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_core_show_hint(a->line, a->word, a->pos, a->n);
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;

	if (ao2_container_count(hints) == 0) {
		ast_cli(a->fd, "There are no registered dialplan hints\n");
		return CLI_SUCCESS;
	}

	extenlen = strlen(a->argv[3]);
	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}
		if (!strncasecmp(ast_get_extension_name(hint->exten), a->argv[3], extenlen)) {
			watchers = ao2_container_count(hint->callbacks);
			sprintf(buf, "%s@%s",
				ast_get_extension_name(hint->exten),
				ast_get_context_name(ast_get_extension_context(hint->exten)));
			ast_cli(a->fd, "%-20.20s: %-20.20s  State:%-15.15s Presence:%-15.15s Watchers %2d\n",
				buf,
				ast_get_extension_app(hint->exten),
				ast_extension_state2str(hint->laststate),
				ast_presence_state2str(hint->last_presence_state),
				watchers);
			num++;
		}
		ao2_unlock(hint);
	}
	ao2_iterator_destroy(&i);
	if (!num)
		ast_cli(a->fd, "No hints matching extension %s\n", a->argv[3]);
	else
		ast_cli(a->fd, "%d hint%s matching extension %s\n", num, (num!=1 ? "s":""), a->argv[3]);
	return CLI_SUCCESS;
}

#if 0
/* This code can be used to test if the system survives running out of memory.
 * It might be an idea to put this in only if ENABLE_AUTODESTRUCT_TESTS is enabled.
 *
 * If you want to test this, these Linux sysctl flags might be appropriate:
 *   vm.overcommit_memory = 2
 *   vm.swappiness = 0
 *
 * <@Corydon76-home> I envision 'core eat disk space' and 'core eat file descriptors' now
 * <@mjordan> egads
 * <@mjordan> it's literally the 'big red' auto-destruct button
 * <@mjordan> if you were wondering who even builds such a thing.... well, now you know
 * ...
 * <@Corydon76-home> What about if they lived only if you defined TEST_FRAMEWORK?  Shouldn't have those on production machines
 * <@mjordan> I think accompanied with an update to one of our README files that "no, really, TEST_FRAMEWORK isn't for you", I'd be fine
 */
static char *handle_eat_memory(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	void **blocks;
	int blocks_pos = 0;
	const int blocks_max = 50000;
	long long int allocated = 0;
	int sizes[] = {
		100 * 1024 * 1024,
		100 * 1024,
		2 * 1024,
		400,
		0
	};
	int i;

	switch (cmd) {
	case CLI_INIT:
		/* To do: add method to free memory again? 5 minutes? */
		e->command = "core eat memory";
		e->usage =
			"Usage: core eat memory\n"
			"       Eats all available memory so you can test if the system survives\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	blocks = ast_malloc(sizeof(void*) * blocks_max);
	if (!blocks) {
		ast_log(LOG_ERROR, "Already out of mem?\n");
		return CLI_SUCCESS;
	}

	for (i = 0; sizes[i]; ++i) {
		int alloc_size = sizes[i];
		ast_log(LOG_WARNING, "Allocating %d sized blocks (got %d blocks already)\n", alloc_size, blocks_pos);
		while (1) {
			void *block;
			if (blocks_pos >= blocks_max) {
				ast_log(LOG_ERROR, "Memory buffer too small? Run me again :)\n");
				break;
			}

			block = ast_malloc(alloc_size);
			if (!block) {
				break;
			}

			blocks[blocks_pos++] = block;
			allocated += alloc_size;
		}
	}

	/* No freeing of the mem! */
	ast_log(LOG_WARNING, "Allocated %lld bytes total!\n", allocated);
	return CLI_SUCCESS;
}
#endif

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

/*! \brief Counters for the show dialplan manager command */
struct dialplan_counters {
	int total_items;
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

/*! \brief Writes CLI output of a single extension for show dialplan */
static void show_dialplan_helper_extension_output(int fd, char *buf1, char *buf2, struct ast_exten *exten)
{
	if (ast_get_extension_registrar_file(exten)) {
		ast_cli(fd, "  %-17s %-45s [%s:%d]\n",
			buf1, buf2,
			ast_get_extension_registrar_file(exten),
			ast_get_extension_registrar_line(exten));
		return;
	}

	ast_cli(fd, "  %-17s %-45s [%s]\n",
		buf1, buf2, ast_get_extension_registrar(exten));
}

/* XXX not verified */
static int show_dialplan_helper(int fd, const char *context, const char *exten, struct dialplan_counters *dpc, const struct ast_include *rinclude, int includecount, const char *includes[])
{
	struct ast_context *c = NULL;
	int res = 0, old_total_exten = dpc->total_exten;

	ast_rdlock_contexts();

	/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		int idx;
		struct ast_exten *e;
#ifndef LOW_MEMORY
		char buf[1024], buf2[1024];
#else
		char buf[256], buf2[256];
#endif
		int context_info_printed = 0;

		if (context && strcmp(ast_get_context_name(c), context))
			continue;	/* skip this one, name doesn't match */

		dpc->context_existence = 1;

		ast_rdlock_context(c);

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
			if (c->autohints) {
				ast_cli(fd, "Autohints support enabled\n");
			}
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
					if (c->autohints) {
						ast_cli(fd, "Autohints support enabled\n");
					}
				}
				context_info_printed = 1;
			}
			dpc->total_prio++;

			/* write extension name and first peer */
			if (e->matchcid == AST_EXT_MATCHCID_ON)
				snprintf(buf, sizeof(buf), "'%s' (CID match '%s') => ", ast_get_extension_name(e), e->cidmatch);
			else
				snprintf(buf, sizeof(buf), "'%s' =>", ast_get_extension_name(e));

			print_ext(e, buf2, sizeof(buf2));

			show_dialplan_helper_extension_output(fd, buf, buf2, e);

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

				show_dialplan_helper_extension_output(fd, buf, buf2, p);
			}
		}

		/* walk included and write info ... */
		for (idx = 0; idx < ast_context_includes_count(c); idx++) {
			const struct ast_include *i = ast_context_includes_get(c, idx);

			snprintf(buf, sizeof(buf), "'%s'", ast_get_include_name(i));
			if (exten) {
				/* Check all includes for the requested extension */
				if (includecount >= AST_PBX_MAX_STACK) {
					ast_log(LOG_WARNING, "Maximum include depth exceeded!\n");
				} else {
					int dupe = 0;
					int x;
					for (x = 0; x < includecount; x++) {
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
		for (idx = 0; idx < ast_context_ignorepats_count(c); idx++) {
			const struct ast_ignorepat *ip = ast_context_ignorepats_get(c, idx);
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
			for (idx = 0; idx < ast_context_switches_count(c); idx++) {
				const struct ast_sw *sw = ast_context_switches_get(c, idx);

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

static int show_debug_helper(int fd, const char *context, const char *exten, struct dialplan_counters *dpc, struct ast_include *rinclude, int includecount, const char *includes[])
{
	struct ast_context *c = NULL;
	int res = 0, old_total_exten = dpc->total_exten;

	ast_cli(fd,"\n     In-mem exten Trie for Fast Extension Pattern Matching:\n\n");

	ast_cli(fd,"\n           Explanation: Node Contents Format = <char(s) to match>:<pattern?>:<specif>:[matched extension]\n");
	ast_cli(fd,    "                        Where <char(s) to match> is a set of chars, any one of which should match the current character\n");
	ast_cli(fd,    "                              <pattern?>: Y if this a pattern match (eg. _XZN[5-7]), N otherwise\n");
	ast_cli(fd,    "                              <specif>: an assigned 'exactness' number for this matching char. The lower the number, the more exact the match\n");
	ast_cli(fd,    "                              [matched exten]: If all chars matched to this point, which extension this matches. In form: EXTEN:<exten string>\n");
	ast_cli(fd,    "                        In general, you match a trie node to a string character, from left to right. All possible matching chars\n");
	ast_cli(fd,    "                        are in a string vertically, separated by an unbroken string of '+' characters.\n\n");
	ast_rdlock_contexts();

	/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		int context_info_printed = 0;

		if (context && strcmp(ast_get_context_name(c), context))
			continue;	/* skip this one, name doesn't match */

		dpc->context_existence = 1;

		if (!c->pattern_tree) {
			/* Ignore check_return warning from Coverity for ast_exists_extension below */
			ast_exists_extension(NULL, c->name, "s", 1, ""); /* do this to force the trie to built, if it is not already */
		}

		ast_rdlock_context(c);

		dpc->total_context++;
		ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
			ast_get_context_name(c), ast_get_context_registrar(c));
		context_info_printed = 1;

		if (c->pattern_tree)
		{
			cli_match_char_tree(c->pattern_tree, " ", fd);
		} else {
			ast_cli(fd,"\n     No Pattern Trie present. Perhaps the context is empty...or there is trouble...\n\n");
		}

		ast_unlock_context(c);

		/* if we print something in context, make an empty line */
		if (context_info_printed)
			ast_cli(fd, "\n");
	}
	ast_unlock_contexts();

	return (dpc->total_exten == old_total_exten) ? -1 : res;
}

static char *handle_show_dialplan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *exten = NULL, *context = NULL;
	/* Variables used for different counters */
	struct dialplan_counters counters;
	const char *incstack[AST_PBX_MAX_STACK];

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan show";
		e->usage =
			"Usage: dialplan show [[exten@]context]\n"
			"       Show dialplan\n";
		return NULL;
	case CLI_GENERATE:
		return complete_show_dialplan_context(a->line, a->word, a->pos, a->n);
	}

	memset(&counters, 0, sizeof(counters));

	if (a->argc != 2 && a->argc != 3)
		return CLI_SHOWUSAGE;

	/* we obtain [exten@]context? if yes, split them ... */
	if (a->argc == 3) {
		if (strchr(a->argv[2], '@')) {	/* split into exten & context */
			context = ast_strdupa(a->argv[2]);
			exten = strsep(&context, "@");
			/* change empty strings to NULL */
			if (ast_strlen_zero(exten))
				exten = NULL;
		} else { /* no '@' char, only context given */
			context = ast_strdupa(a->argv[2]);
		}
		if (ast_strlen_zero(context))
			context = NULL;
	}
	/* else Show complete dial plan, context and exten are NULL */
	show_dialplan_helper(a->fd, context, exten, &counters, NULL, 0, incstack);

	/* check for input failure and throw some error messages */
	if (context && !counters.context_existence) {
		ast_cli(a->fd, "There is no existence of '%s' context\n", context);
		return CLI_FAILURE;
	}

	if (exten && !counters.extension_existence) {
		if (context)
			ast_cli(a->fd, "There is no existence of %s@%s extension\n",
				exten, context);
		else
			ast_cli(a->fd,
				"There is no existence of '%s' extension in all contexts\n",
				exten);
		return CLI_FAILURE;
	}

	ast_cli(a->fd,"-= %d %s (%d %s) in %d %s. =-\n",
				counters.total_exten, counters.total_exten == 1 ? "extension" : "extensions",
				counters.total_prio, counters.total_prio == 1 ? "priority" : "priorities",
				counters.total_context, counters.total_context == 1 ? "context" : "contexts");

	/* everything ok */
	return CLI_SUCCESS;
}

/*! \brief Send ack once */
static char *handle_debug_dialplan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *exten = NULL, *context = NULL;
	/* Variables used for different counters */
	struct dialplan_counters counters;
	const char *incstack[AST_PBX_MAX_STACK];

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan debug";
		e->usage =
			"Usage: dialplan debug [context]\n"
			"       Show dialplan context Trie(s). Usually only useful to folks debugging the deep internals of the fast pattern matcher\n";
		return NULL;
	case CLI_GENERATE:
		return complete_show_dialplan_context(a->line, a->word, a->pos, a->n);
	}

	memset(&counters, 0, sizeof(counters));

	if (a->argc != 2 && a->argc != 3)
		return CLI_SHOWUSAGE;

	/* we obtain [exten@]context? if yes, split them ... */
	/* note: we ignore the exten totally here .... */
	if (a->argc == 3) {
		if (strchr(a->argv[2], '@')) {	/* split into exten & context */
			context = ast_strdupa(a->argv[2]);
			exten = strsep(&context, "@");
			/* change empty strings to NULL */
			if (ast_strlen_zero(exten))
				exten = NULL;
		} else { /* no '@' char, only context given */
			context = ast_strdupa(a->argv[2]);
		}
		if (ast_strlen_zero(context))
			context = NULL;
	}
	/* else Show complete dial plan, context and exten are NULL */
	show_debug_helper(a->fd, context, exten, &counters, NULL, 0, incstack);

	/* check for input failure and throw some error messages */
	if (context && !counters.context_existence) {
		ast_cli(a->fd, "There is no existence of '%s' context\n", context);
		return CLI_FAILURE;
	}


	ast_cli(a->fd,"-= %d %s. =-\n",
			counters.total_context, counters.total_context == 1 ? "context" : "contexts");

	/* everything ok */
	return CLI_SUCCESS;
}

/*! \brief Send ack once */
static void manager_dpsendack(struct mansession *s, const struct message *m)
{
	astman_send_listack(s, m, "DialPlan list will follow", "start");
}

/*! \brief Show dialplan extensions
 * XXX this function is similar but not exactly the same as the CLI's
 * show dialplan. Must check whether the difference is intentional or not.
 */
static int manager_show_dialplan_helper(struct mansession *s, const struct message *m,
					const char *actionidtext, const char *context,
					const char *exten, struct dialplan_counters *dpc,
					const struct ast_include *rinclude,
					int includecount, const char *includes[])
{
	struct ast_context *c;
	int res = 0, old_total_exten = dpc->total_exten;

	if (ast_strlen_zero(exten))
		exten = NULL;
	if (ast_strlen_zero(context))
		context = NULL;

	ast_debug(3, "manager_show_dialplan: Context: -%s- Extension: -%s-\n", context, exten);

	/* try to lock contexts */
	if (ast_rdlock_contexts()) {
		astman_send_error(s, m, "Failed to lock contexts");
		ast_log(LOG_WARNING, "Failed to lock contexts list for manager: listdialplan\n");
		return -1;
	}

	c = NULL;		/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		int idx;
		struct ast_exten *e;

		if (context && strcmp(ast_get_context_name(c), context) != 0)
			continue;	/* not the name we want */

		dpc->context_existence = 1;
		dpc->total_context++;

		ast_debug(3, "manager_show_dialplan: Found Context: %s \n", ast_get_context_name(c));

		if (ast_rdlock_context(c)) {	/* failed to lock */
			ast_debug(3, "manager_show_dialplan: Failed to lock context\n");
			continue;
		}

		/* XXX note- an empty context is not printed */
		e = NULL;		/* walk extensions in context  */
		while ( (e = ast_walk_context_extensions(c, e)) ) {
			struct ast_exten *p;

			/* looking for extension? is this our extension? */
			if (exten && !ast_extension_match(ast_get_extension_name(e), exten)) {
				/* not the one we are looking for, continue */
				ast_debug(3, "manager_show_dialplan: Skipping extension %s\n", ast_get_extension_name(e));
				continue;
			}
			ast_debug(3, "manager_show_dialplan: Found Extension: %s \n", ast_get_extension_name(e));

			dpc->extension_existence = 1;

			dpc->total_exten++;

			p = NULL;		/* walk next extension peers */
			while ( (p = ast_walk_extension_priorities(e, p)) ) {
				int prio = ast_get_extension_priority(p);

				dpc->total_prio++;
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nExtension: %s\r\n", ast_get_context_name(c), ast_get_extension_name(e) );

				/* XXX maybe make this conditional, if p != e ? */
				if (ast_get_extension_label(p))
					astman_append(s, "ExtensionLabel: %s\r\n", ast_get_extension_label(p));

				if (prio == PRIORITY_HINT) {
					astman_append(s, "Priority: hint\r\nApplication: %s\r\n", ast_get_extension_app(p));
				} else {
					astman_append(s, "Priority: %d\r\nApplication: %s\r\nAppData: %s\r\n", prio, ast_get_extension_app(p), (char *) ast_get_extension_app_data(p));
				}
				astman_append(s, "Registrar: %s\r\n\r\n", ast_get_extension_registrar(e));
			}
		}

		for (idx = 0; idx < ast_context_includes_count(c); idx++) {
			const struct ast_include *i = ast_context_includes_get(c, idx);

			if (exten) {
				/* Check all includes for the requested extension */
				if (includecount >= AST_PBX_MAX_STACK) {
					ast_log(LOG_WARNING, "Maximum include depth exceeded!\n");
				} else {
					int dupe = 0;
					int x;
					for (x = 0; x < includecount; x++) {
						if (!strcasecmp(includes[x], ast_get_include_name(i))) {
							dupe++;
							break;
						}
					}
					if (!dupe) {
						includes[includecount] = ast_get_include_name(i);
						manager_show_dialplan_helper(s, m, actionidtext, ast_get_include_name(i), exten, dpc, i, includecount + 1, includes);
					} else {
						ast_log(LOG_WARNING, "Avoiding circular include of %s within %s\n", ast_get_include_name(i), context);
					}
				}
			} else {
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nIncludeContext: %s\r\nRegistrar: %s\r\n", ast_get_context_name(c), ast_get_include_name(i), ast_get_include_registrar(i));
				astman_append(s, "\r\n");
				ast_debug(3, "manager_show_dialplan: Found Included context: %s \n", ast_get_include_name(i));
			}
		}

		for (idx = 0; idx < ast_context_ignorepats_count(c); idx++) {
			const struct ast_ignorepat *ip = ast_context_ignorepats_get(c, idx);
			const char *ipname = ast_get_ignorepat_name(ip);
			char ignorepat[AST_MAX_EXTENSION];

			snprintf(ignorepat, sizeof(ignorepat), "_%s.", ipname);
			if (!exten || ast_extension_match(ignorepat, exten)) {
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nIgnorePattern: %s\r\nRegistrar: %s\r\n", ast_get_context_name(c), ipname, ast_get_ignorepat_registrar(ip));
				astman_append(s, "\r\n");
			}
		}
		if (!rinclude) {
			for (idx = 0; idx < ast_context_switches_count(c); idx++) {
				const struct ast_sw *sw = ast_context_switches_get(c, idx);

				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nSwitch: %s/%s\r\nRegistrar: %s\r\n", ast_get_context_name(c), ast_get_switch_name(sw), ast_get_switch_data(sw), ast_get_switch_registrar(sw));
				astman_append(s, "\r\n");
				ast_debug(3, "manager_show_dialplan: Found Switch : %s \n", ast_get_switch_name(sw));
			}
		}

		ast_unlock_context(c);
	}
	ast_unlock_contexts();

	if (dpc->total_exten == old_total_exten) {
		ast_debug(3, "manager_show_dialplan: Found nothing new\n");
		/* Nothing new under the sun */
		return -1;
	} else {
		return res;
	}
}

/*! \brief  Manager listing of dial plan */
static int manager_show_dialplan(struct mansession *s, const struct message *m)
{
	const char *exten, *context;
	const char *id = astman_get_header(m, "ActionID");
	const char *incstack[AST_PBX_MAX_STACK];
	char idtext[256];

	/* Variables used for different counters */
	struct dialplan_counters counters;

	if (!ast_strlen_zero(id))
		snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);
	else
		idtext[0] = '\0';

	memset(&counters, 0, sizeof(counters));

	exten = astman_get_header(m, "Extension");
	context = astman_get_header(m, "Context");

	manager_show_dialplan_helper(s, m, idtext, context, exten, &counters, NULL, 0, incstack);

	if (!ast_strlen_zero(context) && !counters.context_existence) {
		char errorbuf[BUFSIZ];

		snprintf(errorbuf, sizeof(errorbuf), "Did not find context %s", context);
		astman_send_error(s, m, errorbuf);
		return 0;
	}
	if (!ast_strlen_zero(exten) && !counters.extension_existence) {
		char errorbuf[BUFSIZ];

		if (!ast_strlen_zero(context))
			snprintf(errorbuf, sizeof(errorbuf), "Did not find extension %s@%s", exten, context);
		else
			snprintf(errorbuf, sizeof(errorbuf), "Did not find extension %s in any context", exten);
		astman_send_error(s, m, errorbuf);
		return 0;
	}

	if (!counters.total_items) {
		manager_dpsendack(s, m);
	}

	astman_send_list_complete_start(s, m, "ShowDialPlanComplete", counters.total_items);
	astman_append(s,
		"ListExtensions: %d\r\n"
		"ListPriorities: %d\r\n"
		"ListContexts: %d\r\n",
		counters.total_exten, counters.total_prio, counters.total_context);
	astman_send_list_complete_end(s);

	/* everything ok */
	return 0;
}

#ifdef AST_DEVMODE
static char *handle_show_device2extenstate(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_devstate_aggregate agg;
	int i, j, exten, combined;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show device2extenstate";
		e->usage =
			"Usage: core show device2extenstate\n"
			"       Lists device state to extension state combinations.\n";
	case CLI_GENERATE:
		return NULL;
	}
	for (i = 0; i < AST_DEVICE_TOTAL; i++) {
		for (j = 0; j < AST_DEVICE_TOTAL; j++) {
			ast_devstate_aggregate_init(&agg);
			ast_devstate_aggregate_add(&agg, i);
			ast_devstate_aggregate_add(&agg, j);
			combined = ast_devstate_aggregate_result(&agg);
			exten = ast_devstate_to_extenstate(combined);
			ast_cli(a->fd, "\n Exten:%14s  CombinedDevice:%12s  Dev1:%12s  Dev2:%12s", ast_extension_state2str(exten), ast_devstate_str(combined), ast_devstate_str(j), ast_devstate_str(i));
		}
	}
	ast_cli(a->fd, "\n");
	return CLI_SUCCESS;
}
#endif

static char *handle_set_extenpatternmatchnew(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set extenpatternmatchnew true";
		e->usage =
			"Usage: dialplan set extenpatternmatchnew true|false\n"
			"       Use the NEW extension pattern matching algorithm, true or false.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	oldval =  pbx_set_extenpatternmatchnew(1);

	if (oldval)
		ast_cli(a->fd, "\n    -- Still using the NEW pattern match algorithm for extension names in the dialplan.\n");
	else
		ast_cli(a->fd, "\n    -- Switched to using the NEW pattern match algorithm for extension names in the dialplan.\n");

	return CLI_SUCCESS;
}

static char *handle_unset_extenpatternmatchnew(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set extenpatternmatchnew false";
		e->usage =
			"Usage: dialplan set extenpatternmatchnew true|false\n"
			"       Use the NEW extension pattern matching algorithm, true or false.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	oldval =  pbx_set_extenpatternmatchnew(0);

	if (!oldval)
		ast_cli(a->fd, "\n    -- Still using the OLD pattern match algorithm for extension names in the dialplan.\n");
	else
		ast_cli(a->fd, "\n    -- Switched to using the OLD pattern match algorithm for extension names in the dialplan.\n");

	return CLI_SUCCESS;
}

/*
 * CLI entries for upper commands ...
 */
static struct ast_cli_entry pbx_cli[] = {
#if 0
	AST_CLI_DEFINE(handle_eat_memory, "Eats all available memory"),
#endif
	AST_CLI_DEFINE(handle_show_hints, "Show dialplan hints"),
	AST_CLI_DEFINE(handle_show_hint, "Show dialplan hint"),
#ifdef AST_DEVMODE
	AST_CLI_DEFINE(handle_show_device2extenstate, "Show expected exten state from multiple device states"),
#endif
	AST_CLI_DEFINE(handle_show_dialplan, "Show dialplan"),
	AST_CLI_DEFINE(handle_debug_dialplan, "Show fast extension pattern matching data structures"),
	AST_CLI_DEFINE(handle_unset_extenpatternmatchnew, "Use the Old extension pattern matching algorithm."),
	AST_CLI_DEFINE(handle_set_extenpatternmatchnew, "Use the New extension pattern matching algorithm."),
};

void unreference_cached_app(struct ast_app *app)
{
	struct ast_context *context = NULL;
	struct ast_exten *eroot = NULL, *e = NULL;

	ast_rdlock_contexts();
	while ((context = ast_walk_contexts(context))) {
		while ((eroot = ast_walk_context_extensions(context, eroot))) {
			while ((e = ast_walk_extension_priorities(eroot, e))) {
				if (e->cached_app == app)
					e->cached_app = NULL;
			}
		}
	}
	ast_unlock_contexts();

	return;
}

struct ast_context *ast_context_find_or_create(struct ast_context **extcontexts, struct ast_hashtab *exttable, const char *name, const char *registrar)
{
	struct ast_context *tmp, **local_contexts;
	struct fake_context search;
	int length = sizeof(struct ast_context) + strlen(name) + 1;

	if (!contexts_table) {
		/* Protect creation of contexts_table from reentrancy. */
		ast_wrlock_contexts();
		if (!contexts_table) {
			contexts_table = ast_hashtab_create(17,
				ast_hashtab_compare_contexts,
				ast_hashtab_resize_java,
				ast_hashtab_newsize_java,
				ast_hashtab_hash_contexts,
				0);
		}
		ast_unlock_contexts();
	}

	ast_copy_string(search.name, name, sizeof(search.name));
	if (!extcontexts) {
		ast_rdlock_contexts();
		local_contexts = &contexts;
		tmp = ast_hashtab_lookup(contexts_table, &search);
		if (tmp) {
			tmp->refcount++;
			ast_unlock_contexts();
			return tmp;
		}
	} else { /* local contexts just in a linked list; search there for the new context; slow, linear search, but not frequent */
		local_contexts = extcontexts;
		tmp = ast_hashtab_lookup(exttable, &search);
		if (tmp) {
			tmp->refcount++;
			return tmp;
		}
	}

	if ((tmp = ast_calloc(1, length))) {
		ast_rwlock_init(&tmp->lock);
		ast_mutex_init(&tmp->macrolock);
		strcpy(tmp->name, name);
		tmp->root = NULL;
		tmp->root_table = NULL;
		tmp->registrar = ast_strdup(registrar);
		AST_VECTOR_INIT(&tmp->includes, 0);
		AST_VECTOR_INIT(&tmp->ignorepats, 0);
		AST_VECTOR_INIT(&tmp->alts, 0);
		tmp->refcount = 1;
	} else {
		ast_log(LOG_ERROR, "Danger! We failed to allocate a context for %s!\n", name);
		if (!extcontexts) {
			ast_unlock_contexts();
		}
		return NULL;
	}

	if (!extcontexts) {
		tmp->next = *local_contexts;
		*local_contexts = tmp;
		ast_hashtab_insert_safe(contexts_table, tmp); /*put this context into the tree */
		ast_unlock_contexts();
	} else {
		tmp->next = *local_contexts;
		if (exttable)
			ast_hashtab_insert_immediate(exttable, tmp); /*put this context into the tree */

		*local_contexts = tmp;
	}
	ast_debug(1, "Registered extension context '%s'; registrar: %s\n", tmp->name, registrar);
	return tmp;
}

void ast_context_set_autohints(struct ast_context *con, int enabled)
{
	con->autohints = enabled;
}

void __ast_context_destroy(struct ast_context *list, struct ast_hashtab *contexttab, struct ast_context *con, const char *registrar);

struct store_hint {
	char *context;
	char *exten;
	AST_LIST_HEAD_NOLOCK(, ast_state_cb) callbacks;
	int laststate;
	int last_presence_state;
	char *last_presence_subtype;
	char *last_presence_message;

	AST_LIST_ENTRY(store_hint) list;
	char data[0];
};

AST_LIST_HEAD_NOLOCK(store_hints, store_hint);

static void context_merge_incls_swits_igps_other_registrars(struct ast_context *new, struct ast_context *old, const char *registrar)
{
	int idx;

	ast_debug(1, "merging incls/swits/igpats from old(%s) to new(%s) context, registrar = %s\n", ast_get_context_name(old), ast_get_context_name(new), registrar);
	/* copy in the includes, switches, and ignorepats */
	/* walk through includes */
	for (idx = 0; idx < ast_context_includes_count(old); idx++) {
		const struct ast_include *i = ast_context_includes_get(old, idx);

		if (!strcmp(ast_get_include_registrar(i), registrar)) {
			continue; /* not mine */
		}
		ast_context_add_include2(new, ast_get_include_name(i), ast_get_include_registrar(i));
	}

	/* walk through switches */
	for (idx = 0; idx < ast_context_switches_count(old); idx++) {
		const struct ast_sw *sw = ast_context_switches_get(old, idx);

		if (!strcmp(ast_get_switch_registrar(sw), registrar)) {
			continue; /* not mine */
		}
		ast_context_add_switch2(new, ast_get_switch_name(sw), ast_get_switch_data(sw), ast_get_switch_eval(sw), ast_get_switch_registrar(sw));
	}

	/* walk thru ignorepats ... */
	for (idx = 0; idx < ast_context_ignorepats_count(old); idx++) {
		const struct ast_ignorepat *ip = ast_context_ignorepats_get(old, idx);

		if (strcmp(ast_get_ignorepat_registrar(ip), registrar) == 0) {
			continue; /* not mine */
		}
		ast_context_add_ignorepat2(new, ast_get_ignorepat_name(ip), ast_get_ignorepat_registrar(ip));
	}
}

/*! Set up an autohint placeholder in the hints container */
static void context_table_create_autohints(struct ast_hashtab *table)
{
	struct ast_context *con;
	struct ast_hashtab_iter *iter;

	/* Remove all autohints as the below iteration will recreate them */
	ao2_callback(autohints, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	iter = ast_hashtab_start_traversal(table);
	while ((con = ast_hashtab_next(iter))) {
		size_t name_len = strlen(con->name) + 1;
		size_t registrar_len = strlen(con->registrar) + 1;
		struct ast_autohint *autohint;

		if (!con->autohints) {
			continue;
		}

		autohint = ao2_alloc_options(sizeof(*autohint) + name_len + registrar_len, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!autohint) {
			continue;
		}

		ast_copy_string(autohint->context, con->name, name_len);
		autohint->registrar = autohint->context + name_len;
		ast_copy_string(autohint->registrar, con->registrar, registrar_len);

		ao2_link(autohints, autohint);
		ao2_ref(autohint, -1);

		ast_verb(3, "Enabled autohints support on context '%s'\n", con->name);
	}
	ast_hashtab_end_traversal(iter);
}

/* the purpose of this routine is to duplicate a context, with all its substructure,
   except for any extens that have a matching registrar */
static void context_merge(struct ast_context **extcontexts, struct ast_hashtab *exttable, struct ast_context *context, const char *registrar)
{
	struct ast_context *new = ast_hashtab_lookup(exttable, context); /* is there a match in the new set? */
	struct ast_exten *exten_item, *prio_item, *new_exten_item, *new_prio_item;
	struct ast_hashtab_iter *exten_iter;
	struct ast_hashtab_iter *prio_iter;
	int insert_count = 0;
	int first = 1;

	/* We'll traverse all the extensions/prios, and see which are not registrar'd with
	   the current registrar, and copy them to the new context. If the new context does not
	   exist, we'll create it "on demand". If no items are in this context to copy, then we'll
	   only create the empty matching context if the old one meets the criteria */

	if (context->root_table) {
		exten_iter = ast_hashtab_start_traversal(context->root_table);
		while ((exten_item=ast_hashtab_next(exten_iter))) {
			if (new) {
				new_exten_item = ast_hashtab_lookup(new->root_table, exten_item);
			} else {
				new_exten_item = NULL;
			}
			prio_iter = ast_hashtab_start_traversal(exten_item->peer_table);
			while ((prio_item=ast_hashtab_next(prio_iter))) {
				int res1;
				char *dupdstr;

				if (new_exten_item) {
					new_prio_item = ast_hashtab_lookup(new_exten_item->peer_table, prio_item);
				} else {
					new_prio_item = NULL;
				}
				if (strcmp(prio_item->registrar,registrar) == 0) {
					continue;
				}
				/* make sure the new context exists, so we have somewhere to stick this exten/prio */
				if (!new) {
					new = ast_context_find_or_create(extcontexts, exttable, context->name, prio_item->registrar); /* a new context created via priority from a different context in the old dialplan, gets its registrar from the prio's registrar */
					if (new) {
						new->autohints = context->autohints;
					}
				}

				/* copy in the includes, switches, and ignorepats */
				if (first) { /* but, only need to do this once */
					context_merge_incls_swits_igps_other_registrars(new, context, registrar);
					first = 0;
				}

				if (!new) {
					ast_log(LOG_ERROR,"Could not allocate a new context for %s in merge_and_delete! Danger!\n", context->name);
					ast_hashtab_end_traversal(prio_iter);
					ast_hashtab_end_traversal(exten_iter);
					return; /* no sense continuing. */
				}
				/* we will not replace existing entries in the new context with stuff from the old context.
				   but, if this is because of some sort of registrar conflict, we ought to say something... */

				dupdstr = ast_strdup(prio_item->data);

				res1 = ast_add_extension2(new, 0, prio_item->name, prio_item->priority, prio_item->label,
										  prio_item->matchcid ? prio_item->cidmatch : NULL, prio_item->app, dupdstr, ast_free_ptr, prio_item->registrar,
										  prio_item->registrar_file, prio_item->registrar_line);
				if (!res1 && new_exten_item && new_prio_item){
					ast_verb(3,"Dropping old dialplan item %s/%s/%d [%s(%s)] (registrar=%s) due to conflict with new dialplan\n",
							context->name, prio_item->name, prio_item->priority, prio_item->app, (char*)prio_item->data, prio_item->registrar);
				} else {
					/* we do NOT pass the priority data from the old to the new -- we pass a copy of it, so no changes to the current dialplan take place,
					 and no double frees take place, either! */
					insert_count++;
				}
			}
			ast_hashtab_end_traversal(prio_iter);
		}
		ast_hashtab_end_traversal(exten_iter);
	} else if (new) {
		/* If the context existed but had no extensions, we still want to merge
		 * the includes, switches and ignore patterns.
		 */
		context_merge_incls_swits_igps_other_registrars(new, context, registrar);
	}

	if (!insert_count && !new && (strcmp(context->registrar, registrar) != 0 ||
		  (strcmp(context->registrar, registrar) == 0 && context->refcount > 1))) {
		/* we could have given it the registrar of the other module who incremented the refcount,
		   but that's not available, so we give it the registrar we know about */
		new = ast_context_find_or_create(extcontexts, exttable, context->name, context->registrar);

		if (new) {
			new->autohints = context->autohints;
		}

		/* copy in the includes, switches, and ignorepats */
		context_merge_incls_swits_igps_other_registrars(new, context, registrar);
	}
}


/* XXX this does not check that multiple contexts are merged */
void ast_merge_contexts_and_delete(struct ast_context **extcontexts, struct ast_hashtab *exttable, const char *registrar)
{
	double ft;
	struct ast_context *tmp;
	struct ast_context *oldcontextslist;
	struct ast_hashtab *oldtable;
	struct store_hints hints_stored = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct store_hints hints_removed = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct store_hint *saved_hint;
	struct ast_hint *hint;
	struct ast_exten *exten;
	int length;
	struct ast_state_cb *thiscb;
	struct ast_hashtab_iter *iter;
	struct ao2_iterator i;
	int ctx_count = 0;
	struct timeval begintime;
	struct timeval writelocktime;
	struct timeval endlocktime;
	struct timeval enddeltime;

	/*
	 * It is very important that this function hold the hints
	 * container lock _and_ the conlock during its operation; not
	 * only do we need to ensure that the list of contexts and
	 * extensions does not change, but also that no hint callbacks
	 * (watchers) are added or removed during the merge/delete
	 * process
	 *
	 * In addition, the locks _must_ be taken in this order, because
	 * there are already other code paths that use this order
	 */

	begintime = ast_tvnow();
	ast_mutex_lock(&context_merge_lock);/* Serialize ast_merge_contexts_and_delete */
	ast_wrlock_contexts();

	if (!contexts_table) {
		/* Create any autohint contexts */
		context_table_create_autohints(exttable);

		/* Well, that's odd. There are no contexts. */
		contexts_table = exttable;
		contexts = *extcontexts;
		ast_unlock_contexts();
		ast_mutex_unlock(&context_merge_lock);
		return;
	}

	iter = ast_hashtab_start_traversal(contexts_table);
	while ((tmp = ast_hashtab_next(iter))) {
		++ctx_count;
		context_merge(extcontexts, exttable, tmp, registrar);
	}
	ast_hashtab_end_traversal(iter);

	ao2_lock(hints);
	writelocktime = ast_tvnow();

	/* preserve all watchers for hints */
	i = ao2_iterator_init(hints, AO2_ITERATOR_DONTLOCK);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		if (ao2_container_count(hint->callbacks)) {
			size_t exten_len;

			ao2_lock(hint);
			if (!hint->exten) {
				/* The extension has already been destroyed. (Should never happen here) */
				ao2_unlock(hint);
				continue;
			}

			exten_len = strlen(hint->exten->exten) + 1;
			length = exten_len + strlen(hint->exten->parent->name) + 1
				+ sizeof(*saved_hint);
			if (!(saved_hint = ast_calloc(1, length))) {
				ao2_unlock(hint);
				continue;
			}

			/* This removes all the callbacks from the hint into saved_hint. */
			while ((thiscb = ao2_callback(hint->callbacks, OBJ_UNLINK, NULL, NULL))) {
				AST_LIST_INSERT_TAIL(&saved_hint->callbacks, thiscb, entry);
				/*
				 * We intentionally do not unref thiscb to account for the
				 * non-ao2 reference in saved_hint->callbacks
				 */
			}

			saved_hint->laststate = hint->laststate;
			saved_hint->context = saved_hint->data;
			strcpy(saved_hint->data, hint->exten->parent->name);
			saved_hint->exten = saved_hint->data + strlen(saved_hint->context) + 1;
			ast_copy_string(saved_hint->exten, hint->exten->exten, exten_len);
			if (hint->last_presence_subtype) {
				saved_hint->last_presence_subtype = ast_strdup(hint->last_presence_subtype);
			}
			if (hint->last_presence_message) {
				saved_hint->last_presence_message = ast_strdup(hint->last_presence_message);
			}
			saved_hint->last_presence_state = hint->last_presence_state;
			ao2_unlock(hint);
			AST_LIST_INSERT_HEAD(&hints_stored, saved_hint, list);
		}
	}
	ao2_iterator_destroy(&i);

	/* save the old table and list */
	oldtable = contexts_table;
	oldcontextslist = contexts;

	/* move in the new table and list */
	contexts_table = exttable;
	contexts = *extcontexts;

	/*
	 * Restore the watchers for hints that can be found; notify
	 * those that cannot be restored.
	 */
	while ((saved_hint = AST_LIST_REMOVE_HEAD(&hints_stored, list))) {
		struct pbx_find_info q = { .stacklen = 0 };

		exten = pbx_find_extension(NULL, NULL, &q, saved_hint->context, saved_hint->exten,
			PRIORITY_HINT, NULL, "", E_MATCH);
		/*
		 * If this is a pattern, dynamically create a new extension for this
		 * particular match.  Note that this will only happen once for each
		 * individual extension, because the pattern will no longer match first.
		 */
		if (exten && exten->exten[0] == '_') {
			ast_add_extension_nolock(exten->parent->name, 0, saved_hint->exten,
				PRIORITY_HINT, NULL, 0, exten->app, ast_strdup(exten->data), ast_free_ptr,
				exten->registrar);
			/* rwlocks are not recursive locks */
			exten = ast_hint_extension_nolock(NULL, saved_hint->context,
				saved_hint->exten);
		}

		/* Find the hint in the hints container */
		hint = exten ? ao2_find(hints, exten, 0) : NULL;
		if (!hint) {
			/*
			 * Notify watchers of this removed hint later when we aren't
			 * encumberd by so many locks.
			 */
			AST_LIST_INSERT_HEAD(&hints_removed, saved_hint, list);
		} else {
			ao2_lock(hint);
			while ((thiscb = AST_LIST_REMOVE_HEAD(&saved_hint->callbacks, entry))) {
				ao2_link(hint->callbacks, thiscb);
				/* Ref that we added when putting into saved_hint->callbacks */
				ao2_ref(thiscb, -1);
			}
			hint->laststate = saved_hint->laststate;
			hint->last_presence_state = saved_hint->last_presence_state;
			hint->last_presence_subtype = saved_hint->last_presence_subtype;
			hint->last_presence_message = saved_hint->last_presence_message;
			ao2_unlock(hint);
			ao2_ref(hint, -1);
			/*
			 * The free of saved_hint->last_presence_subtype and
			 * saved_hint->last_presence_message is not necessary here.
			 */
			ast_free(saved_hint);
		}
	}

	/* Create all applicable autohint contexts */
	context_table_create_autohints(contexts_table);

	ao2_unlock(hints);
	ast_unlock_contexts();

	/*
	 * Notify watchers of all removed hints with the same lock
	 * environment as device_state_cb().
	 */
	while ((saved_hint = AST_LIST_REMOVE_HEAD(&hints_removed, list))) {
		/* this hint has been removed, notify the watchers */
		while ((thiscb = AST_LIST_REMOVE_HEAD(&saved_hint->callbacks, entry))) {
			execute_state_callback(thiscb->change_cb,
				saved_hint->context,
				saved_hint->exten,
				thiscb->data,
				AST_HINT_UPDATE_DEVICE,
				NULL,
				NULL);
			/* Ref that we added when putting into saved_hint->callbacks */
			ao2_ref(thiscb, -1);
		}
		ast_free(saved_hint->last_presence_subtype);
		ast_free(saved_hint->last_presence_message);
		ast_free(saved_hint);
	}

	ast_mutex_unlock(&context_merge_lock);
	endlocktime = ast_tvnow();

	/*
	 * The old list and hashtab no longer are relevant, delete them
	 * while the rest of asterisk is now freely using the new stuff
	 * instead.
	 */

	ast_hashtab_destroy(oldtable, NULL);

	for (tmp = oldcontextslist; tmp; ) {
		struct ast_context *next;	/* next starting point */

		next = tmp->next;
		__ast_internal_context_destroy(tmp);
		tmp = next;
	}
	enddeltime = ast_tvnow();

	ft = ast_tvdiff_us(writelocktime, begintime);
	ft /= 1000000.0;
	ast_verb(3,"Time to scan old dialplan and merge leftovers back into the new: %8.6f sec\n", ft);

	ft = ast_tvdiff_us(endlocktime, writelocktime);
	ft /= 1000000.0;
	ast_verb(3,"Time to restore hints and swap in new dialplan: %8.6f sec\n", ft);

	ft = ast_tvdiff_us(enddeltime, endlocktime);
	ft /= 1000000.0;
	ast_verb(3,"Time to delete the old dialplan: %8.6f sec\n", ft);

	ft = ast_tvdiff_us(enddeltime, begintime);
	ft /= 1000000.0;
	ast_verb(3,"Total time merge_contexts_delete: %8.6f sec\n", ft);
	ast_verb(3, "%s successfully loaded %d contexts (enable debug for details).\n", registrar, ctx_count);
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int ast_context_add_include(const char *context, const char *include, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_context_add_include2(c, include, registrar);
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
int ast_context_add_include2(struct ast_context *con, const char *value,
	const char *registrar)
{
	struct ast_include *new_include;
	int idx;

	/* allocate new include structure ... */
	new_include = include_alloc(value, registrar);
	if (!new_include) {
		return -1;
	}

	ast_wrlock_context(con);

	/* ... go to last include and check if context is already included too... */
	for (idx = 0; idx < ast_context_includes_count(con); idx++) {
		const struct ast_include *i = ast_context_includes_get(con, idx);

		if (!strcasecmp(ast_get_include_name(i), ast_get_include_name(new_include))) {
			include_free(new_include);
			ast_unlock_context(con);
			errno = EEXIST;
			return -1;
		}
	}

	/* ... include new context into context list, unlock, return */
	if (AST_VECTOR_APPEND(&con->includes, new_include)) {
		include_free(new_include);
		ast_unlock_context(con);
		return -1;
	}
	ast_debug(1, "Including context '%s' in context '%s'\n",
		ast_get_include_name(new_include), ast_get_context_name(con));

	ast_unlock_context(con);

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
	struct ast_context *c;

	c = find_context_locked(context);
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
	int idx;
	struct ast_sw *new_sw;

	/* allocate new sw structure ... */
	if (!(new_sw = sw_alloc(value, data, eval, registrar))) {
		return -1;
	}

	/* ... try to lock this context ... */
	ast_wrlock_context(con);

	/* ... go to last sw and check if context is already swd too... */
	for (idx = 0; idx < ast_context_switches_count(con); idx++) {
		const struct ast_sw *i = ast_context_switches_get(con, idx);

		if (!strcasecmp(ast_get_switch_name(i), ast_get_switch_name(new_sw)) &&
			!strcasecmp(ast_get_switch_data(i), ast_get_switch_data(new_sw))) {
			sw_free(new_sw);
			ast_unlock_context(con);
			errno = EEXIST;
			return -1;
		}
	}

	/* ... sw new context into context list, unlock, return */
	if (AST_VECTOR_APPEND(&con->alts, new_sw)) {
		sw_free(new_sw);
		ast_unlock_context(con);
		return -1;
	}

	ast_verb(3, "Including switch '%s/%s' in context '%s'\n",
		ast_get_switch_name(new_sw), ast_get_switch_data(new_sw), ast_get_context_name(con));

	ast_unlock_context(con);

	return 0;
}

/*
 * EBUSY  - can't lock
 * ENOENT - there is not context existence
 */
int ast_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_context_remove_ignorepat2(c, ignorepat, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_context_remove_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar)
{
	int idx;

	ast_wrlock_context(con);

	for (idx = 0; idx < ast_context_ignorepats_count(con); idx++) {
		struct ast_ignorepat *ip = AST_VECTOR_GET(&con->ignorepats, idx);

		if (!strcmp(ast_get_ignorepat_name(ip), ignorepat) &&
			(!registrar || (registrar == ast_get_ignorepat_registrar(ip)))) {
			AST_VECTOR_REMOVE_ORDERED(&con->ignorepats, idx);
			ignorepat_free(ip);
			ast_unlock_context(con);
			return 0;
		}
	}

	ast_unlock_context(con);
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
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_context_add_ignorepat2(c, value, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	struct ast_ignorepat *ignorepat = ignorepat_alloc(value, registrar);
	int idx;

	if (!ignorepat) {
		return -1;
	}

	ast_wrlock_context(con);
	for (idx = 0; idx < ast_context_ignorepats_count(con); idx++) {
		const struct ast_ignorepat *i = ast_context_ignorepats_get(con, idx);

		if (!strcasecmp(ast_get_ignorepat_name(i), value)) {
			/* Already there */
			ast_unlock_context(con);
			ignorepat_free(ignorepat);
			errno = EEXIST;
			return -1;
		}
	}
	if (AST_VECTOR_APPEND(&con->ignorepats, ignorepat)) {
		ignorepat_free(ignorepat);
		ast_unlock_context(con);
		return -1;
	}
	ast_unlock_context(con);

	return 0;
}

int ast_ignore_pattern(const char *context, const char *pattern)
{
	int ret = 0;
	struct ast_context *con;

	ast_rdlock_contexts();
	con = ast_context_find(context);
	if (con) {
		int idx;

		for (idx = 0; idx < ast_context_ignorepats_count(con); idx++) {
			const struct ast_ignorepat *pat = ast_context_ignorepats_get(con, idx);

			if (ast_extension_match(ast_get_ignorepat_name(pat), pattern)) {
				ret = 1;
				break;
			}
		}
	}
	ast_unlock_contexts();

	return ret;
}

/*
 * ast_add_extension_nolock -- use only in situations where the conlock is already held
 * ENOENT  - no existence of context
 *
 */
static int ast_add_extension_nolock(const char *context, int replace, const char *extension,
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context(context);
	if (c) {
		ret = ast_add_extension2_lockopt(c, replace, extension, priority, label, callerid,
			application, data, datad, registrar, NULL, 0, 1);
	}

	return ret;
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
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_add_extension2(c, replace, extension, priority, label, callerid,
			application, data, datad, registrar, NULL, 0);
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
		ast_channel_context_set(chan, context);
	if (!ast_strlen_zero(exten))
		ast_channel_exten_set(chan, exten);
	if (priority > -1) {
		/* see flag description in channel.h for explanation */
		if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP)) {
			--priority;
		}
		ast_channel_priority_set(chan, priority);
	}

	ast_channel_unlock(chan);

	return 0;
}

int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	struct ast_channel *newchan;

	ast_channel_lock(chan);
	/* Channels in a bridge or running a PBX can be sent directly to the specified destination */
	if (ast_channel_is_bridged(chan) || ast_channel_pbx(chan)) {
		if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP)) {
			priority += 1;
		}
		ast_explicit_goto(chan, context, exten, priority);
		ast_softhangup_nolock(chan, AST_SOFTHANGUP_ASYNCGOTO);
		ast_channel_unlock(chan);
		return 0;
	}
	ast_channel_unlock(chan);

	/* Otherwise, we need to gain control of the channel first */
	newchan = ast_channel_yank(chan);
	if (!newchan) {
		ast_log(LOG_WARNING, "Unable to gain control of channel %s\n", ast_channel_name(chan));
		return -1;
	}
	ast_explicit_goto(newchan, context, exten, priority);
	if (ast_pbx_start(newchan)) {
		ast_hangup(newchan);
		ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(newchan));
		return -1;
	}

	return 0;
}

int ast_async_goto_by_name(const char *channame, const char *context, const char *exten, int priority)
{
	struct ast_channel *chan;
	int res = -1;

	if ((chan = ast_channel_get_by_name(channame))) {
		res = ast_async_goto(chan, context, exten, priority);
		chan = ast_channel_unref(chan);
	}

	return res;
}

/*!
 * \internal
 * \brief Copy a string skipping whitespace and optionally dashes.
 *
 * \param dst Destination buffer to copy src string.
 * \param src Null terminated string to copy.
 * \param dst_size Number of bytes in the dst buffer.
 * \param nofluf Nonzero if '-' chars are not copied.
 *
 * \return Number of bytes written to dst including null terminator.
 */
static unsigned int ext_strncpy(char *dst, const char *src, size_t dst_size, int nofluff)
{
	unsigned int count;
	unsigned int insquares;
	unsigned int is_pattern;

	if (!dst_size--) {
		/* There really is no dst buffer */
		return 0;
	}

	count = 0;
	insquares = 0;
	is_pattern = *src == '_';
	while (*src && count < dst_size) {
		if (*src == '[') {
			if (is_pattern) {
				insquares = 1;
			}
		} else if (*src == ']') {
			insquares = 0;
		} else if (*src == ' ' && !insquares) {
			++src;
			continue;
		} else if (*src == '-' && !insquares && nofluff) {
			++src;
			continue;
		}
		*dst++ = *src++;
		++count;
	}
	*dst = '\0';

	return count + 1;
}

/*!
 * \brief add the extension in the priority chain.
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int add_priority(struct ast_context *con, struct ast_exten *tmp,
	struct ast_exten *el, struct ast_exten *e, int replace)
{
	struct ast_exten *ep;
	struct ast_exten *eh=e;
	int repeated_label = 0; /* Track if this label is a repeat, assume no. */

	for (ep = NULL; e ; ep = e, e = e->peer) {
		if (e->label && tmp->label && e->priority != tmp->priority && !strcmp(e->label, tmp->label)) {
			if (strcmp(e->name, tmp->name)) {
				ast_log(LOG_WARNING,
					"Extension '%s' priority %d in '%s', label '%s' already in use at aliased extension '%s' priority %d\n",
					tmp->name, tmp->priority, con->name, tmp->label, e->name, e->priority);
			} else {
				ast_log(LOG_WARNING,
					"Extension '%s' priority %d in '%s', label '%s' already in use at priority %d\n",
					tmp->name, tmp->priority, con->name, tmp->label, e->priority);
			}
			repeated_label = 1;
		}
		if (e->priority >= tmp->priority) {
			break;
		}
	}

	if (repeated_label) {	/* Discard the label since it's a repeat. */
		tmp->label = NULL;
	}

	if (!e) {	/* go at the end, and ep is surely set because the list is not empty */
		ast_hashtab_insert_safe(eh->peer_table, tmp);

		if (tmp->label) {
			ast_hashtab_insert_safe(eh->peer_label_table, tmp);
		}
		ep->peer = tmp;
		return 0;	/* success */
	}
	if (e->priority == tmp->priority) {
		/* Can't have something exactly the same.  Is this a
		   replacement?  If so, replace, otherwise, bonk. */
		if (!replace) {
			if (strcmp(e->name, tmp->name)) {
				ast_log(LOG_WARNING,
					"Unable to register extension '%s' priority %d in '%s', already in use by aliased extension '%s'\n",
					tmp->name, tmp->priority, con->name, e->name);
			} else {
				ast_log(LOG_WARNING,
					"Unable to register extension '%s' priority %d in '%s', already in use\n",
					tmp->name, tmp->priority, con->name);
			}

			return -1;
		}
		/* we are replacing e, so copy the link fields and then update
		 * whoever pointed to e to point to us
		 */
		tmp->next = e->next;	/* not meaningful if we are not first in the peer list */
		tmp->peer = e->peer;	/* always meaningful */
		if (ep)	{		/* We're in the peer list, just insert ourselves */
			ast_hashtab_remove_object_via_lookup(eh->peer_table,e);

			if (e->label) {
				ast_hashtab_remove_object_via_lookup(eh->peer_label_table,e);
			}

			ast_hashtab_insert_safe(eh->peer_table,tmp);
			if (tmp->label) {
				ast_hashtab_insert_safe(eh->peer_label_table,tmp);
			}

			ep->peer = tmp;
		} else if (el) {		/* We're the first extension. Take over e's functions */
			struct match_char *x = add_exten_to_pattern_tree(con, e, 1);
			tmp->peer_table = e->peer_table;
			tmp->peer_label_table = e->peer_label_table;
			ast_hashtab_remove_object_via_lookup(tmp->peer_table,e);
			ast_hashtab_insert_safe(tmp->peer_table,tmp);
			if (e->label) {
				ast_hashtab_remove_object_via_lookup(tmp->peer_label_table, e);
			}
			if (tmp->label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}

			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			el->next = tmp;
			/* The pattern trie points to this exten; replace the pointer,
			   and all will be well */
			if (x) { /* if the trie isn't formed yet, don't sweat this */
				if (x->exten) { /* this test for safety purposes */
					x->exten = tmp; /* replace what would become a bad pointer */
				} else {
					ast_log(LOG_ERROR,"Trying to delete an exten from a context, but the pattern tree node returned isn't an extension\n");
				}
			}
		} else {			/* We're the very first extension.  */
			struct match_char *x = add_exten_to_pattern_tree(con, e, 1);
			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			tmp->peer_table = e->peer_table;
			tmp->peer_label_table = e->peer_label_table;
			ast_hashtab_remove_object_via_lookup(tmp->peer_table, e);
			ast_hashtab_insert_safe(tmp->peer_table, tmp);
			if (e->label) {
				ast_hashtab_remove_object_via_lookup(tmp->peer_label_table, e);
			}
			if (tmp->label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}

			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			con->root = tmp;
			/* The pattern trie points to this exten; replace the pointer,
			   and all will be well */
			if (x) { /* if the trie isn't formed yet; no problem */
				if (x->exten) { /* this test for safety purposes */
					x->exten = tmp; /* replace what would become a bad pointer */
				} else {
					ast_log(LOG_ERROR,"Trying to delete an exten from a context, but the pattern tree node returned isn't an extension\n");
				}
			}
		}
		if (tmp->priority == PRIORITY_HINT)
			ast_change_hint(e,tmp);
		/* Destroy the old one */
		if (e->datad)
			e->datad(e->data);
		ast_free(e);
	} else {	/* Slip ourselves in just before e */
		tmp->peer = e;
		tmp->next = e->next;	/* extension chain, or NULL if e is not the first extension */
		if (ep) {			/* Easy enough, we're just in the peer list */
			if (tmp->label) {
				ast_hashtab_insert_safe(eh->peer_label_table, tmp);
			}
			ast_hashtab_insert_safe(eh->peer_table, tmp);
			ep->peer = tmp;
		} else {			/* we are the first in some peer list, so link in the ext list */
			tmp->peer_table = e->peer_table;
			tmp->peer_label_table = e->peer_label_table;
			e->peer_table = 0;
			e->peer_label_table = 0;
			ast_hashtab_insert_safe(tmp->peer_table, tmp);
			if (tmp->label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}
			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			if (el)
				el->next = tmp;	/* in the middle... */
			else
				con->root = tmp; /* ... or at the head */
			e->next = NULL;	/* e is no more at the head, so e->next must be reset */
		}
		/* And immediately return success. */
		if (tmp->priority == PRIORITY_HINT) {
			ast_add_hint(tmp);
		}
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
	const char *registrar, const char *registrar_file, int registrar_line)
{
	return ast_add_extension2_lockopt(con, replace, extension, priority, label, callerid,
		application, data, datad, registrar, registrar_file, registrar_line, 1);
}

int ast_add_extension2_nolock(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar, const char *registrar_file, int registrar_line)
{
	return ast_add_extension2_lockopt(con, replace, extension, priority, label, callerid,
		application, data, datad, registrar, registrar_file, registrar_line, 0);
}


/*!
 * \brief Same as ast_add_extension2() but controls the context locking.
 *
 * \details
 * Does all the work of ast_add_extension2, but adds an arg to
 * determine if context locking should be done.
 */
static int ast_add_extension2_lockopt(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar, const char *registrar_file, int registrar_line, int lock_context)
{
	/*
	 * Sort extensions (or patterns) according to the rules indicated above.
	 * These are implemented by the function ext_cmp()).
	 * All priorities for the same ext/pattern/cid are kept in a list,
	 * using the 'peer' field  as a link field..
	 */
	struct ast_exten *tmp, *tmp2, *e, *el = NULL;
	int res;
	int length;
	char *p;
	char expand_buf[VAR_BUF_SIZE];
	struct ast_exten dummy_exten = {0};
	char dummy_name[1024];
	int exten_fluff;
	int callerid_fluff;

	if (ast_strlen_zero(extension)) {
		ast_log(LOG_ERROR,"You have to be kidding-- add exten '' to context %s? Figure out a name and call me back. Action ignored.\n",
				con->name);
		/* We always need to deallocate 'data' on failure */
		if (datad) {
			datad(data);
		}
		return -1;
	}

	/* If we are adding a hint evalulate in variables and global variables */
	if (priority == PRIORITY_HINT && strstr(application, "${") && extension[0] != '_') {
		int inhibited;
		struct ast_channel *c = ast_dummy_channel_alloc();

		if (c) {
			ast_channel_exten_set(c, extension);
			ast_channel_context_set(c, con->name);
		}

		/*
		 * We can allow dangerous functions when adding a hint since
		 * altering dialplan is itself a privileged activity.  Otherwise,
		 * we could never execute dangerous functions.
		 */
		inhibited = ast_thread_inhibit_escalations_swap(0);
		pbx_substitute_variables_helper(c, application, expand_buf, sizeof(expand_buf));
		if (0 < inhibited) {
			ast_thread_inhibit_escalations();
		}

		application = expand_buf;
		if (c) {
			ast_channel_unref(c);
		}
	}

	exten_fluff = ext_fluff_count(extension);
	callerid_fluff = callerid ? ext_fluff_count(callerid) : 0;

	length = sizeof(struct ast_exten);
	length += strlen(extension) + 1;
	if (exten_fluff) {
		length += strlen(extension) + 1 - exten_fluff;
	}
	length += strlen(application) + 1;
	if (label) {
		length += strlen(label) + 1;
	}
	if (callerid) {
		length += strlen(callerid) + 1;
		if (callerid_fluff) {
			length += strlen(callerid) + 1 - callerid_fluff;
		}
	} else {
		length ++;	/* just the '\0' */
	}
	if (registrar_file) {
		length += strlen(registrar_file) + 1;
	}

	/* Be optimistic:  Build the extension structure first */
	tmp = ast_calloc(1, length);
	if (!tmp) {
		/* We always need to deallocate 'data' on failure */
		if (datad) {
			datad(data);
		}
		return -1;
	}

	if (ast_strlen_zero(label)) /* let's turn empty labels to a null ptr */
		label = 0;

	/* use p as dst in assignments, as the fields are const char * */
	p = tmp->stuff;
	if (label) {
		tmp->label = p;
		strcpy(p, label);
		p += strlen(label) + 1;
	}
	tmp->name = p;
	p += ext_strncpy(p, extension, strlen(extension) + 1, 0);
	if (exten_fluff) {
		tmp->exten = p;
		p += ext_strncpy(p, extension, strlen(extension) + 1 - exten_fluff, 1);
	} else {
		/* no fluff, we don't need a copy. */
		tmp->exten = tmp->name;
	}
	tmp->priority = priority;
	tmp->cidmatch_display = tmp->cidmatch = p;	/* but use p for assignments below */

	/* Blank callerid and NULL callerid are two SEPARATE things.  Do NOT confuse the two!!! */
	if (callerid) {
		p += ext_strncpy(p, callerid, strlen(callerid) + 1, 0);
		if (callerid_fluff) {
			tmp->cidmatch = p;
			p += ext_strncpy(p, callerid, strlen(callerid) + 1 - callerid_fluff, 1);
		}
		tmp->matchcid = AST_EXT_MATCHCID_ON;
	} else {
		*p++ = '\0';
		tmp->matchcid = AST_EXT_MATCHCID_OFF;
	}

	if (registrar_file) {
		tmp->registrar_file = p;
		strcpy(p, registrar_file);
		p += strlen(registrar_file) + 1;
	} else {
		tmp->registrar_file = NULL;
	}

	tmp->app = p;
	strcpy(p, application);
	tmp->parent = con;
	tmp->data = data;
	tmp->datad = datad;
	tmp->registrar = registrar;
	tmp->registrar_line = registrar_line;

	if (lock_context) {
		ast_wrlock_context(con);
	}

	if (con->pattern_tree) { /* usually, on initial load, the pattern_tree isn't formed until the first find_exten; so if we are adding
								an extension, and the trie exists, then we need to incrementally add this pattern to it. */
		ext_strncpy(dummy_name, tmp->exten, sizeof(dummy_name), 1);
		dummy_exten.exten = dummy_name;
		dummy_exten.matchcid = AST_EXT_MATCHCID_OFF;
		dummy_exten.cidmatch = 0;
		tmp2 = ast_hashtab_lookup(con->root_table, &dummy_exten);
		if (!tmp2) {
			/* hmmm, not in the trie; */
			add_exten_to_pattern_tree(con, tmp, 0);
			ast_hashtab_insert_safe(con->root_table, tmp); /* for the sake of completeness */
		}
	}
	res = 0; /* some compilers will think it is uninitialized otherwise */
	for (e = con->root; e; el = e, e = e->next) {   /* scan the extension list */
		res = ext_cmp(e->exten, tmp->exten);
		if (res == 0) { /* extension match, now look at cidmatch */
			if (e->matchcid == AST_EXT_MATCHCID_OFF && tmp->matchcid == AST_EXT_MATCHCID_OFF)
				res = 0;
			else if (tmp->matchcid == AST_EXT_MATCHCID_ON && e->matchcid == AST_EXT_MATCHCID_OFF)
				res = 1;
			else if (e->matchcid == AST_EXT_MATCHCID_ON && tmp->matchcid == AST_EXT_MATCHCID_OFF)
				res = -1;
			else
				res = ext_cmp(e->cidmatch, tmp->cidmatch);
		}
		if (res >= 0)
			break;
	}
	if (e && res == 0) { /* exact match, insert in the priority chain */
		res = add_priority(con, tmp, el, e, replace);
		if (res < 0) {
			if (con->pattern_tree) {
				struct match_char *x = add_exten_to_pattern_tree(con, tmp, 1);

				if (x->exten) {
					x->deleted = 1;
					x->exten = 0;
				}

				ast_hashtab_remove_this_object(con->root_table, tmp);
			}

			if (tmp->datad) {
				tmp->datad(tmp->data);
				/* if you free this, null it out */
				tmp->data = NULL;
			}

			ast_free(tmp);
		}
		if (lock_context) {
			ast_unlock_context(con);
		}
		if (res < 0) {
			errno = EEXIST;
			return -1;
		}
	} else {
		/*
		 * not an exact match, this is the first entry with this pattern,
		 * so insert in the main list right before 'e' (if any)
		 */
		tmp->next = e;
		tmp->peer_table = ast_hashtab_create(13,
						hashtab_compare_exten_numbers,
						ast_hashtab_resize_java,
						ast_hashtab_newsize_java,
						hashtab_hash_priority,
						0);
		tmp->peer_label_table = ast_hashtab_create(7,
							hashtab_compare_exten_labels,
							ast_hashtab_resize_java,
							ast_hashtab_newsize_java,
							hashtab_hash_labels,
							0);

		if (el) {  /* there is another exten already in this context */
			el->next = tmp;
		} else {  /* this is the first exten in this context */
			if (!con->root_table) {
				con->root_table = ast_hashtab_create(27,
													hashtab_compare_extens,
													ast_hashtab_resize_java,
													ast_hashtab_newsize_java,
													hashtab_hash_extens,
													0);
			}
			con->root = tmp;
		}
		if (label) {
			ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
		}
		ast_hashtab_insert_safe(tmp->peer_table, tmp);
		ast_hashtab_insert_safe(con->root_table, tmp);

		if (lock_context) {
			ast_unlock_context(con);
		}
		if (tmp->priority == PRIORITY_HINT) {
			ast_add_hint(tmp);
		}
	}
	if (DEBUG_ATLEAST(1)) {
		if (tmp->matchcid == AST_EXT_MATCHCID_ON) {
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d (CID match '%s') to %s (%p)\n",
				tmp->name, tmp->priority, tmp->cidmatch_display, con->name, con);
		} else {
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d to %s (%p)\n",
				tmp->name, tmp->priority, con->name, con);
		}
	}

	return 0;
}

/*! \brief Structure which contains information about an outgoing dial */
struct pbx_outgoing {
	/*! \brief Dialing structure being used */
	struct ast_dial *dial;
	/*! \brief Condition for synchronous dialing */
	ast_cond_t cond;
	/*! \brief Application to execute */
	char app[AST_MAX_APP];
	/*! \brief Application data to pass to application */
	char *appdata;
	/*! \brief Dialplan context */
	char context[AST_MAX_CONTEXT];
	/*! \brief Dialplan extension */
	char exten[AST_MAX_EXTENSION];
	/*! \brief Dialplan priority */
	int priority;
	/*! \brief Result of the dial operation when dialed is set */
	int dial_res;
	/*! \brief Set when dialing is completed */
	unsigned int dialed:1;
	/*! \brief Set if we've spawned a thread to do our work */
	unsigned int in_separate_thread:1;
};

/*! \brief Destructor for outgoing structure */
static void pbx_outgoing_destroy(void *obj)
{
	struct pbx_outgoing *outgoing = obj;

	if (outgoing->dial) {
		ast_dial_destroy(outgoing->dial);
	}

	ast_cond_destroy(&outgoing->cond);

	ast_free(outgoing->appdata);
}

/*! \brief Internal function which dials an outgoing leg and sends it to a provided extension or application */
static void *pbx_outgoing_exec(void *data)
{
	RAII_VAR(struct pbx_outgoing *, outgoing, data, ao2_cleanup);
	enum ast_dial_result res;
	struct ast_channel *chan;

	res = ast_dial_run(outgoing->dial, NULL, 0);

	if (outgoing->in_separate_thread) {
		/* Notify anyone interested that dialing is complete */
		ao2_lock(outgoing);
		outgoing->dial_res = res;
		outgoing->dialed = 1;
		ast_cond_signal(&outgoing->cond);
		ao2_unlock(outgoing);
	} else {
		/* We still need the dial result, but we don't need to lock */
		outgoing->dial_res = res;
	}

	/* If the outgoing leg was not answered we can immediately return and go no further */
	if (res != AST_DIAL_RESULT_ANSWERED) {
		return NULL;
	}

	/* We steal the channel so we get ownership of when it is hung up */
	chan = ast_dial_answered_steal(outgoing->dial);

	if (!ast_strlen_zero(outgoing->app)) {
		struct ast_app *app = pbx_findapp(outgoing->app);

		if (app) {
			ast_verb(4, "Launching %s(%s) on %s\n", outgoing->app, S_OR(outgoing->appdata, ""),
				ast_channel_name(chan));
			pbx_exec(chan, app, outgoing->appdata);
		} else {
			ast_log(LOG_WARNING, "No such application '%s'\n", outgoing->app);
		}

		ast_hangup(chan);
	} else {
		if (!ast_strlen_zero(outgoing->context)) {
			ast_channel_context_set(chan, outgoing->context);
		}

		if (!ast_strlen_zero(outgoing->exten)) {
			ast_channel_exten_set(chan, outgoing->exten);
		}

		if (outgoing->priority > 0) {
			ast_channel_priority_set(chan, outgoing->priority);
		}

		if (ast_pbx_run(chan)) {
			ast_log(LOG_ERROR, "Failed to start PBX on %s\n", ast_channel_name(chan));
			ast_hangup(chan);
		}
	}

	return NULL;
}

/*! \brief Internal dialing state callback which causes early media to trigger an answer */
static void pbx_outgoing_state_callback(struct ast_dial *dial)
{
	struct ast_channel *channel;

	if (ast_dial_state(dial) != AST_DIAL_RESULT_PROGRESS) {
		return;
	}

	if (!(channel = ast_dial_get_channel(dial, 0))) {
		return;
	}

	ast_verb(4, "Treating progress as answer on '%s' due to early media option\n",
		ast_channel_name(channel));

	ast_queue_control(channel, AST_CONTROL_ANSWER);
}

/*!
 * \brief Attempt to convert disconnect cause to old originate reason.
 *
 * \todo XXX The old originate reasons need to be trashed and replaced
 * with normal disconnect cause codes if the call was not answered.
 * The internal consumers of the reason values would also need to be
 * updated: app_originate, call files, and AMI OriginateResponse.
 */
static enum ast_control_frame_type pbx_dial_reason(enum ast_dial_result dial_result, int cause)
{
	enum ast_control_frame_type pbx_reason;

	if (dial_result == AST_DIAL_RESULT_ANSWERED) {
		/* Remote end answered. */
		pbx_reason = AST_CONTROL_ANSWER;
	} else if (dial_result == AST_DIAL_RESULT_HANGUP) {
		/* Caller hungup */
		pbx_reason = AST_CONTROL_HANGUP;
	} else {
		switch (cause) {
		case AST_CAUSE_USER_BUSY:
			pbx_reason = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CALL_REJECTED:
		case AST_CAUSE_NETWORK_OUT_OF_ORDER:
		case AST_CAUSE_DESTINATION_OUT_OF_ORDER:
		case AST_CAUSE_NORMAL_TEMPORARY_FAILURE:
		case AST_CAUSE_SWITCH_CONGESTION:
		case AST_CAUSE_NORMAL_CIRCUIT_CONGESTION:
			pbx_reason = AST_CONTROL_CONGESTION;
			break;
		case AST_CAUSE_ANSWERED_ELSEWHERE:
		case AST_CAUSE_NO_ANSWER:
			/* Remote end was ringing (but isn't anymore) */
			pbx_reason = AST_CONTROL_RINGING;
			break;
		case AST_CAUSE_UNALLOCATED:
		default:
			/* Call Failure (not BUSY, and not NO_ANSWER, maybe Circuit busy or down?) */
			pbx_reason = 0;
			break;
		}
	}

	return pbx_reason;
}

static int pbx_outgoing_attempt(const char *type, struct ast_format_cap *cap,
	const char *addr, int timeout, const char *context, const char *exten, int priority,
	const char *app, const char *appdata, int *reason, int synchronous,
	const char *cid_num, const char *cid_name, struct ast_variable *vars,
	const char *account, struct ast_channel **locked_channel, int early_media,
	const struct ast_assigned_ids *assignedids, const char *predial_callee)
{
	RAII_VAR(struct pbx_outgoing *, outgoing, NULL, ao2_cleanup);
	struct ast_channel *dialed;
	pthread_t thread;
	char tmp_cid_name[128];
	char tmp_cid_num[128];

	outgoing = ao2_alloc(sizeof(*outgoing), pbx_outgoing_destroy);
	if (!outgoing) {
		return -1;
	}
	ast_cond_init(&outgoing->cond, NULL);

	if (!ast_strlen_zero(app)) {
		ast_copy_string(outgoing->app, app, sizeof(outgoing->app));
		outgoing->appdata = ast_strdup(appdata);
	} else {
		ast_copy_string(outgoing->context, context, sizeof(outgoing->context));
		ast_copy_string(outgoing->exten, exten, sizeof(outgoing->exten));
		outgoing->priority = priority;
	}

	if (!(outgoing->dial = ast_dial_create())) {
		return -1;
	}

	if (ast_dial_append(outgoing->dial, type, addr, assignedids)) {
		return -1;
	}

	ast_dial_set_global_timeout(outgoing->dial, timeout);

	if (!ast_strlen_zero(predial_callee)) {
		/* note casting to void * here to suppress compiler warning message (passing const to non-const function) */
		ast_dial_option_global_enable(outgoing->dial, AST_DIAL_OPTION_PREDIAL, (void *)predial_callee);
	}

	if (ast_dial_prerun(outgoing->dial, NULL, cap)) {
		if (synchronous && reason) {
			*reason = pbx_dial_reason(AST_DIAL_RESULT_FAILED,
				ast_dial_reason(outgoing->dial, 0));
		}
		return -1;
	}

	dialed = ast_dial_get_channel(outgoing->dial, 0);
	if (!dialed) {
		return -1;
	}

	ast_channel_lock(dialed);
	if (vars) {
		ast_set_variables(dialed, vars);
	}
	if (!ast_strlen_zero(account)) {
		ast_channel_stage_snapshot(dialed);
		ast_channel_accountcode_set(dialed, account);
		ast_channel_peeraccount_set(dialed, account);
		ast_channel_stage_snapshot_done(dialed);
	}
	ast_set_flag(ast_channel_flags(dialed), AST_FLAG_ORIGINATED);

	if (!ast_strlen_zero(predial_callee)) {
		char *tmp = NULL;
		/*
		 * The predial sub routine may have set callerid so set this into the new channel
		 * Note... cid_num and cid_name parameters to this function will always be NULL if
		 * predial_callee is non-NULL so we are not overwriting anything here.
		 */
		tmp = S_COR(ast_channel_caller(dialed)->id.number.valid, ast_channel_caller(dialed)->id.number.str, NULL);
		if (tmp) {
			ast_copy_string(tmp_cid_num, tmp, sizeof(tmp_cid_num));
			cid_num = tmp_cid_num;
		}
		tmp = S_COR(ast_channel_caller(dialed)->id.name.valid, ast_channel_caller(dialed)->id.name.str, NULL);
		if (tmp) {
			ast_copy_string(tmp_cid_name, tmp, sizeof(tmp_cid_name));
			cid_name = tmp_cid_name;
		}
	}
	ast_channel_unlock(dialed);

	if (!ast_strlen_zero(cid_num) || !ast_strlen_zero(cid_name)) {
		struct ast_party_connected_line connected;

		/*
		 * It seems strange to set the CallerID on an outgoing call leg
		 * to whom we are calling, but this function's callers are doing
		 * various Originate methods.  This call leg goes to the local
		 * user.  Once the called party answers, the dialplan needs to
		 * be able to access the CallerID from the CALLERID function as
		 * if the called party had placed this call.
		 */
		ast_set_callerid(dialed, cid_num, cid_name, cid_num);

		ast_party_connected_line_set_init(&connected, ast_channel_connected(dialed));
		if (!ast_strlen_zero(cid_num)) {
			connected.id.number.valid = 1;
			connected.id.number.str = (char *) cid_num;
			connected.id.number.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		}
		if (!ast_strlen_zero(cid_name)) {
			connected.id.name.valid = 1;
			connected.id.name.str = (char *) cid_name;
			connected.id.name.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		}
		ast_channel_set_connected_line(dialed, &connected, NULL);
	}

	if (early_media) {
		ast_dial_set_state_callback(outgoing->dial, pbx_outgoing_state_callback);
	}

	if (locked_channel) {
		/*
		 * Keep a dialed channel ref since the caller wants
		 * the channel returned.  We must get the ref before
		 * spawning off pbx_outgoing_exec().
		 */
		ast_channel_ref(dialed);
		if (!synchronous) {
			/*
			 * Lock it now to hold off pbx_outgoing_exec() in case the
			 * calling function needs the channel state/snapshot before
			 * dialing actually happens.
			 */
			ast_channel_lock(dialed);
		}
	}

	/* This extra reference is dereferenced by pbx_outgoing_exec */
	ao2_ref(outgoing, +1);

	if (synchronous == AST_OUTGOING_WAIT_COMPLETE) {
		/*
		 * Because we are waiting until this is complete anyway, there is no
		 * sense in creating another thread that we will just need to wait
		 * for, so instead we commandeer the current thread.
		 */
		pbx_outgoing_exec(outgoing);
	} else {
		outgoing->in_separate_thread = 1;

		if (ast_pthread_create_detached(&thread, NULL, pbx_outgoing_exec, outgoing)) {
			ast_log(LOG_WARNING, "Unable to spawn dialing thread for '%s/%s'\n", type, addr);
			ao2_ref(outgoing, -1);
			if (locked_channel) {
				if (!synchronous) {
					ast_channel_unlock(dialed);
				}
				ast_channel_unref(dialed);
			}
			return -1;
		}

		if (synchronous) {
			ao2_lock(outgoing);
			/* Wait for dialing to complete */
			while (!outgoing->dialed) {
				ast_cond_wait(&outgoing->cond, ao2_object_get_lockaddr(outgoing));
			}
			ao2_unlock(outgoing);
		}
	}

	if (synchronous) {
		/* Determine the outcome of the dialing attempt up to it being answered. */
		if (reason) {
			*reason = pbx_dial_reason(outgoing->dial_res,
				ast_dial_reason(outgoing->dial, 0));
		}

		if (outgoing->dial_res != AST_DIAL_RESULT_ANSWERED) {
			/* The dial operation failed. */
			if (locked_channel) {
				ast_channel_unref(dialed);
			}
			return -1;
		}
		if (locked_channel) {
			ast_channel_lock(dialed);
		}
	}

	if (locked_channel) {
		*locked_channel = dialed;
	}
	return 0;
}

int ast_pbx_outgoing_exten(const char *type, struct ast_format_cap *cap, const char *addr,
	int timeout, const char *context, const char *exten, int priority, int *reason,
	int synchronous, const char *cid_num, const char *cid_name, struct ast_variable *vars,
	const char *account, struct ast_channel **locked_channel, int early_media,
	const struct ast_assigned_ids *assignedids)
{
	return ast_pbx_outgoing_exten_predial(type, cap, addr, timeout, context, exten, priority, reason,
		synchronous, cid_num, cid_name, vars, account, locked_channel, early_media, assignedids, NULL);
}

int ast_pbx_outgoing_exten_predial(const char *type, struct ast_format_cap *cap, const char *addr,
	int timeout, const char *context, const char *exten, int priority, int *reason,
	int synchronous, const char *cid_num, const char *cid_name, struct ast_variable *vars,
	const char *account, struct ast_channel **locked_channel, int early_media,
	const struct ast_assigned_ids *assignedids, const char *predial_callee)
{
	int res;
	int my_reason;

	if (!reason) {
		reason = &my_reason;
	}
	*reason = 0;
	if (locked_channel) {
		*locked_channel = NULL;
	}

	res = pbx_outgoing_attempt(type, cap, addr, timeout, context, exten, priority,
		NULL, NULL, reason, synchronous, cid_num, cid_name, vars, account, locked_channel,
		early_media, assignedids, predial_callee);

	if (res < 0 /* Call failed to get connected for some reason. */
		&& 0 < synchronous
		&& ast_exists_extension(NULL, context, "failed", 1, NULL)) {
		struct ast_channel *failed;

		/* We do not have to worry about a locked_channel if dialing failed. */
		ast_assert(!locked_channel || !*locked_channel);

		/*!
		 * \todo XXX Not good.  The channel name is not unique if more than
		 * one originate fails at a time.
		 */
		failed = ast_channel_alloc(0, AST_STATE_DOWN, cid_num, cid_name, account,
			"failed", context, NULL, NULL, 0, "OutgoingSpoolFailed");
		if (failed) {
			char failed_reason[12];

			ast_set_variables(failed, vars);
			snprintf(failed_reason, sizeof(failed_reason), "%d", *reason);
			pbx_builtin_setvar_helper(failed, "REASON", failed_reason);
			ast_channel_unlock(failed);

			if (ast_pbx_run(failed)) {
				ast_log(LOG_ERROR, "Unable to run PBX on '%s'\n",
					ast_channel_name(failed));
				ast_hangup(failed);
			}
		}
	}

	return res;
}

int ast_pbx_outgoing_app(const char *type, struct ast_format_cap *cap, const char *addr,
	int timeout, const char *app, const char *appdata, int *reason, int synchronous,
	const char *cid_num, const char *cid_name, struct ast_variable *vars,
	const char *account, struct ast_channel **locked_channel,
	const struct ast_assigned_ids *assignedids)
{
	return ast_pbx_outgoing_app_predial(type, cap, addr, timeout, app, appdata, reason, synchronous,
		cid_num, cid_name, vars, account, locked_channel, assignedids, NULL);
}

int ast_pbx_outgoing_app_predial(const char *type, struct ast_format_cap *cap, const char *addr,
	int timeout, const char *app, const char *appdata, int *reason, int synchronous,
	const char *cid_num, const char *cid_name, struct ast_variable *vars,
	const char *account, struct ast_channel **locked_channel,
	const struct ast_assigned_ids *assignedids, const char *predial_callee)
{
	if (reason) {
		*reason = 0;
	}
	if (locked_channel) {
		*locked_channel = NULL;
	}
	if (ast_strlen_zero(app)) {
		return -1;
	}

	return pbx_outgoing_attempt(type, cap, addr, timeout, NULL, NULL, 0, app, appdata,
		reason, synchronous, cid_num, cid_name, vars, account, locked_channel, 0,
		assignedids, predial_callee);
}

/* this is the guts of destroying a context --
   freeing up the structure, traversing and destroying the
   extensions, switches, ignorepats, includes, etc. etc. */

static void __ast_internal_context_destroy( struct ast_context *con)
{
	struct ast_exten *e, *el, *en;
	struct ast_context *tmp = con;

	/* Free includes */
	AST_VECTOR_CALLBACK_VOID(&tmp->includes, include_free);
	AST_VECTOR_FREE(&tmp->includes);

	/* Free ignorepats */
	AST_VECTOR_CALLBACK_VOID(&tmp->ignorepats, ignorepat_free);
	AST_VECTOR_FREE(&tmp->ignorepats);

	/* Free switches */
	AST_VECTOR_CALLBACK_VOID(&tmp->alts, sw_free);
	AST_VECTOR_FREE(&tmp->alts);

	if (tmp->registrar)
		ast_free(tmp->registrar);

	/* destroy the hash tabs */
	if (tmp->root_table) {
		ast_hashtab_destroy(tmp->root_table, 0);
	}
	/* and destroy the pattern tree */
	if (tmp->pattern_tree)
		destroy_pattern_tree(tmp->pattern_tree);

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
	tmp->root = NULL;
	ast_rwlock_destroy(&tmp->lock);
	ast_mutex_destroy(&tmp->macrolock);
	ast_free(tmp);
}


void __ast_context_destroy(struct ast_context *list, struct ast_hashtab *contexttab, struct ast_context *con, const char *registrar)
{
	struct ast_context *tmp, *tmpl=NULL;
	struct ast_exten *exten_item, *prio_item;

	for (tmp = list; tmp; ) {
		struct ast_context *next = NULL;	/* next starting point */
			/* The following code used to skip forward to the next
			   context with matching registrar, but this didn't
			   make sense; individual priorities registrar'd to
			   the matching registrar could occur in any context! */
		ast_debug(1, "Investigate ctx %s %s\n", tmp->name, tmp->registrar);
		if (con) {
			for (; tmp; tmpl = tmp, tmp = tmp->next) { /* skip to the matching context */
				ast_debug(1, "check ctx %s %s\n", tmp->name, tmp->registrar);
				if ( !strcasecmp(tmp->name, con->name) ) {
					break;	/* found it */
				}
			}
		}

		if (!tmp)	/* not found, we are done */
			break;
		ast_wrlock_context(tmp);

		if (registrar) {
			/* then search thru and remove any extens that match registrar. */
			struct ast_hashtab_iter *exten_iter;
			struct ast_hashtab_iter *prio_iter;
			int idx;

			/* remove any ignorepats whose registrar matches */
			for (idx = ast_context_ignorepats_count(tmp) - 1; idx >= 0; idx--) {
				struct ast_ignorepat *ip = AST_VECTOR_GET(&tmp->ignorepats, idx);

				if (!strcmp(ast_get_ignorepat_registrar(ip), registrar)) {
					AST_VECTOR_REMOVE_ORDERED(&tmp->ignorepats, idx);
					ignorepat_free(ip);
				}
			}
			/* remove any includes whose registrar matches */
			for (idx = ast_context_includes_count(tmp) - 1; idx >= 0; idx--) {
				struct ast_include *i = AST_VECTOR_GET(&tmp->includes, idx);

				if (!strcmp(ast_get_include_registrar(i), registrar)) {
					AST_VECTOR_REMOVE_ORDERED(&tmp->includes, idx);
					include_free(i);
				}
			}
			/* remove any switches whose registrar matches */
			for (idx = ast_context_switches_count(tmp) - 1; idx >= 0; idx--) {
				struct ast_sw *sw = AST_VECTOR_GET(&tmp->alts, idx);

				if (!strcmp(ast_get_switch_registrar(sw), registrar)) {
					AST_VECTOR_REMOVE_ORDERED(&tmp->alts, idx);
					sw_free(sw);
				}
			}

			if (tmp->root_table) { /* it is entirely possible that the context is EMPTY */
				exten_iter = ast_hashtab_start_traversal(tmp->root_table);
				while ((exten_item=ast_hashtab_next(exten_iter))) {
					int end_traversal = 1;

					/*
					 * If the extension could not be removed from the root_table due to
					 * a loaded PBX app, it can exist here but have its peer_table be
					 * destroyed due to a previous pass through this function.
					 */
					if (!exten_item->peer_table) {
						continue;
					}

					prio_iter = ast_hashtab_start_traversal(exten_item->peer_table);
					while ((prio_item=ast_hashtab_next(prio_iter))) {
						char extension[AST_MAX_EXTENSION];
						char cidmatch[AST_MAX_EXTENSION];
						if (!prio_item->registrar || strcmp(prio_item->registrar, registrar) != 0) {
							continue;
						}
						ast_verb(3, "Remove %s/%s/%d, registrar=%s; con=%s(%p); con->root=%p\n",
								 tmp->name, prio_item->name, prio_item->priority, registrar, con? con->name : "<nil>", con, con? con->root_table: NULL);
						ast_copy_string(extension, prio_item->exten, sizeof(extension));
						if (prio_item->cidmatch) {
							ast_copy_string(cidmatch, prio_item->cidmatch, sizeof(cidmatch));
						}
						end_traversal &= ast_context_remove_extension_callerid2(tmp, extension, prio_item->priority, cidmatch, prio_item->matchcid, NULL, 1);
					}
					/* Explanation:
					 * ast_context_remove_extension_callerid2 will destroy the extension that it comes across. This
					 * destruction includes destroying the exten's peer_table, which we are currently traversing. If
					 * ast_context_remove_extension_callerid2 ever should return '0' then this means we have destroyed
					 * the hashtable which we are traversing, and thus calling ast_hashtab_end_traversal will result
					 * in reading invalid memory. Thus, if we detect that we destroyed the hashtable, then we will simply
					 * free the iterator
					 */
					if (end_traversal) {
						ast_hashtab_end_traversal(prio_iter);
					} else {
						ast_free(prio_iter);
					}
				}
				ast_hashtab_end_traversal(exten_iter);
			}

			/* delete the context if it's registrar matches, is empty, has refcount of 1, */
			/* it's not empty, if it has includes, ignorepats, or switches that are registered from
			   another registrar. It's not empty if there are any extensions */
			if (strcmp(tmp->registrar, registrar) == 0 && tmp->refcount < 2 && !tmp->root && !ast_context_ignorepats_count(tmp) && !ast_context_includes_count(tmp) && !ast_context_switches_count(tmp)) {
				ast_debug(1, "delete ctx %s %s\n", tmp->name, tmp->registrar);
				ast_hashtab_remove_this_object(contexttab, tmp);

				next = tmp->next;
				if (tmpl)
					tmpl->next = next;
				else
					contexts = next;
				/* Okay, now we're safe to let it go -- in a sense, we were
				   ready to let it go as soon as we locked it. */
				ast_unlock_context(tmp);
				__ast_internal_context_destroy(tmp);
			} else {
				ast_debug(1,"Couldn't delete ctx %s/%s; refc=%d; tmp.root=%p\n", tmp->name, tmp->registrar,
						  tmp->refcount, tmp->root);
				ast_unlock_context(tmp);
				next = tmp->next;
				tmpl = tmp;
			}
		} else if (con) {
			ast_verb(3, "Deleting context %s registrar=%s\n", tmp->name, tmp->registrar);
			ast_debug(1, "delete ctx %s %s\n", tmp->name, tmp->registrar);
			ast_hashtab_remove_this_object(contexttab, tmp);

			next = tmp->next;
			if (tmpl)
				tmpl->next = next;
			else
				contexts = next;
			/* Okay, now we're safe to let it go -- in a sense, we were
			   ready to let it go as soon as we locked it. */
			ast_unlock_context(tmp);
			__ast_internal_context_destroy(tmp);
		}

		/* if we have a specific match, we are done, otherwise continue */
		tmp = con ? NULL : next;
	}
}

int ast_context_destroy_by_name(const char *context, const char *registrar)
{
	struct ast_context *con;
	int ret = -1;

	ast_wrlock_contexts();
	con = ast_context_find(context);
	if (con) {
		ast_context_destroy(con, registrar);
		ret = 0;
	}
	ast_unlock_contexts();

	return ret;
}

void ast_context_destroy(struct ast_context *con, const char *registrar)
{
	ast_wrlock_contexts();
	__ast_context_destroy(contexts, contexts_table, con,registrar);
	ast_unlock_contexts();
}

void wait_for_hangup(struct ast_channel *chan, const void *data)
{
	int res;
	struct ast_frame *f;
	double waitsec;
	int waittime;

	if (ast_strlen_zero(data) || (sscanf(data, "%30lg", &waitsec) != 1) || (waitsec < 0))
		waitsec = -1;
	if (waitsec > -1) {
		waittime = waitsec * 1000.0;
		ast_safe_sleep(chan, waittime);
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
 * \ingroup functions
 */
static int testtime_write(struct ast_channel *chan, const char *cmd, char *var, const char *value)
{
	struct ast_tm tm;
	struct timeval tv;
	char *remainder, result[30], timezone[80];

	/* Turn off testing? */
	if (!pbx_checkcondition(value)) {
		pbx_builtin_setvar_helper(chan, "TESTTIME", NULL);
		return 0;
	}

	/* Parse specified time */
	if (!(remainder = ast_strptime(value, "%Y/%m/%d %H:%M:%S", &tm))) {
		return -1;
	}
	sscanf(remainder, "%79s", timezone);
	tv = ast_mktime(&tm, S_OR(timezone, NULL));

	snprintf(result, sizeof(result), "%ld", (long) tv.tv_sec);
	pbx_builtin_setvar_helper(chan, "__TESTTIME", result);
	return 0;
}

static struct ast_custom_function testtime_function = {
	.name = "TESTTIME",
	.write = testtime_write,
};

int pbx_checkcondition(const char *condition)
{
	int res;
	if (ast_strlen_zero(condition)) {                /* NULL or empty strings are false */
		return 0;
	} else if (sscanf(condition, "%30d", &res) == 1) { /* Numbers are evaluated for truth */
		return res;
	} else {                                         /* Strings are true */
		return 1;
	}
}

static void presence_state_cb(void *unused, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_presence_state_message *presence_state;
	struct ast_str *hint_app = NULL;
	struct ast_hintdevice *device;
	struct ast_hintdevice *cmpdevice;
	struct ao2_iterator *dev_iter;

	if (stasis_message_type(msg) != ast_presence_state_message_type()) {
		return;
	}

	presence_state = stasis_message_data(msg);

	if (ao2_container_count(hintdevices) == 0) {
		/* There are no hints monitoring devices. */
		return;
	}

	hint_app = ast_str_create(1024);
	if (!hint_app) {
		return;
	}

	cmpdevice = ast_alloca(sizeof(*cmpdevice) + strlen(presence_state->provider));
	strcpy(cmpdevice->hintdevice, presence_state->provider);

	ast_mutex_lock(&context_merge_lock);/* Hold off ast_merge_contexts_and_delete */
	dev_iter = ao2_t_callback(hintdevices,
		OBJ_POINTER | OBJ_MULTIPLE,
		hintdevice_cmp_multiple,
		cmpdevice,
		"find devices in container");
	if (!dev_iter) {
		ast_mutex_unlock(&context_merge_lock);
		ast_free(hint_app);
		return;
	}

	for (; (device = ao2_iterator_next(dev_iter)); ao2_t_ref(device, -1, "Next device")) {
		if (device->hint) {
			presence_state_notify_callbacks(device->hint, &hint_app, presence_state);
		}
	}
	ao2_iterator_destroy(dev_iter);
	ast_mutex_unlock(&context_merge_lock);

	ast_free(hint_app);
}

static int action_extensionstatelist(struct mansession *s, const struct message *m)
{
	const char *action_id = astman_get_header(m, "ActionID");
	struct ast_hint *hint;
	struct ao2_iterator it_hints;
	int hint_count = 0;

	if (!hints) {
		astman_send_error(s, m, "No dialplan hints are available");
		return 0;
	}

	astman_send_listack(s, m, "Extension Statuses will follow", "start");

	ao2_lock(hints);
	it_hints = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&it_hints)); ao2_ref(hint, -1)) {

		ao2_lock(hint);

		/* Ignore pattern matching hints; they are stored in the
		 * hints container but aren't real from the perspective of
		 * an AMI user
		 */
		if (hint->exten->exten[0] == '_') {
			ao2_unlock(hint);
			continue;
		}

		++hint_count;

		astman_append(s, "Event: ExtensionStatus\r\n");
		if (!ast_strlen_zero(action_id)) {
			astman_append(s, "ActionID: %s\r\n", action_id);
		}
		astman_append(s,
		   "Exten: %s\r\n"
		   "Context: %s\r\n"
		   "Hint: %s\r\n"
		   "Status: %d\r\n"
		   "StatusText: %s\r\n\r\n",
		   hint->exten->exten,
		   hint->exten->parent->name,
		   hint->exten->app,
		   hint->laststate,
		   ast_extension_state2str(hint->laststate));
		ao2_unlock(hint);
	}

	ao2_iterator_destroy(&it_hints);
	ao2_unlock(hints);

	astman_send_list_complete_start(s, m, "ExtensionStateListComplete", hint_count);
	astman_send_list_complete_end(s);

	return 0;
}


/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown.
 *
 * \note Cleans up resources allocated in load_pbx
 */
static void unload_pbx(void)
{
	presence_state_sub = stasis_unsubscribe_and_join(presence_state_sub);
	device_state_sub = stasis_unsubscribe_and_join(device_state_sub);

	ast_manager_unregister("ShowDialPlan");
	ast_manager_unregister("ExtensionStateList");
	ast_cli_unregister_multiple(pbx_cli, ARRAY_LEN(pbx_cli));
	ast_custom_function_unregister(&exception_function);
	ast_custom_function_unregister(&testtime_function);
}

int load_pbx(void)
{
	int res = 0;

	ast_register_cleanup(unload_pbx);

	/* Initialize the PBX */
	ast_verb(1, "Asterisk PBX Core Initializing\n");

	ast_verb(2, "Registering builtin functions:\n");
	ast_cli_register_multiple(pbx_cli, ARRAY_LEN(pbx_cli));
	__ast_custom_function_register(&exception_function, NULL);
	__ast_custom_function_register(&testtime_function, NULL);

	/* Register manager application */
	res |= ast_manager_register_xml_core("ShowDialPlan", EVENT_FLAG_CONFIG | EVENT_FLAG_REPORTING, manager_show_dialplan);
	res |= ast_manager_register_xml_core("ExtensionStateList", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_extensionstatelist);

	if (res) {
		return -1;
	}

	if (!(device_state_sub = stasis_subscribe(ast_device_state_topic_all(), device_state_cb, NULL))) {
		return -1;
	}
	stasis_subscription_accept_message_type(device_state_sub, ast_device_state_message_type());
	stasis_subscription_accept_message_type(device_state_sub, hint_change_message_type());
	stasis_subscription_accept_message_type(device_state_sub, hint_remove_message_type());
	stasis_subscription_set_filter(device_state_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	if (!(presence_state_sub = stasis_subscribe(ast_presence_state_topic_all(), presence_state_cb, NULL))) {
		return -1;
	}
	stasis_subscription_accept_message_type(presence_state_sub, ast_presence_state_message_type());
	stasis_subscription_set_filter(presence_state_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	return 0;
}

/*
 * Lock context list functions ...
 */
int ast_wrlock_contexts(void)
{
	return ast_mutex_lock(&conlock);
}

int ast_rdlock_contexts(void)
{
	return ast_mutex_lock(&conlock);
}

int ast_unlock_contexts(void)
{
	return ast_mutex_unlock(&conlock);
}

/*
 * Lock context ...
 */
int ast_wrlock_context(struct ast_context *con)
{
	return ast_rwlock_wrlock(&con->lock);
}

int ast_rdlock_context(struct ast_context *con)
{
	return ast_rwlock_rdlock(&con->lock);
}

int ast_unlock_context(struct ast_context *con)
{
	return ast_rwlock_unlock(&con->lock);
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
	return exten ? exten->name : NULL;
}

const char *ast_get_extension_label(struct ast_exten *exten)
{
	return exten ? exten->label : NULL;
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

const char *ast_get_extension_registrar_file(struct ast_exten *e)
{
	return e ? e->registrar_file : NULL;
}

int ast_get_extension_registrar_line(struct ast_exten *e)
{
	return e ? e->registrar_line : 0;
}

int ast_get_extension_matchcid(struct ast_exten *e)
{
	return e ? e->matchcid : 0;
}

const char *ast_get_extension_cidmatch(struct ast_exten *e)
{
	return e ? e->cidmatch_display : NULL;
}

const char *ast_get_extension_app(struct ast_exten *e)
{
	return e ? e->app : NULL;
}

void *ast_get_extension_app_data(struct ast_exten *e)
{
	return e ? e->data : NULL;
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

const struct ast_sw *ast_walk_context_switches(const struct ast_context *con,
	const struct ast_sw *sw)
{
	if (sw) {
		int idx;
		int next = 0;

		for (idx = 0; idx < ast_context_switches_count(con); idx++) {
			const struct ast_sw *s = ast_context_switches_get(con, idx);

			if (next) {
				return s;
			}

			if (sw == s) {
				next = 1;
			}
		}

		return NULL;
	}

	if (!ast_context_switches_count(con)) {
		return NULL;
	}

	return ast_context_switches_get(con, 0);
}

int ast_context_switches_count(const struct ast_context *con)
{
	return AST_VECTOR_SIZE(&con->alts);
}

const struct ast_sw *ast_context_switches_get(const struct ast_context *con, int idx)
{
	return AST_VECTOR_GET(&con->alts, idx);
}

struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority)
{
	return priority ? priority->peer : exten;
}

const struct ast_include *ast_walk_context_includes(const struct ast_context *con,
	const struct ast_include *inc)
{
	if (inc) {
		int idx;
		int next = 0;

		for (idx = 0; idx < ast_context_includes_count(con); idx++) {
			const struct ast_include *include = AST_VECTOR_GET(&con->includes, idx);

			if (next) {
				return include;
			}

			if (inc == include) {
				next = 1;
			}
		}

		return NULL;
	}

	if (!ast_context_includes_count(con)) {
		return NULL;
	}

	return ast_context_includes_get(con, 0);
}

int ast_context_includes_count(const struct ast_context *con)
{
	return AST_VECTOR_SIZE(&con->includes);
}

const struct ast_include *ast_context_includes_get(const struct ast_context *con, int idx)
{
	return AST_VECTOR_GET(&con->includes, idx);
}

const struct ast_ignorepat *ast_walk_context_ignorepats(const struct ast_context *con,
	const struct ast_ignorepat *ip)
{
	if (!con) {
		return NULL;
	}

	if (ip) {
		int idx;
		int next = 0;

		for (idx = 0; idx < ast_context_ignorepats_count(con); idx++) {
			const struct ast_ignorepat *i = ast_context_ignorepats_get(con, idx);

			if (next) {
				return i;
			}

			if (ip == i) {
				next = 1;
			}
		}

		return NULL;
	}

	if (!ast_context_ignorepats_count(con)) {
		return NULL;
	}

	return ast_context_ignorepats_get(con, 0);
}

int ast_context_ignorepats_count(const struct ast_context *con)
{
	return AST_VECTOR_SIZE(&con->ignorepats);
}

const struct ast_ignorepat *ast_context_ignorepats_get(const struct ast_context *con, int idx)
{
	return AST_VECTOR_GET(&con->ignorepats, idx);
}

int ast_context_verify_includes(struct ast_context *con)
{
	int idx;
	int res = 0;

	for (idx = 0; idx < ast_context_includes_count(con); idx++) {
		const struct ast_include *inc = ast_context_includes_get(con, idx);

		if (ast_context_find(include_rname(inc))) {
			continue;
		}

		res = -1;
		ast_log(LOG_WARNING, "Context '%s' tries to include nonexistent context '%s'\n",
			ast_get_context_name(con), include_rname(inc));
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
		context = ast_channel_context(chan);
	if (exten == NULL)
		exten = ast_channel_exten(chan);

	goto_func = (async) ? ast_async_goto : ast_explicit_goto;
	if (ast_exists_extension(chan, context, exten, priority,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL)))
		return goto_func(chan, context, exten, priority);
	else {
		return AST_PBX_GOTO_FAILED;
	}
}

int ast_goto_if_exists(struct ast_channel *chan, const char* context, const char *exten, int priority)
{
	return __ast_goto_if_exists(chan, context, exten, priority, 0);
}

int ast_async_goto_if_exists(struct ast_channel *chan, const char * context, const char *exten, int priority)
{
	return __ast_goto_if_exists(chan, context, exten, priority, 1);
}

static int pbx_parseable_goto(struct ast_channel *chan, const char *goto_string, int async)
{
	char *exten, *pri, *context;
	char *stringp;
	int ipri;
	int mode = 0;
	char rest[2] = "";

	if (ast_strlen_zero(goto_string)) {
		ast_log(LOG_WARNING, "Goto requires an argument ([[context,]extension,]priority)\n");
		return -1;
	}
	stringp = ast_strdupa(goto_string);
	context = strsep(&stringp, ",");	/* guaranteed non-null */
	exten = strsep(&stringp, ",");
	pri = strsep(&stringp, ",");
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
	if (sscanf(pri, "%30d%1s", &ipri, rest) != 1) {
		ipri = ast_findlabel_extension(chan, context ? context : ast_channel_context(chan),
			exten ? exten : ast_channel_exten(chan), pri,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL));
		if (ipri < 1) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0, or valid label\n", pri);
			return -1;
		} else
			mode = 0;
	}
	/* At this point we have a priority and maybe an extension and a context */

	if (mode)
		ipri = ast_channel_priority(chan) + (ipri * mode);

	if (async)
		ast_async_goto(chan, context, exten, ipri);
	else
		ast_explicit_goto(chan, context, exten, ipri);

	return 0;

}

int ast_parseable_goto(struct ast_channel *chan, const char *goto_string)
{
	return pbx_parseable_goto(chan, goto_string, 0);
}

int ast_async_parseable_goto(struct ast_channel *chan, const char *goto_string)
{
	return pbx_parseable_goto(chan, goto_string, 1);
}

static int hint_hash(const void *obj, const int flags)
{
	const struct ast_hint *hint = obj;
	const char *exten_name;
	int res;

	exten_name = ast_get_extension_name(hint->exten);
	if (ast_strlen_zero(exten_name)) {
		/*
		 * If the exten or extension name isn't set, return 0 so that
		 * the ao2_find() search will start in the first bucket.
		 */
		res = 0;
	} else {
		res = ast_str_case_hash(exten_name);
	}

	return res;
}

static int hint_cmp(void *obj, void *arg, int flags)
{
	const struct ast_hint *hint = obj;
	const struct ast_exten *exten = arg;

	return (hint->exten == exten) ? CMP_MATCH | CMP_STOP : 0;
}

static int statecbs_cmp(void *obj, void *arg, int flags)
{
	const struct ast_state_cb *state_cb = obj;
	ast_state_cb_type change_cb = arg;

	return (state_cb->change_cb == change_cb) ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void pbx_shutdown(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(hint_change_message_type);
	STASIS_MESSAGE_TYPE_CLEANUP(hint_remove_message_type);

	if (hints) {
		ao2_container_unregister("hints");
		ao2_ref(hints, -1);
		hints = NULL;
	}
	if (hintdevices) {
		ao2_container_unregister("hintdevices");
		ao2_ref(hintdevices, -1);
		hintdevices = NULL;
	}
	if (autohints) {
		ao2_container_unregister("autohints");
		ao2_ref(autohints, -1);
		autohints = NULL;
	}
	if (statecbs) {
		ao2_container_unregister("statecbs");
		ao2_ref(statecbs, -1);
		statecbs = NULL;
	}
	if (contexts_table) {
		ast_hashtab_destroy(contexts_table, NULL);
	}
}

static void print_hints_key(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct ast_hint *hint = v_obj;

	if (!hint) {
		return;
	}
	prnt(where, "%s@%s", ast_get_extension_name(hint->exten),
		ast_get_context_name(ast_get_extension_context(hint->exten)));
}

static void print_hintdevices_key(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct ast_hintdevice *hintdevice = v_obj;

	if (!hintdevice) {
		return;
	}
	prnt(where, "%s => %s@%s", hintdevice->hintdevice,
		ast_get_extension_name(hintdevice->hint->exten),
		ast_get_context_name(ast_get_extension_context(hintdevice->hint->exten)));
}

static void print_autohint_key(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct ast_autohint *autohint = v_obj;

	if (!autohint) {
		return;
	}
	prnt(where, "%s", autohint->context);
}

static void print_statecbs_key(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct ast_state_cb *state_cb = v_obj;

	if (!state_cb) {
		return;
	}
	prnt(where, "%d", state_cb->id);
}

int ast_pbx_init(void)
{
	hints = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		HASH_EXTENHINT_SIZE, hint_hash, NULL, hint_cmp);
	if (hints) {
		ao2_container_register("hints", hints, print_hints_key);
	}
	hintdevices = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		HASH_EXTENHINT_SIZE, hintdevice_hash_cb, NULL, hintdevice_cmp_multiple);
	if (hintdevices) {
		ao2_container_register("hintdevices", hintdevices, print_hintdevices_key);
	}
	/* This is protected by the context_and_merge lock */
	autohints = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, HASH_EXTENHINT_SIZE,
		autohint_hash_cb, NULL, autohint_cmp);
	if (autohints) {
		ao2_container_register("autohints", autohints, print_autohint_key);
	}
	statecbs = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, statecbs_cmp);
	if (statecbs) {
		ao2_container_register("statecbs", statecbs, print_statecbs_key);
	}

	ast_register_cleanup(pbx_shutdown);

	if (STASIS_MESSAGE_TYPE_INIT(hint_change_message_type) != 0) {
		return -1;
	}
	if (STASIS_MESSAGE_TYPE_INIT(hint_remove_message_type) != 0) {
		return -1;
	}

	return (hints && hintdevices && autohints && statecbs) ? 0 : -1;
}
