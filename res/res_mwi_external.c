/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Core external MWI support.
 *
 * \details
 * The module manages the persistent message counts cache and supplies
 * an API to allow the protocol specific modules to control the counts
 * or a subset.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_mwi_external" language="en_US">
		<synopsis>Core external MWI support</synopsis>
		<configFile name="sorcery.conf">
			<configObject name="mailboxes">
				<synopsis>Persistent cache of external MWI Mailboxs.</synopsis>
				<description>
					<para>Allows the alteration of sorcery backend mapping for
					the persistent cache of external MWI mailboxes.</para>
				</description>
			</configObject>
		</configFile>
	</configInfo>
 ***/


#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/mwi.h"
#include "asterisk/module.h"
#include "asterisk/res_mwi_external.h"
#include "asterisk/sorcery.h"
#include "asterisk/cli.h"

/* ------------------------------------------------------------------- */

/*!
 * Define to include CLI commands to manipulate the external MWI mailboxes.
 * Useful for testing the module functionality.
 */
//#define MWI_DEBUG_CLI		1

#define MWI_ASTDB_PREFIX	"mwi_external"
#define MWI_MAILBOX_TYPE	"mailboxes"

struct ast_mwi_mailbox_object {
	SORCERY_OBJECT(details);
	/*! Number of new messages in mailbox. */
	unsigned int msgs_new;
	/*! Number of old messages in mailbox. */
	unsigned int msgs_old;
};

static struct ast_sorcery *mwi_sorcery;

/*!
 * \internal
 * \brief Post an update event to the MWI counts.
 * \since 12.1.0
 *
 * \return Nothing
 */
static void mwi_post_event(const struct ast_mwi_mailbox_object *mailbox)
{
	ast_publish_mwi_state(ast_sorcery_object_get_id(mailbox), NULL,
		mailbox->msgs_new, mailbox->msgs_old);
}

static void mwi_observe_update(const void *obj)
{
	mwi_post_event(obj);
}

/*!
 * \internal
 * \brief Post a count clearing event to the MWI counts.
 * \since 12.1.0
 *
 * \return Nothing
 */
static void mwi_observe_delete(const void *obj)
{
	const struct ast_mwi_mailbox_object *mailbox = obj;

	if (mailbox->msgs_new || mailbox->msgs_old) {
		/* Post a count clearing event. */
		ast_publish_mwi_state(ast_sorcery_object_get_id(mailbox), NULL, 0, 0);
	}

	/* Post a cache remove event. */
	ast_delete_mwi_state(ast_sorcery_object_get_id(mailbox), NULL);
}

static const struct ast_sorcery_observer mwi_observers = {
	.created = mwi_observe_update,
	.updated = mwi_observe_update,
	.deleted = mwi_observe_delete,
};

/*! \brief Internal function to allocate a mwi object */
static void *mwi_sorcery_object_alloc(const char *id)
{
	return ast_sorcery_generic_alloc(sizeof(struct ast_mwi_mailbox_object), NULL);
}

/*!
 * \internal
 * \brief Initialize sorcery for external MWI.
 * \since 12.1.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int mwi_sorcery_init(void)
{
	int res;

	mwi_sorcery = ast_sorcery_open();
	if (!mwi_sorcery) {
		ast_log(LOG_ERROR, "MWI external: Sorcery failed to open.\n");
		return -1;
	}

	/* Map the external MWI wizards. */
	if (ast_sorcery_apply_default(mwi_sorcery, MWI_MAILBOX_TYPE, "astdb",
			MWI_ASTDB_PREFIX) == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "MWI external: Sorcery could not setup wizards.\n");
		return -1;
	}

	res = ast_sorcery_object_register(mwi_sorcery, MWI_MAILBOX_TYPE,
		mwi_sorcery_object_alloc, NULL, NULL);
	if (res) {
		ast_log(LOG_ERROR, "MWI external: Sorcery could not register object type '%s'.\n",
			MWI_MAILBOX_TYPE);
		return -1;
	}

	/* Define the MWI_MAILBOX_TYPE object fields. */
	res |= ast_sorcery_object_field_register_nodoc(mwi_sorcery, MWI_MAILBOX_TYPE,
		"msgs_new", "0", OPT_UINT_T, 0, FLDSET(struct ast_mwi_mailbox_object, msgs_new));
	res |= ast_sorcery_object_field_register_nodoc(mwi_sorcery, MWI_MAILBOX_TYPE,
		"msgs_old", "0", OPT_UINT_T, 0, FLDSET(struct ast_mwi_mailbox_object, msgs_old));
	return res ? -1 : 0;
}

struct ao2_container *ast_mwi_mailbox_get_all(void)
{
	return ast_sorcery_retrieve_by_fields(mwi_sorcery, MWI_MAILBOX_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct ao2_container *ast_mwi_mailbox_get_by_regex(const char *regex)
{
	return ast_sorcery_retrieve_by_regex(mwi_sorcery, MWI_MAILBOX_TYPE, regex ?: "");
}

const struct ast_mwi_mailbox_object *ast_mwi_mailbox_get(const char *mailbox_id)
{
	if (ast_strlen_zero(mailbox_id)) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(mwi_sorcery, MWI_MAILBOX_TYPE, mailbox_id);
}

struct ast_mwi_mailbox_object *ast_mwi_mailbox_alloc(const char *mailbox_id)
{
	if (ast_strlen_zero(mailbox_id)) {
		return NULL;
	}

	return ast_sorcery_alloc(mwi_sorcery, MWI_MAILBOX_TYPE, mailbox_id);
}

struct ast_mwi_mailbox_object *ast_mwi_mailbox_copy(const struct ast_mwi_mailbox_object *mailbox)
{
	return ast_sorcery_copy(mwi_sorcery, mailbox);
}

const char *ast_mwi_mailbox_get_id(const struct ast_mwi_mailbox_object *mailbox)
{
	return ast_sorcery_object_get_id(mailbox);
}

unsigned int ast_mwi_mailbox_get_msgs_new(const struct ast_mwi_mailbox_object *mailbox)
{
	return mailbox->msgs_new;
}

unsigned int ast_mwi_mailbox_get_msgs_old(const struct ast_mwi_mailbox_object *mailbox)
{
	return mailbox->msgs_old;
}

void ast_mwi_mailbox_set_msgs_new(struct ast_mwi_mailbox_object *mailbox, unsigned int num_msgs)
{
	mailbox->msgs_new = num_msgs;
}

void ast_mwi_mailbox_set_msgs_old(struct ast_mwi_mailbox_object *mailbox, unsigned int num_msgs)
{
	mailbox->msgs_old = num_msgs;
}

int ast_mwi_mailbox_update(struct ast_mwi_mailbox_object *mailbox)
{
	const struct ast_mwi_mailbox_object *exists;
	int res;

	exists = ast_sorcery_retrieve_by_id(mwi_sorcery, MWI_MAILBOX_TYPE,
		ast_sorcery_object_get_id(mailbox));
	if (exists) {
		res = ast_sorcery_update(mwi_sorcery, mailbox);
		ast_mwi_mailbox_unref(exists);
	} else {
		res = ast_sorcery_create(mwi_sorcery, mailbox);
	}
	return res;
}

/*!
 * \internal
 * \brief Delete a mailbox.
 * \since 12.1.0
 *
 * \param mailbox Mailbox object to delete from sorcery.
 *
 * \return Nothing
 */
static void mwi_mailbox_delete(struct ast_mwi_mailbox_object *mailbox)
{
	ast_sorcery_delete(mwi_sorcery, mailbox);
}

/*!
 * \internal
 * \brief Delete all mailboxes in container.
 * \since 12.1.0
 *
 * \param mailboxes Mailbox objects to delete from sorcery.
 *
 * \return Nothing
 */
static void mwi_mailbox_delete_all(struct ao2_container *mailboxes)
{
	struct ast_mwi_mailbox_object *mailbox;
	struct ao2_iterator iter;

	iter = ao2_iterator_init(mailboxes, AO2_ITERATOR_UNLINK);
	for (; (mailbox = ao2_iterator_next(&iter)); ast_mwi_mailbox_unref(mailbox)) {
		mwi_mailbox_delete(mailbox);
	}
	ao2_iterator_destroy(&iter);
}

int ast_mwi_mailbox_delete_all(void)
{
	struct ao2_container *mailboxes;

	mailboxes = ast_mwi_mailbox_get_all();
	if (mailboxes) {
		mwi_mailbox_delete_all(mailboxes);
		ao2_ref(mailboxes, -1);
	}
	return 0;
}

int ast_mwi_mailbox_delete_by_regex(const char *regex)
{
	struct ao2_container *mailboxes;

	mailboxes = ast_mwi_mailbox_get_by_regex(regex);
	if (mailboxes) {
		mwi_mailbox_delete_all(mailboxes);
		ao2_ref(mailboxes, -1);
	}
	return 0;
}

int ast_mwi_mailbox_delete(const char *mailbox_id)
{
	const struct ast_mwi_mailbox_object *mailbox;

	if (ast_strlen_zero(mailbox_id)) {
		return -1;
	}

	mailbox = ast_mwi_mailbox_get(mailbox_id);
	if (mailbox) {
		mwi_mailbox_delete((struct ast_mwi_mailbox_object *) mailbox);
		ast_mwi_mailbox_unref(mailbox);
	}
	return 0;
}

enum folder_map {
	FOLDER_INVALID = 0,
	FOLDER_INBOX = 1,
	FOLDER_OLD = 2,
};

/*!
 * \internal
 * \brief Determine if the requested folder is valid for external MWI support.
 * \since 12.1.0
 *
 * \param folder Folder name to check (NULL is valid).
 *
 * \return Enum of the supported folder.
 */
static enum folder_map mwi_folder_map(const char *folder)
{
	enum folder_map which_folder;

	if (ast_strlen_zero(folder) || !strcasecmp(folder, "INBOX")) {
		which_folder = FOLDER_INBOX;
	} else if (!strcasecmp(folder, "Old")) {
		which_folder = FOLDER_OLD;
	} else {
		which_folder = FOLDER_INVALID;
	}
	return which_folder;
}

/*!
 * \internal
 * \brief Gets the number of messages that exist in a mailbox folder.
 * \since 12.1.0
 *
 * \param mailbox_id The mailbox name.
 * \param folder The folder to look in.  Default is INBOX if not provided.
 *
 * \return The number of messages in the mailbox folder (zero or more).
 */
static int mwi_messagecount(const char *mailbox_id, const char *folder)
{
	const struct ast_mwi_mailbox_object *mailbox;
	int num_msgs;
	enum folder_map which_folder;

	which_folder = mwi_folder_map(folder);
	if (which_folder == FOLDER_INVALID) {
		return 0;
	}

	mailbox = ast_mwi_mailbox_get(mailbox_id);
	if (!mailbox) {
		return 0;
	}
	num_msgs = 0;
	switch (which_folder) {
	case FOLDER_INVALID:
		break;
	case FOLDER_INBOX:
		num_msgs = mailbox->msgs_new;
		break;
	case FOLDER_OLD:
		num_msgs = mailbox->msgs_old;
		break;
	}
	ast_mwi_mailbox_unref(mailbox);

	return num_msgs;
}

/*!
 * \internal
 * \brief Determines if the given folder has messages.
 * \since 12.1.0
 *
 * \param mailboxes Comma or & delimited list of mailboxes.
 * \param folder The folder to look in.  Default is INBOX if not provided.
 *
 * \retval 1 if the folder has one or more messages.
 * \retval 0 otherwise.
 */
static int mwi_has_voicemail(const char *mailboxes, const char *folder)
{
	char *parse;
	char *mailbox_id;
	enum folder_map which_folder;

	which_folder = mwi_folder_map(folder);
	if (which_folder == FOLDER_INVALID) {
		return 0;
	}

	/* For each mailbox in the list. */
	parse = ast_strdupa(mailboxes);
	while ((mailbox_id = strsep(&parse, ",&"))) {
		const struct ast_mwi_mailbox_object *mailbox;
		int num_msgs;

		/* Get the specified mailbox. */
		mailbox = ast_mwi_mailbox_get(mailbox_id);
		if (!mailbox) {
			continue;
		}

		/* Done if the found mailbox has any messages. */
		num_msgs = 0;
		switch (which_folder) {
		case FOLDER_INVALID:
			break;
		case FOLDER_INBOX:
			num_msgs = mailbox->msgs_new;
			break;
		case FOLDER_OLD:
			num_msgs = mailbox->msgs_old;
			break;
		}
		ast_mwi_mailbox_unref(mailbox);
		if (num_msgs) {
			return 1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Gets the number of messages that exist for the mailbox list.
 * \since 12.1.0
 *
 * \param mailboxes Comma or space delimited list of mailboxes.
 * \param newmsgs Where to put the count of new messages. (Can be NULL)
 * \param oldmsgs Where to put the count of old messages. (Can be NULL)
 *
 * \details
 * Simultaneously determines the count of new and old
 * messages.  The total messages would then be the sum of these.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int mwi_inboxcount(const char *mailboxes, int *newmsgs, int *oldmsgs)
{
	char *parse;
	char *mailbox_id;

	if (!newmsgs && !oldmsgs) {
		/* Nowhere to accumulate counts */
		return 0;
	}

	/* For each mailbox in the list. */
	parse = ast_strdupa(mailboxes);
	while ((mailbox_id = strsep(&parse, ", "))) {
		const struct ast_mwi_mailbox_object *mailbox;

		/* Get the specified mailbox. */
		mailbox = ast_mwi_mailbox_get(mailbox_id);
		if (!mailbox) {
			continue;
		}

		/* Accumulate the counts. */
		if (newmsgs) {
			*newmsgs += mailbox->msgs_new;
		}
		if (oldmsgs) {
			*oldmsgs += mailbox->msgs_old;
		}

		ast_mwi_mailbox_unref(mailbox);
	}

	return 0;
}

/*!
 * \internal
 * \brief Gets the number of messages that exist for the mailbox list.
 * \since 12.1.0
 *
 * \param mailboxes Comma or space delimited list of mailboxes.
 * \param urgentmsgs Where to put the count of urgent messages. (Can be NULL)
 * \param newmsgs Where to put the count of new messages. (Can be NULL)
 * \param oldmsgs Where to put the count of old messages. (Can be NULL)
 *
 * \details
 * Simultaneously determines the count of new, old, and urgent
 * messages.  The total messages would then be the sum of these
 * three.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int mwi_inboxcount2(const char *mailboxes, int *urgentmsgs, int *newmsgs, int *oldmsgs)
{
	/*
	 * This module does not support urgentmsgs.  Just ignore them.
	 * The global API call has already set the count to zero.
	 */
	return mwi_inboxcount(mailboxes, newmsgs, oldmsgs);
}

static const struct ast_vm_functions vm_table = {
	.module_version = VM_MODULE_VERSION,
	.module_name = AST_MODULE,

	.has_voicemail = mwi_has_voicemail,
	.inboxcount = mwi_inboxcount,
	.inboxcount2 = mwi_inboxcount2,
	.messagecount = mwi_messagecount,
};

#if defined(MWI_DEBUG_CLI)
static char *complete_mailbox(const char *word, int state)
{
	struct ao2_iterator iter;
	int wordlen = strlen(word);
	int which = 0;
	char *ret = NULL;
	char *regex;
	const struct ast_mwi_mailbox_object *mailbox;
	RAII_VAR(struct ao2_container *, mailboxes, NULL, ao2_cleanup);

	regex = ast_alloca(2 + wordlen);
	sprintf(regex, "^%s", word);/* Safe */

	mailboxes = ast_mwi_mailbox_get_by_regex(regex);
	if (!mailboxes) {
		return NULL;
	}

	iter = ao2_iterator_init(mailboxes, 0);
	for (; (mailbox = ao2_iterator_next(&iter)); ast_mwi_mailbox_unref(mailbox)) {
		if (++which > state) {
			ret = ast_strdup(ast_sorcery_object_get_id(mailbox));
			ast_mwi_mailbox_unref(mailbox);
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	return ret;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_delete_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi delete all";
		e->usage =
			"Usage: mwi delete all\n"
			"       Delete all external MWI mailboxes.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_mwi_mailbox_delete_all();
	ast_cli(a->fd, "Deleted all external MWI mailboxes.\n");
	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_delete_like(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *regex;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi delete like";
		e->usage =
			"Usage: mwi delete like <pattern>\n"
			"       Delete external MWI mailboxes matching a regular expression.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}
	regex = a->argv[3];

	ast_mwi_mailbox_delete_by_regex(regex);
	ast_cli(a->fd, "Deleted external MWI mailboxes matching '%s'.\n", regex);
	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_delete_mailbox(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *mailbox_id;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi delete mailbox";
		e->usage =
			"Usage: mwi delete mailbox <mailbox_id>\n"
			"       Delete a specific external MWI mailbox.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_mailbox(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}
	mailbox_id = a->argv[3];

	ast_mwi_mailbox_delete(mailbox_id);
	ast_cli(a->fd, "Deleted external MWI mailbox '%s'.\n", mailbox_id);

	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#define FORMAT_MAILBOX_HDR "%6s %6s %s\n"
#define FORMAT_MAILBOX_ROW "%6u %6u %s\n"

#if defined(MWI_DEBUG_CLI)
/*!
 * \internal
 * \brief Print a mailbox list line to CLI.
 * \since 12.1.0
 *
 * \param cli_fd File descriptor for CLI output.
 * \param mailbox What to list.
 *
 * \return Nothing
 */
static void mwi_cli_print_mailbox(int cli_fd, const struct ast_mwi_mailbox_object *mailbox)
{
	ast_cli(cli_fd, FORMAT_MAILBOX_ROW, mailbox->msgs_new, mailbox->msgs_old,
		ast_sorcery_object_get_id(mailbox));
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
/*!
 * \internal
 * \brief List all mailboxes in the given container.
 * \since 12.1.0
 *
 * \param cli_fd File descriptor for CLI output.
 * \param mailboxes What to list.
 *
 * \return Nothing
 */
static void mwi_cli_list_mailboxes(int cli_fd, struct ao2_container *mailboxes)
{
	struct ao2_iterator iter;
	const struct ast_mwi_mailbox_object *mailbox;

	ast_cli(cli_fd, FORMAT_MAILBOX_HDR, "New", "Old", "Mailbox");

	iter = ao2_iterator_init(mailboxes, 0);
	for (; (mailbox = ao2_iterator_next(&iter)); ast_mwi_mailbox_unref(mailbox)) {
		mwi_cli_print_mailbox(cli_fd, mailbox);
	}
	ao2_iterator_destroy(&iter);
}
#endif	/* defined(MWI_DEBUG_CLI) */

#undef FORMAT_MAILBOX_HDR
#undef FORMAT_MAILBOX_ROW

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_list_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *mailboxes;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi list all";
		e->usage =
			"Usage: mwi list all\n"
			"       List all external MWI mailboxes.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	mailboxes = ast_mwi_mailbox_get_all();
	if (!mailboxes) {
		ast_cli(a->fd, "Failed to retrieve external MWI mailboxes.\n");
		return CLI_SUCCESS;
	}
	mwi_cli_list_mailboxes(a->fd, mailboxes);
	ao2_ref(mailboxes, -1);
	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_list_like(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *mailboxes;
	const char *regex;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi list like";
		e->usage =
			"Usage: mwi list like <pattern>\n"
			"       List external MWI mailboxes matching a regular expression.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}
	regex = a->argv[3];

	mailboxes = ast_mwi_mailbox_get_by_regex(regex);
	if (!mailboxes) {
		ast_cli(a->fd, "Failed to retrieve external MWI mailboxes.\n");
		return CLI_SUCCESS;
	}
	mwi_cli_list_mailboxes(a->fd, mailboxes);
	ao2_ref(mailboxes, -1);
	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_show_mailbox(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const struct ast_mwi_mailbox_object *mailbox;
	const char *mailbox_id;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi show mailbox";
		e->usage =
			"Usage: mwi show mailbox <mailbox_id>\n"
			"       Show a specific external MWI mailbox.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_mailbox(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}
	mailbox_id = a->argv[3];

	mailbox = ast_mwi_mailbox_get(mailbox_id);
	if (mailbox) {
		ast_cli(a->fd,
			"Mailbox: %s\n"
			"NewMessages: %u\n"
			"OldMessages: %u\n",
			ast_sorcery_object_get_id(mailbox),
			mailbox->msgs_new,
			mailbox->msgs_old);

		ast_mwi_mailbox_unref(mailbox);
	} else {
		ast_cli(a->fd, "External MWI mailbox '%s' not found.\n", mailbox_id);
	}

	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static char *handle_mwi_update_mailbox(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_mwi_mailbox_object *mailbox;
	const char *mailbox_id;
	unsigned int num_new;
	unsigned int num_old;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mwi update mailbox";
		e->usage =
			"Usage: mwi update mailbox <mailbox_id> [<new> [<old>]]\n"
			"       Update a specific external MWI mailbox.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_mailbox(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc < 4 || 6 < a->argc) {
		return CLI_SHOWUSAGE;
	}
	mailbox_id = a->argv[3];

	num_new = 0;
	if (4 < a->argc) {
		const char *count_new = a->argv[4];

		if (sscanf(count_new, "%u", &num_new) != 1) {
			ast_cli(a->fd, "Invalid NewMessages: '%s'.\n", count_new);
			return CLI_SHOWUSAGE;
		}
	}

	num_old = 0;
	if (5 < a->argc) {
		const char *count_old = a->argv[5];

		if (sscanf(count_old, "%u", &num_old) != 1) {
			ast_cli(a->fd, "Invalid OldMessages: '%s'.\n", count_old);
			return CLI_SHOWUSAGE;
		}
	}

	mailbox = ast_mwi_mailbox_alloc(mailbox_id);
	if (mailbox) {
		ast_mwi_mailbox_set_msgs_new(mailbox, num_new);
		ast_mwi_mailbox_set_msgs_old(mailbox, num_old);
		if (ast_mwi_mailbox_update(mailbox)) {
			ast_cli(a->fd, "Could not update mailbox %s.\n",
				ast_sorcery_object_get_id(mailbox));
		} else {
			ast_cli(a->fd, "Updated mailbox %s.\n", ast_sorcery_object_get_id(mailbox));
		}

		ast_mwi_mailbox_unref(mailbox);
	}

	return CLI_SUCCESS;
}
#endif	/* defined(MWI_DEBUG_CLI) */

#if defined(MWI_DEBUG_CLI)
static struct ast_cli_entry mwi_cli[] = {
	AST_CLI_DEFINE(handle_mwi_delete_all, "Delete all external MWI mailboxes"),
	AST_CLI_DEFINE(handle_mwi_delete_like, "Delete external MWI mailboxes matching regex"),
	AST_CLI_DEFINE(handle_mwi_delete_mailbox, "Delete a specific external MWI mailbox"),
	AST_CLI_DEFINE(handle_mwi_list_all, "List all external MWI mailboxes"),
	AST_CLI_DEFINE(handle_mwi_list_like, "List external MWI mailboxes matching regex"),
	AST_CLI_DEFINE(handle_mwi_show_mailbox, "Show a specific external MWI mailbox"),
	AST_CLI_DEFINE(handle_mwi_update_mailbox, "Update a specific external MWI mailbox"),
};
#endif	/* defined(MWI_DEBUG_CLI) */

/*!
 * \internal
 * \brief Post initial MWI count events.
 * \since 12.1.0
 *
 * \return Nothing
 */
static void mwi_initial_events(void)
{
	struct ao2_container *mailboxes;
	const struct ast_mwi_mailbox_object *mailbox;
	struct ao2_iterator iter;

	/* Get all mailbox counts. */
	mailboxes = ast_mwi_mailbox_get_all();
	if (!mailboxes) {
		return;
	}

	/* Post all mailbox counts. */
	iter = ao2_iterator_init(mailboxes, AO2_ITERATOR_UNLINK);
	for (; (mailbox = ao2_iterator_next(&iter)); ast_mwi_mailbox_unref(mailbox)) {
		mwi_post_event(mailbox);
	}
	ao2_iterator_destroy(&iter);

	ao2_ref(mailboxes, -1);
}

static int unload_module(void)
{
	ast_vm_unregister(vm_table.module_name);
#if defined(MWI_DEBUG_CLI)
	ast_cli_unregister_multiple(mwi_cli, ARRAY_LEN(mwi_cli));
#endif	/* defined(MWI_DEBUG_CLI) */
	ast_sorcery_observer_remove(mwi_sorcery, MWI_MAILBOX_TYPE, &mwi_observers);

	ast_sorcery_unref(mwi_sorcery);
	mwi_sorcery = NULL;

	return 0;
}

static int load_module(void)
{
	int res;

	if (mwi_sorcery_init()
		|| ast_sorcery_observer_add(mwi_sorcery, MWI_MAILBOX_TYPE, &mwi_observers)
#if defined(MWI_DEBUG_CLI)
		|| ast_cli_register_multiple(mwi_cli, ARRAY_LEN(mwi_cli))
#endif	/* defined(MWI_DEBUG_CLI) */
		) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* ast_vm_register may return DECLINE if another module registered for vm */
	res = ast_vm_register(&vm_table);
	if (res) {
		ast_log(LOG_ERROR, "Failure registering as a voicemail provider\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Post initial MWI count events. */
	mwi_initial_events();

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Core external MWI resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
);
