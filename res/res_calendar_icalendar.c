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
 * \brief Resource for handling iCalendar calendars
 */

/*** MODULEINFO
	<depend>neon</depend>
	<depend>ical</depend>
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

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/calendar.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/astobj2.h"

static void *ical_load_calendar(void *data);
static void *unref_icalendar(void *obj);

static struct ast_calendar_tech ical_tech = {
	.type = "ical",
	.module = AST_MODULE,
	.description = "iCalendar .ics calendars",
	.load_calendar = ical_load_calendar,
	.unref_calendar = unref_icalendar,
};

struct icalendar_pvt {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(url);
		AST_STRING_FIELD(user);
		AST_STRING_FIELD(secret);
	);
	struct ast_calendar *owner;
	ne_uri uri;
	ne_session *session;
	icalcomponent *data;
	struct ao2_container *events;
};

static void icalendar_destructor(void *obj)
{
	struct icalendar_pvt *pvt = obj;

	ast_debug(1, "Destroying pvt for iCalendar %s\n", pvt->owner->name);
	if (pvt->session) {
		ne_session_destroy(pvt->session);
	}
	if (pvt->data) {
		icalcomponent_free(pvt->data);
	}
	ast_string_field_free_memory(pvt);

	ao2_callback(pvt->events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	ao2_ref(pvt->events, -1);
}

static void *unref_icalendar(void *obj)
{
	struct icalendar_pvt *pvt = obj;

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
	struct icalendar_pvt *pvt = userdata;

	if (attempts > 1) {
		ast_log(LOG_WARNING, "Invalid username or password for iCalendar '%s'\n", pvt->owner->name);
		return -1;
	}

	ne_strnzcpy(username, pvt->user, NE_ABUFSIZ);
	ne_strnzcpy(secret, pvt->secret, NE_ABUFSIZ);

	return 0;
}

static icalcomponent *fetch_icalendar(struct icalendar_pvt *pvt)
{
	int ret;
	struct ast_str *response;
	ne_request *req;
	icalcomponent *comp = NULL;

	if (!pvt) {
		ast_log(LOG_ERROR, "There is no private!\n");
		return NULL;
	}

	if (!(response = ast_str_create(512))) {
		ast_log(LOG_ERROR, "Could not allocate memory for response.\n");
		return NULL;
	}

	req = ne_request_create(pvt->session, "GET", pvt->uri.path);
	ne_add_response_body_reader(req, ne_accept_2xx, fetch_response_reader, &response);

	ret = ne_request_dispatch(req);
	ne_request_destroy(req);
	if (ret != NE_OK || !ast_str_strlen(response)) {
		ast_log(LOG_WARNING, "Unable to retrieve iCalendar '%s' from '%s': %s\n", pvt->owner->name, pvt->url, ne_get_error(pvt->session));
		ast_free(response);
		return NULL;
	}

	if (!ast_strlen_zero(ast_str_buffer(response))) {
		comp = icalparser_parse_string(ast_str_buffer(response));
	}
	ast_free(response);

	return comp;
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

static void icalendar_add_event(icalcomponent *comp, struct icaltime_span *span, void *data)
{
	struct icalendar_pvt *pvt = data;
	struct ast_calendar_event *event;
	icaltimezone *utc = icaltimezone_get_utc_timezone();
	icaltimetype start, end, tmp;
	icalcomponent *valarm;
	icalproperty *prop;
	struct icaltriggertype trigger;

	if (!(pvt && pvt->owner)) {
		ast_log(LOG_ERROR, "Require a private structure with an ownenr\n");
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
		attendee->data = ast_strdup(data);;
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

 static void icalendar_update_events(struct icalendar_pvt *pvt)
{
	struct icaltimetype start_time, end_time;
	icalcomponent *iter;

	if (!pvt) {
		ast_log(LOG_ERROR, "iCalendar is NULL\n");
		return;
	}

	if (!pvt->owner) {
		ast_log(LOG_ERROR, "iCalendar is an orphan!\n");
		return;
	}

	if (!pvt->data) {
		ast_log(LOG_ERROR, "The iCalendar has not been parsed!\n");
		return;
	}

	start_time = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
	end_time = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
	end_time.second += pvt->owner->timeframe * 60;
	icaltime_normalize(end_time);

	for (iter = icalcomponent_get_first_component(pvt->data, ICAL_VEVENT_COMPONENT);
	     iter;
		 iter = icalcomponent_get_next_component(pvt->data, ICAL_VEVENT_COMPONENT))
	{
		icalcomponent_foreach_recurrence(iter, start_time, end_time, icalendar_add_event, pvt);
	}

	ast_calendar_merge_events(pvt->owner, pvt->events);
}

static void *ical_load_calendar(void *void_data)
{
	struct icalendar_pvt *pvt;
	const struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_calendar *cal = void_data;
	ast_mutex_t refreshlock;

	if (!(cal && (cfg = ast_calendar_config_acquire()))) {
		ast_log(LOG_ERROR, "You must enable calendar support for res_icalendar to load\n");
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

	if (!(pvt = ao2_alloc(sizeof(*pvt), icalendar_destructor))) {
		ast_log(LOG_ERROR, "Could not allocate icalendar_pvt structure for calendar: %s\n", cal->name);
		ast_calendar_config_release();
		return NULL;
	}

	pvt->owner = cal;

	if (!(pvt->events = ast_calendar_event_container_alloc())) {
		ast_log(LOG_ERROR, "Could not allocate space for fetching events for calendar: %s\n", cal->name);
		pvt = unref_icalendar(pvt);
		ao2_unlock(cal);
		ast_calendar_config_release();
		return NULL;
	}

	if (ast_string_field_init(pvt, 32)) {
		ast_log(LOG_ERROR, "Couldn't allocate string field space for calendar: %s\n", cal->name);
		pvt = unref_icalendar(pvt);
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
		ast_log(LOG_WARNING, "No URL was specified for iCalendar '%s' - skipping.\n", cal->name);
		pvt = unref_icalendar(pvt);
		ao2_unlock(cal);
		return NULL;
	}

	if (ne_uri_parse(pvt->url, &pvt->uri) || pvt->uri.host == NULL || pvt->uri.path == NULL) {
		ast_log(LOG_WARNING, "Could not parse url '%s' for iCalendar '%s' - skipping.\n", pvt->url, cal->name);
		pvt = unref_icalendar(pvt);
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
	if (!(pvt->data = fetch_icalendar(pvt))) {
		ast_log(LOG_WARNING, "Unable to parse iCalendar '%s'\n", cal->name);
	}

	icalendar_update_events(pvt);

	ao2_unlock(cal);

	/* The only writing from another thread will be if unload is true */
	for(;;) {
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

		/* Free the old calendar data */
		if (pvt->data) {
			icalcomponent_free(pvt->data);
			pvt->data = NULL;
		}
		if (!(pvt->data = fetch_icalendar(pvt))) {
			ast_log(LOG_WARNING, "Unable to parse iCalendar '%s'\n", pvt->owner->name);
			continue;
		}

		icalendar_update_events(pvt);
	}

	return NULL;
}

static int load_module(void)
{
	ne_sock_init();
	if (ast_calendar_register(&ical_tech)) {
		ne_sock_exit();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_calendar_unregister(&ical_tech);
	ne_sock_exit();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk iCalendar .ics file integration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEVSTATE_PLUGIN,
);
