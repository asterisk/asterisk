/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Digium, Inc.
 *
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
 * \brief Resource for handling MS Exchange calendars
 */

/*** MODULEINFO
	<depend>neon</depend>
	<depend>ical</depend>
	<depend>iksemel</depend>
***/
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <libical/ical.h>
#include <ne_session.h>
#include <ne_uri.h>
#include <ne_request.h>
#include <ne_auth.h>
#include <ne_redirect.h>
#include <iksemel.h>

#include "asterisk/module.h"
#include "asterisk/calendar.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/astobj2.h"

static void *exchangecal_load_calendar(void *data);
static void *unref_exchangecal(void *obj);
static int exchangecal_write_event(struct ast_calendar_event *event);

static struct ast_calendar_tech exchangecal_tech = {
	.type = "exchange",
	.description = "MS Exchange calendars",
	.module = AST_MODULE,
	.load_calendar = exchangecal_load_calendar,
	.unref_calendar = unref_exchangecal,
	.write_event = exchangecal_write_event,
};

struct exchangecal_pvt {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(url);
		AST_STRING_FIELD(user);
		AST_STRING_FIELD(secret);
	);
	struct ast_calendar *owner;
	ne_uri uri;
	ne_session *session;
	struct ao2_container *events;
};

struct xmlstate {
	char tag[80];
	int in_response;
	int in_propstat;
	int in_prop;
	void *ptr;
	struct exchangecal_pvt *pvt;
};

static int parse_tag(void *data, char *name, char **atts, int type)
{
	struct xmlstate *state = data;
	char *tmp;

	if ((tmp = strchr(name, ':'))) {
		tmp++;
	} else {
		return IKS_HOOK;
	}

	ast_copy_string(state->tag, tmp, sizeof(state->tag));

	switch (type) {
	case IKS_OPEN:
		if (!strcasecmp(state->tag, "response")) {
			struct ast_calendar_event *event;

			state->in_response = 1;
			if (!(event = ast_calendar_event_alloc(state->pvt->owner))) {
				return IKS_NOMEM;
			}
			state->ptr = event;
		} else if (!strcasecmp(state->tag, "propstat")) {
			state->in_propstat = 1;
		} else if (!strcasecmp(state->tag, "prop")) {
			state->in_prop = 1;
		}
		break;

	case IKS_CLOSE:
		if (!strcasecmp(state->tag, "response")) {
			struct ao2_container *events = state->pvt->events;
			struct ast_calendar_event *event = state->ptr;

			state->in_response = 0;
			if (ast_strlen_zero(event->uid)) {
				ast_log(LOG_ERROR, "This event has no UID, something has gone wrong\n");
				event = ast_calendar_unref_event(event);
				return IKS_HOOK;
			}
			ao2_link(events, event);
			event = ast_calendar_unref_event(event);
		} else if (!strcasecmp(state->tag, "propstat")) {
			state->in_propstat = 0;
		} else if (!strcasecmp(state->tag, "prop")) {
			state->in_prop = 0;
		}
		break;

	default:
		return IKS_OK;
	}

	return IKS_OK;
}

static time_t mstime_to_time_t(char *mstime)
{
	char *read, *write;
	icaltimetype tt;
	for (read = write = mstime; *read; read++) {
		if (*read == '.') {
			*write++ = 'Z';
			*write = '\0';
			break;
		}
		if (*read == '-' || *read == ':')
			continue;
		*write = *read;
		write++;
	}

	tt = icaltime_from_string(mstime);
	return icaltime_as_timet(tt);
}

static enum ast_calendar_busy_state msbusy_to_bs(const char *msbusy)
{
	if (!strcasecmp(msbusy, "FREE")) {
		return AST_CALENDAR_BS_FREE;
	} else if (!strcasecmp(msbusy, "TENTATIVE")) {
		return AST_CALENDAR_BS_BUSY_TENTATIVE;
	} else {
		return AST_CALENDAR_BS_BUSY;
	}
}

static int parse_cdata(void *data, char *value, size_t len)
{
	char *str;
	struct xmlstate *state = data;
	struct ast_calendar_event *event = state->ptr;


	str = ast_skip_blanks(value);

	if (str == value + len)
		return IKS_OK;

	if (!(str = ast_calloc(1, len + 1))) {
		return IKS_NOMEM;
	}
	memcpy(str, value, len);
	if (!(state->in_response && state->in_propstat && state->in_prop)) {
		ast_free(str);
		return IKS_OK;
	}
	/* We use ast_string_field_build here because libiksemel is parsing CDATA with &lt; as
	 * new elements which is a bit odd and shouldn't happen */
	if (!strcasecmp(state->tag, "subject")) {
		ast_string_field_build(event, summary, "%s%s", event->summary, str);
	} else if (!strcasecmp(state->tag, "location")) {
		ast_string_field_build(event, location, "%s%s", event->location, str);
	} else if (!strcasecmp(state->tag, "uid")) {
		ast_string_field_build(event, uid, "%s%s", event->location, str);
	} else if (!strcasecmp(state->tag, "organizer")) {
		ast_string_field_build(event, organizer, "%s%s", event->organizer, str);
	} else if (!strcasecmp(state->tag, "textdescription")) {
		ast_string_field_build(event, description, "%s%s", event->description, str);
	} else if (!strcasecmp(state->tag, "dtstart")) {
		event->start = mstime_to_time_t(str);
	} else if (!strcasecmp(state->tag, "dtend")) {
		event->end = mstime_to_time_t(str);
	} else if (!strcasecmp(state->tag, "busystatus")) {
		event->busy_state = msbusy_to_bs(str);
	} else if (!strcasecmp(state->tag, "reminderoffset")) {
		/*XXX Currently we rely on event->start being set first which means we rely on the response order
		 * which technically should be fine since the query returns in the order we ask for, but ... */
		event->alarm = event->start - atoi(str);
	}

	ast_free(str);
	return IKS_OK;
}

static void exchangecal_destructor(void *obj)
{
	struct exchangecal_pvt *pvt = obj;

	ast_debug(1, "Destroying pvt for Exchange calendar %s\n", pvt->owner->name);
	if (pvt->session) {
		ne_session_destroy(pvt->session);
	}
	ast_string_field_free_memory(pvt);

	ao2_callback(pvt->events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	ao2_ref(pvt->events, -1);
}

static void *unref_exchangecal(void *obj)
{
	struct exchangecal_pvt *pvt = obj;

	ao2_ref(pvt, -1);
	return NULL;
}

/* It is very important to use the return value of this function as a realloc could occur */
static struct ast_str *generate_exchange_uuid(struct ast_str *uid)
{
	unsigned short val[8];
	int x;

	for (x = 0; x < 8; x++) {
		val[x] = ast_random();
	}
	ast_str_set(&uid, 0, "%04x%04x-%04x-%04x-%04x-%04x%04x%04x", val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7]);

	return uid;
}

static int is_valid_uuid(struct ast_str *uid)
{
	int i;

	if (ast_str_strlen(uid) != 36) {
		return 0;
	}

	for (i = 0; i < ast_str_strlen(uid); i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (ast_str_buffer(uid)[i] != '-') {
				return 0;
			}
		} else if (!((ast_str_buffer(uid)[i] > 47 && ast_str_buffer(uid)[i] < 58) || (ast_str_buffer(uid)[i] > 96 && ast_str_buffer(uid)[i] < 103))) {
			return 0;
		}
	}

	return 1;
}

static struct ast_str *xml_encode_str(struct ast_str *dst, const char *src)
{
	const char *tmp;
	char buf[7];

	for (tmp = src; *tmp; tmp++) {
		switch (*tmp) {
		case '\"':
			strcpy(buf, "&quot;");
			break;

		case '\'':
			strcpy(buf, "&apos;");
			break;

		case '&':
			strcpy(buf, "&amp;");
			break;

		case '<':
			strcpy(buf, "&lt;");
			break;

		case '>':
			strcpy(buf, "&gt;");
			break;

		default:
			sprintf(buf, "%c", *tmp);
		}

		ast_str_append(&dst, 0, "%s", buf);
	}

	return dst;
}

static struct ast_str *epoch_to_exchange_time(struct ast_str *dst, time_t epoch)
{
	icaltimezone *utc = icaltimezone_get_utc_timezone();
	icaltimetype tt = icaltime_from_timet_with_zone(epoch, 0, utc);
	char tmp[30];
	int i;

	ast_copy_string(tmp, icaltime_as_ical_string(tt), sizeof(tmp));
	for (i = 0; tmp[i]; i++) {
		ast_str_append(&dst, 0, "%c", tmp[i]);
		if (i == 3 || i == 5)
			ast_str_append(&dst, 0, "%c", '-');
		if (i == 10 || i == 12)
			ast_str_append(&dst, 0, "%c", ':');
		if (i == 14)
			ast_str_append(&dst, 0, "%s", ".000");
	}

	return dst;
}

static struct ast_str *bs_to_exchange_bs(struct ast_str *dst, enum ast_calendar_busy_state bs)
{
	switch (bs) {
	case AST_CALENDAR_BS_BUSY:
		ast_str_set(&dst, 0, "%s", "BUSY");
		break;

	case AST_CALENDAR_BS_BUSY_TENTATIVE:
		ast_str_set(&dst, 0, "%s", "TENTATIVE");
		break;

	default:
		ast_str_set(&dst, 0, "%s", "FREE");
	}

	return dst;
}

static int fetch_response_reader(void *data, const char *block, size_t len)
{
	struct ast_str **response = data;
	unsigned char *tmp;

	if (!(tmp = ast_malloc(len + 1))) {
		return -1;
	}
	memcpy(tmp, block, len);
	tmp[len] = '\0';
	ast_str_append(response, 0, "%s", tmp);
	ast_free(tmp);

	return 0;
}

static int auth_credentials(void *userdata, const char *realm, int attempts, char *username, char *secret)
{
	struct exchangecal_pvt *pvt = userdata;

	if (attempts > 1) {
		ast_log(LOG_WARNING, "Invalid username or password for Exchange calendar '%s'\n", pvt->owner->name);
		return -1;
	}

	ne_strnzcpy(username, pvt->user, NE_ABUFSIZ);
	ne_strnzcpy(secret, pvt->secret, NE_ABUFSIZ);

	return 0;
}

static struct ast_str *exchangecal_request(struct exchangecal_pvt *pvt, const char *method, struct ast_str *req_body, struct ast_str *subdir)
{
	struct ast_str *response;
	ne_request *req;
	int ret;
	char buf[1000];

	if (!pvt) {
		ast_log(LOG_ERROR, "There is no private!\n");
		return NULL;
	}

	if (!(response = ast_str_create(512))) {
		ast_log(LOG_ERROR, "Could not allocate memory for response.\n");
		return NULL;
	}

	snprintf(buf, sizeof(buf), "%s%s", pvt->uri.path, subdir ? ast_str_buffer(subdir) : "");

	req = ne_request_create(pvt->session, method, buf);
	ne_add_response_body_reader(req, ne_accept_2xx, fetch_response_reader, &response);
	ne_set_request_body_buffer(req, ast_str_buffer(req_body), ast_str_strlen(req_body));
	ne_add_request_header(req, "Content-type", "text/xml");

	ret = ne_request_dispatch(req);
	ne_request_destroy(req);

	if (ret != NE_OK || !ast_str_strlen(response)) {
		ast_log(LOG_WARNING, "Unknown response to CalDAV calendar %s, request %s to %s: %s\n", pvt->owner->name, method, pvt->url, ne_get_error(pvt->session));
		ast_free(response);
		return NULL;
	}

	return response;
}

static int exchangecal_write_event(struct ast_calendar_event *event)
{
	struct ast_str *body = NULL, *response = NULL, *subdir = NULL;
	struct ast_str *uid = NULL, *summary = NULL, *description = NULL, *organizer = NULL,
	               *location = NULL, *start = NULL, *end = NULL, *busystate = NULL;
	int ret = -1;

	if (!event) {
		ast_log(LOG_WARNING, "No event passed!\n");
		return -1;
	}

	if (!(event->start && event->end)) {
		ast_log(LOG_WARNING, "The event must contain a start and an end\n");
		return -1;
	}
	if (!(body = ast_str_create(512)) ||
		!(subdir = ast_str_create(32)) ||
		!(response = ast_str_create(512))) {
		ast_log(LOG_ERROR, "Could not allocate memory for request and response!\n");
		goto write_cleanup;
	}

	if (!(uid = ast_str_create(32)) ||
		!(summary = ast_str_create(32)) ||
		!(description = ast_str_create(32)) ||
		!(organizer = ast_str_create(32)) ||
		!(location = ast_str_create(32)) ||
		!(start = ast_str_create(32)) ||
		!(end = ast_str_create(32)) ||
		!(busystate = ast_str_create(32))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for request values\n");
		goto write_cleanup;
	}

	if (ast_strlen_zero(event->uid)) {
		uid = generate_exchange_uuid(uid);
	} else {
		ast_str_set(&uid, 36, "%s", event->uid);
	}

	if (!is_valid_uuid(uid)) {
		ast_log(LOG_WARNING, "An invalid uid was provided, you may leave this field blank to have one generated for you\n");
		goto write_cleanup;
	}

	summary = xml_encode_str(summary, event->summary);
	description = xml_encode_str(description, event->description);
	organizer = xml_encode_str(organizer, event->organizer);
	location = xml_encode_str(location, event->location);
	start = epoch_to_exchange_time(start, event->start);
	end = epoch_to_exchange_time(end, event->end);
	busystate = bs_to_exchange_bs(busystate, event->busy_state);

	ast_str_append(&body, 0,
		"<?xml version=\"1.0\"?>\n"
		"<a:propertyupdate\n"
		"  xmlns:a=\"DAV:\"\n"
		"  xmlns:e=\"http://schemas.microsoft.com/exchange/\"\n"
		"  xmlns:mapi=\"http://schemas.microsoft.com/mapi/\"\n"
		"  xmlns:mapit=\"http://schemas.microsoft.com/mapi/proptag/\"\n"
		"  xmlns:x=\"xml:\" xmlns:cal=\"urn:schemas:calendar:\"\n"
		"  xmlns:dt=\"uuid:%s/\"\n" /* uid */
		"  xmlns:header=\"urn:schemas:mailheader:\"\n"
		"  xmlns:mail=\"urn:schemas:httpmail:\"\n"
		">\n"
		"    <a:set>\n"
		"      <a:prop>\n"
		"        <a:contentclass>urn:content-classes:appointment</a:contentclass>\n"
		"        <e:outlookmessageclass>IPM.Appointment</e:outlookmessageclass>\n"
		"        <mail:subject>%s</mail:subject>\n" /* summary */
		"        <mail:description>%s</mail:description>\n" /* description */
		"        <header:to>%s</header:to>\n" /* organizer */
		"        <cal:location>%s</cal:location>\n" /* location */
		"        <cal:dtstart dt:dt=\"dateTime.tz\">%s</cal:dtstart>\n" /* start */
		"        <cal:dtend dt:dt=\"dateTime.tz\">%s</cal:dtend>\n" /* end */
		"        <cal:instancetype dt:dt=\"int\">0</cal:instancetype>\n"
		"        <cal:busystatus>%s</cal:busystatus>\n" /* busy_state (BUSY, FREE, BUSY_TENTATIVE) */
		"        <cal:meetingstatus>CONFIRMED</cal:meetingstatus>\n"
		"        <cal:alldayevent dt:dt=\"boolean\">0</cal:alldayevent>\n" /* XXX need to add event support for all day events */
		"        <cal:responserequested dt:dt=\"boolean\">0</cal:responserequested>\n"
		"        <mapi:finvited dt:dt=\"boolean\">1</mapi:finvited>\n"
		"      </a:prop>\n"
		"    </a:set>\n"
		"</a:propertyupdate>\n",
		ast_str_buffer(uid), ast_str_buffer(summary), ast_str_buffer(description), ast_str_buffer(organizer), ast_str_buffer(location), ast_str_buffer(start), ast_str_buffer(end), ast_str_buffer(busystate));
	ast_verb(0, "\n\n%s\n\n", ast_str_buffer(body));
	ast_str_set(&subdir, 0, "/Calendar/%s.eml", ast_str_buffer(uid));

	response = exchangecal_request(event->owner->tech_pvt, "PROPPATCH", body, subdir);

	ret = 0;
write_cleanup:
	if (uid) {
		ast_free(uid);
	}
	if (summary) {
		ast_free(summary);
	}
	if (description) {
		ast_free(description);
	}
	if (organizer) {
		ast_free(organizer);
	}
	if (location) {
		ast_free(location);
	}
	if (start) {
		ast_free(start);
	}
	if (end) {
		ast_free(end);
	}
	if (busystate) {
		ast_free(busystate);
	}
	if (body) {
		ast_free(body);
	}
	if (response) {
		ast_free(response);
	}
	if (subdir) {
		ast_free(subdir);
	}

	return ret;
}


static struct ast_str *exchangecal_get_events_between(struct exchangecal_pvt *pvt, time_t start_time, time_t end_time)
{
	struct ast_str *body, *response;
	char start[80], end[80];
	struct timeval tv = {0,};
	struct ast_tm tm;

	tv.tv_sec = start_time;
	ast_localtime(&tv, &tm, "UTC");
	ast_strftime(start, sizeof(start), "%Y/%m/%d %T", &tm);

	tv.tv_sec = end_time;
	ast_localtime(&tv, &tm, "UTC");
	ast_strftime(end, sizeof(end), "%Y/%m/%d %T", &tm);

	if (!(body = ast_str_create(512))) {
		ast_log(LOG_ERROR, "Could not allocate memory for body of request!\n");
		return NULL;
	}

	ast_str_append(&body, 0,
		"<?xml version=\"1.0\"?>\n"
		"<g:searchrequest xmlns:g=\"DAV:\">\n"
		"        <g:sql> SELECT \"urn:schemas:calendar:location\", \"urn:schemas:httpmail:subject\",\n"
		"                \"urn:schemas:calendar:dtstart\", \"urn:schemas:calendar:dtend\",\n"
		"                \"urn:schemas:calendar:busystatus\", \"urn:schemas:calendar:instancetype\",\n"
		"                \"urn:schemas:calendar:uid\", \"urn:schemas:httpmail:textdescription\",\n"
		"                \"urn:schemas:calendar:organizer\", \"urn:schemas:calendar:reminderoffset\"\n"
		"                FROM Scope('SHALLOW TRAVERSAL OF \"%s/Calendar\"')\n"
		"                WHERE NOT \"urn:schemas:calendar:instancetype\" = 1\n"
		"                AND \"DAV:contentclass\" = 'urn:content-classes:appointment'\n"
		"                AND NOT (\"urn:schemas:calendar:dtend\" &lt; '%s'\n"
		"                OR \"urn:schemas:calendar:dtstart\" &gt; '%s')\n"
		"                ORDER BY \"urn:schemas:calendar:dtstart\" ASC\n"
		"         </g:sql>\n"
		"</g:searchrequest>\n", pvt->url, start, end);

	ast_debug(5, "Request:\n%s\n", ast_str_buffer(body));
	response = exchangecal_request(pvt, "SEARCH", body, NULL);
	ast_debug(5, "Response:\n%s\n", ast_str_buffer(response));
	ast_free(body);

	return response;
}

static int update_exchangecal(struct exchangecal_pvt *pvt)
{
	struct xmlstate state;
	struct timeval now = ast_tvnow();
	time_t start, end;
	struct ast_str *response;
	iksparser *p;

	state.pvt = pvt;
	start = now.tv_sec;
	end = now.tv_sec + 60 * pvt->owner->timeframe;
	if (!(response = exchangecal_get_events_between(pvt, start, end))) {
		return -1;
	}

	p = iks_sax_new(&state, parse_tag, parse_cdata);
	iks_parse(p, ast_str_buffer(response), ast_str_strlen(response), 1);
	ast_calendar_merge_events(pvt->owner, pvt->events);
	ast_free(response);

	return 0;
}

static void *exchangecal_load_calendar(void *void_data)
{
	struct exchangecal_pvt *pvt;
	const struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_calendar *cal = void_data;
	ast_mutex_t refreshlock;

	if (!(cal && (cfg = ast_calendar_config_acquire()))) {
		ast_log(LOG_ERROR, "You must enable calendar support for res_exchangecal to load\n");
		return NULL;
	}

	if (ao2_trylock(cal)) {
		if (cal->unloading) {
			ast_log(LOG_WARNING, "Unloading module, load_calendar cancelled.\n");
		} else {
			ast_log(LOG_WARNING, "Could not lock calendar, aborting!\n");
		}
		ast_calendar_config_release();
		return NULL;
	}

	if (!(pvt = ao2_alloc(sizeof(*pvt), exchangecal_destructor))) {
		ast_log(LOG_ERROR, "Could not allocate exchangecal_pvt structure for calendar: %s\n", cal->name);
		ast_calendar_config_release();
		return NULL;
	}

	pvt->owner = cal;

	if (!(pvt->events = ast_calendar_event_container_alloc())) {
		ast_log(LOG_ERROR, "Could not allocate space for fetching events for calendar: %s\n", cal->name);
		pvt = unref_exchangecal(pvt);
		ao2_unlock(cal);
		ast_calendar_config_release();
		return NULL;
	}

	if (ast_string_field_init(pvt, 32)) {
		ast_log(LOG_ERROR, "Couldn't allocate string field space for calendar: %s\n", cal->name);
		pvt = unref_exchangecal(pvt);
		ao2_unlock(cal);
		ast_calendar_config_release();
		return NULL;
	}

	for (v = ast_variable_browse(cfg, cal->name); v; v = v->next) {
		if (!strcasecmp(v->name, "url")) {
			ast_string_field_set(pvt, url, v->value);
		} else if (!strcasecmp(v->name, "user")) {
			ast_string_field_set(pvt, user, v->value);
		} else if (!strcasecmp(v->name, "secret")) {
			ast_string_field_set(pvt, secret, v->value);
		}
	}

	ast_calendar_config_release();

	if (ast_strlen_zero(pvt->url)) {
		ast_log(LOG_WARNING, "No URL was specified for Exchange calendar '%s' - skipping.\n", cal->name);
		pvt = unref_exchangecal(pvt);
		ao2_unlock(cal);
		return NULL;
	}

	if (ne_uri_parse(pvt->url, &pvt->uri) || pvt->uri.host == NULL || pvt->uri.path == NULL) {
		ast_log(LOG_WARNING, "Could not parse url '%s' for Exchange calendar '%s' - skipping.\n", pvt->url, cal->name);
		pvt = unref_exchangecal(pvt);
		ao2_unlock(cal);
		return NULL;
	}

	if (pvt->uri.scheme == NULL) {
		pvt->uri.scheme = "http";
	}

	if (pvt->uri.port == 0) {
		pvt->uri.port = ne_uri_defaultport(pvt->uri.scheme);
	}

	pvt->session = ne_session_create(pvt->uri.scheme, pvt->uri.host, pvt->uri.port);
	ne_redirect_register(pvt->session);
	ne_set_server_auth(pvt->session, auth_credentials, pvt);
	if (!strcasecmp(pvt->uri.scheme, "https")) {
		ne_ssl_trust_default_ca(pvt->session);
	}

	cal->tech_pvt = pvt;

	ast_mutex_init(&refreshlock);

	/* Load it the first time */
	update_exchangecal(pvt);

	ao2_unlock(cal);

	/* The only writing from another thread will be if unload is true */
	for (;;) {
		struct timeval tv = ast_tvnow();
		struct timespec ts = {0,};

		ts.tv_sec = tv.tv_sec + (60 * pvt->owner->refresh);

		ast_mutex_lock(&refreshlock);
		while (!pvt->owner->unloading) {
			if (ast_cond_timedwait(&pvt->owner->unload, &refreshlock, &ts) == ETIMEDOUT) {
				break;
			}
		}
		ast_mutex_unlock(&refreshlock);

		if (pvt->owner->unloading) {
			ast_debug(10, "Skipping refresh since we got a shutdown signal\n");
			return NULL;
		}

		ast_debug(10, "Refreshing after %d minute timeout\n", pvt->owner->refresh);

		update_exchangecal(pvt);
	}

	return NULL;
}

static int load_module(void)
{
	ne_sock_init();
	if (ast_calendar_register(&exchangecal_tech)) {
		ne_sock_exit();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_calendar_unregister(&exchangecal_tech);
	ne_sock_exit();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk MS Exchange Calendar Integration",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_DEVSTATE_PLUGIN,
	);
