/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C)  2004 - 2006, Tilghman Lesher
 *
 * Tilghman Lesher <curl-20050919@the-tilghman.com>
 * and Brian Wilkins <bwilkins@cfl.rr.com> (Added POST option)
 *
 * app_curl.c is distributed with no restrictions on usage or
 * redistribution.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 * 
 * \brief Curl - Load a URL
 *
 * \author Tilghman Lesher <curl-20050919@the-tilghman.com>
 *
 * \note Brian Wilkins <bwilkins@cfl.rr.com> (Added POST option) 
 *
 * \extref Depends on the CURL library  - http://curl.haxx.se/
 * 
 * \ingroup functions
 */
 
/*** MODULEINFO
	<depend>curl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <curl/curl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"

/*** DOCUMENTATION
	<function name="CURL" language="en_US">
		<synopsis>
			Retrieve content from a remote web or ftp server
		</synopsis>
		<syntax>
			<parameter name="url" required="true" />
			<parameter name="post-data">
				<para>If specified, an <literal>HTTP POST</literal> will be
				performed with the content of
				<replaceable>post-data</replaceable>, instead of an
				<literal>HTTP GET</literal> (default).</para>
			</parameter>
		</syntax>
		<description />
		<see-also>
			<ref type="function">CURLOPT</ref>
		</see-also>
	</function>
	<function name="CURLOPT" language="en_US">
		<synopsis>
			Sets various options for future invocations of CURL.
		</synopsis>
		<syntax>
			<parameter name="key" required="yes">
				<enumlist>
					<enum name="cookie">
						<para>A cookie to send with the request.  Multiple
						cookies are supported.</para>
					</enum>
					<enum name="conntimeout">
						<para>Number of seconds to wait for a connection to succeed</para>
					</enum>
					<enum name="dnstimeout">
						<para>Number of seconds to wait for DNS to be resolved</para>
					</enum>
					<enum name="ftptext">
						<para>For FTP URIs, force a text transfer (boolean)</para>
					</enum>
					<enum name="ftptimeout">
						<para>For FTP URIs, number of seconds to wait for a
						server response</para>
					</enum>
					<enum name="header">
						<para>Include header information in the result
						(boolean)</para>
					</enum>
					<enum name="httptimeout">
						<para>For HTTP(S) URIs, number of seconds to wait for a
						server response</para>
					</enum>
					<enum name="maxredirs">
						<para>Maximum number of redirects to follow</para>
					</enum>
					<enum name="proxy">
						<para>Hostname or IP address to use as a proxy server</para>
					</enum>
					<enum name="proxytype">
						<para>Type of <literal>proxy</literal></para>
						<enumlist>
							<enum name="http" />
							<enum name="socks4" />
							<enum name="socks5" />
						</enumlist>
					</enum>
					<enum name="proxyport">
						<para>Port number of the <literal>proxy</literal></para>
					</enum>
					<enum name="proxyuserpwd">
						<para>A <replaceable>username</replaceable><literal>:</literal><replaceable>password</replaceable>
						combination to use for authenticating requests through a
						<literal>proxy</literal></para>
					</enum>
					<enum name="referer">
						<para>Referer URL to use for the request</para>
					</enum>
					<enum name="useragent">
						<para>UserAgent string to use for the request</para>
					</enum>
					<enum name="userpwd">
						<para>A <replaceable>username</replaceable><literal>:</literal><replaceable>password</replaceable>
						to use for authentication when the server response to
						an initial request indicates a 401 status code.</para>
					</enum>
					<enum name="ssl_verifypeer">
						<para>Whether to verify the server certificate against
						a list of known root certificate authorities (boolean).</para>
					</enum>
					<enum name="hashcompat">
						<para>Assuming the responses will be in <literal>key1=value1&amp;key2=value2</literal>
						format, reformat the response such that it can be used
						by the <literal>HASH</literal> function.</para>
						<enumlist>
							<enum name="yes" />
							<enum name="no" />
							<enum name="legacy">
								<para>Also translate <literal>+</literal> to the
								space character, in violation of current RFC
								standards.</para>
							</enum>
						</enumlist>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Options may be set globally or per channel.  Per-channel
			settings will override global settings.</para>
		</description>
		<see-also>
			<ref type="function">CURL</ref>
			<ref type="function">HASH</ref>
		</see-also>
	</function>
 ***/

#define CURLVERSION_ATLEAST(a,b,c) \
	((LIBCURL_VERSION_MAJOR > (a)) || ((LIBCURL_VERSION_MAJOR == (a)) && (LIBCURL_VERSION_MINOR > (b))) || ((LIBCURL_VERSION_MAJOR == (a)) && (LIBCURL_VERSION_MINOR == (b)) && (LIBCURL_VERSION_PATCH >= (c))))

#define CURLOPT_SPECIAL_HASHCOMPAT -500

static void curlds_free(void *data);

static struct ast_datastore_info curl_info = {
	.type = "CURL",
	.destroy = curlds_free,
};

struct curl_settings {
	AST_LIST_ENTRY(curl_settings) list;
	CURLoption key;
	void *value;
};

AST_LIST_HEAD_STATIC(global_curl_info, curl_settings);

static void curlds_free(void *data)
{
	AST_LIST_HEAD(global_curl_info, curl_settings) *list = data;
	struct curl_settings *setting;
	if (!list) {
		return;
	}
	while ((setting = AST_LIST_REMOVE_HEAD(list, list))) {
		free(setting);
	}
	AST_LIST_HEAD_DESTROY(list);
}

enum optiontype {
	OT_BOOLEAN,
	OT_INTEGER,
	OT_INTEGER_MS,
	OT_STRING,
	OT_ENUM,
};

enum hashcompat {
	HASHCOMPAT_NO = 0,
	HASHCOMPAT_YES,
	HASHCOMPAT_LEGACY,
};

static int parse_curlopt_key(const char *name, CURLoption *key, enum optiontype *ot)
{
	if (!strcasecmp(name, "header")) {
		*key = CURLOPT_HEADER;
		*ot = OT_BOOLEAN;
	} else if (!strcasecmp(name, "proxy")) {
		*key = CURLOPT_PROXY;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "proxyport")) {
		*key = CURLOPT_PROXYPORT;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "proxytype")) {
		*key = CURLOPT_PROXYTYPE;
		*ot = OT_ENUM;
	} else if (!strcasecmp(name, "dnstimeout")) {
		*key = CURLOPT_DNS_CACHE_TIMEOUT;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "userpwd")) {
		*key = CURLOPT_USERPWD;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "proxyuserpwd")) {
		*key = CURLOPT_PROXYUSERPWD;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "maxredirs")) {
		*key = CURLOPT_MAXREDIRS;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "referer")) {
		*key = CURLOPT_REFERER;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "useragent")) {
		*key = CURLOPT_USERAGENT;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "cookie")) {
		*key = CURLOPT_COOKIE;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "ftptimeout")) {
		*key = CURLOPT_FTP_RESPONSE_TIMEOUT;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "httptimeout")) {
#if CURLVERSION_ATLEAST(7,16,2)
		*key = CURLOPT_TIMEOUT_MS;
		*ot = OT_INTEGER_MS;
#else
		*key = CURLOPT_TIMEOUT;
		*ot = OT_INTEGER;
#endif
	} else if (!strcasecmp(name, "conntimeout")) {
#if CURLVERSION_ATLEAST(7,16,2)
		*key = CURLOPT_CONNECTTIMEOUT_MS;
		*ot = OT_INTEGER_MS;
#else
		*key = CURLOPT_CONNECTTIMEOUT;
		*ot = OT_INTEGER;
#endif
	} else if (!strcasecmp(name, "ftptext")) {
		*key = CURLOPT_TRANSFERTEXT;
		*ot = OT_BOOLEAN;
	} else if (!strcasecmp(name, "ssl_verifypeer")) {
		*key = CURLOPT_SSL_VERIFYPEER;
		*ot = OT_BOOLEAN;
	} else if (!strcasecmp(name, "hashcompat")) {
		*key = CURLOPT_SPECIAL_HASHCOMPAT;
		*ot = OT_ENUM;
	} else {
		return -1;
	}
	return 0;
}

static int acf_curlopt_write(struct ast_channel *chan, const char *cmd, char *name, const char *value)
{
	struct ast_datastore *store;
	struct global_curl_info *list;
	struct curl_settings *cur, *new = NULL;
	CURLoption key;
	enum optiontype ot;

	if (chan) {
		if (!(store = ast_channel_datastore_find(chan, &curl_info, NULL))) {
			/* Create a new datastore */
			if (!(store = ast_datastore_alloc(&curl_info, NULL))) {
				ast_log(LOG_ERROR, "Unable to allocate new datastore.  Cannot set any CURL options\n");
				return -1;
			}

			if (!(list = ast_calloc(1, sizeof(*list)))) {
				ast_log(LOG_ERROR, "Unable to allocate list head.  Cannot set any CURL options\n");
				ast_datastore_free(store);
			}

			store->data = list;
			AST_LIST_HEAD_INIT(list);
			ast_channel_datastore_add(chan, store);
		} else {
			list = store->data;
		}
	} else {
		/* Populate the global structure */
		list = &global_curl_info;
	}

	if (!parse_curlopt_key(name, &key, &ot)) {
		if (ot == OT_BOOLEAN) {
			if ((new = ast_calloc(1, sizeof(*new)))) {
				new->value = (void *)((long) ast_true(value));
			}
		} else if (ot == OT_INTEGER) {
			long tmp = atol(value);
			if ((new = ast_calloc(1, sizeof(*new)))) {
				new->value = (void *)tmp;
			}
		} else if (ot == OT_INTEGER_MS) {
			long tmp = atof(value) * 1000.0;
			if ((new = ast_calloc(1, sizeof(*new)))) {
				new->value = (void *)tmp;
			}
		} else if (ot == OT_STRING) {
			if ((new = ast_calloc(1, sizeof(*new) + strlen(value) + 1))) {
				new->value = (char *)new + sizeof(*new);
				strcpy(new->value, value);
			}
		} else if (ot == OT_ENUM) {
			if (key == CURLOPT_PROXYTYPE) {
				long ptype =
#if CURLVERSION_ATLEAST(7,10,0)
					CURLPROXY_HTTP;
#else
					CURLPROXY_SOCKS5;
#endif
				if (0) {
#if CURLVERSION_ATLEAST(7,15,2)
				} else if (!strcasecmp(value, "socks4")) {
					ptype = CURLPROXY_SOCKS4;
#endif
#if CURLVERSION_ATLEAST(7,18,0)
				} else if (!strcasecmp(value, "socks4a")) {
					ptype = CURLPROXY_SOCKS4A;
#endif
#if CURLVERSION_ATLEAST(7,18,0)
				} else if (!strcasecmp(value, "socks5")) {
					ptype = CURLPROXY_SOCKS5;
#endif
#if CURLVERSION_ATLEAST(7,18,0)
				} else if (!strncasecmp(value, "socks5", 6)) {
					ptype = CURLPROXY_SOCKS5_HOSTNAME;
#endif
				}

				if ((new = ast_calloc(1, sizeof(*new)))) {
					new->value = (void *)ptype;
				}
			} else if (key == CURLOPT_SPECIAL_HASHCOMPAT) {
				if ((new = ast_calloc(1, sizeof(*new)))) {
					new->value = (void *) (long) (!strcasecmp(value, "legacy") ? HASHCOMPAT_LEGACY : ast_true(value) ? HASHCOMPAT_YES : HASHCOMPAT_NO);
				}
			} else {
				/* Highly unlikely */
				goto yuck;
			}
		}

		/* Memory allocation error */
		if (!new) {
			return -1;
		}

		new->key = key;
	} else {
yuck:
		ast_log(LOG_ERROR, "Unrecognized option: %s\n", name);
		return -1;
	}

	/* Remove any existing entry */
	AST_LIST_LOCK(list);
	AST_LIST_TRAVERSE_SAFE_BEGIN(list, cur, list) {
		if (cur->key == new->key) {
			AST_LIST_REMOVE_CURRENT(list);
			free(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END

	/* Insert new entry */
	ast_debug(1, "Inserting entry %p with key %d and value %p\n", new, new->key, new->value);
	AST_LIST_INSERT_TAIL(list, new, list);
	AST_LIST_UNLOCK(list);

	return 0;
}

static int acf_curlopt_helper(struct ast_channel *chan, const char *cmd, char *data, char *buf, struct ast_str **bufstr, ssize_t len)
{
	struct ast_datastore *store;
	struct global_curl_info *list[2] = { &global_curl_info, NULL };
	struct curl_settings *cur = NULL;
	CURLoption key;
	enum optiontype ot;
	int i;

	if (parse_curlopt_key(data, &key, &ot)) {
		ast_log(LOG_ERROR, "Unrecognized option: '%s'\n", data);
		return -1;
	}

	if (chan && (store = ast_channel_datastore_find(chan, &curl_info, NULL))) {
		list[0] = store->data;
		list[1] = &global_curl_info;
	}

	for (i = 0; i < 2; i++) {
		if (!list[i]) {
			break;
		}
		AST_LIST_LOCK(list[i]);
		AST_LIST_TRAVERSE(list[i], cur, list) {
			if (cur->key == key) {
				if (ot == OT_BOOLEAN || ot == OT_INTEGER) {
					if (buf) {
						snprintf(buf, len, "%ld", (long) cur->value);
					} else {
						ast_str_set(bufstr, len, "%ld", (long) cur->value);
					}
				} else if (ot == OT_INTEGER_MS) {
					if ((long) cur->value % 1000 == 0) {
						if (buf) {
							snprintf(buf, len, "%ld", (long)cur->value / 1000);
						} else {
							ast_str_set(bufstr, len, "%ld", (long) cur->value / 1000);
						}
					} else {
						if (buf) {
							snprintf(buf, len, "%.3f", (double) ((long) cur->value) / 1000.0);
						} else {
							ast_str_set(bufstr, len, "%.3f", (double) ((long) cur->value) / 1000.0);
						}
					}
				} else if (ot == OT_STRING) {
					ast_debug(1, "Found entry %p, with key %d and value %p\n", cur, cur->key, cur->value);
					if (buf) {
						ast_copy_string(buf, cur->value, len);
					} else {
						ast_str_set(bufstr, 0, "%s", (char *) cur->value);
					}
				} else if (key == CURLOPT_PROXYTYPE) {
					const char *strval = "unknown";
					if (0) {
#if CURLVERSION_ATLEAST(7,15,2)
					} else if ((long)cur->value == CURLPROXY_SOCKS4) {
						strval = "socks4";
#endif
#if CURLVERSION_ATLEAST(7,18,0)
					} else if ((long)cur->value == CURLPROXY_SOCKS4A) {
						strval = "socks4a";
#endif
					} else if ((long)cur->value == CURLPROXY_SOCKS5) {
						strval = "socks5";
#if CURLVERSION_ATLEAST(7,18,0)
					} else if ((long)cur->value == CURLPROXY_SOCKS5_HOSTNAME) {
						strval = "socks5hostname";
#endif
#if CURLVERSION_ATLEAST(7,10,0)
					} else if ((long)cur->value == CURLPROXY_HTTP) {
						strval = "http";
#endif
					}
					if (buf) {
						ast_copy_string(buf, strval, len);
					} else {
						ast_str_set(bufstr, 0, "%s", strval);
					}
				} else if (key == CURLOPT_SPECIAL_HASHCOMPAT) {
					const char *strval = "unknown";
					if ((long) cur->value == HASHCOMPAT_LEGACY) {
						strval = "legacy";
					} else if ((long) cur->value == HASHCOMPAT_YES) {
						strval = "yes";
					} else if ((long) cur->value == HASHCOMPAT_NO) {
						strval = "no";
					}
					if (buf) {
						ast_copy_string(buf, strval, len);
					} else {
						ast_str_set(bufstr, 0, "%s", strval);
					}
				}
				break;
			}
		}
		AST_LIST_UNLOCK(list[i]);
		if (cur) {
			break;
		}
	}

	return cur ? 0 : -1;
}

static int acf_curlopt_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	return acf_curlopt_helper(chan, cmd, data, buf, NULL, len);
}

static int acf_curlopt_read2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	return acf_curlopt_helper(chan, cmd, data, NULL, buf, len);
}

static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = size * nmemb;
	struct ast_str **pstr = (struct ast_str **)data;

	ast_debug(3, "Called with data=%p, str=%p, realsize=%d, len=%zu, used=%zu\n", data, *pstr, realsize, ast_str_size(*pstr), ast_str_strlen(*pstr));

	ast_str_append_substr(pstr, 0, ptr, realsize);

	ast_debug(3, "Now, len=%zu, used=%zu\n", ast_str_size(*pstr), ast_str_strlen(*pstr));

	return realsize;
}

static const char * const global_useragent = "asterisk-libcurl-agent/1.0";

static int curl_instance_init(void *data)
{
	CURL **curl = data;

	if (!(*curl = curl_easy_init()))
		return -1;

	curl_easy_setopt(*curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(*curl, CURLOPT_TIMEOUT, 180);
	curl_easy_setopt(*curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(*curl, CURLOPT_USERAGENT, global_useragent);

	return 0;
}

static void curl_instance_cleanup(void *data)
{
	CURL **curl = data;

	curl_easy_cleanup(*curl);

	ast_free(data);
}

AST_THREADSTORAGE_CUSTOM(curl_instance, curl_instance_init, curl_instance_cleanup);

static int acf_curl_helper(struct ast_channel *chan, const char *cmd, char *info, char *buf, struct ast_str **input_str, ssize_t len)
{
	struct ast_str *str = ast_str_create(16);
	int ret = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(url);
		AST_APP_ARG(postdata);
	);
	CURL **curl;
	struct curl_settings *cur;
	struct ast_datastore *store = NULL;
	int hashcompat = 0;
	AST_LIST_HEAD(global_curl_info, curl_settings) *list = NULL;

	if (buf) {
		*buf = '\0';
	}

	if (ast_strlen_zero(info)) {
		ast_log(LOG_WARNING, "CURL requires an argument (URL)\n");
		ast_free(str);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, info);

	if (chan) {
		ast_autoservice_start(chan);
	}

	if (!(curl = ast_threadstorage_get(&curl_instance, sizeof(*curl)))) {
		ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
		return -1;
	}

	AST_LIST_LOCK(&global_curl_info);
	AST_LIST_TRAVERSE(&global_curl_info, cur, list) {
		if (cur->key == CURLOPT_SPECIAL_HASHCOMPAT) {
			hashcompat = (long) cur->value;
		} else {
			curl_easy_setopt(*curl, cur->key, cur->value);
		}
	}

	if (chan && (store = ast_channel_datastore_find(chan, &curl_info, NULL))) {
		list = store->data;
		AST_LIST_LOCK(list);
		AST_LIST_TRAVERSE(list, cur, list) {
			if (cur->key == CURLOPT_SPECIAL_HASHCOMPAT) {
				hashcompat = (long) cur->value;
			} else {
				curl_easy_setopt(*curl, cur->key, cur->value);
			}
		}
	}

	curl_easy_setopt(*curl, CURLOPT_URL, args.url);
	curl_easy_setopt(*curl, CURLOPT_FILE, (void *) &str);

	if (args.postdata) {
		curl_easy_setopt(*curl, CURLOPT_POST, 1);
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, args.postdata);
	}

	curl_easy_perform(*curl);

	if (store) {
		AST_LIST_UNLOCK(list);
	}
	AST_LIST_UNLOCK(&global_curl_info);

	if (args.postdata) {
		curl_easy_setopt(*curl, CURLOPT_POST, 0);
	}

	if (ast_str_strlen(str)) {
		ast_str_trim_blanks(str);

		ast_debug(3, "str='%s'\n", ast_str_buffer(str));
		if (hashcompat) {
			char *remainder = ast_str_buffer(str);
			char *piece;
			struct ast_str *fields = ast_str_create(ast_str_strlen(str) / 2);
			struct ast_str *values = ast_str_create(ast_str_strlen(str) / 2);
			int rowcount = 0;
			while (fields && values && (piece = strsep(&remainder, "&"))) {
				char *name = strsep(&piece, "=");
				if (!piece) {
					piece = "";
				}
				ast_uri_decode(piece);
				ast_uri_decode(name);
				ast_str_append(&fields, 0, "%s%s", rowcount ? "," : "", name);
				ast_str_append(&values, 0, "%s%s", rowcount ? "," : "", piece);
				rowcount++;
			}
			pbx_builtin_setvar_helper(chan, "~ODBCFIELDS~", ast_str_buffer(fields));
			if (buf) {
				ast_copy_string(buf, ast_str_buffer(values), len);
			} else {
				ast_str_set(input_str, len, "%s", ast_str_buffer(values));
			}
			ast_free(fields);
			ast_free(values);
		} else {
			if (buf) {
				ast_copy_string(buf, ast_str_buffer(str), len);
			} else {
				ast_str_set(input_str, len, "%s", ast_str_buffer(str));
			}
		}
		ret = 0;
	}
	ast_free(str);

	if (chan)
		ast_autoservice_stop(chan);

	return ret;
}

static int acf_curl_exec(struct ast_channel *chan, const char *cmd, char *info, char *buf, size_t len)
{
	return acf_curl_helper(chan, cmd, info, buf, NULL, len);
}

static int acf_curl2_exec(struct ast_channel *chan, const char *cmd, char *info, struct ast_str **buf, ssize_t len)
{
	return acf_curl_helper(chan, cmd, info, NULL, buf, len);
}

static struct ast_custom_function acf_curl = {
	.name = "CURL",
	.synopsis = "Retrieves the contents of a URL",
	.syntax = "CURL(url[,post-data])",
	.desc =
	"  url       - URL to retrieve\n"
	"  post-data - Optional data to send as a POST (GET is default action)\n",
	.read = acf_curl_exec,
	.read2 = acf_curl2_exec,
};

static struct ast_custom_function acf_curlopt = {
	.name = "CURLOPT",
	.synopsis = "Set options for use with the CURL() function",
	.syntax = "CURLOPT(<option>)",
	.desc =
"  cookie         - Send cookie with request [none]\n"
"  conntimeout    - Number of seconds to wait for connection\n"
"  dnstimeout     - Number of seconds to wait for DNS response\n"
"  ftptext        - For FTP, force a text transfer (boolean)\n"
"  ftptimeout     - For FTP, the server response timeout\n"
"  header         - Retrieve header information (boolean)\n"
"  httptimeout    - Number of seconds to wait for HTTP response\n"
"  maxredirs      - Maximum number of redirects to follow\n"
"  proxy          - Hostname or IP to use as a proxy\n"
"  proxytype      - http, socks4, or socks5\n"
"  proxyport      - port number of the proxy\n"
"  proxyuserpwd   - A <user>:<pass> to use for authentication\n"
"  referer        - Referer URL to use for the request\n"
"  useragent      - UserAgent string to use\n"
"  userpwd        - A <user>:<pass> to use for authentication\n"
"  ssl_verifypeer - Whether to verify the peer certificate (boolean)\n"
"  hashcompat     - Result data will be compatible for use with HASH()\n"
"                 - if value is \"legacy\", will translate '+' to ' '\n"
"",
	.read = acf_curlopt_read,
	.read2 = acf_curlopt_read2,
	.write = acf_curlopt_write,
};

static int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&acf_curl);
	res |= ast_custom_function_unregister(&acf_curlopt);

	return res;
}

static int load_module(void)
{
	int res;

	if (!ast_module_check("res_curl.so")) {
		if (ast_load_resource("res_curl.so") != AST_MODULE_LOAD_SUCCESS) {
			ast_log(LOG_ERROR, "Cannot load res_curl, so func_curl cannot be loaded\n");
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	res = ast_custom_function_register(&acf_curl);
	res |= ast_custom_function_register(&acf_curlopt);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Load external URL",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_REALTIME_DEPEND2,
	);

