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
 * \brief Resource for handling CalDAV calendars
 */

/*** MODULEINFO
	<depend>neon</depend>
	<depend>ical</depend>
	<depend>libxml2</depend>
	<support_level>core</support_level>
***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <libical/ical.h>
#include <ne_session.h>
#include <ne_uri.h>
#include <ne_request.h>
#include <ne_auth.h>
#include <ne_redirect.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/calendar.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/astobj2.h"

static void *caldav_load_calendar(void *data);
static void *unref_caldav(void *obj);
static int caldav_write_event(struct ast_calendar_event *event);

static struct ast_calendar_tech caldav_tech = {
	.type = "caldav",
	.description = "CalDAV calendars",
	.module = AST_MODULE,
	.load_calendar = caldav_load_calendar,
	.unref_calendar = unref_caldav,
	.write_event = caldav_write_event,
};

struct caldav_pvt {
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

static void caldav_destructor(void *obj)
{
	struct caldav_pvt *pvt = obj;

	ast_debug(1, "Destroying pvt for CalDAV calendar %s\n", pvt->owner->name);
	if (pvt->session) {
		ne_session_destroy(pvt->session);
	}
	ast_string_field_free_memory(pvt);

	ao2_callback(pvt->events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	ao2_ref(pvt->events, -1);
}

static void *unref_caldav(void *obj)
{
	struct caldav_pvt *pvt = obj;

	ao2_ref(pvt, -1);
	return NULL;
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
	struct caldav_pvt *pvt = userdata;

	if (attempts > 1) {
		ast_log(LOG_WARNING, "Invalid username or password for CalDAV calendar '%s'\n", pvt->owner->name);
		return -1;
	}

	ne_strnzcpy(username, pvt->user, NE_ABUFSIZ);
	ne_strnzcpy(secret, pvt->secret, NE_ABUFSIZ);

	return 0;
}

static int debug_response_handler(void *userdata, ne_request *req, const ne_status *st)
{
	if (st->code < 200 || st->code > 299) {
		ast_debug(1, "Unexpected response from server, %d: %s\n", st->code, st->reason_phrase);
		return 0;
	}
	return 1;
}

static struct ast_str *caldav_request(struct caldav_pvt *pvt, const char *method, struct ast_str *req_body, struct ast_str *subdir, const char *content_type)
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
	ne_add_response_body_reader(req, debug_response_handler, fetch_response_reader, &response);
	ne_set_request_body_buffer(req, ast_str_buffer(req_body), ast_str_strlen(req_body));
	ne_add_request_header(req, "Content-type", ast_strlen_zero(content_type) ? "text/xml" : content_type);

	ret = ne_request_dispatch(req);
	ne_request_destroy(req);

	if (ret != NE_OK) {
		ast_log(LOG_WARNING, "Unknown response to CalDAV calendar %s, request %s to %s: %s\n", pvt->owner->name, method, buf, ne_get_error(pvt->session));
		ast_free(response);
		return NULL;
	}

	return response;
}

static int caldav_write_event(struct ast_calendar_event *event)
{
	struct caldav_pvt *pvt;
	struct ast_str *body = NULL, *response = NULL, *subdir = NULL;
	icalcomponent *calendar, *icalevent;
	icaltimezone *utc = icaltimezone_get_utc_timezone();
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
		!(subdir = ast_str_create(32))) {
		ast_log(LOG_ERROR, "Could not allocate memory for request!\n");
		goto write_cleanup;
	}

	pvt = event->owner->tech_pvt;

	if (ast_strlen_zero(event->uid)) {
		unsigned short val[8];
		int x;
		for (x = 0; x < 8; x++) {
			val[x] = ast_random();
		}
		ast_string_field_build(event, uid, "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
			(unsigned)val[0], (unsigned)val[1], (unsigned)val[2],
			(unsigned)val[3], (unsigned)val[4], (unsigned)val[5],
			(unsigned)val[6], (unsigned)val[7]);
	}

	calendar = icalcomponent_new(ICAL_VCALENDAR_COMPONENT);
	icalcomponent_add_property(calendar, icalproperty_new_version("2.0"));
	icalcomponent_add_property(calendar, icalproperty_new_prodid("-//Digium, Inc.//res_caldav//EN"));

	icalevent = icalcomponent_new(ICAL_VEVENT_COMPONENT);
	icalcomponent_add_property(icalevent, icalproperty_new_dtstamp(icaltime_current_time_with_zone(utc)));
	icalcomponent_add_property(icalevent, icalproperty_new_uid(event->uid));
	icalcomponent_add_property(icalevent, icalproperty_new_dtstart(icaltime_from_timet_with_zone(event->start, 0, utc)));
	icalcomponent_add_property(icalevent, icalproperty_new_dtend(icaltime_from_timet_with_zone(event->end, 0, utc)));
	if (!ast_strlen_zero(event->organizer)) {
		icalcomponent_add_property(icalevent, icalproperty_new_organizer(event->organizer));
	}
	if (!ast_strlen_zero(event->summary)) {
		icalcomponent_add_property(icalevent, icalproperty_new_summary(event->summary));
	}
	if (!ast_strlen_zero(event->description)) {
		icalcomponent_add_property(icalevent, icalproperty_new_description(event->description));
	}
	if (!ast_strlen_zero(event->location)) {
		icalcomponent_add_property(icalevent, icalproperty_new_location(event->location));
	}
	if (!ast_strlen_zero(event->categories)) {
		icalcomponent_add_property(icalevent, icalproperty_new_categories(event->categories));
	}
	if (event->priority > 0) {
		icalcomponent_add_property(icalevent, icalproperty_new_priority(event->priority));
	}

	switch (event->busy_state) {
	case AST_CALENDAR_BS_BUSY:
		icalcomponent_add_property(icalevent, icalproperty_new_status(ICAL_STATUS_CONFIRMED));
		break;

	case AST_CALENDAR_BS_BUSY_TENTATIVE:
		icalcomponent_add_property(icalevent, icalproperty_new_status(ICAL_STATUS_TENTATIVE));
		break;

	default:
		icalcomponent_add_property(icalevent, icalproperty_new_status(ICAL_STATUS_NONE));
	}

	icalcomponent_add_component(calendar, icalevent);

	ast_str_append(&body, 0, "%s", icalcomponent_as_ical_string(calendar));
	ast_str_set(&subdir, 0, "%s%s.ics", pvt->url[strlen(pvt->url) - 1] == '/' ? "" : "/", event->uid);

	if ((response = caldav_request(pvt, "PUT", body, subdir, "text/calendar"))) {
		ret = 0;
	}

write_cleanup:
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

static struct ast_str *caldav_get_events_between(struct caldav_pvt *pvt, time_t start_time, time_t end_time)
{
	struct ast_str *body, *response;
	icaltimezone *utc = icaltimezone_get_utc_timezone();
	icaltimetype start, end;
	const char *start_str, *end_str;

	if (!(body = ast_str_create(512))) {
		ast_log(LOG_ERROR, "Could not allocate memory for body of request!\n");
		return NULL;
	}

	start = icaltime_from_timet_with_zone(start_time, 0, utc);
	end = icaltime_from_timet_with_zone(end_time, 0, utc);
	start_str = icaltime_as_ical_string(start);
	end_str = icaltime_as_ical_string(end);

	/* If I was really being efficient, I would store a collection of event URIs and etags,
	 * first doing a query of just the etag and seeing if anything had changed.  If it had,
	 * then I would do a request for each of the events that had changed, and only bother
	 * updating those.  Oh well. */
	ast_str_append(&body, 0,
		"<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
		"<C:calendar-query xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
		"  <D:prop>\n"
		"    <C:calendar-data>\n"
		"      <C:expand start=\"%s\" end=\"%s\"/>\n"
		"    </C:calendar-data>\n"
		"  </D:prop>\n"
		"  <C:filter>\n"
		"    <C:comp-filter name=\"VCALENDAR\">\n"
		"      <C:comp-filter name=\"VEVENT\">\n"
		"        <C:time-range start=\"%s\" end=\"%s\"/>\n"
		"      </C:comp-filter>\n"
		"    </C:comp-filter>\n"
		"  </C:filter>\n"
		"</C:calendar-query>\n", start_str, end_str, start_str, end_str);

	response = caldav_request(pvt, "REPORT", body, NULL, NULL);
	ast_free(body);
	if (response && !ast_str_strlen(response)) {
		ast_free(response);
		return NULL;
	}

	return response;
}

static time_t icalfloat_to_timet(icaltimetype time) 
{
	struct ast_tm tm = {0,};
	struct timeval tv;

	tm.tm_mday = time.day;
	tm.tm_mon = time.month - 1;
	tm.tm_year = time.year - 1900;
	tm.tm_hour = time.hour;
	tm.tm_min = time.minute;
	tm.tm_sec = time.second;
	tm.tm_isdst = -1;
	tv = ast_mktime(&tm, NULL);

	return tv.tv_sec;
}

/* span->start & span->end may be dates or floating times which have no timezone,
 * which would mean that they should apply to the local timezone for all recepients.
 * For example, if a meeting was set for 1PM-2PM floating time, people in different time
 * zones would not be scheduled at the same local times.  Dates are often treated as
 * floating times, so all day events will need to be converted--so we can trust the
 * span here, and instead will grab the start and end from the component, which will
 * allow us to test for floating times or dates.
 */
static void caldav_add_event(icalcomponent *comp, struct icaltime_span *span, void *data)
{
	struct caldav_pvt *pvt = data;
	struct ast_calendar_event *event;
	icaltimezone *utc = icaltimezone_get_utc_timezone();
	icaltimetype start, end, tmp;
	icalcomponent *valarm;
	icalproperty *prop;
	struct icaltriggertype trigger;

	if (!(pvt && pvt->owner)) {
		ast_log(LOG_ERROR, "Require a private structure with an owner\n");
		return;
	}

	if (!(event = ast_calendar_event_alloc(pvt->owner))) {
		ast_log(LOG_ERROR, "Could not allocate an event!\n");
		return;
	}

	start = icalcomponent_get_dtstart(comp);
	end = icalcomponent_get_dtend(comp);

	event->start = icaltime_get_tzid(start) ? span->start : icalfloat_to_timet(start);
	event->end = icaltime_get_tzid(end) ? span->end : icalfloat_to_timet(end);
	event->busy_state = span->is_busy ? AST_CALENDAR_BS_BUSY : AST_CALENDAR_BS_FREE;

	if ((prop = icalcomponent_get_first_property(comp, ICAL_SUMMARY_PROPERTY))) {
		ast_string_field_set(event, summary, icalproperty_get_value_as_string(prop));
	}

	if ((prop = icalcomponent_get_first_property(comp, ICAL_DESCRIPTION_PROPERTY))) {
		ast_string_field_set(event, description, icalproperty_get_value_as_string(prop));
	}

	if ((prop = icalcomponent_get_first_property(comp, ICAL_ORGANIZER_PROPERTY))) {
		ast_string_field_set(event, organizer, icalproperty_get_value_as_string(prop));
	}

	if ((prop = icalcomponent_get_first_property(comp, ICAL_LOCATION_PROPERTY))) {
		ast_string_field_set(event, location, icalproperty_get_value_as_string(prop));
	}

	if ((prop = icalcomponent_get_first_property(comp, ICAL_CATEGORIES_PROPERTY))) {
		ast_string_field_set(event, categories, icalproperty_get_value_as_string(prop));
	}

	if ((prop = icalcomponent_get_first_property(comp, ICAL_PRIORITY_PROPERTY))) {
		event->priority = icalvalue_get_integer(icalproperty_get_value(prop));
	}

	if ((prop = icalcomponent_get_first_property(comp, ICAL_UID_PROPERTY))) {
		ast_string_field_set(event, uid, icalproperty_get_value_as_string(prop));
	} else {
		ast_log(LOG_WARNING, "No UID found, but one is required. Generating, but updates may not be acurate\n");
		if (!ast_strlen_zero(event->summary)) {
			ast_string_field_set(event, uid, event->summary);
		} else {
			char tmp[100];
			snprintf(tmp, sizeof(tmp), "%ld", event->start);
			ast_string_field_set(event, uid, tmp);
		}
	}

	/* Get the attendees */
	for (prop = icalcomponent_get_first_property(comp, ICAL_ATTENDEE_PROPERTY);
			prop; prop = icalcomponent_get_next_property(comp, ICAL_ATTENDEE_PROPERTY)) {
		struct ast_calendar_attendee *attendee;
		const char *data;

		if (!(attendee = ast_calloc(1, sizeof(*attendee)))) {
			event = ast_calendar_unref_event(event);
			return;
		}
		data = icalproperty_get_attendee(prop);
		if (ast_strlen_zero(data)) {
			ast_free(attendee);
			continue;
		}
		attendee->data = ast_strdup(data);
		AST_LIST_INSERT_TAIL(&event->attendees, attendee, next);
	}


	/* Only set values for alarm based on VALARM.  Can be overriden in main/calendar.c by autoreminder
	 * therefore, go ahead and add events even if their is no VALARM or it is malformed
	 * Currently we are only getting the first VALARM and are handling repitition in main/calendar.c from calendar.conf */
	if (!(valarm = icalcomponent_get_first_component(comp, ICAL_VALARM_COMPONENT))) {
		ao2_link(pvt->events, event);
		event = ast_calendar_unref_event(event);
		return;
	}

	if (!(prop = icalcomponent_get_first_property(valarm, ICAL_TRIGGER_PROPERTY))) {
		ast_log(LOG_WARNING, "VALARM has no TRIGGER, skipping!\n");
		ao2_link(pvt->events, event);
		event = ast_calendar_unref_event(event);
		return;
	}

	trigger = icalproperty_get_trigger(prop);

	if (icaltriggertype_is_null_trigger(trigger)) {
		ast_log(LOG_WARNING, "Bad TRIGGER for VALARM, skipping!\n");
		ao2_link(pvt->events, event);
		event = ast_calendar_unref_event(event);
		return;
	}

	if (!icaltime_is_null_time(trigger.time)) { /* This is an absolute time */
		tmp = icaltime_convert_to_zone(trigger.time, utc);
		event->alarm = icaltime_as_timet_with_zone(tmp, utc);
	} else { /* Offset from either dtstart or dtend */
		/* XXX Technically you can check RELATED to see if the event fires from the END of the event
		 * But, I'm not sure I've ever seen anyone implement it in calendaring software, so I'm ignoring for now */
		tmp = icaltime_add(start, trigger.duration);
		event->alarm = icaltime_as_timet_with_zone(tmp, icaltime_get_timezone(start));
	}

	ao2_link(pvt->events, event);
	event = ast_calendar_unref_event(event);

	return;
}

struct xmlstate {
	int in_caldata;
	struct caldav_pvt *pvt;
	struct ast_str *cdata;
	time_t start;
	time_t end;
};

static void handle_start_element(void *data, const xmlChar *fullname, const xmlChar **atts)
{
	struct xmlstate *state = data;

	if (!xmlStrcasecmp(fullname, BAD_CAST "C:calendar-data")) {
		state->in_caldata = 1;
		ast_str_reset(state->cdata);
	}
}

static void handle_end_element(void *data, const xmlChar *name)
{
	struct xmlstate *state = data;
	struct icaltimetype start, end;
	icaltimezone *utc = icaltimezone_get_utc_timezone();
	icalcomponent *iter;
	icalcomponent *comp;

	if (xmlStrcasecmp(name, BAD_CAST "C:calendar-data")) {
		return;
	}

	state->in_caldata = 0;
	if (!(state->cdata && ast_str_strlen(state->cdata))) {
		return;
	}
	/* XXX Parse the calendar blurb for recurrence events in the time range,
	 * create an event, and add it to pvt->events */
	start = icaltime_from_timet_with_zone(state->start, 0, utc);
	end = icaltime_from_timet_with_zone(state->end, 0, utc);
	comp = icalparser_parse_string(ast_str_buffer(state->cdata));

	for (iter = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);
	     iter;
	     iter = icalcomponent_get_next_component(comp, ICAL_VEVENT_COMPONENT))
	{
		icalcomponent_foreach_recurrence(iter, start, end, caldav_add_event, state->pvt);
	}

	icalcomponent_free(comp);
}

static void handle_characters(void *data, const xmlChar *ch, int len)
{
	struct xmlstate *state = data;
	xmlChar *tmp;

	if (!state->in_caldata) {
		return;
	}

	tmp = xmlStrndup(ch, len);
	ast_str_append(&state->cdata, 0, "%s", (char *)tmp);
	xmlFree(tmp);
}

static int update_caldav(struct caldav_pvt *pvt)
{
	struct timeval now = ast_tvnow();
	time_t start, end;
	struct ast_str *response;
	xmlSAXHandler saxHandler;
	struct xmlstate state = {
		.in_caldata = 0,
		.pvt = pvt
	};

	start = now.tv_sec;
	end = now.tv_sec + 60 * pvt->owner->timeframe;
	if (!(response = caldav_get_events_between(pvt, start, end))) {
		return -1;
	}

	if (!(state.cdata = ast_str_create(512))) {
		ast_free(response);
		return -1;
	}

	state.start = start;
	state.end = end;

	memset(&saxHandler, 0, sizeof(saxHandler));
	saxHandler.startElement = handle_start_element;
	saxHandler.endElement = handle_end_element;
	saxHandler.characters = handle_characters;

	xmlSAXUserParseMemory(&saxHandler, &state, ast_str_buffer(response), ast_str_strlen(response));

	ast_calendar_merge_events(pvt->owner, pvt->events);

	ast_free(response);
	ast_free(state.cdata);

	return 0;
}

static int verify_cert(void *userdata, int failures, const ne_ssl_certificate *cert)
{
	/* Verify all certs */
	return 0;
}

static void *caldav_load_calendar(void *void_data)
{
	struct caldav_pvt *pvt;
	const struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_calendar *cal = void_data;
	ast_mutex_t refreshlock;

	if (!(cal && (cfg = ast_calendar_config_acquire()))) {
		ast_log(LOG_ERROR, "You must enable calendar support for res_caldav to load\n");
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

	if (!(pvt = ao2_alloc(sizeof(*pvt), caldav_destructor))) {
		ast_log(LOG_ERROR, "Could not allocate caldav_pvt structure for calendar: %s\n", cal->name);
		ast_calendar_config_release();
		return NULL;
	}

	pvt->owner = cal;

	if (!(pvt->events = ast_calendar_event_container_alloc())) {
		ast_log(LOG_ERROR, "Could not allocate space for fetching events for calendar: %s\n", cal->name);
		pvt = unref_caldav(pvt);
		ao2_unlock(cal);
		ast_calendar_config_release();
		return NULL;
	}

	if (ast_string_field_init(pvt, 32)) {
		ast_log(LOG_ERROR, "Couldn't allocate string field space for calendar: %s\n", cal->name);
		pvt = unref_caldav(pvt);
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
		ast_log(LOG_WARNING, "No URL was specified for CalDAV calendar '%s' - skipping.\n", cal->name);
		pvt = unref_caldav(pvt);
		ao2_unlock(cal);
		return NULL;
	}

	if (ne_uri_parse(pvt->url, &pvt->uri) || pvt->uri.host == NULL || pvt->uri.path == NULL) {
		ast_log(LOG_WARNING, "Could not parse url '%s' for CalDAV calendar '%s' - skipping.\n", pvt->url, cal->name);
		pvt = unref_caldav(pvt);
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
		ne_ssl_set_verify(pvt->session, verify_cert, NULL);
	}

	cal->tech_pvt = pvt;

	ast_mutex_init(&refreshlock);

	/* Load it the first time */
	update_caldav(pvt);

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

		update_caldav(pvt);
	}

	return NULL;
}

static int load_module(void)
{
	ne_sock_init();
	if (ast_calendar_register(&caldav_tech)) {
		ne_sock_exit();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_calendar_unregister(&caldav_tech);
	ne_sock_exit();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk CalDAV Calendar Integration",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_DEVSTATE_PLUGIN,
	);
