/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Matthew Brooks <mbrooks@digium.com>
 * Terry Wilson <twilson@digium.com>
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
 */

/*** MODULEINFO
	<support_level>extended</support_level>
	<defaultenabled>no</defaultenabled>
 ***/

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

#ifdef LOW_MEMORY
#define MAX_PROFILE_BUCKETS 1
#define MAX_ROUTE_BUCKETS 1
#define MAX_USER_BUCKETS 1
#else
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
			<parameter name="template" required="true" />
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

/*! \brief for use in lookup_iface */
static struct in_addr __ourip = { .s_addr = 0x00000000, };

/* \note This enum and the pp_variable_list must be in the same order or
 * bad things happen! */
enum pp_variables {
	PP_MACADDRESS,
	PP_USERNAME,
	PP_FULLNAME,
	PP_SECRET,
	PP_LABEL,
	PP_CALLERID,
	PP_TIMEZONE,
	PP_LINENUMBER,
	PP_LINEKEYS,
	PP_VAR_LIST_LENGTH,	/* This entry must always be the last in the list */
};

/*! \brief Lookup table to translate between users.conf property names and
 * variables for use in phoneprov templates */
static const struct pp_variable_lookup {
	enum pp_variables id;
	const char * const user_var;
	const char * const template_var;
} pp_variable_list[] = {
	{ PP_MACADDRESS, "macaddress", "MAC" },
	{ PP_USERNAME, "username", "USERNAME" },
	{ PP_FULLNAME, "fullname", "DISPLAY_NAME" },
	{ PP_SECRET, "secret", "SECRET" },
	{ PP_LABEL, "label", "LABEL" },
	{ PP_CALLERID, "cid_number", "CALLERID" },
	{ PP_TIMEZONE, "timezone", "TIMEZONE" },
	{ PP_LINENUMBER, "linenumber", "LINE" },
 	{ PP_LINEKEYS, "linekeys", "LINEKEYS" },
};

/*! \brief structure to hold file data */
struct phoneprov_file {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(format);	/*!< After variable substitution, becomes route->uri */
		AST_STRING_FIELD(template); /*!< Template/physical file location */
		AST_STRING_FIELD(mime_type);/*!< Mime-type of the file */
	);
	AST_LIST_ENTRY(phoneprov_file) entry;
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

struct extension {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
	);
	int index;
	struct varshead *headp;	/*!< List of variables to substitute into templates */
	AST_LIST_ENTRY(extension) entry;
};

/*! \brief structure to hold users read from users.conf */
struct user {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(macaddress);	/*!< Mac address of user's phone */
	);
	struct phone_profile *profile;	/*!< Profile the phone belongs to */
	AST_LIST_HEAD_NOLOCK(, extension) extensions;
};

/*! \brief structure to hold http routes (valid URIs, and the files they link to) */
struct http_route {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uri);	/*!< The URI requested */
	);
	struct phoneprov_file *file;	/*!< The file that links to the URI */
	struct user *user;	/*!< The user that has variables to substitute into the file
						 * NULL in the case of a static route */
};

static struct ao2_container *profiles;
static struct ao2_container *http_routes;
static struct ao2_container *users;

static char global_server[80] = "";	/*!< Server to substitute into templates */
static char global_serverport[6] = "";	/*!< Server port to substitute into templates */
static char global_default_profile[80] = "";	/*!< Default profile to use if one isn't specified */

/*! \brief List of global variables currently available: VOICEMAIL_EXTEN, EXTENSION_LENGTH */
static struct varshead global_variables;
static ast_mutex_t globals_lock;

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

static struct phone_profile *unref_profile(struct phone_profile *prof)
{
	ao2_ref(prof, -1);

	return NULL;
}

/*! \brief Return a phone profile looked up by name */
static struct phone_profile *find_profile(const char *name)
{
	struct phone_profile tmp = {
		.name = name,
	};

	return ao2_find(profiles, &tmp, OBJ_POINTER);
}

static int profile_hash_fn(const void *obj, const int flags)
{
	const struct phone_profile *profile = obj;

	return ast_str_case_hash(profile->name);
}

static int profile_cmp_fn(void *obj, void *arg, int flags)
{
	const struct phone_profile *profile1 = obj, *profile2 = arg;

	return !strcasecmp(profile1->name, profile2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static void delete_file(struct phoneprov_file *file)
{
	ast_string_field_free_memory(file);
	free(file);
}

static void profile_destructor(void *obj)
{
	struct phone_profile *profile = obj;
	struct phoneprov_file *file;
	struct ast_var_t *var;

	while ((file = AST_LIST_REMOVE_HEAD(&profile->static_files, entry)))
		delete_file(file);

	while ((file = AST_LIST_REMOVE_HEAD(&profile->dynamic_files, entry)))
		delete_file(file);

	while ((var = AST_LIST_REMOVE_HEAD(profile->headp, entries)))
		ast_var_delete(var);

	ast_free(profile->headp);
	ast_string_field_free_memory(profile);
}

static struct http_route *unref_route(struct http_route *route)
{
	ao2_ref(route, -1);

	return NULL;
}

static int routes_hash_fn(const void *obj, const int flags)
{
	const struct http_route *route = obj;

	return ast_str_case_hash(route->uri);
}

static int routes_cmp_fn(void *obj, void *arg, int flags)
{
	const struct http_route *route1 = obj, *route2 = arg;

	return !strcasecmp(route1->uri, route2->uri) ? CMP_MATCH | CMP_STOP : 0;
}

static void route_destructor(void *obj)
{
	struct http_route *route = obj;

	ast_string_field_free_memory(route);
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
	struct ast_var_t *var;
	struct timeval when;

	time(&utc_time);
	ast_get_dst_info(&utc_time, &dstenable, &dststart, &dstend, &tzoffset, zone);
	snprintf(buffer, sizeof(buffer), "%d", tzoffset);
	var = ast_var_assign("TZOFFSET", buffer);
	if (var)
		AST_LIST_INSERT_TAIL(headp, var, entries);

	if (!dstenable)
		return;

	if ((var = ast_var_assign("DST_ENABLE", "1")))
		AST_LIST_INSERT_TAIL(headp, var, entries);

	when.tv_sec = dststart;
	ast_localtime(&when, &tm_info, zone);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mon+1);
	if ((var = ast_var_assign("DST_START_MONTH", buffer)))
		AST_LIST_INSERT_TAIL(headp, var, entries);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mday);
	if ((var = ast_var_assign("DST_START_MDAY", buffer)))
		AST_LIST_INSERT_TAIL(headp, var, entries);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_hour);
	if ((var = ast_var_assign("DST_START_HOUR", buffer)))
		AST_LIST_INSERT_TAIL(headp, var, entries);

	when.tv_sec = dstend;
	ast_localtime(&when, &tm_info, zone);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mon + 1);
	if ((var = ast_var_assign("DST_END_MONTH", buffer)))
		AST_LIST_INSERT_TAIL(headp, var, entries);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_mday);
	if ((var = ast_var_assign("DST_END_MDAY", buffer)))
		AST_LIST_INSERT_TAIL(headp, var, entries);

	snprintf(buffer, sizeof(buffer), "%d", tm_info.tm_hour);
	if ((var = ast_var_assign("DST_END_HOUR", buffer)))
		AST_LIST_INSERT_TAIL(headp, var, entries);
}

/*! \brief Callback that is executed everytime an http request is received by this module */
static int phoneprov_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers)
{
	struct http_route *route;
	struct http_route search_route = {
		.uri = uri,
	};
	struct ast_str *result;
	char path[PATH_MAX];
	char *file = NULL;
	int len;
	int fd;
	struct ast_str *http_header;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return -1;
	}

	if (!(route = ao2_find(http_routes, &search_route, OBJ_POINTER))) {
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
		if (ast_strlen_zero(global_server)) {
			union {
				struct sockaddr sa;
				struct sockaddr_in sa_in;
			} name;
			socklen_t namelen = sizeof(name.sa);
			int res;

			if ((res = getsockname(ser->fd, &name.sa, &namelen))) {
				ast_log(LOG_WARNING, "Could not get server IP, breakage likely.\n");
			} else {
				struct ast_var_t *var;
				struct extension *exten_iter;

				if ((var = ast_var_assign("SERVER", ast_inet_ntoa(name.sa_in.sin_addr)))) {
					AST_LIST_TRAVERSE(&route->user->extensions, exten_iter, entry) {
						AST_LIST_INSERT_TAIL(exten_iter->headp, var, entries);
					}
				}
			}
		}

		ast_str_substitute_variables_varshead(&tmp, 0, AST_LIST_FIRST(&route->user->extensions)->headp, file);

		if (file) {
			ast_free(file);
		}

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
		if (tmp) {
			ast_free(tmp);
		}

		route = unref_route(route);

		return 0;
	}

out404:
	ast_http_error(ser, 404, "Not Found", "Nothing to see here.  Move along.");
	return -1;

out500:
	route = unref_route(route);
	ast_http_error(ser, 500, "Internal Error", "An internal error has occured.");
	return -1;
}

/*! \brief Build a route structure and add it to the list of available http routes
	\param pp_file File to link to the route
	\param user User to link to the route (NULL means static route)
	\param uri URI of the route
*/
static void build_route(struct phoneprov_file *pp_file, struct user *user, char *uri)
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

	ao2_link(http_routes, route);

	route = unref_route(route);
}

/*! \brief Build a phone profile and add it to the list of phone profiles
	\param name the name of the profile
	\param v ast_variable from parsing phoneprov.conf
*/
static void build_profile(const char *name, struct ast_variable *v)
{
	struct phone_profile *profile;
	struct ast_var_t *var;

	if (!(profile = ao2_alloc(sizeof(*profile), profile_destructor))) {
		return;
	}

	if (ast_string_field_init(profile, 32)) {
		profile = unref_profile(profile);
		return;
	}

	if (!(profile->headp = ast_calloc(1, sizeof(*profile->headp)))) {
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
			struct ast_var_t *variable;
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
				if ((variable = ast_var_assign(args.varname, args.varval)))
					AST_LIST_INSERT_TAIL(profile->headp, variable, entries);
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
				build_route(pp_file, NULL, NULL);
			} else {
				ast_string_field_set(pp_file, format, v->name);
				ast_string_field_set(pp_file, template, args.filename);
				AST_LIST_INSERT_TAIL(&profile->dynamic_files, pp_file, entry);
			}
		}
	}

	/* Append the global variables to the variables list for this profile.
	 * This is for convenience later, when we need to provide a single
	 * variable list for use in substitution. */
	ast_mutex_lock(&globals_lock);
	AST_LIST_TRAVERSE(&global_variables, var, entries) {
		struct ast_var_t *new_var;
		if ((new_var = ast_var_assign(var->name, var->value))) {
			AST_LIST_INSERT_TAIL(profile->headp, new_var, entries);
		}
	}
	ast_mutex_unlock(&globals_lock);

	ao2_link(profiles, profile);

	profile = unref_profile(profile);
}

static struct extension *delete_extension(struct extension *exten)
{
	struct ast_var_t *var;
	while ((var = AST_LIST_REMOVE_HEAD(exten->headp, entries))) {
		ast_var_delete(var);
	}
	ast_free(exten->headp);
	ast_string_field_free_memory(exten);

	ast_free(exten);

	return NULL;
}

static struct extension *build_extension(struct ast_config *cfg, const char *name)
{
	struct extension *exten;
	struct ast_var_t *var;
	const char *tmp;
	int i;

	if (!(exten = ast_calloc_with_stringfields(1, struct extension, 32))) {
		return NULL;
	}

	ast_string_field_set(exten, name, name);

	if (!(exten->headp = ast_calloc(1, sizeof(*exten->headp)))) {
		ast_free(exten);
		exten = NULL;
		return NULL;
	}

	for (i = 0; i < PP_VAR_LIST_LENGTH; i++) {
		tmp = ast_variable_retrieve(cfg, name, pp_variable_list[i].user_var);

		/* If we didn't get a USERNAME variable, set it to the user->name */
		if (i == PP_USERNAME && !tmp) {
			if ((var = ast_var_assign(pp_variable_list[PP_USERNAME].template_var, exten->name))) {
				AST_LIST_INSERT_TAIL(exten->headp, var, entries);
			}
			continue;
		} else if (i == PP_TIMEZONE) {
			/* perfectly ok if tmp is NULL, will set variables based on server's time zone */
			set_timezone_variables(exten->headp, tmp);
		} else if (i == PP_LINENUMBER) {
			if (!tmp) {
				tmp = "1";
			}
			exten->index = atoi(tmp);
		} else if (i == PP_LINEKEYS) {
			if (!tmp) {
				tmp = "1";
			}
		}

		if (tmp && (var = ast_var_assign(pp_variable_list[i].template_var, tmp))) {
			AST_LIST_INSERT_TAIL(exten->headp, var, entries);
		}
	}

	if (!ast_strlen_zero(global_server)) {
		if ((var = ast_var_assign("SERVER", global_server)))
			AST_LIST_INSERT_TAIL(exten->headp, var, entries);
	}

	if (!ast_strlen_zero(global_serverport)) {
		if ((var = ast_var_assign("SERVER_PORT", global_serverport)))
			AST_LIST_INSERT_TAIL(exten->headp, var, entries);
	}

	return exten;
}

static struct user *unref_user(struct user *user)
{
	ao2_ref(user, -1);

	return NULL;
}

/*! \brief Return a user looked up by name */
static struct user *find_user(const char *macaddress)
{
	struct user tmp = {
		.macaddress = macaddress,
	};

	return ao2_find(users, &tmp, OBJ_POINTER);
}

static int users_hash_fn(const void *obj, const int flags)
{
	const struct user *user = obj;

	return ast_str_case_hash(user->macaddress);
}

static int users_cmp_fn(void *obj, void *arg, int flags)
{
	const struct user *user1 = obj, *user2 = arg;

	return !strcasecmp(user1->macaddress, user2->macaddress) ? CMP_MATCH | CMP_STOP : 0;
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

	ast_string_field_free_memory(user);
}

/*! \brief Delete all users */
static void delete_users(void)
{
	struct ao2_iterator i;
	struct user *user;

	i = ao2_iterator_init(users, 0);
	while ((user = ao2_iterator_next(&i))) {
		ao2_unlink(users, user);
		user = unref_user(user);
	}
	ao2_iterator_destroy(&i);
}

/*! \brief Build and return a user structure based on gathered config data */
static struct user *build_user(const char *mac, struct phone_profile *profile)
{
	struct user *user;

	if (!(user = ao2_alloc(sizeof(*user), user_destructor))) {
		profile = unref_profile(profile);
		return NULL;
	}

	if (ast_string_field_init(user, 32)) {
		profile = unref_profile(profile);
		user = unref_user(user);
		return NULL;
	}

	ast_string_field_set(user, macaddress, mac);
	user->profile = profile; /* already ref counted by find_profile */

	return user;
}

/*! \brief Add an extension to a user ordered by index/linenumber */
static int add_user_extension(struct user *user, struct extension *exten)
{
	struct ast_var_t *var;
	struct ast_str *str = ast_str_create(16);

	if (!str) {
		return -1;
	}

	/* Append profile variables here, and substitute variables on profile
	 * setvars, so that we can use user specific variables in them */
	AST_LIST_TRAVERSE(user->profile->headp, var, entries) {
		struct ast_var_t *var2;

		ast_str_substitute_variables_varshead(&str, 0, exten->headp, var->value);
		if ((var2 = ast_var_assign(var->name, ast_str_buffer(str)))) {
			AST_LIST_INSERT_TAIL(exten->headp, var2, entries);
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
		build_route(pp_file, user, ast_str_buffer(str));
	}

	ast_free(str);
	return 0;
}

/* \brief Parse config files and create appropriate structures */
static int set_config(void)
{
	struct ast_config *cfg, *phoneprov_cfg;
	char *cat;
	struct ast_variable *v;
	struct ast_flags config_flags = { 0 };
	struct ast_var_t *var;

	/* Try to grab the port from sip.conf.  If we don't get it here, we'll set it
	 * to whatever is set in phoneprov.conf or default to 5060 */
	if ((cfg = ast_config_load("sip.conf", config_flags)) && cfg != CONFIG_STATUS_FILEINVALID) {
		ast_copy_string(global_serverport, S_OR(ast_variable_retrieve(cfg, "general", "bindport"), "5060"), sizeof(global_serverport));
		ast_config_destroy(cfg);
	}

	if (!(cfg = ast_config_load("users.conf", config_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load users.conf\n");
		return 0;
	}

	/* Go ahead and load global variables from users.conf so we can append to profiles */
	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, "vmexten")) {
			if ((var = ast_var_assign("VOICEMAIL_EXTEN", v->value))) {
				ast_mutex_lock(&globals_lock);
				AST_LIST_INSERT_TAIL(&global_variables, var, entries);
				ast_mutex_unlock(&globals_lock);
			}
		}
		if (!strcasecmp(v->name, "localextenlength")) {
			if ((var = ast_var_assign("EXTENSION_LENGTH", v->value)))
				ast_mutex_lock(&globals_lock);
				AST_LIST_INSERT_TAIL(&global_variables, var, entries);
				ast_mutex_unlock(&globals_lock);
		}
	}

	if (!(phoneprov_cfg = ast_config_load("phoneprov.conf", config_flags)) || phoneprov_cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load config phoneprov.conf\n");
		ast_config_destroy(cfg);
		return -1;
	}

	cat = NULL;
	while ((cat = ast_category_browse(phoneprov_cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			for (v = ast_variable_browse(phoneprov_cfg, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "serveraddr"))
					ast_copy_string(global_server, v->value, sizeof(global_server));
				else if (!strcasecmp(v->name, "serveriface")) {
					struct in_addr addr;
					lookup_iface(v->value, &addr);
					ast_copy_string(global_server, ast_inet_ntoa(addr), sizeof(global_server));
				} else if (!strcasecmp(v->name, "serverport"))
					ast_copy_string(global_serverport, v->value, sizeof(global_serverport));
				else if (!strcasecmp(v->name, "default_profile"))
					ast_copy_string(global_default_profile, v->value, sizeof(global_default_profile));
			}
		} else
			build_profile(cat, ast_variable_browse(phoneprov_cfg, cat));
	}

	ast_config_destroy(phoneprov_cfg);

	cat = NULL;
	while ((cat = ast_category_browse(cfg, cat))) {
		const char *tmp, *mac;
		struct user *user;
		struct phone_profile *profile;
		struct extension *exten;

		if (!strcasecmp(cat, "general")) {
			continue;
		}

		if (!strcasecmp(cat, "authentication"))
			continue;

		if (!((tmp = ast_variable_retrieve(cfg, cat, "autoprov")) && ast_true(tmp)))
			continue;

		if (!(mac = ast_variable_retrieve(cfg, cat, "macaddress"))) {
			ast_log(LOG_WARNING, "autoprov set for %s, but no mac address - skipping.\n", cat);
			continue;
		}

		tmp = S_OR(ast_variable_retrieve(cfg, cat, "profile"), global_default_profile);
		if (ast_strlen_zero(tmp)) {
			ast_log(LOG_WARNING, "No profile for user [%s] with mac '%s' - skipping\n", cat, mac);
			continue;
		}

		if (!(user = find_user(mac))) {
			if (!(profile = find_profile(tmp))) {
				ast_log(LOG_WARNING, "Could not look up profile '%s' - skipping.\n", tmp);
				continue;
			}

			if (!(user = build_user(mac, profile))) {
				ast_log(LOG_WARNING, "Could not create user for '%s' - skipping\n", mac);
				continue;
			}

			if (!(exten = build_extension(cfg, cat))) {
				ast_log(LOG_WARNING, "Could not create extension for %s - skipping\n", user->macaddress);
				user = unref_user(user);
				continue;
			}

			if (add_user_extension(user, exten)) {
				ast_log(LOG_WARNING, "Could not add extension '%s' to user '%s'\n", exten->name, user->macaddress);
				user = unref_user(user);
				exten = delete_extension(exten);
				continue;
			}

			if (build_user_routes(user)) {
				ast_log(LOG_WARNING, "Could not create http routes for %s - skipping\n", user->macaddress);
				user = unref_user(user);
				continue;
			}

			ao2_link(users, user);
			user = unref_user(user);
		} else {
			if (!(exten = build_extension(cfg, cat))) {
				ast_log(LOG_WARNING, "Could not create extension for %s - skipping\n", user->macaddress);
				user = unref_user(user);
				continue;
			}

			if (add_user_extension(user, exten)) {
				ast_log(LOG_WARNING, "Could not add extension '%s' to user '%s'\n", exten->name, user->macaddress);
				user = unref_user(user);
				exten = delete_extension(exten);
				continue;
			}

			user = unref_user(user);
		}
	}

	ast_config_destroy(cfg);

	return 0;
}

/*! \brief Delete all http routes, freeing their memory */
static void delete_routes(void)
{
	struct ao2_iterator i;
	struct http_route *route;

	i = ao2_iterator_init(http_routes, 0);
	while ((route = ao2_iterator_next(&i))) {
		ao2_unlink(http_routes, route);
		route = unref_route(route);
	}
	ao2_iterator_destroy(&i);
}

/*! \brief Delete all phone profiles, freeing their memory */
static void delete_profiles(void)
{
	struct ao2_iterator i;
	struct phone_profile *profile;

	i = ao2_iterator_init(profiles, 0);
	while ((profile = ao2_iterator_next(&i))) {
		ao2_unlink(profiles, profile);
		profile = unref_profile(profile);
	}
	ao2_iterator_destroy(&i);
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

/*! \brief CLI command to list static and dynamic routes */
static char *handle_show_routes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-40.40s  %-30.30s\n"
	struct ao2_iterator i;
	struct http_route *route;

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
	ast_cli(a->fd, FORMAT, "Relative URI", "Physical location");
	i = ao2_iterator_init(http_routes, 0);
	while ((route = ao2_iterator_next(&i))) {
		if (!route->user)
			ast_cli(a->fd, FORMAT, route->uri, route->file->template);
		route = unref_route(route);
	}
	ao2_iterator_destroy(&i);

	ast_cli(a->fd, "\nDynamic routes\n\n");
	ast_cli(a->fd, FORMAT, "Relative URI", "Template");

	i = ao2_iterator_init(http_routes, 0);
	while ((route = ao2_iterator_next(&i))) {
		if (route->user)
			ast_cli(a->fd, FORMAT, route->uri, route->file->template);
		route = unref_route(route);
	}
	ao2_iterator_destroy(&i);

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

static int load_module(void)
{
	profiles = ao2_container_alloc(MAX_PROFILE_BUCKETS, profile_hash_fn, profile_cmp_fn);

	http_routes = ao2_container_alloc(MAX_ROUTE_BUCKETS, routes_hash_fn, routes_cmp_fn);

	users = ao2_container_alloc(MAX_USER_BUCKETS, users_hash_fn, users_cmp_fn);

	AST_LIST_HEAD_INIT_NOLOCK(&global_variables);
	ast_mutex_init(&globals_lock);

	ast_custom_function_register(&pp_each_user_function);
	ast_custom_function_register(&pp_each_extension_function);
	ast_cli_register_multiple(pp_cli, ARRAY_LEN(pp_cli));

	set_config();
	ast_http_uri_link(&phoneprovuri);

	return 0;
}

static int unload_module(void)
{
	struct ast_var_t *var;

	ast_http_uri_unlink(&phoneprovuri);
	ast_custom_function_unregister(&pp_each_user_function);
	ast_custom_function_unregister(&pp_each_extension_function);
	ast_cli_unregister_multiple(pp_cli, ARRAY_LEN(pp_cli));

	delete_routes();
	delete_users();
	delete_profiles();
	ao2_ref(profiles, -1);
	ao2_ref(http_routes, -1);
	ao2_ref(users, -1);

	ast_mutex_lock(&globals_lock);
	while ((var = AST_LIST_REMOVE_HEAD(&global_variables, entries))) {
		ast_var_delete(var);
	}
	ast_mutex_unlock(&globals_lock);

	ast_mutex_destroy(&globals_lock);

	return 0;
}

static int reload(void)
{
	struct ast_var_t *var;

	delete_routes();
	delete_users();
	delete_profiles();

	ast_mutex_lock(&globals_lock);
	while ((var = AST_LIST_REMOVE_HEAD(&global_variables, entries))) {
		ast_var_delete(var);
	}
	ast_mutex_unlock(&globals_lock);

	set_config();

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "HTTP Phone Provisioning",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	);
