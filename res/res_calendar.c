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
 * \brief Calendaring API
 * 
 * \todo Support responding to a meeting invite
 * \todo Support writing attendees
 */

/*! \li \ref res_calendar.c uses the configuration file \ref calendar.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page calendar.conf calendar.conf
 * \verbinclude calendar.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"
#include "asterisk/channel.h"
#include "asterisk/calendar.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/devicestate.h"
#include "asterisk/linkedlists.h"
#include "asterisk/sched.h"
#include "asterisk/dial.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<function name="CALENDAR_BUSY" language="en_US">
		<synopsis>
			Determine if the calendar is marked busy at this time.
		</synopsis>
		<syntax>
			<parameter name="calendar" required="true" />
		</syntax>
		<description>
			<para>Check the specified calendar's current busy status.</para>
		</description>
		<see-also>
			<ref type="function">CALENDAR_EVENT</ref>
			<ref type="function">CALENDAR_QUERY</ref>
			<ref type="function">CALENDAR_QUERY_RESULT</ref>
			<ref type="function">CALENDAR_WRITE</ref>
		</see-also>
	</function>
	<function name="CALENDAR_EVENT" language="en_US">
		<synopsis>
			Get calendar event notification data from a notification call.
		</synopsis>
		<syntax>
			<parameter name="field" required="true">
				<enumlist>
					<enum name="summary"><para>The VEVENT SUMMARY property or Exchange event 'subject'</para></enum>
					<enum name="description"><para>The text description of the event</para></enum>
					<enum name="organizer"><para>The organizer of the event</para></enum>
					<enum name="location"><para>The location of the eventt</para></enum>
					<enum name="categories"><para>The categories of the event</para></enum>
					<enum name="priority"><para>The priority of the event</para></enum>
					<enum name="calendar"><para>The name of the calendar associated with the event</para></enum>
					<enum name="uid"><para>The unique identifier for this event</para></enum>
					<enum name="start"><para>The start time of the event</para></enum>
					<enum name="end"><para>The end time of the event</para></enum>
					<enum name="busystate"><para>The busy state of the event 0=FREE, 1=TENTATIVE, 2=BUSY</para></enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Whenever a calendar event notification call is made, the event data
			may be accessed with this function.</para>
		</description>
		<see-also>
			<ref type="function">CALENDAR_BUSY</ref>
			<ref type="function">CALENDAR_QUERY</ref>
			<ref type="function">CALENDAR_QUERY_RESULT</ref>
			<ref type="function">CALENDAR_WRITE</ref>
		</see-also>
	</function>
	<function name="CALENDAR_QUERY" language="en_US">
		<synopsis>Query a calendar server and store the data on a channel
		</synopsis>
		<syntax>
			<parameter name="calendar" required="true">
				<para>The calendar that should be queried</para>
			</parameter>
			<parameter name="start" required="false">
				<para>The start time of the query (in seconds since epoch)</para>
			</parameter>
			<parameter name="end" required="false">
				<para>The end time of the query (in seconds since epoch)</para>
			</parameter>
		</syntax>
		<description>
			<para>Get a list of events in the currently accessible timeframe of the <replaceable>calendar</replaceable>
			The function returns the id for accessing the result with CALENDAR_QUERY_RESULT()</para>
		</description>
		<see-also>
			<ref type="function">CALENDAR_BUSY</ref>
			<ref type="function">CALENDAR_EVENT</ref>
			<ref type="function">CALENDAR_QUERY_RESULT</ref>
			<ref type="function">CALENDAR_WRITE</ref>
		</see-also>
	</function>
	<function name="CALENDAR_QUERY_RESULT" language="en_US">
		<synopsis>
			Retrieve data from a previously run CALENDAR_QUERY() call
		</synopsis>
		<syntax>
			<parameter name="id" required="true">
				<para>The query ID returned by <literal>CALENDAR_QUERY</literal></para>
			</parameter>
			<parameter name="field" required="true">
				<enumlist>
					<enum name="getnum"><para>number of events occurring during time range</para></enum>
					<enum name="summary"><para>A summary of the event</para></enum>
					<enum name="description"><para>The full event description</para></enum>
					<enum name="organizer"><para>The event organizer</para></enum>
					<enum name="location"><para>The event location</para></enum>
					<enum name="categories"><para>The categories of the event</para></enum>
					<enum name="priority"><para>The priority of the event</para></enum>
					<enum name="calendar"><para>The name of the calendar associted with the event</para></enum>
					<enum name="uid"><para>The unique identifier for the event</para></enum>
					<enum name="start"><para>The start time of the event (in seconds since epoch)</para></enum>
					<enum name="end"><para>The end time of the event (in seconds since epoch)</para></enum>
					<enum name="busystate"><para>The busy status of the event 0=FREE, 1=TENTATIVE, 2=BUSY</para></enum>
				</enumlist>
			</parameter>
			<parameter name="entry" required="false" default="1">
				<para>Return data from a specific event returned by the query</para>
			</parameter>
		</syntax>
		<description>
			<para>After running CALENDAR_QUERY and getting a result <replaceable>id</replaceable>, calling
			<literal>CALENDAR_QUERY</literal> with that <replaceable>id</replaceable> and a <replaceable>field</replaceable>
			will return the data for that field. If multiple events matched the query, and <replaceable>entry</replaceable>
			is provided, information from that event will be returned.</para>
		</description>
		<see-also>
			<ref type="function">CALENDAR_BUSY</ref>
			<ref type="function">CALENDAR_EVENT</ref>
			<ref type="function">CALENDAR_QUERY</ref>
			<ref type="function">CALENDAR_WRITE</ref>
		</see-also>
	</function>
	<function name="CALENDAR_WRITE" language="en_US">
		<synopsis>Write an event to a calendar</synopsis>
		<syntax>
			<parameter name="calendar" required="true">
				<para>The calendar to write to</para>
			</parameter>
			<parameter name="field" multiple="true" required="true">
				<enumlist>
					<enum name="summary"><para>A summary of the event</para></enum>
					<enum name="description"><para>The full event description</para></enum>
					<enum name="organizer"><para>The event organizer</para></enum>
					<enum name="location"><para>The event location</para></enum>
					<enum name="categories"><para>The categories of the event</para></enum>
					<enum name="priority"><para>The priority of the event</para></enum>
					<enum name="uid"><para>The unique identifier for the event</para></enum>
					<enum name="start"><para>The start time of the event (in seconds since epoch)</para></enum>
					<enum name="end"><para>The end time of the event (in seconds since epoch)</para></enum>
					<enum name="busystate"><para>The busy status of the event 0=FREE, 1=TENTATIVE, 2=BUSY</para></enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Example: CALENDAR_WRITE(calendar,field1,field2,field3)=val1,val2,val3</para>
			<para>The field and value arguments can easily be set/passed using the HASHKEYS() and HASH() functions</para>
			<variablelist>
				<variable name="CALENDAR_SUCCESS">
					<para>The status of the write operation to the calendar</para>
					<value name="1" >
						The event was successfully written to the calendar.
					</value>
					<value name="0" >
						The event was not written to the calendar due to network issues, permissions, etc.
					</value>
				</variable>
			</variablelist>

		</description>
		<see-also>
			<ref type="function">CALENDAR_BUSY</ref>
			<ref type="function">CALENDAR_EVENT</ref>
			<ref type="function">CALENDAR_QUERY</ref>
			<ref type="function">CALENDAR_QUERY_RESULT</ref>
		</see-also>
	</function>

***/
#define CALENDAR_BUCKETS 19

static struct ao2_container *calendars;
static struct ast_sched_context *sched;
static pthread_t refresh_thread = AST_PTHREADT_NULL;
static ast_mutex_t refreshlock;
static ast_cond_t refresh_condition;
static ast_mutex_t reloadlock;
static int module_unloading;

static void event_notification_destroy(void *data);
static void *event_notification_duplicate(void *data);
static void eventlist_destroy(void *data);
static void *eventlist_duplicate(void *data);

static const struct ast_datastore_info event_notification_datastore = {
	.type = "EventNotification",
	.destroy = event_notification_destroy,
	.duplicate = event_notification_duplicate,
};

static const struct ast_datastore_info eventlist_datastore_info = {
	.type = "CalendarEventList",
	.destroy = eventlist_destroy,
	.duplicate = eventlist_duplicate,
};

struct evententry {
	struct ast_calendar_event *event;
	AST_LIST_ENTRY(evententry) list;
};

static AST_LIST_HEAD_STATIC(techs, ast_calendar_tech);
AST_LIST_HEAD_NOLOCK(eventlist, evententry); /* define the type */

static struct ast_config *calendar_config;
AST_RWLOCK_DEFINE_STATIC(config_lock);

const struct ast_config *ast_calendar_config_acquire(void)
{
	ast_rwlock_rdlock(&config_lock);

	if (!calendar_config) {
		ast_rwlock_unlock(&config_lock);
		return NULL;
	}

	return calendar_config;
}

void ast_calendar_config_release(void)
{
	ast_rwlock_unlock(&config_lock);
}

static struct ast_calendar *unref_calendar(struct ast_calendar *cal)
{
	ao2_ref(cal, -1);
	return NULL;
}

static int calendar_hash_fn(const void *obj, const int flags)
{
	const struct ast_calendar *cal = obj;
	return ast_str_case_hash(cal->name);
}

static int calendar_cmp_fn(void *obj, void *arg, int flags)
{
	const struct ast_calendar *one = obj, *two = arg;
	return !strcasecmp(one->name, two->name) ? CMP_MATCH | CMP_STOP: 0;
}

static struct ast_calendar *find_calendar(const char *name)
{
	struct ast_calendar tmp = {
		.name = name,
	};
	return ao2_find(calendars, &tmp, OBJ_POINTER);
}

static int event_hash_fn(const void *obj, const int flags)
{
	const struct ast_calendar_event *event = obj;
	return ast_str_hash(event->uid);
}

static int event_cmp_fn(void *obj, void *arg, int flags)
{
	const struct ast_calendar_event *one = obj, *two = arg;
	return !strcmp(one->uid, two->uid) ? CMP_MATCH | CMP_STOP : 0;
}

static struct ast_calendar_event *find_event(struct ao2_container *events, const char *uid)
{
	struct ast_calendar_event tmp = {
		.uid = uid,
	};
	return ao2_find(events, &tmp, OBJ_POINTER);
}

struct ast_calendar_event *ast_calendar_unref_event(struct ast_calendar_event *event)
{
	ao2_ref(event, -1);
	return NULL;
}

static void calendar_destructor(void *obj)
{
	struct ast_calendar *cal = obj;

	ast_debug(3, "Destroying calendar %s\n", cal->name);

	ao2_lock(cal);
	cal->unloading = 1;
	ast_cond_signal(&cal->unload);
	pthread_join(cal->thread, NULL);
	if (cal->tech_pvt) {
		cal->tech_pvt = cal->tech->unref_calendar(cal->tech_pvt);
	}
	ast_calendar_clear_events(cal);
	ast_string_field_free_memory(cal);
	if (cal->vars) {
		ast_variables_destroy(cal->vars);
		cal->vars = NULL;
	}
	ao2_ref(cal->events, -1);
	ao2_unlock(cal);
}

static void eventlist_destructor(void *obj)
{
	struct eventlist *events = obj;
	struct evententry *entry;

	while ((entry = AST_LIST_REMOVE_HEAD(events, list))) {
		ao2_ref(entry->event, -1);
		ast_free(entry);
	}
}

static int calendar_busy_callback(void *obj, void *arg, int flags)
{
	struct ast_calendar_event *event = obj;
	int *is_busy = arg;
	struct timeval tv = ast_tvnow();

	if (tv.tv_sec >= event->start && tv.tv_sec <= event->end && event->busy_state > AST_CALENDAR_BS_FREE) {
		*is_busy = 1;
		return CMP_STOP;
	}

	return 0;
}

static int calendar_is_busy(struct ast_calendar *cal)
{
	int is_busy = 0;

	ao2_callback(cal->events, OBJ_NODATA, calendar_busy_callback, &is_busy);

	return is_busy;
}

static enum ast_device_state calendarstate(const char *data)
{
	enum ast_device_state state;
	struct ast_calendar *cal;

	if (ast_strlen_zero(data) || (!(cal = find_calendar(data)))) {
		return AST_DEVICE_INVALID;
	}

	if (cal->tech->is_busy) {
		state = cal->tech->is_busy(cal) ? AST_DEVICE_INUSE : AST_DEVICE_NOT_INUSE;
	} else {
		state = calendar_is_busy(cal) ? AST_DEVICE_INUSE : AST_DEVICE_NOT_INUSE;
	}

	cal = unref_calendar(cal);
	return state;
}

static struct ast_calendar *build_calendar(struct ast_config *cfg, const char *cat, const struct ast_calendar_tech *tech)
{
	struct ast_calendar *cal;
	struct ast_variable *v, *last = NULL;
	int new_calendar = 0;

	if (!(cal = find_calendar(cat))) {
		new_calendar = 1;
		if (!(cal = ao2_alloc(sizeof(*cal), calendar_destructor))) {
			ast_log(LOG_ERROR, "Could not allocate calendar structure. Stopping.\n");
			return NULL;
		}

		if (!(cal->events = ao2_container_alloc(CALENDAR_BUCKETS, event_hash_fn, event_cmp_fn))) {
			ast_log(LOG_ERROR, "Could not allocate events container for %s\n", cat);
			cal = unref_calendar(cal);
			return NULL;
		}

		if (ast_string_field_init(cal, 32)) {
			ast_log(LOG_ERROR, "Couldn't create string fields for %s\n", cat);
			cal = unref_calendar(cal);
			return NULL;
		}
	} else {
		cal->pending_deletion = 0;
	}

	ast_string_field_set(cal, name, cat);
	cal->tech = tech;

	cal->refresh = 3600;
	cal->timeframe = 60;
	cal->notify_waittime = 30000;

	for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
		if (!strcasecmp(v->name, "autoreminder")) {
			cal->autoreminder = atoi(v->value);
		} else if (!strcasecmp(v->name, "channel")) {
			ast_string_field_set(cal, notify_channel, v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_string_field_set(cal, notify_context, v->value);
		} else if (!strcasecmp(v->name, "extension")) {
			ast_string_field_set(cal, notify_extension, v->value);
		} else if (!strcasecmp(v->name, "waittime")) {
			int i = atoi(v->value);
			if (i > 0) {
				cal->notify_waittime = 1000 * i;
			}
		} else if (!strcasecmp(v->name, "app")) {
			ast_string_field_set(cal, notify_app, v->value);
		} else if (!strcasecmp(v->name, "appdata")) {
			ast_string_field_set(cal, notify_appdata, v->value);
		} else if (!strcasecmp(v->name, "refresh")) {
			cal->refresh = atoi(v->value);
		} else if (!strcasecmp(v->name, "timeframe")) {
			cal->timeframe = atoi(v->value);
		} else if (!strcasecmp(v->name, "setvar")) {
			char *name, *value;
			struct ast_variable *var;

			if ((name = (value = ast_strdup(v->value)))) {
				strsep(&value, "=");
				if (value) {
					if ((var = ast_variable_new(ast_strip(name), ast_strip(value), ""))) {
						if (last) {
							last->next = var;
						} else {
							cal->vars = var;
						}
						last = var;
					}
				} else {
					ast_log(LOG_WARNING, "Malformed argument. Should be '%s: variable=value'\n", v->name);
				}
				ast_free(name);
			}
		}
	}

	if (new_calendar) {
		cal->thread = AST_PTHREADT_NULL;
		ast_cond_init(&cal->unload, NULL);
		ao2_link(calendars, cal);
		if (ast_pthread_create(&cal->thread, NULL, cal->tech->load_calendar, cal)) {
			/* If we start failing to create threads, go ahead and return NULL
			 * and the tech module will be unregistered
			 */ 
			ao2_unlink(calendars, cal);
			cal = unref_calendar(cal);
		}
	}

	return cal;
}

static int load_tech_calendars(struct ast_calendar_tech *tech)
{
	struct ast_calendar *cal;
	const char *cat = NULL;
	const char *val;

	if (!calendar_config) {
		ast_log(LOG_WARNING, "Calendar support disabled, not loading %s calendar module\n", tech->type);
		return -1;
	}

	ast_rwlock_wrlock(&config_lock);
	while ((cat = ast_category_browse(calendar_config, cat))) {
		if (!strcasecmp(cat, "general")) {
			continue;
		}

		if (!(val = ast_variable_retrieve(calendar_config, cat, "type")) || strcasecmp(val, tech->type)) {
			continue;
		}

		/* A serious error occurred loading calendars from this tech and it should be disabled */
		if (!(cal = build_calendar(calendar_config, cat, tech))) {
			ast_calendar_unregister(tech);
			ast_rwlock_unlock(&config_lock);
			return -1;
		}

		cal = unref_calendar(cal);
	}

	ast_rwlock_unlock(&config_lock);

	return 0;
}

int ast_calendar_register(struct ast_calendar_tech *tech)
{
	struct ast_calendar_tech *iter;

	if (!calendar_config) {
		ast_log(LOG_WARNING, "Calendar support disabled, not loading %s calendar module\n", tech->type);
		return -1;
	}

	AST_LIST_LOCK(&techs);
	AST_LIST_TRAVERSE(&techs, iter, list) {
		if(!strcasecmp(tech->type, iter->type)) {
			ast_log(LOG_WARNING, "Already have a handler for calendar type '%s'\n", tech->type);
			AST_LIST_UNLOCK(&techs);
			return -1;
		}
	}
	AST_LIST_INSERT_HEAD(&techs, tech, list);
	tech->user = ast_module_user_add(NULL);
	AST_LIST_UNLOCK(&techs);

	ast_verb(2, "Registered calendar type '%s' (%s)\n", tech->type, tech->description);

	return load_tech_calendars(tech);
}

static int match_caltech_cb(void *user_data, void *arg, int flags)
{
	struct ast_calendar *cal = user_data;
	struct ast_calendar_tech *tech = arg;

	if (cal->tech == tech) {
		return CMP_MATCH;
	}

	return 0;
}

void ast_calendar_unregister(struct ast_calendar_tech *tech)
{
	struct ast_calendar_tech *iter;

	AST_LIST_LOCK(&techs);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&techs, iter, list) {
		if (iter != tech) {
			continue;
		}

		ao2_callback(calendars, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, match_caltech_cb, tech);

		AST_LIST_REMOVE_CURRENT(list);
		ast_module_user_remove(iter->user);
		ast_verb(2, "Unregistered calendar type '%s'\n", tech->type);
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&techs);

}

static void calendar_event_destructor(void *obj)
{
	struct ast_calendar_event *event = obj;
	struct ast_calendar_attendee *attendee;

	ast_debug(3, "Destroying event for calendar '%s'\n", event->owner->name);
	ast_string_field_free_memory(event);
	while ((attendee = AST_LIST_REMOVE_HEAD(&event->attendees, next))) {
		if (attendee->data) {
			ast_free(attendee->data);
		}
		ast_free(attendee);
	}
}

/* This is only called from ao2_callbacks that are going to unref the event for us,
 * so we don't unref the event here.  */
static struct ast_calendar_event *destroy_event(struct ast_calendar_event *event)
{
	if (event->notify_sched > -1 && ast_sched_del(sched, event->notify_sched)) {
		ast_debug(3, "Notification running, can't delete sched entry\n");
	}
	if (event->bs_start_sched > -1 && ast_sched_del(sched, event->bs_start_sched)) {
		ast_debug(3, "Devicestate update (start) running, can't delete sched entry\n");
	}
	if (event->bs_end_sched > -1 && ast_sched_del(sched, event->bs_end_sched)) {
		ast_debug(3, "Devicestate update (end) running, can't delete sched entry\n");
	}

	/* If an event is being deleted and we've fired an event changing the status at the beginning,
	 * but haven't hit the end event yet, go ahead and set the devicestate to the current busy status */
	if (event->bs_start_sched < 0 && event->bs_end_sched >= 0) {
		if (!calendar_is_busy(event->owner)) {
			ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Calendar:%s", event->owner->name);
		} else {
			ast_devstate_changed(AST_DEVICE_BUSY, AST_DEVSTATE_CACHABLE, "Calendar:%s", event->owner->name);
		}
	}

	return NULL;
}

static int clear_events_cb(void *user_data, void *arg, int flags)
{
	struct ast_calendar_event *event = user_data;

	event = destroy_event(event);

	return CMP_MATCH;
}

void ast_calendar_clear_events(struct ast_calendar *cal)
{
	ast_debug(3, "Clearing all events for calendar %s\n", cal->name);

	ao2_callback(cal->events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, clear_events_cb, NULL);
}

struct ast_calendar_event *ast_calendar_event_alloc(struct ast_calendar *cal)
{
	struct ast_calendar_event *event;
	if (!(event = ao2_alloc(sizeof(*event), calendar_event_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(event, 32)) {
		event = ast_calendar_unref_event(event);
		return NULL;
	}

	event->owner = cal;
	event->notify_sched = -1;
	event->bs_start_sched = -1;
	event->bs_end_sched = -1;

	AST_LIST_HEAD_INIT_NOLOCK(&event->attendees);

	return event;
}

struct ao2_container *ast_calendar_event_container_alloc(void)
{
	return ao2_container_alloc(CALENDAR_BUCKETS, event_hash_fn, event_cmp_fn);
}

static void event_notification_destroy(void *data)
{
	struct ast_calendar_event *event = data;

	event = ast_calendar_unref_event(event);

}

static void *event_notification_duplicate(void *data)
{
	struct ast_calendar_event *event = data;

	if (!event) {
		return NULL;
	}

	ao2_ref(event, +1);

	return event;
}

/*! \brief Generate 32 byte random string (stolen from chan_sip.c)*/
static char *generate_random_string(char *buf, size_t size)
{
	unsigned long val[4];
	int x;

	for (x = 0; x < 4; x++) {
		val[x] = ast_random();
	}
	snprintf(buf, size, "%08lx%08lx%08lx%08lx", val[0], val[1], val[2], val[3]);

	return buf;
}

static int null_chan_write(struct ast_channel *chan, struct ast_frame *frame)
{
	return 0;
}

static const struct ast_channel_tech null_tech = {
        .type = "NULL",
        .description = "Null channel (should not see this)",
		.write = null_chan_write,
};

static void *do_notify(void *data)
{
	struct ast_calendar_event *event = data;
	struct ast_dial *dial = NULL;
	struct ast_str *apptext = NULL, *tmpstr = NULL;
	struct ast_datastore *datastore;
	enum ast_dial_result res;
	struct ast_channel *chan = NULL;
	struct ast_variable *itervar;
	char *tech, *dest;
	char buf[8];
	struct ast_format_cap *caps;

	tech = ast_strdupa(event->owner->notify_channel);

	if ((dest = strchr(tech, '/'))) {
		*dest = '\0';
		dest++;
	} else {
		ast_log(LOG_WARNING, "Channel should be in form Tech/Dest (was '%s')\n", tech);
		goto notify_cleanup;
	}

	if (!(dial = ast_dial_create())) {
		ast_log(LOG_ERROR, "Could not create dial structure\n");
		goto notify_cleanup;
	}

	if (ast_dial_append(dial, tech, dest, NULL) < 0) {
		ast_log(LOG_ERROR, "Could not append channel\n");
		goto notify_cleanup;
	}

	ast_dial_set_global_timeout(dial, event->owner->notify_waittime);
	generate_random_string(buf, sizeof(buf));

	if (!(chan = ast_channel_alloc(1, AST_STATE_DOWN, 0, 0, 0, 0, 0, NULL, NULL, 0, "Calendar/%s-%s", event->owner->name, buf))) {
		ast_log(LOG_ERROR, "Could not allocate notification channel\n");
		goto notify_cleanup;
	}

	ast_channel_tech_set(chan, &null_tech);
	ast_channel_set_writeformat(chan, ast_format_slin);
	ast_channel_set_readformat(chan, ast_format_slin);
	ast_channel_set_rawwriteformat(chan, ast_format_slin);
	ast_channel_set_rawreadformat(chan, ast_format_slin);

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_log(LOG_ERROR, "Could not allocate capabilities, notification not being sent!\n");
		goto notify_cleanup;
	}
	ast_format_cap_append(caps, ast_format_slin, 0);
	ast_channel_nativeformats_set(chan, caps);
	ao2_ref(caps, -1);

	ast_channel_unlock(chan);

	if (!(datastore = ast_datastore_alloc(&event_notification_datastore, NULL))) {
		ast_log(LOG_ERROR, "Could not allocate datastore, notification not being sent!\n");
		goto notify_cleanup;
	}

	datastore->data = event;
	datastore->inheritance = DATASTORE_INHERIT_FOREVER;

	ao2_ref(event, +1);

	ast_channel_lock(chan);
	res = ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	if (!(tmpstr = ast_str_create(32))) {
		goto notify_cleanup;
	}

	for (itervar = event->owner->vars; itervar; itervar = itervar->next) {
		ast_str_substitute_variables(&tmpstr, 0, chan, itervar->value);
		pbx_builtin_setvar_helper(chan, itervar->name, ast_str_buffer(tmpstr));
	}

	if (!(apptext = ast_str_create(32))) {
		goto notify_cleanup;
	}

	if (!ast_strlen_zero(event->owner->notify_app)) {
		ast_str_set(&apptext, 0, "%s,%s", event->owner->notify_app, event->owner->notify_appdata);
		ast_dial_option_global_enable(dial, AST_DIAL_OPTION_ANSWER_EXEC, ast_str_buffer(apptext));
	} else {
	}

	ast_verb(3, "Dialing %s for notification on calendar %s\n", event->owner->notify_channel, event->owner->name);
	res = ast_dial_run(dial, chan, 0);

	if (res != AST_DIAL_RESULT_ANSWERED) {
		ast_verb(3, "Notification call for %s was not completed\n", event->owner->name);
	} else {
		struct ast_channel *answered;

		answered = ast_dial_answered_steal(dial);
		if (ast_strlen_zero(event->owner->notify_app)) {
			ast_channel_context_set(answered, event->owner->notify_context);
			ast_channel_exten_set(answered, event->owner->notify_extension);
			ast_channel_priority_set(answered, 1);
			ast_pbx_run(answered);
		}
	}

notify_cleanup:
	if (apptext) {
		ast_free(apptext);
	}
	if (tmpstr) {
		ast_free(tmpstr);
	}
	if (dial) {
		ast_dial_destroy(dial);
	}
	if (chan) {
		ast_channel_release(chan);
	}

	event = ast_calendar_unref_event(event);

	return NULL;
}

static int calendar_event_notify(const void *data)
{
	struct ast_calendar_event *event = (void *)data;
	int res = -1;
	pthread_t notify_thread = AST_PTHREADT_NULL;

	if (!(event && event->owner)) {
		ast_log(LOG_ERROR, "Extremely low-cal...in fact cal is NULL!\n");
		return res;
	}

	ao2_ref(event, +1);
	event->notify_sched = -1;

	if (ast_pthread_create_background(&notify_thread, NULL, do_notify, event) < 0) {
		ast_log(LOG_ERROR, "Could not create notification thread\n");
		return res;
	}

	res = 0;

	return res;
}

static int calendar_devstate_change(const void *data)
{
	struct ast_calendar_event *event = (struct ast_calendar_event *)data;
	struct timeval now = ast_tvnow();
	int is_end_event;

	if (!event) {
		ast_log(LOG_WARNING, "Event was NULL!\n");
		return 0;
	}

	ao2_ref(event, +1);

	is_end_event = event->end <= now.tv_sec;

	if (is_end_event) {
		event->bs_end_sched = -1;
	} else {
		event->bs_start_sched = -1;
	}

	/* We can have overlapping events, so ignore the event->busy_state and check busy state
	 * based on all events in the calendar */
	if (!calendar_is_busy(event->owner)) {
		ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Calendar:%s", event->owner->name);
	} else {
		ast_devstate_changed(AST_DEVICE_BUSY, AST_DEVSTATE_CACHABLE, "Calendar:%s", event->owner->name);
	}

	event = ast_calendar_unref_event(event);

	return 0;
}

static void copy_event_data(struct ast_calendar_event *dst, struct ast_calendar_event *src)
{
	struct ast_calendar_attendee *attendee;

	ast_string_field_set(dst, summary, src->summary);
	ast_string_field_set(dst, description, src->description);
	ast_string_field_set(dst, organizer, src->organizer);
	ast_string_field_set(dst, location, src->location);
	ast_string_field_set(dst, uid, src->uid);
	ast_string_field_set(dst, categories, src->categories);
	dst->priority = src->priority;
	dst->owner = src->owner;
	dst->start = src->start;
	dst->end = src->end;
	dst->alarm = src->alarm;
	dst->busy_state = src->busy_state;

	/* Delete any existing attendees */
	while ((attendee = AST_LIST_REMOVE_HEAD(&dst->attendees, next))) {
		ast_free(attendee);
	}

	/* Copy over the new attendees */
	while ((attendee = AST_LIST_REMOVE_HEAD(&src->attendees, next))) {
		AST_LIST_INSERT_TAIL(&dst->attendees, attendee, next);
	}
}

static int schedule_calendar_event(struct ast_calendar *cal, struct ast_calendar_event *old_event, struct ast_calendar_event *cmp_event)
{
	struct timeval now = ast_tvnow();
	struct ast_calendar_event *event;
	time_t alarm_notify_sched = 0, devstate_sched_start, devstate_sched_end;
	int changed = 0;

	event = cmp_event ? cmp_event : old_event;

	ao2_lock(event);
	if (!cmp_event || old_event->alarm != event->alarm) {
		changed = 1;
		if (cal->autoreminder) {
			alarm_notify_sched = (event->start - (60 * cal->autoreminder) - now.tv_sec) * 1000;
		} else if (event->alarm) {
			alarm_notify_sched = (event->alarm - now.tv_sec) * 1000;
		}

		/* For now, send the notification if we missed it, but the meeting hasn't happened yet */
		if (event->start >=  now.tv_sec) {
			if (alarm_notify_sched <= 0) {
				alarm_notify_sched = 1;
			}
			ast_mutex_lock(&refreshlock);
			AST_SCHED_REPLACE(old_event->notify_sched, sched, alarm_notify_sched, calendar_event_notify, old_event);
			ast_mutex_unlock(&refreshlock);
			ast_debug(3, "Calendar alarm event notification scheduled to happen in %ld ms\n", (long) alarm_notify_sched);
		}
	}

	if (!cmp_event || old_event->start != event->start) {
		changed = 1;
		devstate_sched_start = (event->start - now.tv_sec) * 1000;

		if (devstate_sched_start < 1) {
			devstate_sched_start = 1;
		}

		ast_mutex_lock(&refreshlock);
		AST_SCHED_REPLACE(old_event->bs_start_sched, sched, devstate_sched_start, calendar_devstate_change, old_event);
		ast_mutex_unlock(&refreshlock);
		ast_debug(3, "Calendar bs_start event notification scheduled to happen in %ld ms\n", (long) devstate_sched_start);
	}

	if (!cmp_event || old_event->end != event->end) {
		changed = 1;
		devstate_sched_end = (event->end - now.tv_sec) * 1000;
		ast_mutex_lock(&refreshlock);
		AST_SCHED_REPLACE(old_event->bs_end_sched, sched, devstate_sched_end, calendar_devstate_change, old_event);
		ast_mutex_unlock(&refreshlock);
		ast_debug(3, "Calendar bs_end event notification scheduled to happen in %ld ms\n", (long) devstate_sched_end);
	}

	if (changed) {
		ast_cond_signal(&refresh_condition);
	}

	ao2_unlock(event);

	return 0;
}

static int merge_events_cb(void *obj, void *arg, int flags)
{
	struct ast_calendar_event *old_event = obj, *new_event;
	struct ao2_container *new_events = arg;

	/* If we don't find the old_event in new_events, then we can safely delete the old_event */
	if (!(new_event = find_event(new_events, old_event->uid))) {
		old_event = destroy_event(old_event);
		return CMP_MATCH;
	}

	/* We have events to merge.  If any data that will affect a scheduler event has changed,
	 * then we need to replace the scheduler event */
	schedule_calendar_event(old_event->owner, old_event, new_event);

	/* Since we don't want to mess with cancelling sched events and adding new ones, just
	 * copy the internals of the new_event to the old_event */
	copy_event_data(old_event, new_event);

	/* Now we can go ahead and unlink the new_event from new_events and unref it so that only completely
	 * new events remain in the container */
	ao2_unlink(new_events, new_event);
	new_event = ast_calendar_unref_event(new_event);

	return 0;
}

static int add_new_event_cb(void *obj, void *arg, int flags)
{
	struct ast_calendar_event *new_event = obj;
	struct ao2_container *events = arg;

	ao2_link(events, new_event);
	schedule_calendar_event(new_event->owner, new_event, NULL);
	return CMP_MATCH;
}

void ast_calendar_merge_events(struct ast_calendar *cal, struct ao2_container *new_events)
{
	/* Loop through all events attached to the calendar.  If there is a matching new event
	 * merge its data over and handle any schedule changes that need to be made.  Then remove
	 * the new_event from new_events so that we are left with only new_events that we can add later. */
	ao2_callback(cal->events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, merge_events_cb, new_events);

	/* Now, we should only have completely new events in new_events.  Loop through and add them */
	ao2_callback(new_events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, add_new_event_cb, cal->events);
}


static int load_config(int reload)
{
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_config *tmpcfg;

	if (!(tmpcfg = ast_config_load2("calendar.conf", "calendar", config_flags)) ||
		tmpcfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load config calendar.conf\n");
		return -1;
	}

	if (tmpcfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	ast_rwlock_wrlock(&config_lock);
	if (calendar_config) {
		ast_config_destroy(calendar_config);
	}

	calendar_config = tmpcfg;
	ast_rwlock_unlock(&config_lock);

	return 0;
}

/*! \brief A dialplan function that can be used to determine the busy status of a calendar */
static int calendar_busy_exec(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_calendar *cal;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "CALENDAR_BUSY requires an argument: CALENDAR_BUSY(<calendar_name>)\n");
		return -1;
	}

	cal = find_calendar(data);

	if (!cal) {
		ast_log(LOG_WARNING, "Could not find calendar '%s'\n", data);
		return -1;
	}

	strcpy(buf, calendar_is_busy(cal) ? "1" : "0");
	cal = unref_calendar(cal);

	return 0;
}

static struct ast_custom_function calendar_busy_function = {
    .name = "CALENDAR_BUSY",
    .read = calendar_busy_exec,
};

static int add_event_to_list(struct eventlist *events, struct ast_calendar_event *event, time_t start, time_t end)
{
	struct evententry *entry, *iter;
	long event_startdiff = labs(start - event->start);
	long event_enddiff = labs(end - event->end);
	int i = 0;

	if (!(entry = ast_calloc(1, sizeof(*entry)))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for event list\n");
		return -1;
	}

	entry->event = event;
	ao2_ref(event, +1);

	if (start == end) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(events, iter, list) {
			long startdiff = labs(iter->event->start - start);

			ast_debug(10, "Comparing %s with startdiff %ld to %s with startdiff %ld\n", event->summary, event_startdiff, iter->event->summary, startdiff);
			++i;
			if (startdiff > event_startdiff) {
				AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
				return i;
			}
			if (startdiff == event_startdiff) {
				long enddiff = labs(iter->event->end - end);

				if (enddiff > event_enddiff) {
					AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
					return i;
				}
				if (event_startdiff == enddiff) {
					if (strcmp(event->uid, iter->event->uid) < 0) {
						AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
						return i;
					}
				}
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		AST_LIST_INSERT_TAIL(events, entry, list);

		return i;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(events, iter, list) {
		++i;
		if (iter->event->start > event->start) {
			AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
			return i;
		}

		if (iter->event->start == event->start) {
			if ((iter->event->end - iter->event->start) == (event->end - event->start)) {
				if (strcmp(event->uid, iter->event->uid) < 0) {
					AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
					return i;
				}
			}
			if ((iter->event->end - iter->event->start) < (event->end - event->start)) {
				AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
				return i;
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_LIST_INSERT_TAIL(events, entry, list);

	return i;
}

static void eventlist_destroy(void *data)
{
	struct eventlist *events = data;

	ao2_ref(events, -1);
}

static void *eventlist_duplicate(void *data)
{
	struct eventlist *events = data;

	if (!events) {
		return NULL;
	}

	ao2_ref(events, +1);

	return events;
}

static int calendar_query_exec(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_calendar *cal;
	struct ao2_iterator i;
	struct ast_calendar_event *event;
	struct eventlist *events;
	time_t start = INT_MIN, end = INT_MAX;
	struct ast_datastore *eventlist_datastore;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(calendar);
		AST_APP_ARG(start);
		AST_APP_ARG(end);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "%s requires a channel to store the data on\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.calendar)) {
		ast_log(LOG_WARNING, "%s requires a calendar argument\n", cmd);
		return -1;
	}

	if (!(cal = find_calendar(args.calendar))) {
		ast_log(LOG_WARNING, "Unknown calendar '%s'\n", args.calendar);
		return -1;
	}

	if (!(events = ao2_alloc(sizeof(*events), eventlist_destructor))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for event list\n");
		cal = unref_calendar(cal);
		return -1;
	}

	if (!ast_strlen_zero(args.start)) {
		start = atoi(args.start);
	}

	if (!ast_strlen_zero(args.end)) {
		end = atoi(args.end);
	}

	i = ao2_iterator_init(cal->events, 0);
	while ((event = ao2_iterator_next(&i))) {
		if (!(start > event->end || end < event->start)) {
			ast_debug(10, "%s (%ld - %ld) overlapped with (%ld - %ld)\n", event->summary, (long) event->start, (long) event->end, (long) start, (long) end);
			if (add_event_to_list(events, event, start, end) < 0) {
				event = ast_calendar_unref_event(event);
				cal = unref_calendar(cal);
				ao2_ref(events, -1);
				ao2_iterator_destroy(&i);
				return -1;
			}
		}

		event = ast_calendar_unref_event(event);
	}
	ao2_iterator_destroy(&i);

	ast_channel_lock(chan);
	do {
		generate_random_string(buf, len);
	} while (ast_channel_datastore_find(chan, &eventlist_datastore_info, buf));
	ast_channel_unlock(chan);

	if (!(eventlist_datastore = ast_datastore_alloc(&eventlist_datastore_info, buf))) {
		ast_log(LOG_ERROR, "Could not allocate datastore!\n");
		cal = unref_calendar(cal);
		ao2_ref(events, -1);
		return -1;
	}

	eventlist_datastore->inheritance = DATASTORE_INHERIT_FOREVER;
	eventlist_datastore->data = events;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, eventlist_datastore);
	ast_channel_unlock(chan);

	cal = unref_calendar(cal);
	return 0;
}

static struct ast_custom_function calendar_query_function = {
    .name = "CALENDAR_QUERY",
    .read = calendar_query_exec,
};

static void calendar_join_attendees(struct ast_calendar_event *event, char *buf, size_t len)
{
	struct ast_str *tmp;
	struct ast_calendar_attendee *attendee;

	if (!(tmp = ast_str_create(32))) {
		ast_log(LOG_ERROR, "Could not allocate memory for attendees!\n");
		return;
	}

	AST_LIST_TRAVERSE(&event->attendees, attendee, next) {
		ast_str_append(&tmp, 0, "%s%s", attendee == AST_LIST_FIRST(&event->attendees) ? "" : ",", attendee->data);
	}

	ast_copy_string(buf, ast_str_buffer(tmp), len);
	ast_free(tmp);
}

static int calendar_query_result_exec(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *datastore;
	struct eventlist *events;
	struct evententry *entry;
	int row = 1;
	size_t listlen = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(id);
		AST_APP_ARG(field);
		AST_APP_ARG(row);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "%s requires a channel\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.id) || ast_strlen_zero(args.field)) {
		ast_log(LOG_WARNING, "%s requires an id and a field", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &eventlist_datastore_info, args.id))) {
		ast_log(LOG_WARNING, "There is no event notification datastore with id '%s' on '%s'!\n", args.id, ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}
	ast_channel_unlock(chan);

	if (!(events = datastore->data)) {
		ast_log(LOG_WARNING, "The datastore contains no data!\n");
		return -1;
	}

	if (!ast_strlen_zero(args.row)) {
		row = atoi(args.row);
	}

	AST_LIST_TRAVERSE(events, entry, list) {
		listlen++;
	}

	if (!strcasecmp(args.field, "getnum")) {
		snprintf(buf, len, "%zu", listlen);
		return 0;
	}

	AST_LIST_TRAVERSE(events, entry, list) {
		if (--row) {
			continue;
		}
		if (!strcasecmp(args.field, "summary")) {
			ast_copy_string(buf, entry->event->summary, len);
		} else if (!strcasecmp(args.field, "description")) {
			ast_copy_string(buf, entry->event->description, len);
		} else if (!strcasecmp(args.field, "organizer")) {
			ast_copy_string(buf, entry->event->organizer, len);
		} else if (!strcasecmp(args.field, "location")) {
			ast_copy_string(buf, entry->event->location, len);
		} else if (!strcasecmp(args.field, "categories")) {
			ast_copy_string(buf, entry->event->categories, len);
		} else if (!strcasecmp(args.field, "priority")) {
			snprintf(buf, len, "%d", entry->event->priority);
		} else if (!strcasecmp(args.field, "calendar")) {
			ast_copy_string(buf, entry->event->owner->name, len);
		} else if (!strcasecmp(args.field, "uid")) {
			ast_copy_string(buf, entry->event->uid, len);
		} else if (!strcasecmp(args.field, "start")) {
			snprintf(buf, len, "%ld", (long) entry->event->start);
		} else if (!strcasecmp(args.field, "end")) {
			snprintf(buf, len, "%ld", (long) entry->event->end);
		} else if (!strcasecmp(args.field, "busystate")) {
			snprintf(buf, len, "%u", entry->event->busy_state);
		} else if (!strcasecmp(args.field, "attendees")) {
			calendar_join_attendees(entry->event, buf, len);
		} else {
			ast_log(LOG_WARNING, "Unknown field '%s'\n", args.field);
		}
		break;
	}

	return 0;
}

static struct ast_custom_function calendar_query_result_function = {
	.name = "CALENDAR_QUERY_RESULT",
	.read = calendar_query_result_exec,
};

static int calendar_write_exec(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int i, j, ret = -1;
	char *val_dup = NULL;
	struct ast_calendar *cal = NULL;
	struct ast_calendar_event *event = NULL;
	struct timeval tv = ast_tvnow();
	AST_DECLARE_APP_ARGS(fields,
		AST_APP_ARG(field)[10];
	);
	AST_DECLARE_APP_ARGS(values,
		AST_APP_ARG(value)[10];
	);

	if (!(val_dup = ast_strdup(value))) {
		ast_log(LOG_ERROR, "Could not allocate memory for values\n");
		goto write_cleanup;
	}

	AST_STANDARD_APP_ARGS(fields, data);
	AST_STANDARD_APP_ARGS(values, val_dup);

	/* XXX Eventually we will support unnamed calendars, so if we don't find one, we parse
	 * for a calendar type and create it */
	if (!(cal = find_calendar(fields.field[0]))) {
		ast_log(LOG_WARNING, "Couldn't find calendar '%s'\n", fields.field[0]);
		goto write_cleanup;
	}

	if (!(cal->tech->write_event)) {
		ast_log(LOG_WARNING, "Calendar '%s' has no write function!\n", cal->name);
		goto write_cleanup;
	}

	if (!(event = ast_calendar_event_alloc(cal))) {
		goto write_cleanup;
	}

	if (ast_strlen_zero(fields.field[0])) {
		ast_log(LOG_WARNING, "CALENDAR_WRITE requires a calendar name!\n");
		goto write_cleanup;
	}

	if (fields.argc - 1 != values.argc) {
		ast_log(LOG_WARNING, "CALENDAR_WRITE should have the same number of fields (%u) and values (%u)!\n", fields.argc - 1, values.argc);
		goto write_cleanup;
	}

	event->owner = cal;

	for (i = 1, j = 0; i < fields.argc; i++, j++) {
		if (!strcasecmp(fields.field[i], "summary")) {
			ast_string_field_set(event, summary, values.value[j]);
		} else if (!strcasecmp(fields.field[i], "description")) {
			ast_string_field_set(event, description, values.value[j]);
		} else if (!strcasecmp(fields.field[i], "organizer")) {
			ast_string_field_set(event, organizer, values.value[j]);
		} else if (!strcasecmp(fields.field[i], "location")) {
			ast_string_field_set(event, location, values.value[j]);
		} else if (!strcasecmp(fields.field[i], "categories")) {
			ast_string_field_set(event, categories, values.value[j]);
		} else if (!strcasecmp(fields.field[i], "priority")) {
			event->priority = atoi(values.value[j]);
		} else if (!strcasecmp(fields.field[i], "uid")) {
			ast_string_field_set(event, uid, values.value[j]);
		} else if (!strcasecmp(fields.field[i], "start")) {
			event->start = atoi(values.value[j]);
		} else if (!strcasecmp(fields.field[i], "end")) {
			event->end = atoi(values.value[j]);
		} else if (!strcasecmp(fields.field[i], "busystate")) {
			event->busy_state = atoi(values.value[j]);
		} else {
			ast_log(LOG_WARNING, "Unknown calendar event field '%s'\n", fields.field[i]);
		}
	}

	if (!event->start) {
		event->start = tv.tv_sec;
	}

	if (!event->end) {
		event->end = tv.tv_sec;
	}

	if((ret = cal->tech->write_event(event))) {
		ast_log(LOG_WARNING, "Writing event to calendar '%s' failed!\n", cal->name);
	}

write_cleanup:
	if (ret) {
		pbx_builtin_setvar_helper(chan, "CALENDAR_SUCCESS", "0");
	} else {
		pbx_builtin_setvar_helper(chan, "CALENDAR_SUCCESS", "1");
	}
	if (cal) {
		cal = unref_calendar(cal);
	}
	if (event) {
		event = ast_calendar_unref_event(event);
	}
	if (val_dup) {
		ast_free(val_dup);
	}

	return ret;
}

static struct ast_custom_function calendar_write_function = {
	.name = "CALENDAR_WRITE",
	.write = calendar_write_exec,
};

/*! \brief CLI command to list available calendars */
static char *handle_show_calendars(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-20.20s %-10.10s %-6.6s\n"
	struct ao2_iterator i;
	struct ast_calendar *cal;

	switch(cmd) {
	case CLI_INIT:
		e->command = "calendar show calendars";
		e->usage =
			"Usage: calendar show calendars\n"
			"       Lists all registered calendars.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, FORMAT, "Calendar", "Type", "Status");
	ast_cli(a->fd, FORMAT, "--------", "----", "------");
	i = ao2_iterator_init(calendars, 0);
	while ((cal = ao2_iterator_next(&i))) {
		ast_cli(a->fd, FORMAT, cal->name, cal->tech->type, calendar_is_busy(cal) ? "busy" : "free");
		cal = unref_calendar(cal);
	}
	ao2_iterator_destroy(&i);

	return CLI_SUCCESS;
#undef FORMAT
}

/*! \brief CLI command to list of all calendars types currently loaded on the backend */
static char *handle_show_calendars_types(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-10.10s %-30.30s\n"
        struct ast_calendar_tech *iter;


	switch(cmd) {
	case CLI_INIT:
		e->command = "calendar show types";
		e->usage =
			"Usage: calendar show types\n"
			"       Lists all registered calendars types.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, FORMAT, "Type", "Description");
	AST_LIST_LOCK(&techs);
	AST_LIST_TRAVERSE(&techs, iter, list) {
		ast_cli(a->fd, FORMAT, iter->type, iter->description);
	}
	AST_LIST_UNLOCK(&techs);

	return CLI_SUCCESS;
#undef FORMAT
}

static char *epoch_to_string(char *buf, size_t buflen, time_t epoch)
{
	struct ast_tm tm;
	struct timeval tv = {
		.tv_sec = epoch,
	};

	if (!epoch) {
		*buf = '\0';
		return buf;
	}
	ast_localtime(&tv, &tm, NULL);
	ast_strftime(buf, buflen, "%F %r %z", &tm);

	return buf;
}

static char *handle_show_calendar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-17.17s : %-20.20s\n"
#define FORMAT2 "%-12.12s: %-40.60s\n"
	struct ao2_iterator i;
	struct ast_calendar *cal;
	struct ast_calendar_event *event;
	int which = 0;
	char *ret = NULL;

	switch(cmd) {
	case CLI_INIT:
		e->command = "calendar show calendar";
		e->usage =
			"Usage: calendar show calendar <calendar name>\n"
			"       Displays information about a calendar\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos != 3) {
			return NULL;
		}
		i = ao2_iterator_init(calendars, 0);
		while ((cal = ao2_iterator_next(&i))) {
			if (!strncasecmp(a->word, cal->name, strlen(a->word)) && ++which > a->n) {
				ret = ast_strdup(cal->name);
				cal = unref_calendar(cal);
				break;
			}
			cal = unref_calendar(cal);
		}
		ao2_iterator_destroy(&i);
		return ret;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(cal = find_calendar(a->argv[3]))) {
		return NULL;
	}

	ast_cli(a->fd, FORMAT, "Name", cal->name);
	ast_cli(a->fd, FORMAT, "Notify channel", cal->notify_channel);
	ast_cli(a->fd, FORMAT, "Notify context", cal->notify_context);
	ast_cli(a->fd, FORMAT, "Notify extension", cal->notify_extension);
	ast_cli(a->fd, FORMAT, "Notify application", cal->notify_app);
	ast_cli(a->fd, FORMAT, "Notify appdata", cal->notify_appdata);
	ast_cli(a->fd, "%-17.17s : %d\n", "Refresh time", cal->refresh);
	ast_cli(a->fd, "%-17.17s : %d\n", "Timeframe", cal->timeframe);
	ast_cli(a->fd, "%-17.17s : %d\n", "Autoreminder", cal->autoreminder);
	ast_cli(a->fd, "%s\n", "Events");
	ast_cli(a->fd, "%s\n", "------");

	i = ao2_iterator_init(cal->events, 0);
	while ((event = ao2_iterator_next(&i))) {
		char buf[100];

		ast_cli(a->fd, FORMAT2, "Summary", event->summary);
		ast_cli(a->fd, FORMAT2, "Description", event->description);
		ast_cli(a->fd, FORMAT2, "Organizer", event->organizer);
		ast_cli(a->fd, FORMAT2, "Location", event->location);
		ast_cli(a->fd, FORMAT2, "Categories", event->categories);
		ast_cli(a->fd, "%-12.12s: %d\n", "Priority", event->priority);
		ast_cli(a->fd, FORMAT2, "UID", event->uid);
		ast_cli(a->fd, FORMAT2, "Start", epoch_to_string(buf, sizeof(buf), event->start));
		ast_cli(a->fd, FORMAT2, "End", epoch_to_string(buf, sizeof(buf), event->end));
		ast_cli(a->fd, FORMAT2, "Alarm", epoch_to_string(buf, sizeof(buf), event->alarm));
		ast_cli(a->fd, "\n");

		event = ast_calendar_unref_event(event);
	}
	ao2_iterator_destroy(&i);
	cal = unref_calendar(cal);
	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char *handle_dump_sched(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch(cmd) {
	case CLI_INIT:
		e->command = "calendar dump sched";
		e->usage =
			"Usage: calendar dump sched\n"
			"       Dump the calendar sched context";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}

	ast_sched_dump(sched);

	return CLI_SUCCESS;
}

static struct ast_cli_entry calendar_cli[] = {
	AST_CLI_DEFINE(handle_show_calendar, "Display information about a calendar"),
	AST_CLI_DEFINE(handle_show_calendars, "Show registered calendars"),
	AST_CLI_DEFINE(handle_dump_sched, "Dump calendar sched context"),
	AST_CLI_DEFINE(handle_show_calendars_types, "Show all calendar types loaded"),
};

static int calendar_event_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *datastore;
	struct ast_calendar_event *event;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &event_notification_datastore, NULL))) {
		ast_log(LOG_WARNING, "There is no event notification datastore on '%s'!\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}
	ast_channel_unlock(chan);

	if (!(event = datastore->data)) {
		ast_log(LOG_WARNING, "The datastore contains no data!\n");
		return -1;
	}

	if (!strcasecmp(data, "summary")) {
		ast_copy_string(buf, event->summary, len);
	} else if (!strcasecmp(data, "description")) {
		ast_copy_string(buf, event->description, len);
	} else if (!strcasecmp(data, "organizer")) {
		ast_copy_string(buf, event->organizer, len);
	} else if (!strcasecmp(data, "location")) {
		ast_copy_string(buf, event->location, len);
	} else if (!strcasecmp(data, "categories")) {
		ast_copy_string(buf, event->categories, len);
	} else if (!strcasecmp(data, "priority")) {
		snprintf(buf, len, "%d", event->priority);
	} else if (!strcasecmp(data, "calendar")) {
		ast_copy_string(buf, event->owner->name, len);
	} else if (!strcasecmp(data, "uid")) {
		ast_copy_string(buf, event->uid, len);
	} else if (!strcasecmp(data, "start")) {
		snprintf(buf, len, "%ld", (long)event->start);
	} else if (!strcasecmp(data, "end")) {
		snprintf(buf, len, "%ld", (long)event->end);
	} else if (!strcasecmp(data, "busystate")) {
		snprintf(buf, len, "%u", event->busy_state);
	} else if (!strcasecmp(data, "attendees")) {
		calendar_join_attendees(event, buf, len);
	}


	return 0;
}

static struct ast_custom_function calendar_event_function = {
	.name = "CALENDAR_EVENT",
	.read = calendar_event_read,
};

static int cb_pending_deletion(void *user_data, void *arg, int flags)
{
	struct ast_calendar *cal = user_data;

	cal->pending_deletion = 1;

	return CMP_MATCH;
}

static int cb_rm_pending_deletion(void *user_data, void *arg, int flags)
{
	struct ast_calendar *cal = user_data;

	return cal->pending_deletion ? CMP_MATCH : 0;
}

static int reload(void)
{
	struct ast_calendar_tech *iter;

	ast_mutex_lock(&reloadlock);

	/* Mark existing calendars for deletion */
	ao2_callback(calendars, OBJ_NODATA | OBJ_MULTIPLE, cb_pending_deletion, NULL);
	load_config(1);

	AST_LIST_LOCK(&techs);
	AST_LIST_TRAVERSE(&techs, iter, list) {
		if (load_tech_calendars(iter)) {
			ast_log(LOG_WARNING, "Failed to reload %s calendars, module disabled\n", iter->type);
		}
	}
	AST_LIST_UNLOCK(&techs);

	/* Delete calendars that no longer show up in the config */
	ao2_callback(calendars, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, cb_rm_pending_deletion, NULL);

	ast_mutex_unlock(&reloadlock);

	return 0;
}

static void *do_refresh(void *data)
{
	for (;;) {
		struct timeval now = ast_tvnow();
		struct timespec ts = {0,};
		int wait;

		ast_mutex_lock(&refreshlock);

		while (!module_unloading) {
			if ((wait = ast_sched_wait(sched)) < 0) {
				wait = 1000;
			}

			ts.tv_sec = (now.tv_sec + wait / 1000) + 1;
			if (ast_cond_timedwait(&refresh_condition, &refreshlock, &ts) == ETIMEDOUT) {
				break;
			}
		}
		ast_mutex_unlock(&refreshlock);

		if (module_unloading) {
			break;
		}
		ast_sched_runq(sched);
	}

	return NULL;
}

/* If I were to allow unloading it would look something like this */
static int unload_module(void)
{
	struct ast_calendar_tech *tech;

	ast_devstate_prov_del("calendar");
	ast_custom_function_unregister(&calendar_busy_function);
	ast_custom_function_unregister(&calendar_event_function);
	ast_custom_function_unregister(&calendar_query_function);
	ast_custom_function_unregister(&calendar_query_result_function);
	ast_custom_function_unregister(&calendar_write_function);
	ast_cli_unregister_multiple(calendar_cli, ARRAY_LEN(calendar_cli));

	/* Remove all calendars */
	ao2_callback(calendars, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
	ao2_cleanup(calendars);
	calendars = NULL;

	ast_mutex_lock(&refreshlock);
	module_unloading = 1;
	ast_cond_signal(&refresh_condition);
	ast_mutex_unlock(&refreshlock);
	pthread_join(refresh_thread, NULL);

	AST_LIST_LOCK(&techs);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&techs, tech, list) {
		ast_unload_resource(tech->module, 0);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&techs);

	ast_config_destroy(calendar_config);
	calendar_config = NULL;

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
	if (!(calendars = ao2_container_alloc(CALENDAR_BUCKETS, calendar_hash_fn, calendar_cmp_fn))) {
		ast_log(LOG_ERROR, "Unable to allocate calendars container!\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (load_config(0)) {
		/* We don't have calendar support enabled */
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_mutex_init(&refreshlock);
	ast_cond_init(&refresh_condition, NULL);
	ast_mutex_init(&reloadlock);

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create sched context\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_pthread_create_background(&refresh_thread, NULL, do_refresh, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start refresh thread--notifications disabled!\n");
	}

	ast_custom_function_register(&calendar_busy_function);
	ast_custom_function_register(&calendar_event_function);
	ast_custom_function_register(&calendar_query_function);
	ast_custom_function_register(&calendar_query_result_function);
	ast_custom_function_register(&calendar_write_function);
	ast_cli_register_multiple(calendar_cli, ARRAY_LEN(calendar_cli));

	ast_devstate_prov_add("Calendar", calendarstate);

	return AST_MODULE_LOAD_SUCCESS;
}
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Asterisk Calendar integration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);
