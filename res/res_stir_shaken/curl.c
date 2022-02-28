/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/logger.h"
#include "asterisk/file.h"
#include "asterisk/acl.h"

#include "curl.h"
#include "general.h"
#include "stir_shaken.h"
#include "profile.h"

#include <curl/curl.h>
#include <sys/stat.h>

/* Used to check CURL headers */
#define MAX_HEADER_LENGTH 1023

/* Used to limit download size */
#define MAX_DOWNLOAD_SIZE 8192

/* Used to limit how many bytes we get from CURL per write */
#define MAX_BUF_SIZE_PER_WRITE 1024

/* Certificates should begin with this */
#define BEGIN_CERTIFICATE_STR "-----BEGIN CERTIFICATE-----"

/* CURL callback data to avoid storing useless info in AstDB */
struct curl_cb_data {
	char *cache_control;
	char *expires;
};

struct curl_cb_write_buf {
	char buf[MAX_DOWNLOAD_SIZE + 1];
	size_t size;
	const char *url;
};

struct curl_cb_open_socket {
	const struct ast_acl_list *acl;
	curl_socket_t *sockfd;
};

struct curl_cb_data *curl_cb_data_create(void)
{
	struct curl_cb_data *data;

	data = ast_calloc(1, sizeof(*data));

	return data;
}

void curl_cb_data_free(struct curl_cb_data *data)
{
	if (!data) {
		return;
	}

	ast_free(data->cache_control);
	ast_free(data->expires);

	ast_free(data);
}

static void curl_cb_open_socket_free(struct curl_cb_open_socket *data)
{
	if (!data) {
		return;
	}

	close(*data->sockfd);

	/* We don't need to free the ACL since we just use a reference */
	ast_free(data);
}

char *curl_cb_data_get_cache_control(const struct curl_cb_data *data)
{
	if (!data) {
		return NULL;
	}

	return data->cache_control;
}

char *curl_cb_data_get_expires(const struct curl_cb_data *data)
{
	if (!data) {
		return NULL;
	}

	return data->expires;
}

/*!
 * \brief Called when a CURL request completes
 *
 * \param buffer, size, nitems
 * \param data The curl_cb_data structure to store expiration info
 */
static size_t curl_header_callback(char *buffer, size_t size, size_t nitems, void *data)
{
	struct curl_cb_data *cb_data = data;
	size_t realsize;
	char *header;
	char *value;

	realsize = size * nitems;

	if (realsize > MAX_HEADER_LENGTH) {
		ast_log(LOG_WARNING, "CURL header length is too large (size: '%zu' | max: '%d')\n",
			realsize, MAX_HEADER_LENGTH);
		return 0;
	}

	header = ast_alloca(realsize + 1);
	memcpy(header, buffer, realsize);
	header[realsize] = '\0';
	value = strchr(header, ':');
	if (!value) {
		return realsize;
	}
	*value++ = '\0';
	value = ast_trim_blanks(ast_skip_blanks(value));

	if (!strcasecmp(header, "Cache-Control")) {
		cb_data->cache_control = ast_strdup(value);
	} else if (!strcasecmp(header, "Expires")) {
		cb_data->expires = ast_strdup(value);
	}

	return realsize;
}

/*!
 * \brief Prepare a CURL instance to use
 *
 * \param data The CURL callback data
 *
 * \retval NULL on failure
 * \return CURL instance on success
 */
static CURL *get_curl_instance(struct curl_cb_data *data)
{
	CURL *curl;
	struct stir_shaken_general *cfg;
	unsigned int curl_timeout;

	cfg = stir_shaken_general_get();
	curl_timeout = ast_stir_shaken_curl_timeout(cfg);
	ao2_cleanup(cfg);

	curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, curl_timeout);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, data);

	return curl;
}

/*!
 * \brief Write callback passed to libcurl
 *
 * \note If this function returns anything other than the size of the data
 * libcurl expected us to process, the request will cancel. That's why we return
 * 0 on error, otherwise the amount of data we were given
 *
 * \param curl_data The data from libcurl
 * \param size Always 1 according to libcurl
 * \param actual_size The actual size of the data
 * \param our_data The data we passed to libcurl
 *
 * \retval The size of the data we processed
 * \retval 0 if there was an error
 */
static size_t curl_write_cb(void *curl_data, size_t size, size_t actual_size, void *our_data)
{
	/* Just in case size is NOT always 1 or if it's changed in the future, let's go ahead
	 * and do the math for the actual size */
	size_t real_size = size * actual_size;
	struct curl_cb_write_buf *buf = our_data;
	size_t new_size = buf->size + real_size;

	if (new_size > MAX_DOWNLOAD_SIZE) {
		ast_log(LOG_WARNING, "Attempted to retrieve certificate from %s failed "
			"because it's size exceeds the maximum %d bytes\n", buf->url, MAX_DOWNLOAD_SIZE);
		return 0;
	}

	memcpy(&(buf->buf[buf->size]), curl_data, real_size);
	buf->size += real_size;
	buf->buf[buf->size] = 0;

	return real_size;
}

static curl_socket_t stir_shaken_curl_open_socket_callback(void *our_data, curlsocktype purpose, struct curl_sockaddr *address)
{
	struct curl_cb_open_socket *data = our_data;

	if (!ast_acl_list_is_empty((struct ast_acl_list *)data->acl)) {
		struct ast_sockaddr ast_address = { {0,} };

		ast_sockaddr_copy_sockaddr(&ast_address, &address->addr, address->addrlen);

		if (ast_apply_acl((struct ast_acl_list *)data->acl, &ast_address, NULL) != AST_SENSE_ALLOW) {
			return CURLE_COULDNT_CONNECT;
		}
	}

	*data->sockfd = socket(address->family, address->socktype, address->protocol);

	return *data->sockfd;
}

char *curl_public_key(const char *public_cert_url, const char *path, struct curl_cb_data *data, const struct ast_acl_list *acl)
{
	FILE *public_key_file;
	char *filename;
	char *serial;
	long http_code;
	CURL *curl;
	char curl_errbuf[CURL_ERROR_SIZE + 1];
	struct curl_cb_write_buf *buf;
	struct curl_cb_open_socket *open_socket_data;
	curl_socket_t sockfd;

	curl_errbuf[CURL_ERROR_SIZE] = '\0';

 	buf = ast_calloc(1, sizeof(*buf));
	if (!buf) {
		ast_log(LOG_ERROR, "Failed to allocate memory for CURL write buffer for %s\n", public_cert_url);
		return NULL;
	}

	open_socket_data = ast_calloc(1, sizeof(*open_socket_data));
	if (!open_socket_data) {
		ast_log(LOG_ERROR, "Failed to allocate memory for open socket callback\n");
		return NULL;
	}
	open_socket_data->acl = acl;
	open_socket_data->sockfd = &sockfd;

	buf->url = public_cert_url;
	curl_errbuf[CURL_ERROR_SIZE] = '\0';

	curl = get_curl_instance(data);
	if (!curl) {
		ast_log(LOG_ERROR, "Failed to set up CURL instance for '%s'\n", public_cert_url);
		ast_free(buf);
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_URL, public_cert_url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, MAX_BUF_SIZE_PER_WRITE);
	curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, stir_shaken_curl_open_socket_callback);
	curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, open_socket_data);

	if (curl_easy_perform(curl)) {
		ast_log(LOG_ERROR, "%s\n", curl_errbuf);
		curl_easy_cleanup(curl);
		ast_free(buf);
		curl_cb_open_socket_free(open_socket_data);
		return NULL;
	}

	curl_cb_open_socket_free(open_socket_data);

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_easy_cleanup(curl);

	if (http_code / 100 != 2) {
		ast_log(LOG_ERROR, "Failed to retrieve URL '%s': code %ld\n", public_cert_url, http_code);
		ast_free(buf);
		return NULL;
	}

	if (!ast_begins_with(buf->buf, BEGIN_CERTIFICATE_STR)) {
		ast_log(LOG_WARNING, "Certificate from %s does not begin with what we expect\n", public_cert_url);
		ast_free(buf);
		return NULL;
	}

	serial = stir_shaken_get_serial_number_x509(buf->buf, buf->size);
	if (!serial) {
		ast_log(LOG_ERROR, "Failed to get serial from CURL buffer from %s\n", public_cert_url);
		ast_free(buf);
		return NULL;
	}

	if (ast_asprintf(&filename, "%s/%s.pem", path, serial) < 0) {
		ast_log(LOG_ERROR, "Failed to allocate memory for filename after CURL from %s\n", public_cert_url);
		ast_free(serial);
		ast_free(buf);
		return NULL;
	}

	ast_free(serial);

	public_key_file = fopen(filename, "w");
	if (!public_key_file) {
		ast_log(LOG_ERROR, "Failed to open file '%s' to write public key from '%s': %s (%d)\n",
			filename, public_cert_url, strerror(errno), errno);
		ast_free(buf);
		ast_free(filename);
		return NULL;
	}

	if (fputs(buf->buf, public_key_file) == EOF) {
		ast_log(LOG_ERROR, "Failed to write string to file from URL %s\n", public_cert_url);
		fclose(public_key_file);
		ast_free(buf);
		ast_free(filename);
		return NULL;
	}

	fclose(public_key_file);
	ast_free(buf);

	return filename;
}
