/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_websocket_client" language="en_US">
		<synopsis>Websocket Client Configuration</synopsis>
		<configFile name="websocket_client.conf">
			<configObject name="websocket_client">
				<since>
					<version>20.15.0</version>
					<version>21.10.0</version>
					<version>22.5.0</version>
				</since>
				<synopsis>Websocket Client Configuration</synopsis>
				<see-also>
					<ref type="link">/Configuration/Channel-Drivers/WebSocket/</ref>
					<ref type="link">/Configuration/Interfaces/Asterisk-REST-Interface-ARI/ARI-Outbound-Websockets/</ref>
				</see-also>
				<description>
					<para>
					These config objects are currently shared by the following Asterisk capabilities:
					</para>
					<enumlist>
							<enum name="chan_websocket"><para>The WebSocket channel driver.</para></enum>
							<enum name="res_ari"><para>ARI Outbound WebSockets.</para></enum>
					</enumlist>
					<para>
					They may have more specific information or restrictions on the parameters below.
					</para>
					<example title="websocket_client.conf">
;
; A connection for use by chan_websocket
[media_connection1]
type = websocket_client
uri = ws://localhost:8787
protocols = media
username = media_username
password = media_password
connection_type = per_call_config
connection_timeout = 500
reconnect_interval = 500
reconnect_attempts = 5
tls_enabled = no
;
; A TLS connection for use by ARI Outbound Websocket
[ari_connection1]
type = websocket_client
uri = wss://localhost:8765
protocols = ari
username = some_username
password = some_password
connection_type = persistent
connection_timeout = 500
reconnect_interval = 500
reconnect_attempts = 5
tls_enabled = yes
ca_list_file = /etc/pki/tls/cert.pem
verify_server_cert = no
verify_server_hostname = no
					</example>
				</description>
				<configOption name="type">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Must be "websocket_client".</synopsis>
				</configOption>
				<configOption name="uri">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Full URI to remote server.</synopsis>
				</configOption>
				<configOption name="protocols">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Comma separated list of protocols acceptable to the server.</synopsis>
				</configOption>
				<configOption name="username">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Server authentication username if required.</synopsis>
				</configOption>
				<configOption name="password">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Server authentication password if required.</synopsis>
				</configOption>
				<configOption name="connection_type">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Single persistent connection or per-call configuration.</synopsis>
					<description>
					<enumlist>
						<enum name="persistent"><para>Single persistent connection for all calls.</para></enum>
						<enum name="per_call_config"><para>New connection for each call to the Stasis() dialplan app.</para></enum>
					</enumlist>
					</description>
				</configOption>
				<configOption name="connection_timeout">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Connection timeout (ms).</synopsis>
				</configOption>
				<configOption name="reconnect_attempts">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>On failure, how many times should reconnection be attempted?</synopsis>
					<description>
						<para>
							For per_call connections, this is the number of
							(re)connection attempts to make before returning an
							and terminating the call.  Persistent connections
							always retry forever but this setting will control
							how often failure messages are logged.
						</para>
					</description>
				</configOption>
				<configOption name="reconnect_interval">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>How often should reconnection be attempted (ms)?</synopsis>
				</configOption>
				<configOption name="tls_enabled">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Enable TLS</synopsis>
				</configOption>
				<configOption name="ca_list_file">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>File containing the server's CA certificate. (optional)</synopsis>
				</configOption>
				<configOption name="ca_list_path">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>Path to a directory containing one or more hashed CA certificates. (optional)</synopsis>
				</configOption>
				<configOption name="cert_file">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>File containing a client certificate. (optional)</synopsis>
				</configOption>
				<configOption name="priv_key_file">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>File containing the client's private key. (optional)</synopsis>
				</configOption>
				<configOption name="verify_server_cert">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>If set to true, verify the server's certificate. (optional)</synopsis>
				</configOption>
				<configOption name="verify_server_hostname">
					<since>
						<version>20.15.0</version>
						<version>21.10.0</version>
						<version>22.5.0</version>
					</since>
					<synopsis>If set to true, verify that the server's hostname matches the common name in it's certificate. (optional)</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/


#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"
#include "asterisk/websocket_client.h"

static struct ast_sorcery *sorcery = NULL;

void ast_websocket_client_add_uri_params(struct ast_websocket_client *wc,
	const char *uri_params)
{
	ast_string_field_set(wc, uri_params, uri_params);
}

struct ast_websocket *ast_websocket_client_connect(struct ast_websocket_client *wc,
	void *lock_obj, const char *display_name, enum ast_websocket_result *result)
{
	int reconnect_counter = wc->reconnect_attempts;
	char *uri = NULL;

	if (ast_strlen_zero(display_name)) {
		display_name = ast_sorcery_object_get_id(wc);
	}

	if (!ast_strlen_zero(wc->uri_params)) {
		/*
		 * If the configured URI doesn't already contain parameters, we append the
		 * new ones to the URI path component with '?'.  If it does, we append the
		 * new ones to the existing ones with a '&'.
		 */
		char sep = '?';
		uri = ast_alloca(strlen(wc->uri) + strlen(wc->uri_params) + 2);
		if (strchr(wc->uri, '?')) {
			sep = '&';
		}
		sprintf(uri, "%s%c%s", wc->uri, sep, wc->uri_params); /*Safe */
	}

	while (1) {
		struct ast_websocket *astws = NULL;
		struct ast_websocket_client_options options = {
			.uri = S_OR(uri, wc->uri),
			.protocols = wc->protocols,
			.username = wc->username,
			.password = wc->password,
			.timeout = wc->connect_timeout,
			.suppress_connection_msgs = 1,
			.tls_cfg = NULL,
		};

		if (lock_obj) {
			ao2_lock(lock_obj);
		}

		if (wc->tls_enabled) {
			/*
			 * tls_cfg and its contents are freed automatically
			 * by res_http_websocket when the connection ends.
			 * We create it even if tls is not enabled to we can
			 * suppress connection error messages and print our own.
			 */
			options.tls_cfg = ast_calloc(1, sizeof(*options.tls_cfg));
			if (!options.tls_cfg) {
				if (lock_obj) {
					ao2_unlock(lock_obj);
				}
				return NULL;
			}
			/* TLS options */
			options.tls_cfg->enabled = wc->tls_enabled;
			options.tls_cfg->cafile = ast_strdup(wc->ca_list_file);
			options.tls_cfg->capath = ast_strdup(wc->ca_list_path);
			options.tls_cfg->certfile = ast_strdup(wc->cert_file);
			options.tls_cfg->pvtfile = ast_strdup(wc->priv_key_file);
			ast_set2_flag(&options.tls_cfg->flags, !wc->verify_server_cert, AST_SSL_DONT_VERIFY_SERVER);
			ast_set2_flag(&options.tls_cfg->flags, !wc->verify_server_hostname, AST_SSL_IGNORE_COMMON_NAME);
		}

		astws = ast_websocket_client_create_with_options(&options, result);
		if (astws && *result == WS_OK) {
			if (lock_obj) {
				ao2_unlock(lock_obj);
			}
			return astws;
		}

		reconnect_counter--;
		if (reconnect_counter <= 0) {
			if (wc->connection_type == AST_WS_TYPE_CLIENT_PERSISTENT) {
				ast_log(LOG_WARNING,
					"%s: Websocket connection to %s failed after %d tries: %s%s%s%s.  Retrying in %d ms.\n",
					display_name,
					wc->uri,
					wc->reconnect_attempts,
					ast_websocket_result_to_str(*result),
					errno ? " (" : "",
					errno ? strerror(errno) : "",
					errno ? ")" : "",
					wc->reconnect_interval
				);
			} else {
				ast_log(LOG_WARNING,
					"%s: Websocket connection to %s failed after %d tries: %s%s%s%s.  Hanging up after exhausting retries.\n",
					display_name,
					wc->uri,
					wc->reconnect_attempts,
					ast_websocket_result_to_str(*result),
					errno ? " (" : "",
					errno ? strerror(errno) : "",
					errno ? ")" : ""
				);
			}
			break;
		}

		if (lock_obj) {
			ao2_lock(lock_obj);
		}
		usleep(wc->reconnect_interval * 1000);
	}

	return NULL;
}



static void wc_dtor(void *obj)
{
	struct ast_websocket_client *wc = obj;

	ast_debug(3, "%s: Disposing of websocket client config\n",
		ast_sorcery_object_get_id(wc));
	ast_string_field_free_memory(wc);
}

static void *wc_alloc(const char *id)
{
	struct ast_websocket_client *wc = NULL;

	wc = ast_sorcery_generic_alloc(sizeof(*wc), wc_dtor);
	if (!wc) {
		return NULL;
	}

	if (ast_string_field_init(wc, 1024) != 0) {
		ao2_cleanup(wc);
		return NULL;
	}

	if (ast_string_field_init_extended(wc, uri_params) != 0) {
		ao2_cleanup(wc);
		return NULL;
	}

	ast_debug(2, "%s: Allocated websocket client config\n", id);
	return wc;
}

static int websocket_client_connection_type_from_str(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_websocket_client *ws = obj;

	if (strcasecmp(var->value, "persistent") == 0) {
		ws->connection_type = AST_WS_TYPE_CLIENT_PERSISTENT;
	} else if (strcasecmp(var->value, "per_call_config") == 0) {
		ws->connection_type = AST_WS_TYPE_CLIENT_PER_CALL_CONFIG;
	} else {
		return -1;
	}

	return 0;
}

static int websocket_client_connection_type_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_websocket_client *wc = obj;

	if (wc->connection_type ==	AST_WS_TYPE_CLIENT_PERSISTENT) {
		*buf = ast_strdup("persistent");
	} else if (wc->connection_type == AST_WS_TYPE_CLIENT_PER_CALL_CONFIG) {
		*buf = ast_strdup("per_call_config");
	} else {
		return -1;
	}

	return 0;
}

/*
 * Can't use INT_MIN because it's an expression
 * and macro substitutions using stringify can't
 * handle that.
 */
#define DEFAULT_RECONNECT_ATTEMPTS -2147483648

static int wc_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_websocket_client *wc = obj;
	const char *id = ast_sorcery_object_get_id(wc);
	int res = 0;

	ast_debug(3, "%s: Applying config\n", id);

	if (ast_strlen_zero(wc->uri)) {
		ast_log(LOG_WARNING, "%s: Websocket client missing uri\n", id);
		res = -1;
	}

	if (res != 0) {
		ast_log(LOG_WARNING, "%s: Websocket client configuration failed\n", id);
	} else {
		ast_debug(3, "%s: Websocket client configuration succeeded\n", id);

		if (wc->reconnect_attempts == DEFAULT_RECONNECT_ATTEMPTS) {
			if (wc->connection_type == AST_WS_TYPE_CLIENT_PERSISTENT) {
				wc->reconnect_attempts = INT_MAX;
			} else {
				wc->reconnect_attempts = 4;
			}
		}
	}

	return res;
}

struct ao2_container *ast_websocket_client_retrieve_all(void)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_fields(sorcery, "websocket_client",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct ast_websocket_client *ast_websocket_client_retrieve_by_id(const char *id)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(sorcery, "websocket_client", id);
}

enum ast_ws_client_fields ast_websocket_client_get_field_diff(
	struct ast_websocket_client *old_wc,
	struct ast_websocket_client *new_wc)
{
	enum ast_ws_client_fields changed = AST_WS_CLIENT_FIELD_NONE;
	const char *new_id = ast_sorcery_object_get_id(new_wc);
	RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
	struct ast_variable *v = NULL;
	int res = 0;
	int changes_found = 0;

	ast_debug(2, "%s: Detecting changes\n", new_id);

	res = ast_sorcery_diff(sorcery, old_wc, new_wc, &changes);
	if (res != 0) {
		ast_log(LOG_WARNING, "%s: Failed to create changeset\n", new_id);
		return AST_WS_CLIENT_FIELD_NONE;
	}

	for (v = changes; v; v = v->next) {
		changes_found = 1;
		ast_debug(2, "%s: %s changed to %s\n", new_id, v->name, v->value);
		if (ast_strings_equal(v->name, "connection_type")) {
			changed |= AST_WS_CLIENT_FIELD_CONNECTION_TYPE;
		} else if (ast_strings_equal(v->name, "uri")) {
			changed |= AST_WS_CLIENT_FIELD_URI;
		} else if (ast_strings_equal(v->name, "protocols")) {
			changed |= AST_WS_CLIENT_FIELD_PROTOCOLS;
		} else if (ast_strings_equal(v->name, "username")) {
			changed |= AST_WS_CLIENT_FIELD_USERNAME;
		} else if (ast_strings_equal(v->name, "password")) {
			changed |= AST_WS_CLIENT_FIELD_PASSWORD;
		} else if (ast_strings_equal(v->name, "tls_enabled")) {
			changed |= AST_WS_CLIENT_FIELD_TLS_ENABLED;
		} else if (ast_strings_equal(v->name, "ca_list_file")) {
			changed |= AST_WS_CLIENT_FIELD_CA_LIST_FILE;
		} else if (ast_strings_equal(v->name, "ca_list_path")) {
			changed |= AST_WS_CLIENT_FIELD_CA_LIST_PATH;
		} else if (ast_strings_equal(v->name, "cert_file")) {
			changed |= AST_WS_CLIENT_FIELD_CERT_FILE;
		} else if (ast_strings_equal(v->name, "priv_key_file")) {
			changed |= AST_WS_CLIENT_FIELD_PRIV_KEY_FILE;
		} else if (ast_strings_equal(v->name, "reconnect_interval")) {
			changed |= AST_WS_CLIENT_FIELD_RECONNECT_INTERVAL;
		} else if (ast_strings_equal(v->name, "reconnect_attempts")) {
			changed |= AST_WS_CLIENT_FIELD_RECONNECT_ATTEMPTS;
		} else if (ast_strings_equal(v->name, "connection_timeout")) {
			changed |= AST_WS_CLIENT_FIELD_CONNECTION_TIMEOUT;
		} else if (ast_strings_equal(v->name, "verify_server_cert")) {
			changed |= AST_WS_CLIENT_FIELD_VERIFY_SERVER_CERT;
		} else if (ast_strings_equal(v->name, "verify_server_hostname")) {
			changed |= AST_WS_CLIENT_FIELD_VERIFY_SERVER_HOSTNAME;
		} else {
			ast_debug(2, "%s: Unknown change %s\n", new_id, v->name);
		}
	}

	if (!changes_found) {
		ast_debug(2, "%s: No changes found %p %p\n", new_id,
			old_wc,new_wc);
	}
	return changed;

}

int ast_websocket_client_observer_add(const struct ast_sorcery_observer *callbacks)
{
	if (!sorcery || !callbacks) {
		return -1;
	}

	if (ast_sorcery_observer_add(sorcery, "websocket_client", callbacks)) {
		ast_log(LOG_ERROR, "Failed to register websocket client observers\n");
		return -1;
	}

	return 0;
}

void ast_websocket_client_observer_remove(const struct ast_sorcery_observer *callbacks)
{
	if (!sorcery || !callbacks) {
		return;
	}

	ast_sorcery_observer_remove(sorcery, "websocket_client", callbacks);
}


static int load_module(void)
{
	ast_debug(2, "Initializing Websocket Client Configuration\n");
	sorcery = ast_sorcery_open();
	if (!sorcery) {
		ast_log(LOG_ERROR, "Failed to open sorcery\n");
		return -1;
	}

	ast_sorcery_apply_default(sorcery, "websocket_client", "config",
		"websocket_client.conf,criteria=type=websocket_client");

	if (ast_sorcery_object_register(sorcery, "websocket_client", wc_alloc,
		NULL, wc_apply)) {
		ast_log(LOG_ERROR, "Failed to register websocket_client object with sorcery\n");
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "websocket_client", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_register_cust(websocket_client, connection_type, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, uri, uri, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, protocols, protocols, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, username, username, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, password, password, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, ca_list_file, ca_list_file, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, ca_list_path, ca_list_path, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, cert_file, cert_file, "");
	ast_sorcery_register_sf(websocket_client, ast_websocket_client, priv_key_file, priv_key_file, "");
	ast_sorcery_register_bool(websocket_client, ast_websocket_client, tls_enabled, tls_enabled, "no");
	ast_sorcery_register_bool(websocket_client, ast_websocket_client, verify_server_cert, verify_server_cert, "yes");
	ast_sorcery_register_bool(websocket_client, ast_websocket_client, verify_server_hostname, verify_server_hostname, "yes");
	ast_sorcery_register_int(websocket_client, ast_websocket_client, connection_timeout, connect_timeout, 500);
	ast_sorcery_register_int(websocket_client, ast_websocket_client, reconnect_attempts, reconnect_attempts, 4);
	ast_sorcery_register_int(websocket_client, ast_websocket_client, reconnect_interval, reconnect_interval, 500);

	ast_sorcery_load(sorcery);

	return 0;
}

static int reload_module(void)
{
	ast_debug(2, "Reloading Websocket Client Configuration\n");
	ast_sorcery_reload(sorcery);

	return 0;
}

int ast_websocket_client_reload(void)
{
	ast_debug(2, "Reloading Websocket Client Configuration\n");
	if (sorcery) {
		ast_sorcery_reload(sorcery);
	}

	return 0;
}

static int unload_module(void)
{
	ast_debug(2, "Unloading Websocket Client Configuration\n");
	if (sorcery) {
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
	}
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "WebSocket Client Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_http_websocket",
);
