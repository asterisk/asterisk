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
	<depend>res_curl</depend>
	<depend>curl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

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
#include "asterisk/test.h"

/*** DOCUMENTATION
	<function name="CURL" language="en_US">
		<synopsis>
			Retrieve content from a remote web or ftp server
		</synopsis>
		<syntax>
			<parameter name="url" required="true">
				<para>The full URL for the resource to retrieve.</para>
			</parameter>
			<parameter name="post-data">
				<para><emphasis>Read Only</emphasis></para>
				<para>If specified, an <literal>HTTP POST</literal> will be
				performed with the content of
				<replaceable>post-data</replaceable>, instead of an
				<literal>HTTP GET</literal> (default).</para>
			</parameter>
		</syntax>
		<description>
			<para>When this function is read, a <literal>HTTP GET</literal>
			(by default) will be used to retrieve the contents of the provided
			<replaceable>url</replaceable>. The contents are returned as the
			result of the function.</para>
			<example title="Displaying contents of a page" language="text">
			exten => s,1,Verbose(0, ${CURL(http://localhost:8088/static/astman.css)})
			</example>
			<para>When this function is written to, a <literal>HTTP GET</literal>
			will be used to retrieve the contents of the provided
			<replaceable>url</replaceable>. The value written to the function
			specifies the destination file of the cURL'd resource.</para>
			<example title="Retrieving a file" language="text">
			exten => s,1,Set(CURL(http://localhost:8088/static/astman.css)=/var/spool/asterisk/tmp/astman.css))
			</example>
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be written to from the
				dialplan, and not directly from external protocols. Read operations are
				unaffected.</para>
			</note>
		</description>
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
					<enum name="followlocation">
						<para>Whether or not to follow HTTP 3xx redirects (boolean)</para>
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
					<enum name="httpheader">
						<para>Add HTTP header. Multiple calls add multiple headers.
						Setting of any header will remove the default
						"Content-Type application/x-www-form-urlencoded"</para>
					</enum>
					<enum name="httptimeout">
						<para>For HTTP(S) URIs, number of seconds to wait for a
						server response</para>
					</enum>
					<enum name="maxredirs">
						<para>Maximum number of redirects to follow. The default is -1,
						which allows for unlimited redirects. This only makes sense when
						followlocation is also set.</para>
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
			settings will override global settings. Only HTTP headers are added instead of overriding</para>
		</description>
		<see-also>
			<ref type="function">CURL</ref>
			<ref type="function">HASH</ref>
		</see-also>
	</function>
 ***/

#define CURLVERSION_ATLEAST(a,b,c) \
	((LIBCURL_VERSION_MAJOR > (a)) || ((LIBCURL_VERSION_MAJOR == (a)) && (LIBCURL_VERSION_MINOR > (b))) || ((LIBCURL_VERSION_MAJOR == (a)) && (LIBCURL_VERSION_MINOR == (b)) && (LIBCURL_VERSION_PATCH >= (c))))

#define CURLOPT_SPECIAL_HASHCOMPAT ((CURLoption) -500)

static void curlds_free(void *data);

static const struct ast_datastore_info curl_info = {
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
		ast_free(setting);
	}
	AST_LIST_HEAD_DESTROY(list);
	ast_free(list);
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
	} else if (!strcasecmp(name, "httpheader")) {
		*key = CURLOPT_HTTPHEADER;
		*ot = OT_STRING;
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
	} else if (!strcasecmp(name, "followlocation")) {
		*key = CURLOPT_FOLLOWLOCATION;
		*ot = OT_BOOLEAN;
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
				return -1;
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

	/* Remove any existing entry, only http headers are left */
	AST_LIST_LOCK(list);
	if (new->key != CURLOPT_HTTPHEADER) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(list, cur, list) {
			if (cur->key == new->key) {
				AST_LIST_REMOVE_CURRENT(list);
				ast_free(cur);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
	}

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

/*! \brief Callback data passed to \ref WriteMemoryCallback */
struct curl_write_callback_data {
	/*! \brief If a string is being built, the string buffer */
	struct ast_str *str;
	/*! \brief The max size of \ref str */
	ssize_t len;
	/*! \brief If a file is being retrieved, the file to write to */
	FILE *out_file;
};

static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = 0;
	struct curl_write_callback_data *cb_data = data;

	if (cb_data->str) {
		realsize = size * nmemb;
		ast_str_append_substr(&cb_data->str, 0, ptr, realsize);
	} else if (cb_data->out_file) {
		realsize = fwrite(ptr, size, nmemb, cb_data->out_file);
	}

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
AST_THREADSTORAGE(thread_escapebuf);

/*!
 * \brief Check for potential HTTP injection risk.
 *
 * CVE-2014-8150 brought up the fact that HTTP proxies are subject to injection
 * attacks. An HTTP URL sent to a proxy contains a carriage-return linefeed combination,
 * followed by a complete HTTP request. Proxies will handle this as two separate HTTP
 * requests rather than as a malformed URL.
 *
 * libcURL patched this vulnerability in version 7.40.0, but we have no guarantee that
 * Asterisk systems will be using an up-to-date cURL library. Therefore, we implement
 * the same fix as libcURL for determining if a URL is vulnerable to an injection attack.
 *
 * \param url The URL to check for vulnerability
 * \retval 0 The URL is not vulnerable
 * \retval 1 The URL is vulnerable.
 */
static int url_is_vulnerable(const char *url)
{
	if (strpbrk(url, "\r\n")) {
		return 1;
	}

	return 0;
}

struct curl_args {
	const char *url;
	const char *postdata;
	struct curl_write_callback_data cb_data;
};

static int acf_curl_helper(struct ast_channel *chan, struct curl_args *args)
{
	struct ast_str *escapebuf = ast_str_thread_get(&thread_escapebuf, 16);
	int ret = -1;
	CURL **curl;
	struct curl_settings *cur;
	struct curl_slist *headers = NULL;
	struct ast_datastore *store = NULL;
	int hashcompat = 0;
	AST_LIST_HEAD(global_curl_info, curl_settings) *list = NULL;
	char curl_errbuf[CURL_ERROR_SIZE + 1]; /* add one to be safe */

	if (!escapebuf) {
		return -1;
	}

	if (!(curl = ast_threadstorage_get(&curl_instance, sizeof(*curl)))) {
		ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
		return -1;
	}

	if (url_is_vulnerable(args->url)) {
		ast_log(LOG_ERROR, "URL '%s' is vulnerable to HTTP injection attacks. Aborting CURL() call.\n", args->url);
		return -1;
	}

	if (chan) {
		ast_autoservice_start(chan);
	}

	AST_LIST_LOCK(&global_curl_info);
	AST_LIST_TRAVERSE(&global_curl_info, cur, list) {
		if (cur->key == CURLOPT_SPECIAL_HASHCOMPAT) {
			hashcompat = (long) cur->value;
		} else if (cur->key == CURLOPT_HTTPHEADER) {
			headers = curl_slist_append(headers, (char*) cur->value);
		} else {
			curl_easy_setopt(*curl, cur->key, cur->value);
		}
	}
	AST_LIST_UNLOCK(&global_curl_info);

	if (chan) {
		ast_channel_lock(chan);
		store = ast_channel_datastore_find(chan, &curl_info, NULL);
		ast_channel_unlock(chan);
		if (store) {
			list = store->data;
			AST_LIST_LOCK(list);
			AST_LIST_TRAVERSE(list, cur, list) {
				if (cur->key == CURLOPT_SPECIAL_HASHCOMPAT) {
					hashcompat = (long) cur->value;
				} else if (cur->key == CURLOPT_HTTPHEADER) {
					headers = curl_slist_append(headers, (char*) cur->value);
				} else {
					curl_easy_setopt(*curl, cur->key, cur->value);
				}
			}
		}
	}

	curl_easy_setopt(*curl, CURLOPT_URL, args->url);
	curl_easy_setopt(*curl, CURLOPT_FILE, (void *) &args->cb_data);

	if (args->postdata) {
		curl_easy_setopt(*curl, CURLOPT_POST, 1);
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, args->postdata);
	}

	if (headers) {
		curl_easy_setopt(*curl, CURLOPT_HTTPHEADER, headers);
	}

	/* Temporarily assign a buffer for curl to write errors to. */
	curl_errbuf[0] = curl_errbuf[CURL_ERROR_SIZE] = '\0';
	curl_easy_setopt(*curl, CURLOPT_ERRORBUFFER, curl_errbuf);

	if (curl_easy_perform(*curl) != 0) {
		ast_log(LOG_WARNING, "%s ('%s')\n", curl_errbuf, args->url);
	}

	/* Reset buffer to NULL so curl doesn't try to write to it when the
	 * buffer is deallocated. Documentation is vague about allowing NULL
	 * here, but the source allows it. See: "typecheck: allow NULL to unset
	 * CURLOPT_ERRORBUFFER" (62bcf005f4678a93158358265ba905bace33b834). */
	curl_easy_setopt(*curl, CURLOPT_ERRORBUFFER, (char*)NULL);

	if (store) {
		AST_LIST_UNLOCK(list);
	}
	curl_slist_free_all(headers);

	if (args->postdata) {
		curl_easy_setopt(*curl, CURLOPT_POST, 0);
	}

	if (args->cb_data.str && ast_str_strlen(args->cb_data.str)) {
		ast_str_trim_blanks(args->cb_data.str);

		ast_debug(3, "CURL returned str='%s'\n", ast_str_buffer(args->cb_data.str));
		if (hashcompat) {
			char *remainder = ast_str_buffer(args->cb_data.str);
			char *piece;
			struct ast_str *fields = ast_str_create(ast_str_strlen(args->cb_data.str) / 2);
			struct ast_str *values = ast_str_create(ast_str_strlen(args->cb_data.str) / 2);
			int rowcount = 0;
			while (fields && values && (piece = strsep(&remainder, "&"))) {
				char *name = strsep(&piece, "=");
				struct ast_flags mode = (hashcompat == HASHCOMPAT_LEGACY ? ast_uri_http_legacy : ast_uri_http);
				if (piece) {
					ast_uri_decode(piece, mode);
				}
				ast_uri_decode(name, mode);
				ast_str_append(&fields, 0, "%s%s", rowcount ? "," : "", ast_str_set_escapecommas(&escapebuf, 0, name, INT_MAX));
				ast_str_append(&values, 0, "%s%s", rowcount ? "," : "", ast_str_set_escapecommas(&escapebuf, 0, S_OR(piece, ""), INT_MAX));
				rowcount++;
			}
			pbx_builtin_setvar_helper(chan, "~ODBCFIELDS~", ast_str_buffer(fields));
			ast_str_set(&args->cb_data.str, 0, "%s", ast_str_buffer(values));
			ast_free(fields);
			ast_free(values);
		}
		ret = 0;
	}

	if (chan) {
		ast_autoservice_stop(chan);
	}

	return ret;
}

static int acf_curl_exec(struct ast_channel *chan, const char *cmd, char *info, struct ast_str **buf, ssize_t len)
{
	struct curl_args curl_params = { 0, };
	int res;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(url);
		AST_APP_ARG(postdata);
	);

	AST_STANDARD_APP_ARGS(args, info);

	if (ast_strlen_zero(info)) {
		ast_log(LOG_WARNING, "CURL requires an argument (URL)\n");
		return -1;
	}

	curl_params.url = args.url;
	curl_params.postdata = args.postdata;
	curl_params.cb_data.str = ast_str_create(16);
	if (!curl_params.cb_data.str) {
		return -1;
	}

	res = acf_curl_helper(chan, &curl_params);
	ast_str_set(buf, len, "%s", ast_str_buffer(curl_params.cb_data.str));
	ast_free(curl_params.cb_data.str);

	return res;
}

static int acf_curl_write(struct ast_channel *chan, const char *cmd, char *name, const char *value)
{
	struct curl_args curl_params = { 0, };
	int res;
	char *args_value = ast_strdupa(value);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(file_path);
	);

	AST_STANDARD_APP_ARGS(args, args_value);

	if (ast_strlen_zero(name)) {
		ast_log(LOG_WARNING, "CURL requires an argument (URL)\n");
		return -1;
	}

	if (ast_strlen_zero(args.file_path)) {
		ast_log(LOG_WARNING, "CURL requires a file to write\n");
		return -1;
	}

	curl_params.url = name;
	curl_params.cb_data.out_file = fopen(args.file_path, "w");
	if (!curl_params.cb_data.out_file) {
		ast_log(LOG_WARNING, "Failed to open file %s: %s (%d)\n",
			args.file_path,
			strerror(errno),
			errno);
		return -1;
	}

	res = acf_curl_helper(chan, &curl_params);

	fclose(curl_params.cb_data.out_file);

	return res;
}

static struct ast_custom_function acf_curl = {
	.name = "CURL",
	.read2 = acf_curl_exec,
	.write = acf_curl_write,
};

static struct ast_custom_function acf_curlopt = {
	.name = "CURLOPT",
	.synopsis = "Set options for use with the CURL() function",
	.syntax = "CURLOPT(<option>)",
	.desc =
"  cookie         - Send cookie with request [none]\n"
"  conntimeout    - Number of seconds to wait for connection\n"
"  dnstimeout     - Number of seconds to wait for DNS response\n"
"  followlocation - Follow HTTP 3xx redirects (boolean)\n"
"  ftptext        - For FTP, force a text transfer (boolean)\n"
"  ftptimeout     - For FTP, the server response timeout\n"
"  header         - Retrieve header information (boolean)\n"
"  httpheader     - Add new custom http header (string)\n"
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

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(vulnerable_url)
{
	const char *bad_urls [] = {
		"http://example.com\r\nDELETE http://example.com/everything",
		"http://example.com\rDELETE http://example.com/everything",
		"http://example.com\nDELETE http://example.com/everything",
		"\r\nhttp://example.com",
		"\rhttp://example.com",
		"\nhttp://example.com",
		"http://example.com\r\n",
		"http://example.com\r",
		"http://example.com\n",
	};
	const char *good_urls [] = {
		"http://example.com",
		"http://example.com/%5Cr%5Cn",
	};
	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "vulnerable_url";
		info->category = "/funcs/func_curl/";
		info->summary = "cURL vulnerable URL test";
		info->description =
			"Ensure that any combination of '\\r' or '\\n' in a URL invalidates the URL";
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(bad_urls); ++i) {
		if (!url_is_vulnerable(bad_urls[i])) {
			ast_test_status_update(test, "String '%s' detected as valid when it should be invalid\n", bad_urls[i]);
			res = AST_TEST_FAIL;
		}
	}

	for (i = 0; i < ARRAY_LEN(good_urls); ++i) {
		if (url_is_vulnerable(good_urls[i])) {
			ast_test_status_update(test, "String '%s' detected as invalid when it should be valid\n", good_urls[i]);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}
#endif

static int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&acf_curl);
	res |= ast_custom_function_unregister(&acf_curlopt);

	AST_TEST_UNREGISTER(vulnerable_url);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_custom_function_register_escalating(&acf_curl, AST_CFE_WRITE);
	res |= ast_custom_function_register(&acf_curlopt);

	AST_TEST_REGISTER(vulnerable_url);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Load external URL",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DEPEND2,
	.requires = "res_curl",
);
