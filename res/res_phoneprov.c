/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 * Copyright (C) 2014, Fairview 5 Engineering, LLC
 *
 * Mark Spencer <markster@digium.com>
 * Matthew Brooks <mbrooks@digium.com>
 * Terry Wilson <twilson@digium.com>
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Phone provisioning application for the asterisk internal http server
 *
 * \author Matthew Brooks <mbrooks@digium.com>
 * \author Terry Wilson <twilson@digium.com>
 * \author George Joseph <george.joseph@fairview5.com>
  */

/*! \li \ref res_phoneprov.c uses the configuration file \ref phoneprov.conf and \ref users.conf and \ref sip.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page phoneprov.conf phoneprov.conf
 * \verbinclude phoneprov.conf.sample
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#define AST_API_MODULE

#include "asterisk.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#ifdef SOLARIS
#include <sys/sockio.h>
#endif
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/paths.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/strings.h"
#include "asterisk/stringfields.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/acl.h"
#include "asterisk/astobj2.h"
#include "asterisk/ast_version.h"
#include "asterisk/phoneprov.h"

#ifdef LOW_MEMORY
#define MAX_PROVIDER_BUCKETS 1
#define MAX_PROFILE_BUCKETS 1
#define MAX_ROUTE_BUCKETS 1
#define MAX_USER_BUCKETS 1
#else
#define MAX_PROVIDER_BUCKETS 17
#define MAX_PROFILE_BUCKETS 17
#define MAX_ROUTE_BUCKETS 563
#define MAX_USER_BUCKETS 563
#endif /* LOW_MEMORY */

#define VAR_BUF_SIZE 4096

/*** DOCUMENTATION
	<function name="PP_EACH_EXTENSION" language="en_US">
		<synopsis>
			Execute specified template for each extension.
		</synopsis>
		<syntax>
			<parameter name="mac" required="true" />
			<parameter name="template_file" required="true" />
		</syntax>
		<description>
			<para>Output the specified template for each extension associated with the specified MAC address.</para>
		</description>
	</function>
	<function name="PP_EACH_USER" language="en_US">
		<synopsis>
			Generate a string for each phoneprov user.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
			<parameter name="exclude_mac" required="true" />
		</syntax>
		<description>
			<para>Pass in a string, with phoneprov variables you want substituted in the format of
			%{VARNAME}, and you will get the string rendered for each user in phoneprov
			excluding ones with MAC address <replaceable>exclude_mac</replaceable>. Probably not
			useful outside of res_phoneprov.</para>
			<para>Example: ${PP_EACH_USER(&lt;item&gt;&lt;fn&gt;%{DISPLAY_NAME}&lt;/fn&gt;&lt;/item&gt;|${MAC})</para>
		</description>
	</function>
 ***/

/*!
 * \brief Creates a hash function for a structure string field.
 * \param fname The name to use for the function
 * \param stype The structure type
 * \param field The field in the structure to hash
 *
 * SIMPLE_HASH_FN(mystruct, myfield) will produce a function
 * named mystruct_hash_fn which hashes mystruct->myfield.
 */
#define SIMPLE_HASH_FN(fname, stype, field) \
static int fname(const void *obj, const int flags) \
{ \
	const struct stype *provider = obj; \
	const char *key; \
	switch (flags & OBJ_SEARCH_MASK) { \
	case OBJ_SEARCH_KEY: \
		key = obj; \
		break; \
	case OBJ_SEARCH_OBJECT: \
		provider = obj; \
		key = provider->field; \
		break; \
	default: \
		ast_assert(0); \
		return 0; \
	} \
	return ast_str_hash(key); \
}

/*!
 * \brief Creates a compare function for a structure string field.
 * \param fname The name to use for the function
 * \param stype The structure type
 * \param field The field in the structure to compare
 *
 * SIMPLE_CMP_FN(mystruct, myfield) will produce a function
 * named mystruct_cmp_fn which compares mystruct->myfield.
 */
#define SIMPLE_CMP_FN(fname, stype, field) \
static int fname(void *obj, void *arg, int flags) \
{ \
	const struct stype *object_left = obj, *object_right = arg; \
	const char *right_key = arg; \
	int cmp; \
	switch (flags & OBJ_SEARCH_MASK) { \
	case OBJ_SEARCH_OBJECT: \
		right_key = object_right->field; \
	case OBJ_SEARCH_KEY: \
		cmp = strcmp(object_left->field, right_key); \
		break; \
	case OBJ_SEARCH_PARTIAL_KEY: \
		cmp = strncmp(object_left->field, right_key, strlen(right_key)); \
		break; \
	default: \
		cmp = 0; \
		break; \
	} \
	if (cmp) { \
		return 0; \
	} \
	return CMP_MATCH; \
}

const char *ast_phoneprov_std_variable_lookup[] = {
	[AST_PHONEPROV_STD_MAC] = "MAC",
	[AST_PHONEPROV_STD_PROFILE] = "PROFILE",
	[AST_PHONEPROV_STD_USERNAME] = "USERNAME",
	[AST_PHONEPROV_STD_DISPLAY_NAME] = "DISPLAY_NAME",
	[AST_PHONEPROV_STD_SECRET] = "SECRET",
	[AST_PHONEPROV_STD_LABEL] = "LABEL",
	[AST_PHONEPROV_STD_CALLERID] = "CALLERID",
	[AST_PHONEPROV_STD_TIMEZONE] = "TIMEZONE",
	[AST_PHONEPROV_STD_LINENUMBER] = "LINE",
	[AST_PHONEPROV_STD_LINEKEYS] = "LINEKEYS",
	[AST_PHONEPROV_STD_SERVER] = "SERVER",
	[AST_PHONEPROV_STD_SERVER_PORT] = "SERVER_PORT",
	[AST_PHONEPROV_STD_SERVER_IFACE] = "SERVER_IFACE",
	[AST_PHONEPROV_STD_VOICEMAIL_EXTEN] = "VOICEMAIL_EXTEN",
	[AST_PHONEPROV_STD_EXTENSION_LENGTH] = "EXTENSION_LENGTH",
	[AST_PHONEPROV_STD_TZOFFSET] = "TZOFFSET",
	[AST_PHONEPROV_STD_DST_ENABLE] = "DST_ENABLE",
	[AST_PHONEPROV_STD_DST_START_MONTH] = "DST_START_MONTH",
	[AST_PHONEPROV_STD_DST_START_MDAY] = "DST_START_MDAY",
	[AST_PHONEPROV_STD_DST_START_HOUR] = "DST_START_HOUR",
	[AST_PHONEPROV_STD_DST_END_MONTH] = "DST_END_MONTH",
	[AST_PHONEPROV_STD_DST_END_MDAY] = "DST_END_MDAY",
	[AST_PHONEPROV_STD_DST_END_HOUR] = "DST_END_HOUR",
};

/* Translate the standard variables to their users.conf equivalents. */
const char *pp_user_lookup[] = {
	[AST_PHONEPROV_STD_MAC] = "macaddress",
	[AST_PHONEPROV_STD_PROFILE] = "profile",
	[AST_PHONEPROV_STD_USERNAME] = "username",
	[AST_PHONEPROV_STD_DISPLAY_NAME] = "fullname",
	[AST_PHONEPROV_STD_SECRET] = "secret",
	[AST_PHONEPROV_STD_LABEL] = "label",
	[AST_PHONEPROV_STD_CALLERID] = "cid_number",
	[AST_PHONEPROV_STD_TIMEZONE] = "timezone",
	[AST_PHONEPROV_STD_LINENUMBER] = "linenumber",
	[AST_PHONEPROV_STD_LINEKEYS] = "linekeys",
	[AST_PHONEPROV_STD_SERVER] = NULL,
	[AST_PHONEPROV_STD_SERVER_PORT] = NULL,
	[AST_PHONEPROV_STD_SERVER_IFACE] = NULL,
	[AST_PHONEPROV_STD_VOICEMAIL_EXTEN] = "vmexten",
	[AST_PHONEPROV_STD_EXTENSION_LENGTH] = "localextenlength",
	[AST_PHONEPROV_STD_TZOFFSET] = NULL,
	[AST_PHONEPROV_STD_DST_ENABLE] = NULL,
	[AST_PHONEPROV_STD_DST_START_MONTH] = NULL,
	[AST_PHONEPROV_STD_DST_START_MDAY] = NULL,
	[AST_PHONEPROV_STD_DST_START_HOUR] = NULL,
	[AST_PHONEPROV_STD_DST_END_MONTH] = NULL,
	[AST_PHONEPROV_STD_DST_END_MDAY] = NULL,
	[AST_PHONEPROV_STD_DST_END_HOUR] = NULL,
};

/* Translate the standard variables to their phoneprov.conf [general] equivalents. */
const char *pp_general_lookup[] = {
	[AST_PHONEPROV_STD_MAC] = NULL,
	[AST_PHONEPROV_STD_PROFILE] = "default_profile",
	[AST_PHONEPROV_STD_USERNAME] = NULL,
	[AST_PHONEPROV_STD_DISPLAY_NAME] = NULL,
	[AST_PHONEPROV_STD_SECRET] = NULL,
	[AST_PHONEPROV_STD_LABEL] = NULL,
	[AST_PHONEPROV_STD_CALLERID] = NULL,
	[AST_PHONEPROV_STD_TIMEZONE] = NULL,
	[AST_PHONEPROV_STD_LINENUMBER] = NULL,
	[AST_PHONEPROV_STD_LINEKEYS] = NULL,
	[AST_PHONEPROV_STD_SERVER] = "serveraddr",
	[AST_PHONEPROV_STD_SERVER_PORT] = "serverport",
	[AST_PHONEPROV_STD_SERVER_IFACE] = "serveriface",
	[AST_PHONEPROV_STD_VOICEMAIL_EXTEN] = NULL,
	[AST_PHONEPROV_STD_EXTENSION_LENGTH] = NULL,
	[AST_PHONEPROV_STD_TZOFFSET] = NULL,
	[AST_PHONEPROV_STD_DST_ENABLE] = NULL,
	[AST_PHONEPROV_STD_DST_START_MONTH] = NULL,
	[AST_PHONEPROV_STD_DST_START_MDAY] = NULL,
	[AST_PHONEPROV_STD_DST_START_HOUR] = NULL,
	[AST_PHONEPROV_STD_DST_END_MONTH] = NULL,
	[AST_PHONEPROV_STD_DST_END_MDAY] = NULL,
	[AST_PHONEPROV_STD_DST_END_HOUR] = NULL,
};

/*! \brief for use in lookup_iface */
static struct in_addr __ourip = { .s_addr = 0x00000000, };

/*! \brief structure to hold config providers */
struct phoneprov_provider {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(provider_name);
	);
	ast_phoneprov_load_users_cb load_users;
};
struct ao2_container *providers;
SIMPLE_HASH_FN(phoneprov_provider_hash_fn, phoneprov_provider, provider_name)
SIMPLE_CMP_FN(phoneprov_provider_cmp_fn, phoneprov_provider, provider_name)

/*! \brief structure to hold file data */
struct phoneprov_file {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(format);	/*!< After variable substitution, becomes route->uri */
		AST_STRING_FIELD(template); /*!< Template/physical file location */
		AST_STRING_FIELD(mime_type);/*!< Mime-type of the file */
	);
	AST_LIST_ENTRY(phoneprov_file) entry;
};

/*! \brief structure to hold extensions */
struct extension {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
	);
	int index;
	struct varshead *headp;	/*!< List of variables to substitute into templates */
	AST_LIST_ENTRY(extension) entry;
};

/*! \brief structure to hold phone profiles read from phoneprov.conf */
struct phone_profile {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);	/*!< Name of phone profile */
		AST_STRING_FIELD(default_mime_type);	/*!< Default mime type if it isn't provided */
		AST_STRING_FIELD(staticdir);	/*!< Subdirectory that static files are stored in */
	);
	struct varshead *headp;	/*!< List of variables set with 'setvar' in phoneprov.conf */
	AST_LIST_HEAD_NOLOCK(, phoneprov_file) static_files;	/*!< List of static files */
	AST_LIST_HEAD_NOLOCK(, phoneprov_file) dynamic_files;	/*!< List of dynamic files */
};
struct ao2_container *profiles;
SIMPLE_HASH_FN(phone_profile_hash_fn, phone_profile, name)
SIMPLE_CMP_FN(phone_profile_cmp_fn, phone_profile, name)

/*! \brief structure to hold users read from users.conf */
struct user {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(macaddress);	/*!< Mac address of user's phone */
		AST_STRING_FIELD(provider_name);	/*!< Name of the provider who registered this mac */
	);
	struct phone_profile *profile;	/*!< Profile the phone belongs to */
	AST_LIST_HEAD_NOLOCK(, extension) extensions;
};
struct ao2_container *users;
SIMPLE_HASH_FN(user_hash_fn, user, macaddress)
SIMPLE_CMP_FN(user_cmp_fn, user, macaddress)

/*! \brief structure to hold http routes (valid URIs, and the files they link to) */
struct http_route {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uri);	/*!< The URI requested */
	);
	struct phoneprov_file *file;	/*!< The file that links to the URI */
	struct user *user;	/*!< The user that has variables to substitute into the file
						 * NULL in the case of a static route */
	struct phone_profile *profile;
};
struct ao2_container *http_routes;
SIMPLE_HASH_FN(http_route_hash_fn, http_route, uri)
SIMPLE_CMP_FN(http_route_cmp_fn, http_route, uri)

#define SIPUSERS_PROVIDER_NAME "sipusers"

/* iface is the interface (e.g. eth0); address is the return value */
static int lookup_iface(const char *iface, struct in_addr *address)
{
	int mysock, res = 0;
	struct ifreq ifr;
	struct sockaddr_in *sin;

	memset(&ifr, 0, sizeof(ifr));
	ast_copy_string(ifr.ifr_name, iface, sizeof(ifr.ifr_name));

	mysock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (mysock < 0) {
		ast_log(LOG_ERROR, "Failed to create socket: %s\n", strerror(errno));
		return -1;
	}

	res = ioctl(mysock, SIOCGIFADDR, &ifr);

	close(mysock);

	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to get IP of %s: %s\n", iface, strerror(errno));
		memcpy(address, &__ourip, sizeof(__ourip));
		return -1;
	} else {
		sin = (struct sockaddr_in *)&ifr.ifr_addr;
		memcpy(address, &sin->sin_addr, sizeof(*address));
		return 0;
	}
}

static struct phoneprov_provider *find_provider(char *name)
{
	return ao2_find(providers, name, OBJ_SEARCH_KEY);
}

/*! \brief Delete all providers */
static void delete_providers(void)
{
	if (!providers) {
		return;
	}

	ao2_callback(providers, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

static void provider_destructor(void *obj)
{
	struct phoneprov_provider *provider = obj;
	ast_string_field_free_memory(provider);
}

static void delete_file(struct phoneprov_file *file)
{
	ast_string_field_free_memory(file);
	free(file);
}

/*! \brief Read a TEXT file into a string and return the length */
static int load_file(const char *filename, char **ret)
{
	int len = 0;
	FILE *f;

	if (!(f = fopen(filename, "r"))) {
		*ret = NULL;
		return -1;
	}

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (!(*ret = ast_malloc(len + 1)))
		return -2;

	if (len != fread(*ret, sizeof(char), len, f)) {
		free(*ret);
		*ret = NULL;
		return -3;
	}

	fclose(f);

	(*ret)[len] = '\0';

	return len;
}

/*! \brief Set all timezone-related variables based on a zone (i.e. America/New_York)
	\param headp pointer to list of user variables
	\param zone A time zone. NULL sets variables based on timezone of the machine
*/
static void set_timezone_variables(struct varshead *headp, const char *zone)
{
	time_t utc_time;
	int dstenable;
	time_t dststart;
	time_t dstend;
	struct ast_tm tm_info;
	int tzoffset;
	char buffer[21];
	struct timeval when;

	time(&utc_time);
	ast_get_dst_info(&utc_time, &dstenable, &dststart, &dstend, &tzoffset, zone);
	snprintf(buffer, sizeof(buffer), "%d", tzoffset);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("TZOFFSET", buffer));

	if (!dstenable) {
		return;
	}

	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_ENABLE", "1"));

	when.tv_sec = dststart;
	ast_localtime(&when, &tm_info, zone);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mon+1);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_START_MONTH", buffer));

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mday);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_START_MDAY", buffer));

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_hour);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_START_HOUR", buffer));

	when.tv_sec = dstend;
	ast_localtime(&when, &tm_info, zone);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mon + 1);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_END_MONTH", buffer));

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mday);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_END_MDAY", buffer));

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_hour);
	AST_VAR_LIST_INSERT_TAIL(headp, ast_var_assign("DST_END_HOUR", buffer));
}

static struct http_route *unref_route(struct http_route *route)
{
	ao2_cleanup(route);

	return NULL;
}

static void route_destructor(void *obj)
{
	struct http_route *route = obj;

	ast_string_field_free_memory(route);
}

/*! \brief Delete all http routes, freeing their memory */
static void delete_routes(void)
{
	if (!http_routes) {
		return;
	}

	ao2_callback(http_routes, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

/*! \brief Build a route structure and add it to the list of available http routes
	\param pp_file File to link to the route
	\param user User to link to the route (NULL means static route)
	\param uri URI of the route
*/
static void build_route(struct phoneprov_file *pp_file, struct phone_profile *profile, struct user *user, char *uri)
{
	struct http_route *route;

	if (!(route = ao2_alloc(sizeof(*route), route_destructor))) {
		return;
	}

	if (ast_string_field_init(route, 32)) {
		ast_log(LOG_ERROR, "Couldn't create string fields for %s\n", pp_file->format);
		route = unref_route(route);
		return;
	}

	ast_string_field_set(route, uri, S_OR(uri, pp_file->format));
	route->user = user;
	route->file = pp_file;
	route->profile = profile;

	ao2_link(http_routes, route);

	route = unref_route(route);
}

static struct phone_profile *unref_profile(struct phone_profile *prof)
{
	ao2_cleanup(prof);

	return NULL;
}

/*! \brief Return a phone profile looked up by name */
static struct phone_profile *find_profile(const char *name)
{
	return ao2_find(profiles, name, OBJ_SEARCH_KEY);
}

static void profile_destructor(void *obj)
{
	struct phone_profile *profile = obj;
	struct phoneprov_file *file;
	struct ast_var_t *var;

	while ((file = AST_LIST_REMOVE_HEAD(&profile->static_files, entry))) {
		delete_file(file);
	}

	while ((file = AST_LIST_REMOVE_HEAD(&profile->dynamic_files, entry))) {
		delete_file(file);
	}

	while ((var = AST_LIST_REMOVE_HEAD(profile->headp, entries))) {
		ast_var_delete(var);
	}

	ast_free(profile->headp);
	ast_string_field_free_memory(profile);
}

/*! \brief Delete all phone profiles, freeing their memory */
static void delete_profiles(void)
{
	if (!profiles) {
		return;
	}

	ao2_callback(profiles, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

/*! \brief Build a phone profile and add it to the list of phone profiles
	\param name the name of the profile
	\param v ast_variable from parsing phoneprov.conf
*/
static void build_profile(const char *name, struct ast_variable *v)
{
	struct phone_profile *profile;

	if (!(profile = ao2_alloc(sizeof(*profile), profile_destructor))) {
		return;
	}

	if (ast_string_field_init(profile, 32)) {
		profile = unref_profile(profile);
		return;
	}

	if (!(profile->headp = ast_var_list_create())) {
		profile = unref_profile(profile);
		return;
	}

	AST_LIST_HEAD_INIT_NOLOCK(&profile->static_files);
	AST_LIST_HEAD_INIT_NOLOCK(&profile->dynamic_files);

	ast_string_field_set(profile, name, name);
	for (; v; v = v->next) {
		if (!strcasecmp(v->name, "mime_type")) {
			ast_string_field_set(profile, default_mime_type, v->value);
		} else if (!strcasecmp(v->name, "setvar")) {
			char *value_copy = ast_strdupa(v->value);

			AST_DECLARE_APP_ARGS(args,
				AST_APP_ARG(varname);
				AST_APP_ARG(varval);
			);

			AST_NONSTANDARD_APP_ARGS(args, value_copy, '=');
			do {
				if (ast_strlen_zero(args.varname) || ast_strlen_zero(args.varval))
					break;
				args.varname = ast_strip(args.varname);
				args.varval = ast_strip(args.varval);
				if (ast_strlen_zero(args.varname) || ast_strlen_zero(args.varval))
					break;
				AST_VAR_LIST_INSERT_TAIL(profile->headp, ast_var_assign(args.varname, args.varval));
			} while (0);
		} else if (!strcasecmp(v->name, "staticdir")) {
			ast_string_field_set(profile, staticdir, v->value);
		} else {
			struct phoneprov_file *pp_file;
			char *file_extension;
			char *value_copy = ast_strdupa(v->value);

			AST_DECLARE_APP_ARGS(args,
				AST_APP_ARG(filename);
				AST_APP_ARG(mimetype);
			);

			if (!(pp_file = ast_calloc_with_stringfields(1, struct phoneprov_file, 32))) {
				profile = unref_profile(profile);
				return;
			}

			if ((file_extension = strrchr(pp_file->format, '.')))
				file_extension++;

			AST_STANDARD_APP_ARGS(args, value_copy);

			/* Mime type order of preference
			 * 1) Specific mime-type defined for file in profile
			 * 2) Mime determined by extension
			 * 3) Default mime type specified in profile
			 * 4) text/plain
			 */
			ast_string_field_set(pp_file, mime_type, S_OR(args.mimetype,
				(S_OR(S_OR(ast_http_ftype2mtype(file_extension), profile->default_mime_type), "text/plain"))));

			if (!strcasecmp(v->name, "static_file")) {
				ast_string_field_set(pp_file, format, args.filename);
				ast_string_field_build(pp_file, template, "%s%s", profile->staticdir, args.filename);
				AST_LIST_INSERT_TAIL(&profile->static_files, pp_file, entry);
				/* Add a route for the static files, as their filenames won't change per-user */
				build_route(pp_file, profile, NULL, NULL);
			} else {
				ast_string_field_set(pp_file, format, v->name);
				ast_string_field_set(pp_file, template, args.filename);
				AST_LIST_INSERT_TAIL(&profile->dynamic_files, pp_file, entry);
			}
		}
	}

	ao2_link(profiles, profile);

	profile = unref_profile(profile);
}

static struct extension *delete_extension(struct extension *exten)
{
	ast_var_list_destroy(exten->headp);
	ast_string_field_free_memory(exten);
	ast_free(exten);

	return NULL;
}

static struct extension *build_extension(const char *name, struct varshead *vars)
{
	struct extension *exten;
	const char *tmp;

	if (!(exten = ast_calloc_with_stringfields(1, struct extension, 32))) {
		return NULL;
	}

	ast_string_field_set(exten, name, name);

	exten->headp = ast_var_list_clone(vars);
	if (!exten->headp) {
		ast_log(LOG_ERROR, "Unable to clone variables for extension '%s'\n", name);
		delete_extension(exten);
		return NULL;
	}

	tmp = ast_var_find(exten->headp, ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_LINENUMBER]);
	if (!tmp) {
		AST_VAR_LIST_INSERT_TAIL(exten->headp,
			ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_LINENUMBER], "1"));
		exten->index = 1;
	} else {
		sscanf(tmp, "%d", &exten->index);
	}

	if (!ast_var_find(exten->headp, ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_LINEKEYS])) {
		AST_VAR_LIST_INSERT_TAIL(exten->headp,
			ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_LINEKEYS], "1"));
	}

	set_timezone_variables(exten->headp,
		ast_var_find(vars, ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_TIMEZONE]));

	return exten;
}

static struct user *unref_user(struct user *user)
{
	ao2_cleanup(user);

	return NULL;
}

/*! \brief Return a user looked up by name */
static struct user *find_user(const char *macaddress)
{
	return ao2_find(users, macaddress, OBJ_SEARCH_KEY);
}

static int routes_delete_cb(void *obj, void *arg, int flags)
{
	struct http_route *route = obj;
	struct user *user = route->user;
	char *macaddress = arg;

	if (user && !strcmp(user->macaddress, macaddress)) {
		return CMP_MATCH;
	}
	return 0;
}

/*! \brief Free all memory associated with a user */
static void user_destructor(void *obj)
{
	struct user *user = obj;
	struct extension *exten;

	while ((exten = AST_LIST_REMOVE_HEAD(&user->extensions, entry))) {
		exten = delete_extension(exten);
	}

	if (user->profile) {
		user->profile = unref_profile(user->profile);
	}

	ao2_callback(http_routes, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, routes_delete_cb, (void *)user->macaddress);

	ast_string_field_free_memory(user);
}

/*! \brief Delete all users */
static void delete_users(void)
{
	if (!users) {
		return;
	}

	ao2_callback(users, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

/*! \brief Build and return a user structure based on gathered config data */
static struct user *build_user(const char *mac, struct phone_profile *profile, char *provider_name)
{
	struct user *user;

	if (!(user = ao2_alloc(sizeof(*user), user_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(user, 64)) {
		user = unref_user(user);
		return NULL;
	}

	ast_string_field_set(user, macaddress, mac);
	ast_string_field_set(user, provider_name, provider_name);
	user->profile = profile;
	ao2_ref(profile, 1);

	return user;
}

/*! \brief Add an extension to a user ordered by index/linenumber */
static int add_user_extension(struct user *user, struct extension *exten)
{
	struct ast_var_t *pvar, *var2;
	struct ast_str *str = ast_str_create(16);

	if (!str) {
		return -1;
	}

	/* Append profile variables here, and substitute variables on profile
	 * setvars, so that we can use user specific variables in them */
	AST_VAR_LIST_TRAVERSE(user->profile->headp, pvar) {
		if (ast_var_find(exten->headp, pvar->name)) {
			continue;
		}

		ast_str_substitute_variables_varshead(&str, 0, exten->headp, pvar->value);
		if ((var2 = ast_var_assign(pvar->name, ast_str_buffer(str)))) {
			AST_VAR_LIST_INSERT_TAIL(exten->headp, var2);
		}
	}
	ast_free(str);

	if (AST_LIST_EMPTY(&user->extensions)) {
		AST_LIST_INSERT_HEAD(&user->extensions, exten, entry);
	} else {
		struct extension *exten_iter;

		AST_LIST_TRAVERSE_SAFE_BEGIN(&user->extensions, exten_iter, entry) {
			if (exten->index < exten_iter->index) {
				AST_LIST_INSERT_BEFORE_CURRENT(exten, entry);
			} else if (exten->index == exten_iter->index) {
				ast_log(LOG_WARNING, "Duplicate linenumber=%d for %s\n", exten->index, user->macaddress);
				return -1;
			} else if (!AST_LIST_NEXT(exten_iter, entry)) {
				AST_LIST_INSERT_TAIL(&user->extensions, exten, entry);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	return 0;
}

/*! \brief Add an http route for dynamic files attached to the profile of the user */
static int build_user_routes(struct user *user)
{
	struct phoneprov_file *pp_file;
	struct ast_str *str;

	if (!(str = ast_str_create(16))) {
		return -1;
	}

	AST_LIST_TRAVERSE(&user->profile->dynamic_files, pp_file, entry) {
		ast_str_substitute_variables_varshead(&str, 0, AST_LIST_FIRST(&user->extensions)->headp, pp_file->format);
		build_route(pp_file, user->profile, user, ast_str_buffer(str));
	}

	ast_free(str);
	return 0;
}

/*! \brief Callback that is executed everytime an http request is received by this module */
static int phoneprov_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers)
{
	struct http_route *route;
	struct ast_str *result;
	char path[PATH_MAX];
	char *file = NULL;
	char *server;
	int len;
	int fd;
	struct ast_str *http_header;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return 0;
	}

	if (!(route = ao2_find(http_routes, uri, OBJ_SEARCH_KEY))) {
		goto out404;
	}

	snprintf(path, sizeof(path), "%s/phoneprov/%s", ast_config_AST_DATA_DIR, route->file->template);

	if (!route->user) { /* Static file */

		fd = open(path, O_RDONLY);
		if (fd < 0) {
			goto out500;
		}

		len = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		if (len < 0) {
			ast_log(LOG_WARNING, "Could not load file: %s (%d)\n", path, len);
			close(fd);
			goto out500;
		}

		http_header = ast_str_create(80);
		ast_str_set(&http_header, 0, "Content-type: %s\r\n",
			route->file->mime_type);

		ast_http_send(ser, method, 200, NULL, http_header, NULL, fd, 0);

		close(fd);
		route = unref_route(route);
		return 0;
	} else { /* Dynamic file */
		struct ast_str *tmp;

		len = load_file(path, &file);
		if (len < 0) {
			ast_log(LOG_WARNING, "Could not load file: %s (%d)\n", path, len);
			if (file) {
				ast_free(file);
			}

			goto out500;
		}

		if (!file) {
			goto out500;
		}

		if (!(tmp = ast_str_create(len))) {
			if (file) {
				ast_free(file);
			}

			goto out500;
		}

		/* Unless we are overridden by serveriface or serveraddr, we set the SERVER variable to
		 * the IP address we are listening on that the phone contacted for this config file */

		server = ast_var_find(AST_LIST_FIRST(&route->user->extensions)->headp,
			ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_SERVER]);

		if (!server) {
			union {
				struct sockaddr sa;
				struct sockaddr_in sa_in;
			} name;
			socklen_t namelen = sizeof(name.sa);
			int res;

			if ((res = getsockname(ser->fd, &name.sa, &namelen))) {
				ast_log(LOG_WARNING, "Could not get server IP, breakage likely.\n");
			} else {
				struct extension *exten_iter;
				const char *newserver = ast_inet_ntoa(name.sa_in.sin_addr);

				AST_LIST_TRAVERSE(&route->user->extensions, exten_iter, entry) {
					AST_VAR_LIST_INSERT_TAIL(exten_iter->headp,
						ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_SERVER], newserver));
				}
			}
		}

		ast_str_substitute_variables_varshead(&tmp, 0, AST_LIST_FIRST(&route->user->extensions)->headp, file);

		ast_free(file);

		http_header = ast_str_create(80);
		ast_str_set(&http_header, 0, "Content-type: %s\r\n",
			route->file->mime_type);

		if (!(result = ast_str_create(512))) {
			ast_log(LOG_ERROR, "Could not create result string!\n");
			if (tmp) {
				ast_free(tmp);
			}
			ast_free(http_header);
			goto out500;
		}
		ast_str_append(&result, 0, "%s", ast_str_buffer(tmp));

		ast_http_send(ser, method, 200, NULL, http_header, result, 0, 0);
		ast_free(tmp);

		route = unref_route(route);

		return 0;
	}

out404:
	ast_http_error(ser, 404, "Not Found", uri);
	return 0;

out500:
	route = unref_route(route);
	ast_http_error(ser, 500, "Internal Error", "An internal error has occured.");
	return 0;
}

/*! \brief A dialplan function that can be used to print a string for each phoneprov user */
static int pp_each_user_helper(struct ast_channel *chan, char *data, char *buf, struct ast_str **bufstr, int len)
{
	char *tmp;
	struct ao2_iterator i;
	struct user *user;
	struct ast_str *str;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(string);
		AST_APP_ARG(exclude_mac);
	);
	AST_STANDARD_APP_ARGS(args, data);

	if (!(str = ast_str_create(16))) {
		return -1;
	}

	/* Fix data by turning %{ into ${ */
	while ((tmp = strstr(args.string, "%{")))
		*tmp = '$';

	i = ao2_iterator_init(users, 0);
	while ((user = ao2_iterator_next(&i))) {
		if (!ast_strlen_zero(args.exclude_mac) && !strcasecmp(user->macaddress, args.exclude_mac)) {
			continue;
		}
		ast_str_substitute_variables_varshead(&str, len, AST_LIST_FIRST(&user->extensions)->headp, args.string);
		if (buf) {
			size_t slen = len;
			ast_build_string(&buf, &slen, "%s", ast_str_buffer(str));
		} else {
			ast_str_append(bufstr, len, "%s", ast_str_buffer(str));
		}
		user = unref_user(user);
	}
	ao2_iterator_destroy(&i);

	ast_free(str);
	return 0;
}

static int pp_each_user_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	return pp_each_user_helper(chan, data, buf, NULL, len);
}

static int pp_each_user_read2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	return pp_each_user_helper(chan, data, NULL, buf, len);
}

static struct ast_custom_function pp_each_user_function = {
	.name = "PP_EACH_USER",
	.read = pp_each_user_read,
	.read2 = pp_each_user_read2,
};

/*! \brief A dialplan function that can be used to output a template for each extension attached to a user */
static int pp_each_extension_helper(struct ast_channel *chan, const char *cmd, char *data, char *buf, struct ast_str **bufstr, int len)
{
	struct user *user;
	struct extension *exten;
	char path[PATH_MAX];
	char *file;
	int filelen;
	struct ast_str *str;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(mac);
		AST_APP_ARG(template);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.mac) || ast_strlen_zero(args.template)) {
		ast_log(LOG_WARNING, "PP_EACH_EXTENSION requries both a macaddress and template filename.\n");
		return 0;
	}

	if (!(user = find_user(args.mac))) {
		ast_log(LOG_WARNING, "Could not find user with mac = '%s'\n", args.mac);
		return 0;
	}

	snprintf(path, sizeof(path), "%s/phoneprov/%s", ast_config_AST_DATA_DIR, args.template);
	filelen = load_file(path, &file);
	if (filelen < 0) {
		ast_log(LOG_WARNING, "Could not load file: %s (%d)\n", path, filelen);
		if (file) {
			ast_free(file);
		}
		return 0;
	}

	if (!file) {
		return 0;
	}

	if (!(str = ast_str_create(filelen))) {
		return 0;
	}

	AST_LIST_TRAVERSE(&user->extensions, exten, entry) {
		ast_str_substitute_variables_varshead(&str, 0, exten->headp, file);
		if (buf) {
			size_t slen = len;
			ast_build_string(&buf, &slen, "%s", ast_str_buffer(str));
		} else {
			ast_str_append(bufstr, len, "%s", ast_str_buffer(str));
		}
	}

	ast_free(file);
	ast_free(str);

	user = unref_user(user);

	return 0;
}

static int pp_each_extension_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	return pp_each_extension_helper(chan, cmd, data, buf, NULL, len);
}

static int pp_each_extension_read2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	return pp_each_extension_helper(chan, cmd, data, NULL, buf, len);
}

static struct ast_custom_function pp_each_extension_function = {
	.name = "PP_EACH_EXTENSION",
	.read = pp_each_extension_read,
	.read2 = pp_each_extension_read2,
};

#define FORMATS "%-20.20s %-40.40s  %-30.30s\n"
#define FORMATD "%-20.20s %-20.20s %-40.40s  %-30.30s\n"
static int route_list_cb(void *obj, void *arg, void *data, int flags)
{
	int fd = *(int *)arg;
	struct http_route *route = obj;

	if (data && route->user) {
		ast_cli(fd, FORMATD, route->user->provider_name, route->profile->name, route->uri, route->file->template);
	}
	if (!data && !route->user) {
		ast_cli(fd, FORMATS, route->profile->name, route->uri, route->file->template);
	}

	return CMP_MATCH;
}

/*! \brief CLI command to list static and dynamic routes */
static char *handle_show_routes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int fd = a->fd;
	switch(cmd) {
	case CLI_INIT:
		e->command = "phoneprov show routes";
		e->usage =
			"Usage: phoneprov show routes\n"
			"       Lists all registered phoneprov http routes.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	/* This currently iterates over routes twice, but it is the only place I've needed
	 * to really separate static an dynamic routes, so I've just left it this way. */
	ast_cli(a->fd, "Static routes\n\n");
	ast_cli(a->fd, FORMATS, "Profile", "Relative URI", "Physical location");

	ao2_callback_data(http_routes, OBJ_NODATA | OBJ_MULTIPLE, route_list_cb, &fd, NULL);

	ast_cli(a->fd, "\nDynamic routes\n\n");
	ast_cli(a->fd, FORMATD, "Provider", "Profile", "Relative URI", "Template");

	ao2_callback_data(http_routes, OBJ_NODATA | OBJ_MULTIPLE, route_list_cb, &fd, (void *)1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry pp_cli[] = {
	AST_CLI_DEFINE(handle_show_routes, "Show registered phoneprov http routes"),
};

static struct ast_http_uri phoneprovuri = {
	.callback = phoneprov_callback,
	.description = "Asterisk HTTP Phone Provisioning Tool",
	.uri = "phoneprov",
	.has_subtree = 1,
	.data = NULL,
	.key = __FILE__,
};

static struct varshead *get_defaults(void)
{
	struct ast_config *phoneprov_cfg;
	struct ast_config *cfg;
	const char *value;
	struct ast_variable *v;
	struct ast_var_t *var;
	struct ast_flags config_flags = { 0 };
	struct varshead *defaults = ast_var_list_create();

	if (!defaults) {
		ast_log(LOG_ERROR, "Unable to create default var list.\n");
		return NULL;
	}

	if (!(phoneprov_cfg = ast_config_load("phoneprov.conf", config_flags))
		|| phoneprov_cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load config phoneprov.conf\n");
		ast_var_list_destroy(defaults);
		return NULL;
	}

	value = ast_variable_retrieve(phoneprov_cfg, "general", pp_general_lookup[AST_PHONEPROV_STD_SERVER]);
	if (!value) {
		struct in_addr addr;
		value = ast_variable_retrieve(phoneprov_cfg, "general", pp_general_lookup[AST_PHONEPROV_STD_SERVER_IFACE]);
		if (value) {
			lookup_iface(value, &addr);
			value = ast_inet_ntoa(addr);
		}
	}
	if (value) {
		var = ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_SERVER], value);
		AST_VAR_LIST_INSERT_TAIL(defaults, var);
	} else {
		ast_log(LOG_WARNING, "Unable to find a valid server address or name.\n");
	}

	value = ast_variable_retrieve(phoneprov_cfg, "general", pp_general_lookup[AST_PHONEPROV_STD_SERVER_PORT]);
	if (!value) {
		if ((cfg = ast_config_load("sip.conf", config_flags)) && cfg != CONFIG_STATUS_FILEINVALID) {
			value = ast_variable_retrieve(cfg, "general", "bindport");
			ast_config_destroy(cfg);
		}
	}
	var = ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_SERVER_PORT], S_OR(value, "5060"));
	AST_VAR_LIST_INSERT_TAIL(defaults, var);

	value = ast_variable_retrieve(phoneprov_cfg, "general", pp_general_lookup[AST_PHONEPROV_STD_PROFILE]);
	if (!value) {
		ast_log(LOG_ERROR, "Unable to load default profile.\n");
		ast_config_destroy(phoneprov_cfg);
		ast_var_list_destroy(defaults);
		return NULL;
	}
	var = ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_PROFILE], value);
	AST_VAR_LIST_INSERT_TAIL(defaults, var);
	ast_config_destroy(phoneprov_cfg);

	if (!(cfg = ast_config_load("users.conf", config_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load users.conf\n");
		ast_var_list_destroy(defaults);
		return NULL;
	}

	/* Go ahead and load global variables from users.conf so we can append to profiles */
	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, pp_user_lookup[AST_PHONEPROV_STD_VOICEMAIL_EXTEN])) {
			var = ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_VOICEMAIL_EXTEN], v->value);
			AST_VAR_LIST_INSERT_TAIL(defaults, var);
		}
		if (!strcasecmp(v->name, pp_user_lookup[AST_PHONEPROV_STD_EXTENSION_LENGTH])) {
			var = ast_var_assign(ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_EXTENSION_LENGTH], v->value);
			AST_VAR_LIST_INSERT_TAIL(defaults, var);
		}
	}
	ast_config_destroy(cfg);

	return defaults;
}

static int load_users(void)
{
	struct ast_config *cfg;
	char *cat;
	const char *value;
	struct ast_flags config_flags = { 0 };
	struct varshead *defaults = get_defaults();

	if (!defaults) {
		ast_log(LOG_WARNING, "Unable to load default variables.\n");
		return -1;
	}

	if (!(cfg = ast_config_load("users.conf", config_flags))
		|| cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load users.conf\n");
		return -1;
	}

	cat = NULL;
	while ((cat = ast_category_browse(cfg, cat))) {
		const char *tmp;
		int i;
		struct ast_var_t *varx;
		struct ast_var_t *vard;

		if (strcasecmp(cat, "general") && strcasecmp(cat, "authentication")) {
			struct varshead *variables = ast_var_list_create();

			if (!((tmp = ast_variable_retrieve(cfg, cat, "autoprov")) && ast_true(tmp))) {
				ast_var_list_destroy(variables);
				continue;
			}

			/* Transfer the standard variables */
			for (i = 0; i < AST_PHONEPROV_STD_VAR_LIST_LENGTH; i++) {
				if (pp_user_lookup[i]) {
					value = ast_variable_retrieve(cfg, cat, pp_user_lookup[i]);
					if (value) {
						varx = ast_var_assign(ast_phoneprov_std_variable_lookup[i],
							value);
						AST_VAR_LIST_INSERT_TAIL(variables, varx);
					}
				}
			}

			if (!ast_var_find(variables, ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_MAC])) {
				ast_log(LOG_WARNING, "autoprov set for %s, but no mac address - skipping.\n", cat);
				ast_var_list_destroy(variables);
				continue;
			}

			/* Apply defaults */
			AST_VAR_LIST_TRAVERSE(defaults, vard) {
				if (ast_var_find(variables, vard->name)) {
					continue;
				}
				varx = ast_var_assign(vard->name, vard->value);
				AST_VAR_LIST_INSERT_TAIL(variables, varx);
			}

			ast_phoneprov_add_extension(SIPUSERS_PROVIDER_NAME, variables);
		}
	}
	ast_config_destroy(cfg);
	return 0;
}

static int load_common(void)
{
	struct ast_config *phoneprov_cfg;
	struct ast_flags config_flags = { 0 };
	char *cat;

	if (!(phoneprov_cfg = ast_config_load("phoneprov.conf", config_flags))
		|| phoneprov_cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load config phoneprov.conf\n");
		return -1;
	}

	cat = NULL;
	while ((cat = ast_category_browse(phoneprov_cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			continue;
		}
		build_profile(cat, ast_variable_browse(phoneprov_cfg, cat));
	}
	ast_config_destroy(phoneprov_cfg);

	if (!ao2_container_count(profiles)) {
		ast_log(LOG_ERROR, "There are no provisioning profiles in phoneprov.conf.\n");
		return -1;
	}

	return 0;
}

static int unload_module(void)
{
	ast_http_uri_unlink(&phoneprovuri);
	ast_custom_function_unregister(&pp_each_user_function);
	ast_custom_function_unregister(&pp_each_extension_function);
	ast_cli_unregister_multiple(pp_cli, ARRAY_LEN(pp_cli));

	/* This cleans up the sip.conf/users.conf provider (called specifically for clarity) */
	ast_phoneprov_provider_unregister(SIPUSERS_PROVIDER_NAME);

	/* This cleans up the framework which also cleans up the providers. */
	delete_profiles();
	ao2_cleanup(profiles);
	profiles = NULL;
	delete_routes();
	ao2_cleanup(http_routes);
	http_routes = NULL;
	delete_users();
	ao2_cleanup(users);
	users = NULL;
	delete_providers();
	ao2_cleanup(providers);
	providers = NULL;

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	profiles = ao2_container_alloc(MAX_PROFILE_BUCKETS, phone_profile_hash_fn, phone_profile_cmp_fn);
	if (!profiles) {
		ast_log(LOG_ERROR, "Unable to allocate profiles container.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	http_routes = ao2_container_alloc(MAX_ROUTE_BUCKETS, http_route_hash_fn, http_route_cmp_fn);
	if (!http_routes) {
		ast_log(LOG_ERROR, "Unable to allocate routes container.\n");
		goto error;
	}

	if (load_common()) {
		ast_log(LOG_ERROR, "Unable to load provisioning profiles.\n");
		goto error;
	}

	users = ao2_container_alloc(MAX_USER_BUCKETS, user_hash_fn, user_cmp_fn);
	if (!users) {
		ast_log(LOG_ERROR, "Unable to allocate users container.\n");
		goto error;
	}

	providers = ao2_container_alloc(MAX_PROVIDER_BUCKETS, phoneprov_provider_hash_fn, phoneprov_provider_cmp_fn);
	if (!providers) {
		ast_log(LOG_ERROR, "Unable to allocate providers container.\n");
		goto error;
	}

	/* Register ourselves as the provider for sip.conf/users.conf */
	if (ast_phoneprov_provider_register(SIPUSERS_PROVIDER_NAME, load_users)) {
		ast_log(LOG_WARNING, "Unable register sip/users config provider.  Others may succeed.\n");
	}

	ast_http_uri_link(&phoneprovuri);

	ast_custom_function_register(&pp_each_user_function);
	ast_custom_function_register(&pp_each_extension_function);
	ast_cli_register_multiple(pp_cli, ARRAY_LEN(pp_cli));

	return AST_MODULE_LOAD_SUCCESS;

error:
	unload_module();
	return AST_MODULE_LOAD_DECLINE;
}

static int reload(void)
{
	struct ao2_iterator i;
	struct phoneprov_provider *provider;

	/* Clean everything except the providers */
	delete_routes();
	delete_users();
	delete_profiles();

	/* Reload the profiles */
	if (load_common()) {
		ast_log(LOG_ERROR, "Unable to reload provisioning profiles.\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* For each provider, reload the users */
	ao2_lock(providers);
	i = ao2_iterator_init(providers, 0);
	for(; (provider = ao2_iterator_next(&i)); ao2_ref(provider, -1)) {
		ast_log(LOG_VERBOSE, "Reloading provider '%s' users.\n", provider->provider_name);
		if (provider->load_users()) {
			ast_log(LOG_ERROR, "Unable to load provider '%s' users. Reload aborted.\n", provider->provider_name);
			continue;
		}
	}
	ao2_iterator_destroy(&i);
	ao2_unlock(providers);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT | AST_MODFLAG_GLOBAL_SYMBOLS, "HTTP Phone Provisioning",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	);

/****  Public API for register/unregister, set defaults, and add extension. ****/

int ast_phoneprov_provider_register(char *provider_name,
	ast_phoneprov_load_users_cb load_users)
{
	struct phoneprov_provider *provider;

	if (ast_strlen_zero(provider_name)) {
		ast_log(LOG_ERROR, "Provider name can't be empty.\n");
		return -1;
	}

	if (!providers) {
		ast_log(LOG_WARNING, "Provider '%s' cannot be registered: res_phoneprov not loaded.\n", provider_name);
		return -1;
	}

	provider = find_provider(provider_name);
	if (provider) {
		ast_log(LOG_ERROR, "There is already a provider registered named '%s'.\n", provider_name);
		ao2_ref(provider, -1);
		return -1;
	}

	provider = ao2_alloc(sizeof(struct phoneprov_provider), provider_destructor);
	if (!provider) {
		ast_log(LOG_ERROR, "Unable to allocate sufficient memory for provider '%s'.\n", provider_name);
		return -1;
	}

	if (ast_string_field_init(provider, 32)) {
		ao2_ref(provider, -1);
		ast_log(LOG_ERROR, "Unable to allocate sufficient memory for provider '%s' stringfields.\n", provider_name);
		return -1;
	}

	ast_string_field_set(provider, provider_name, provider_name);
	provider->load_users = load_users;

	ao2_link(providers, provider);
	ao2_ref(provider, -1);

	if (provider->load_users()) {
		ast_log(LOG_ERROR, "Unable to load provider '%s' users. Register aborted.\n", provider_name);
		ast_phoneprov_provider_unregister(provider_name);
		return -1;
	}

	ast_log(LOG_VERBOSE, "Registered phoneprov provider '%s'.\n", provider_name);
	return 0;
}

static int extensions_delete_cb(void *obj, void *arg, int flags)
{
	char *provider_name = arg;
	struct user *user = obj;
	if (strcmp(user->provider_name, provider_name)) {
		return 0;
	}
	return CMP_MATCH;
}

static int extension_delete_cb(void *obj, void *arg, void *data, int flags)
{
	struct user *user = obj;
	char *provider_name = data;
	char *macaddress = arg;

	if (!strcmp(user->provider_name, provider_name) && !strcasecmp(user->macaddress, macaddress)) {
		return CMP_MATCH;
	}
	return 0;
}

void ast_phoneprov_delete_extension(char *provider_name, char *macaddress)
{
	ao2_callback_data(users, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE | OBJ_SEARCH_KEY,
		extension_delete_cb, macaddress, provider_name);
}

void ast_phoneprov_delete_extensions(char *provider_name)
{
	ao2_callback(users, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, extensions_delete_cb, provider_name);
}

void ast_phoneprov_provider_unregister(char *provider_name)
{
	if (!providers) {
		return;
	}

	ast_phoneprov_delete_extensions(provider_name);
	ao2_find(providers, provider_name, OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);
	ast_log(LOG_VERBOSE, "Unegistered phoneprov provider '%s'.\n", provider_name);
}

int ast_phoneprov_add_extension(char *provider_name, struct varshead *vars)
{
	RAII_VAR(struct phoneprov_provider *, provider, NULL, ao2_cleanup);
	RAII_VAR(struct user *, user, NULL, ao2_cleanup);
	RAII_VAR(struct phone_profile *, profile, NULL, ao2_cleanup);
	struct extension *exten;
	char *profile_name;
	char *mac;
	char *username;

	if (ast_strlen_zero(provider_name)) {
		ast_log(LOG_ERROR, "Provider name can't be empty.\n");
		return -1;
	}
	if (!vars) {
		ast_log(LOG_ERROR, "Variable list can't be empty.\n");
		return -1;
	}

	username = ast_var_find(vars, ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_USERNAME]);
	if (!username) {
		ast_log(LOG_ERROR, "Extension name can't be empty.\n");
		return -1;
	}

	mac = ast_var_find(vars, ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_MAC]);
	if (!mac) {
		ast_log(LOG_ERROR, "MAC Address can't be empty.\n");
		return -1;
	}

	provider = find_provider(provider_name);
	if (!provider) {
		ast_log(LOG_ERROR, "Provider '%s' wasn't found in the registry.\n", provider_name);
		return -1;
	}

	profile_name = ast_var_find(vars,
		ast_phoneprov_std_variable_lookup[AST_PHONEPROV_STD_PROFILE]);
	if (!profile_name) {
		ast_log(LOG_ERROR, "No profile could be found for user '%s' - skipping.\n", username);
		return -1;
	}
	if (!(profile = find_profile(profile_name))) {
		ast_log(LOG_ERROR, "Could not look up profile '%s' - skipping.\n", profile_name);
		return -1;
	}

	if (!(user = find_user(mac))) {

		if (!(user = build_user(mac, profile, provider_name))) {
			ast_log(LOG_ERROR, "Could not create user for '%s' - skipping\n", mac);
			return -1;
		}

		if (!(exten = build_extension(username, vars))) {
			ast_log(LOG_ERROR, "Could not create extension for '%s' - skipping\n", user->macaddress);
			return -1;
		}

		if (add_user_extension(user, exten)) {
			ast_log(LOG_WARNING, "Could not add extension '%s' to user '%s'\n", exten->name, user->macaddress);
			exten = delete_extension(exten);
			return -1;
		}

		if (build_user_routes(user)) {
			ast_log(LOG_WARNING, "Could not create http routes for '%s' - skipping\n", user->macaddress);
			return -1;
		}
		ast_log(LOG_VERBOSE, "Created %s/%s for provider '%s'.\n", username, mac, provider_name);
		ao2_link(users, user);

	} else {
		if (strcmp(provider_name, user->provider_name)) {
			ast_log(LOG_ERROR, "MAC address '%s' was already added by provider '%s' - skipping\n", user->macaddress, user->provider_name);
			return -1;
		}

		if (!(exten = build_extension(username, vars))) {
			ast_log(LOG_ERROR, "Could not create extension for '%s' - skipping\n", user->macaddress);
			return -1;
		}

		if (add_user_extension(user, exten)) {
			ast_log(LOG_WARNING, "Could not add extension '%s' to user '%s'\n", exten->name, user->macaddress);
			exten = delete_extension(exten);
			return -1;
		}
		ast_log(LOG_VERBOSE, "Added %s/%s for provider '%s'.\n", username, mac, provider_name);
	}

	return 0;
}
