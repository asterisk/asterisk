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
 * \brief Provide a directory of extensions
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>

#include "asterisk/paths.h" /* use ast_config_AST_SPOOL_DIR */
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"

#ifdef ODBC_STORAGE
#include <sys/mman.h>
#include "asterisk/res_odbc.h"

static char odbc_database[80] = "asterisk";
static char odbc_table[80] = "voicemessages";
static char vmfmts[80] = "wav";
#endif

static char *app = "Directory";

static char *synopsis = "Provide directory of voicemail extensions";
static char *descrip =
"  Directory(vm-context[,dial-context[,options]]): This application will present\n"
"the calling channel with a directory of extensions from which they can search\n"
"by name. The list of names and corresponding extensions is retrieved from the\n"
"voicemail configuration file, voicemail.conf.\n"
"  This application will immediately exit if one of the following DTMF digits are\n"
"received and the extension to jump to exists:\n"
"    0 - Jump to the 'o' extension, if it exists.\n"
"    * - Jump to the 'a' extension, if it exists.\n\n"
"  Parameters:\n"
"    vm-context   - This is the context within voicemail.conf to use for the\n"
"                   Directory.\n"
"    dial-context - This is the dialplan context to use when looking for an\n"
"                   extension that the user has selected, or when jumping to the\n"
"                   'o' or 'a' extension.\n\n"
"  Options:\n"
"    e           In addition to the name, also read the extension number to the\n"
"              caller before presenting dialing options.\n"
"    f[(<n>)]    Allow the caller to enter the first name of a user in the\n"
"              directory instead of using the last name.  If specified, the\n"
"              optional number argument will be used for the number of\n"
"              characters the user should enter.\n"
"    l[(<n>)]    Allow the caller to enter the last name of a user in the\n"
"              directory.  This is the default.  If specified, the\n"
"              optional number argument will be used for the number of\n"
"              characters the user should enter.\n"
"    b[(<n>)]    Allow the caller to enter either the first or the last name\n"
"              of a user in the directory.  If specified, the optional number\n"
"              argument will be used for the number of characters the user\n"
"              should enter.\n"
"    m           Instead of reading each name sequentially and asking for\n"
"              confirmation, create a menu of up to 8 names.\n"
"    p(<n>)      Pause for n milliseconds after the digits are typed.  This is\n"
"              helpful for people with cellphones, who are not holding the\n"
"              receiver to their ear while entering DTMF.\n"
"\n"
"    Only one of the f, l, or b options may be specified.  If more than one is\n"
"    specified, then Directory will act as if 'b' was specified.  The number\n"
"    of characters for the user to type defaults to 3.\n";

/* For simplicity, I'm keeping the format compatible with the voicemail config,
   but i'm open to suggestions for isolating it */

#define VOICEMAIL_CONFIG "voicemail.conf"

enum {
	OPT_LISTBYFIRSTNAME = (1 << 0),
	OPT_SAYEXTENSION =    (1 << 1),
	OPT_FROMVOICEMAIL =   (1 << 2),
	OPT_SELECTFROMMENU =  (1 << 3),
	OPT_LISTBYLASTNAME =  (1 << 4),
	OPT_LISTBYEITHER =    OPT_LISTBYFIRSTNAME | OPT_LISTBYLASTNAME,
	OPT_PAUSE =           (1 << 5),
} directory_option_flags;

enum {
	OPT_ARG_FIRSTNAME =   0,
	OPT_ARG_LASTNAME =    1,
	OPT_ARG_EITHER =      2,
	OPT_ARG_PAUSE =       3,
	/* This *must* be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE =  4,
};

struct directory_item {
	char exten[AST_MAX_EXTENSION + 1];
	char name[AST_MAX_EXTENSION + 1];
	char key[50]; /* Text to order items. Either lastname+firstname or firstname+lastname */

	AST_LIST_ENTRY(directory_item) entry;
};

AST_APP_OPTIONS(directory_app_options, {
	AST_APP_OPTION_ARG('f', OPT_LISTBYFIRSTNAME, OPT_ARG_FIRSTNAME),
	AST_APP_OPTION_ARG('l', OPT_LISTBYLASTNAME, OPT_ARG_LASTNAME),
	AST_APP_OPTION_ARG('b', OPT_LISTBYEITHER, OPT_ARG_EITHER),
	AST_APP_OPTION_ARG('p', OPT_PAUSE, OPT_ARG_PAUSE),
	AST_APP_OPTION('e', OPT_SAYEXTENSION),
	AST_APP_OPTION('v', OPT_FROMVOICEMAIL),
	AST_APP_OPTION('m', OPT_SELECTFROMMENU),
});

#ifdef ODBC_STORAGE
struct generic_prepare_struct {
	const char *sql;
	const char *param;
};

static SQLHSTMT generic_prepare(struct odbc_obj *obj, void *data)
{
	struct generic_prepare_struct *gps = data;
	SQLHSTMT stmt;
	int res;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	res = SQLPrepare(stmt, (unsigned char *)gps->sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", (char *)gps->sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	if (!ast_strlen_zero(gps->param))
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(gps->param), 0, (void *)gps->param, 0, NULL);

	return stmt;
}

static void retrieve_file(char *dir)
{
	int x = 0;
	int res;
	int fd=-1;
	size_t fdlen = 0;
	void *fdm = MAP_FAILED;
	SQLHSTMT stmt;
	char sql[256];
	char fmt[80]="", empty[10] = "";
	char *c;
	SQLLEN colsize;
	char full_fn[256];
	struct odbc_obj *obj;
	struct generic_prepare_struct gps = { .sql = sql, .param = dir };

	obj = ast_odbc_request_obj(odbc_database, 1);
	if (obj) {
		do {
			ast_copy_string(fmt, vmfmts, sizeof(fmt));
			c = strchr(fmt, '|');
			if (c)
				*c = '\0';
			if (!strcasecmp(fmt, "wav49"))
				strcpy(fmt, "WAV");
			snprintf(full_fn, sizeof(full_fn), "%s.%s", dir, fmt);
			snprintf(sql, sizeof(sql), "SELECT recording FROM %s WHERE dir=? AND msgnum=-1", odbc_table);
			stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);

			if (!stmt) {
				ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
				break;
			}
			res = SQLFetch(stmt);
			if (res == SQL_NO_DATA) {
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				break;
			} else if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				break;
			}
			fd = open(full_fn, O_RDWR | O_CREAT | O_TRUNC, AST_FILE_MODE);
			if (fd < 0) {
				ast_log(LOG_WARNING, "Failed to write '%s': %s\n", full_fn, strerror(errno));
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				break;
			}

			res = SQLGetData(stmt, 1, SQL_BINARY, empty, 0, &colsize);
			fdlen = colsize;
			if (fd > -1) {
				char tmp[1]="";
				lseek(fd, fdlen - 1, SEEK_SET);
				if (write(fd, tmp, 1) != 1) {
					close(fd);
					fd = -1;
					break;
				}
				if (fd > -1)
					fdm = mmap(NULL, fdlen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			}
			if (fdm != MAP_FAILED) {
				memset(fdm, 0, fdlen);
				res = SQLGetData(stmt, x + 1, SQL_BINARY, fdm, fdlen, &colsize);
				if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
					ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
					SQLFreeHandle(SQL_HANDLE_STMT, stmt);
					break;
				}
			}
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		} while (0);
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
	if (fdm != MAP_FAILED)
		munmap(fdm, fdlen);
	if (fd > -1)
		close(fd);
	return;
}
#endif

static int compare(const char *text, const char *template)
{
	char digit;

	while (*template) {
		digit = toupper(*text++);
		switch (digit) {
		case 0:
			return -1;
		case '1':
			digit = '1';
			break;
		case '2':
		case 'A':
		case 'B':
		case 'C':
			digit = '2';
			break;
		case '3':
		case 'D':
		case 'E':
		case 'F':
			digit = '3';
			break;
		case '4':
		case 'G':
		case 'H':
		case 'I':
			digit = '4';
			break;
		case '5':
		case 'J':
		case 'K':
		case 'L':
			digit = '5';
			break;
		case '6':
		case 'M':
		case 'N':
		case 'O':
			digit = '6';
			break;
		case '7':
		case 'P':
		case 'Q':
		case 'R':
		case 'S':
			digit = '7';
			break;
		case '8':
		case 'T':
		case 'U':
		case 'V':
			digit = '8';
			break;
		case '9':
		case 'W':
		case 'X':
		case 'Y':
		case 'Z':
			digit = '9';
			break;

		default:
			if (digit > ' ')
				return -1;
			continue;
		}

		if (*template++ != digit)
			return -1;
	}

	return 0;
}

/* play name of mailbox owner.
 * returns:  -1 for bad or missing extension
 *           '1' for selected entry from directory
 *           '*' for skipped entry from directory
 */
static int play_mailbox_owner(struct ast_channel *chan, const char *context,
	const char *ext, const char *name, struct ast_flags *flags)
{
	int res = 0;
	char fn[256];

	/* Check for the VoiceMail2 greeting first */
	snprintf(fn, sizeof(fn), "%s/voicemail/%s/%s/greet",
		ast_config_AST_SPOOL_DIR, context, ext);
#ifdef ODBC_STORAGE
	retrieve_file(fn);
#endif

	if (ast_fileexists(fn, NULL, chan->language) <= 0) {
		/* no file, check for an old-style Voicemail greeting */
		snprintf(fn, sizeof(fn), "%s/vm/%s/greet",
			ast_config_AST_SPOOL_DIR, ext);
	}
#ifdef ODBC_STORAGE
	retrieve_file(fn);
#endif

	if (ast_fileexists(fn, NULL, chan->language) > 0) {
		res = ast_stream_and_wait(chan, fn, AST_DIGIT_ANY);
		ast_stopstream(chan);
		/* If Option 'e' was specified, also read the extension number with the name */
		if (ast_test_flag(flags, OPT_SAYEXTENSION)) {
			ast_stream_and_wait(chan, "vm-extension", AST_DIGIT_ANY);
			res = ast_say_character_str(chan, ext, AST_DIGIT_ANY, chan->language);
		}
	} else {
		res = ast_say_character_str(chan, S_OR(name, ext), AST_DIGIT_ANY, chan->language);
		if (!ast_strlen_zero(name) && ast_test_flag(flags, OPT_SAYEXTENSION)) {
			ast_stream_and_wait(chan, "vm-extension", AST_DIGIT_ANY);
			res = ast_say_character_str(chan, ext, AST_DIGIT_ANY, chan->language);
		}
	}
#ifdef ODBC_STORAGE
	ast_filedelete(fn, NULL);
#endif

	return res;
}

static int select_entry(struct ast_channel *chan, const char *context, const char *dialcontext, const struct directory_item *item, struct ast_flags *flags)
{
	ast_debug(1, "Selecting '%s' - %s@%s\n", item->name, item->exten, dialcontext);

	if (ast_test_flag(flags, OPT_FROMVOICEMAIL)) {
		/* We still want to set the exten though */
		ast_copy_string(chan->exten, item->exten, sizeof(chan->exten));
	} else if (ast_goto_if_exists(chan, dialcontext, item->exten, 1)) {
		ast_log(LOG_WARNING,
			"Can't find extension '%s' in context '%s'.  "
			"Did you pass the wrong context to Directory?\n",
			item->exten, dialcontext);
		return -1;
	}

	return 0;
}

static int select_item_seq(struct ast_channel *chan, struct directory_item **items, int count, const char *context, const char *dialcontext, struct ast_flags *flags)
{
	struct directory_item *item, **ptr;
	int i, res, loop;

	for (ptr = items, i = 0; i < count; i++, ptr++) {
		item = *ptr;

		for (loop = 3 ; loop > 0; loop--) {
			res = play_mailbox_owner(chan, context, item->exten, item->name, flags);

			if (!res)
				res = ast_stream_and_wait(chan, "dir-instr", AST_DIGIT_ANY);
			if (!res)
				res = ast_waitfordigit(chan, 3000);
			ast_stopstream(chan);
	
			if (res == '1') { /* Name selected */
				return select_entry(chan, context, dialcontext, item, flags) ? -1 : 1;
			} else if (res == '*') {
				/* Skip to next match in list */
				break;
			}

			if (res < 0)
				return -1;

			res = 0;
		}
	}

	/* Nothing was selected */
	return 0;
}

static int select_item_menu(struct ast_channel *chan, struct directory_item **items, int count, const char *context, const char *dialcontext, struct ast_flags *flags)
{
	struct directory_item **block, *item;
	int i, limit, res = 0;
	char buf[9];

	for (block = items; count; block += limit, count -= limit) {
		limit = count;
		if (limit > 8)
			limit = 8;

		for (i = 0; i < limit && !res; i++) {
			item = block[i];

			snprintf(buf, sizeof(buf), "digits/%d", i + 1);
			/* Press <num> for <name>, [ extension <ext> ] */
			res = ast_streamfile(chan, "dir-multi1", chan->language);
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = ast_streamfile(chan, buf, chan->language);
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = ast_streamfile(chan, "dir-multi2", chan->language);
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = play_mailbox_owner(chan, context, item->exten, item->name, flags);
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = ast_waitfordigit(chan, 800);
		}

		/* Press "9" for more names. */
		if (!res && count > limit) {
			res = ast_streamfile(chan, "dir-multi9", chan->language);
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
		}

		if (!res) {
			res = ast_waitfordigit(chan, 3000);
		}

		if (res && res > '0' && res < '1' + limit) {
			return select_entry(chan, context, dialcontext, block[res - '1'], flags) ? -1 : 1;
		}

		if (res < 0)
			return -1;

		res = 0;
	}

	/* Nothing was selected */
	return 0;
}

static struct ast_config *realtime_directory(char *context)
{
	struct ast_config *cfg;
	struct ast_config *rtdata;
	struct ast_category *cat;
	struct ast_variable *var;
	char *mailbox;
	const char *fullname;
	const char *hidefromdir;
	char tmp[100];
	struct ast_flags config_flags = { 0 };

	/* Load flat file config. */
	cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags);

	if (!cfg) {
		/* Loading config failed. */
		ast_log(LOG_WARNING, "Loading config failed.\n");
		return NULL;
	}

	/* Get realtime entries, categorized by their mailbox number
	   and present in the requested context */
	rtdata = ast_load_realtime_multientry("voicemail", "mailbox LIKE", "%", "context", context, NULL);

	/* if there are no results, just return the entries from the config file */
	if (!rtdata)
		return cfg;

	/* Does the context exist within the config file? If not, make one */
	cat = ast_category_get(cfg, context);
	if (!cat) {
		cat = ast_category_new(context, "", 99999);
		if (!cat) {
			ast_log(LOG_WARNING, "Out of memory\n");
			ast_config_destroy(cfg);
			return NULL;
		}
		ast_category_append(cfg, cat);
	}

	mailbox = NULL;
	while ( (mailbox = ast_category_browse(rtdata, mailbox)) ) {
		fullname = ast_variable_retrieve(rtdata, mailbox, "fullname");
		if (ast_true((hidefromdir = ast_variable_retrieve(rtdata, mailbox, "hidefromdir")))) {
			/* Skip hidden */
			continue;
		}
		snprintf(tmp, sizeof(tmp), "no-password,%s", S_OR(fullname, ""));
		var = ast_variable_new(mailbox, tmp, "");
		if (var)
			ast_variable_append(cat, var);
		else
			ast_log(LOG_WARNING, "Out of memory adding mailbox '%s'\n", mailbox);
	}
	ast_config_destroy(rtdata);

	return cfg;
}

static int check_match(struct directory_item **result, const char *item_fullname, const char *item_ext, const char *pattern_ext, int use_first_name)
{
	struct directory_item *item;
	const char *key = NULL;
	int namelen;


	/* Set key to last name or first name depending on search mode */
	if (!use_first_name)
		key = strchr(item_fullname, ' ');

	if (key)
		key++;
	else
		key = item_fullname;

	if (compare(key, pattern_ext))
		return 0;

	/* Match */
	item = ast_calloc(1, sizeof(*item));
	if (!item)
		return -1;
	ast_copy_string(item->name, item_fullname, sizeof(item->name));
	ast_copy_string(item->exten, item_ext, sizeof(item->exten));

	ast_copy_string(item->key, key, sizeof(item->key));
	if (key != item_fullname) {
		/* Key is the last name. Append first name to key in order to sort Last,First */
		namelen = key - item_fullname - 1;
		if (namelen > sizeof(item->key) - strlen(item->key) - 1)
			namelen = sizeof(item->key) - strlen(item->key) - 1;
		strncat(item->key, item_fullname, namelen);
	}

	*result = item;
	return 1;
}

typedef AST_LIST_HEAD_NOLOCK(, directory_item) itemlist;

static int search_directory(const char *context, struct ast_config *vmcfg, struct ast_config *ucfg, const char *ext, struct ast_flags flags, itemlist *alist)
{
	struct ast_variable *v;
	char buf[AST_MAX_EXTENSION + 1], *pos, *bufptr, *cat;
	struct directory_item *item;
	int res;

	ast_debug(2, "Pattern: %s\n", ext);

	for (v = ast_variable_browse(vmcfg, context); v; v = v->next) {

		/* Ignore hidden */
		if (strcasestr(v->value, "hidefromdir=yes"))
			continue;

		ast_copy_string(buf, v->value, sizeof(buf));
		bufptr = buf;

		/* password,Full Name,email,pager,options */
		strsep(&bufptr, ",");
		pos = strsep(&bufptr, ",");

		res = 0;
		if (ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
			res = check_match(&item, pos, v->name, ext, 0 /* use_first_name */);
		}
		if (!res && ast_test_flag(&flags, OPT_LISTBYFIRSTNAME)) {
			res = check_match(&item, pos, v->name, ext, 1 /* use_first_name */);
		}

		if (!res)
			continue;
		else if (res < 0)
			return -1;

		AST_LIST_INSERT_TAIL(alist, item, entry);
	}

	if (ucfg) {
		for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
			const char *pos;
			if (!strcasecmp(cat, "general"))
				continue;
			if (!ast_true(ast_config_option(ucfg, cat, "hasdirectory")))
				continue;

			/* Find all candidate extensions */
			pos = ast_variable_retrieve(ucfg, cat, "fullname");
			if (!pos)
				continue;

			res = 0;
			if (ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
				res = check_match(&item, pos, cat, ext, 0 /* use_first_name */);
			}
			if (!res && ast_test_flag(&flags, OPT_LISTBYFIRSTNAME)) {
				res = check_match(&item, pos, cat, ext, 1 /* use_first_name */);
			}

			if (!res)
				continue;
			else if (res < 0)
				return -1;

			AST_LIST_INSERT_TAIL(alist, item, entry);
		}
	}
	return 0;
}

static void sort_items(struct directory_item **sorted, int count)
{
	int reordered, i;
	struct directory_item **ptr, *tmp;

	if (count < 2)
		return;

	/* Bubble-sort items by the key */
	do {
		reordered = 0;
		for (ptr = sorted, i = 0; i < count - 1; i++, ptr++) {
			if (strcasecmp(ptr[0]->key, ptr[1]->key) > 0) {
				tmp = ptr[0];
				ptr[0] = ptr[1];
				ptr[1] = tmp;
				reordered++;
			}
		}
	} while (reordered);
}

static int goto_exten(struct ast_channel *chan, const char *dialcontext, char *ext)
{
	if (!ast_goto_if_exists(chan, dialcontext, ext, 1) ||
		(!ast_strlen_zero(chan->macrocontext) &&
		!ast_goto_if_exists(chan, chan->macrocontext, ext, 1))) {
		return 0;
	} else {
		ast_log(LOG_WARNING, "Can't find extension '%s' in current context.  "
			"Not Exiting the Directory!\n", ext);
		return -1;
	}
}

static int do_directory(struct ast_channel *chan, struct ast_config *vmcfg, struct ast_config *ucfg, char *context, char *dialcontext, char digit, int digits, struct ast_flags *flags)
{
	/* Read in the first three digits..  "digit" is the first digit, already read */
	int res = 0;
	itemlist alist = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct directory_item *item, **ptr, **sorted = NULL;
	int count, i;
	char ext[10] = "";

	if (ast_strlen_zero(context)) {
		ast_log(LOG_WARNING,
			"Directory must be called with an argument "
			"(context in which to interpret extensions)\n");
		return -1;
	}

	if (digit == '0' && !goto_exten(chan, dialcontext, "o")) {
		return 0;
	}

	if (digit == '*' && !goto_exten(chan, dialcontext, "a")) {
		return 0;
	}

	ext[0] = digit;
	if (ast_readstring(chan, ext + 1, digits - 1, 3000, 3000, "#") < 0)
		return -1;

	res = search_directory(context, vmcfg, ucfg, ext, *flags, &alist);
	if (res)
		goto exit;

	/* Count items in the list */
	count = 0;
	AST_LIST_TRAVERSE(&alist, item, entry) {
		count++;
	}

	if (count < 1) {
		res = ast_streamfile(chan, "dir-nomatch", chan->language);
		goto exit;
	}


	/* Create plain array of pointers to items (for sorting) */
	sorted = ast_calloc(count, sizeof(*sorted));

	ptr = sorted;
	AST_LIST_TRAVERSE(&alist, item, entry) {
		*ptr++ = item;
	}

	/* Sort items */
	sort_items(sorted, count);

	if (option_debug) {
		ast_debug(2, "Listing matching entries:\n");
		for (ptr = sorted, i = 0; i < count; i++, ptr++) {
			ast_log(LOG_DEBUG, "%s: %s\n", ptr[0]->exten, ptr[0]->name);
		}
	}

	if (ast_test_flag(flags, OPT_SELECTFROMMENU)) {
		/* Offer multiple entries at the same time */
		res = select_item_menu(chan, sorted, count, context, dialcontext, flags);
	} else {
		/* Offer entries one by one */
		res = select_item_seq(chan, sorted, count, context, dialcontext, flags);
	}

	if (!res) {
		res = ast_streamfile(chan, "dir-nomore", chan->language);
	}

exit:
	if (sorted)
		ast_free(sorted);

	while ((item = AST_LIST_REMOVE_HEAD(&alist, entry)))
		ast_free(item);

	return res;
}

static int directory_exec(struct ast_channel *chan, void *data)
{
	int res = 0, digit = 3;
	struct ast_config *cfg, *ucfg;
	const char *dirintro;
	char *parse, *opts[OPT_ARG_ARRAY_SIZE];
	struct ast_flags flags = { 0 };
	struct ast_flags config_flags = { 0 };
	enum { FIRST, LAST, BOTH } which = LAST;
	char digits[9] = "digits/3";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vmcontext);
		AST_APP_ARG(dialcontext);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Directory requires an argument (context[,dialcontext])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options && ast_app_parse_options(directory_app_options, &flags, opts, args.options))
		return -1;

	if (ast_strlen_zero(args.dialcontext))
		args.dialcontext = args.vmcontext;

	cfg = realtime_directory(args.vmcontext);
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to read the configuration data!\n");
		return -1;
	}

	ucfg = ast_config_load("users.conf", config_flags);

	dirintro = ast_variable_retrieve(cfg, args.vmcontext, "directoryintro");
	if (ast_strlen_zero(dirintro))
		dirintro = ast_variable_retrieve(cfg, "general", "directoryintro");

	if (ast_test_flag(&flags, OPT_LISTBYFIRSTNAME) && ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
		if (!ast_strlen_zero(opts[OPT_ARG_EITHER])) {
			digit = atoi(opts[OPT_ARG_EITHER]);
		}
		which = BOTH;
	} else if (ast_test_flag(&flags, OPT_LISTBYFIRSTNAME)) {
		if (!ast_strlen_zero(opts[OPT_ARG_FIRSTNAME])) {
			digit = atoi(opts[OPT_ARG_FIRSTNAME]);
		}
		which = FIRST;
	} else {
		if (!ast_strlen_zero(opts[OPT_ARG_LASTNAME])) {
			digit = atoi(opts[OPT_ARG_LASTNAME]);
		}
		which = LAST;
	}

	/* If no options specified, search by last name */
	if (!ast_test_flag(&flags, OPT_LISTBYFIRSTNAME) && !ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
		ast_set_flag(&flags, OPT_LISTBYLASTNAME);
		which = LAST;
	}

	if (digit > 9) {
		digit = 9;
	} else if (digit < 1) {
		digit = 3;
	}
	digits[7] = digit + '0';

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	for (;;) {
		if (!ast_strlen_zero(dirintro) && !res) {
			res = ast_stream_and_wait(chan, dirintro, AST_DIGIT_ANY);
		} else if (!res) {
			/* Stop playing sounds as soon as we have a digit. */
			res = ast_stream_and_wait(chan, "dir-welcome", AST_DIGIT_ANY);
			if (!res) {
				res = ast_stream_and_wait(chan, "dir-pls-enter", AST_DIGIT_ANY);
			}
			if (!res) {
				res = ast_stream_and_wait(chan, digits, AST_DIGIT_ANY);
			}
			if (!res) {
				res = ast_stream_and_wait(chan, 
					which == FIRST ? "dir-first" :
					which == LAST ? "dir-last" :
					"dir-firstlast", AST_DIGIT_ANY);
			}
			if (!res) {
				ast_stream_and_wait(chan, "dir-usingkeypad", AST_DIGIT_ANY);
			}
		}
		ast_stopstream(chan);
		if (!res)
			res = ast_waitfordigit(chan, 5000);

		if (res <= 0)
			break;

		res = do_directory(chan, cfg, ucfg, args.vmcontext, args.dialcontext, res, digit, &flags);
		if (res)
			break;

		res = ast_waitstream(chan, AST_DIGIT_ANY);
		ast_stopstream(chan);

		if (res)
			break;
	}

	if (ucfg)
		ast_config_destroy(ucfg);
	ast_config_destroy(cfg);

	return res < 0 ? -1 : 0;
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	return res;
}

static int load_module(void)
{
#ifdef ODBC_STORAGE
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags);
	const char *tmp;

	if (cfg) {
		if ((tmp = ast_variable_retrieve(cfg, "general", "odbcstorage"))) {
			ast_copy_string(odbc_database, tmp, sizeof(odbc_database));
		}
		if ((tmp = ast_variable_retrieve(cfg, "general", "odbctable"))) {
			ast_copy_string(odbc_table, tmp, sizeof(odbc_table));
		}
		if ((tmp = ast_variable_retrieve(cfg, "general", "format"))) {
			ast_copy_string(vmfmts, tmp, sizeof(vmfmts));
		}
		ast_config_destroy(cfg);
	} else
		ast_log(LOG_WARNING, "Unable to load " VOICEMAIL_CONFIG " - ODBC defaults will be used\n");
#endif

	return ast_register_application(app, directory_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Extension Directory");
