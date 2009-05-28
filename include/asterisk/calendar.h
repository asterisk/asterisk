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

#ifndef _ASTERISK_CALENDAR_H
#define _ASTERISK_CALENDAR_H

#include "asterisk.h"
#include "asterisk/stringfields.h"
#include "asterisk/config.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"

/*! \file calendar.h
 * \brief A general API for managing calendar events with Asterisk
 *
 * \author Terry Wilson <twilson@digium.com>
 *
 * \note This API implements an abstraction for handling different calendaring
 * technologies in Asterisk. The services provided by the API are a dialplan
 * function to query whether or not a calendar is busy at the present time, a
 * adialplan function to query specific information about events in a time range,
 * a devicestate provider, and notification of calendar events through execution
 * of dialplan apps or dialplan logic at a specific context and extension.  The
 * information available through the CALENDAR_EVENT() dialplan function are:
 *
 *   SUMMARY, DESCRIPTION, ORGANIZER, LOCATION
 *   CALENDAR, UID, START, END, and BUSYSTATE
 *
 * BUSYSTATE can have the values 0 (free), 1 (tentatively busy), or 2 (busy)
 *
 * Usage
 * All calendaring configuration data is located in calendar.conf and is only read
 * directly by the Calendaring API. Each calendar technology resource must register
 * a load_calendar callback which will be passed an ast_calendar_load_data structure.
 * The load_calendar callback function should then set the values it needs from this
 * cfg, load the calendar data, and then loop updating the calendar data and events
 * baesd on the refresh interval in the ast_calendar object.  Each call to
 * the load_calendar callback will be will run in its own thread.
 *
 * Updating events involves creating an astobj2 container of new events and passing
 * it to the API through ast_calendar_merge_events.
 *
 * Calendar technology resource modules must also register an unref_calendar callback
 * which will only be called when the resource module calls ast_calendar_unregister()
 * to unregister that module's calendar type (usually done in module_unload())
 */

extern struct ast_config *calendar_config;

struct ast_calendar;
struct ast_calendar_event;

/*! \brief Individual calendaring technology data */
struct ast_calendar_tech {
	const char *type;
	const char *description;
	const char *module;
	int (* is_busy)(struct ast_calendar *calendar); /*!< Override default busy determination */
	void *(* load_calendar)(void *data);   /*!< Create private structure, add calendar events, etc. */
	void *(* unref_calendar)(void *obj);   /*!< Function to be called to free the private structure */
	int (* write_event)(struct ast_calendar_event *event);  /*!< Function for writing an event to the calendar */
	AST_LIST_ENTRY(ast_calendar_tech) list;
};

enum ast_calendar_busy_state {
	AST_CALENDAR_BS_FREE = 0,
	AST_CALENDAR_BS_BUSY_TENTATIVE,
	AST_CALENDAR_BS_BUSY,
};

struct ast_calendar_attendee {
	char *data;
	AST_LIST_ENTRY(ast_calendar_attendee) next;
};

/* \brief Calendar events */
struct ast_calendar_event {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(summary);
		AST_STRING_FIELD(description);
		AST_STRING_FIELD(organizer);
		AST_STRING_FIELD(location);
		AST_STRING_FIELD(uid);
	);
	struct ast_calendar *owner;   /*!< The calendar that owns this event */
	time_t start;        /*!< Start of event (UTC) */
	time_t end;          /*!< End of event (UTC) */
	time_t alarm;        /*!< Time for event notification */
	enum ast_calendar_busy_state busy_state;  /*!< The busy status of the event */
	int notify_sched;    /*!< The sched for event notification */
	int bs_start_sched;  /*!< The sched for changing the device state at the start of an event */
	int bs_end_sched;    /*!< The sched for changing the device state at the end of an event */
	AST_LIST_HEAD_NOLOCK(attendees, ast_calendar_attendee) attendees;
};

/*! \brief Asterisk calendar structure */
struct ast_calendar {
	const struct ast_calendar_tech *tech;
	void *tech_pvt;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);				/*!< Name from config file [name] */
		AST_STRING_FIELD(notify_channel);	/*!< Channel to use for notification */
		AST_STRING_FIELD(notify_context);	/*!< Optional context to execute from for notification */
		AST_STRING_FIELD(notify_extension);	/*!< Optional extension to execute from for notification */
		AST_STRING_FIELD(notify_app);		/*!< Optional dialplan app to execute for notification */
		AST_STRING_FIELD(notify_appdata);	/*!< Optional arguments for dialplan app */
	);
	int autoreminder;    /*!< If set, override any calendar_tech specific notification times and use this time (in mins) */
	int notify_waittime; /*!< Maxiumum time to allow for a notification attempt */
	int refresh;         /*!< When to refresh the calendar events */
	int timeframe;       /*!< Span (in mins) of calendar data to pull with each request */
	pthread_t thread;    /*!< The thread that the calendar is loaded/updated in */
	ast_cond_t unload;
	int unloading:1;
	int pending_deletion:1;
	struct ao2_container *events;  /*!< The events that are known at this time */
};

/*! \brief Register a new calendar technology
 *
 * \param tech calendar technology to register
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_calendar_register(struct ast_calendar_tech *tech);

/*! \brief Unregister a new calendar technology
 *
 * \param tech calendar technology to unregister
 *
 * \retval 0 success
 * \retva -1 failure
 */
void ast_calendar_unregister(struct ast_calendar_tech *tech);

/*! \brief Allocate an astobj2 ast_calendar_event object
 *
 * \param cal calendar to allocate an event for
 *
 * \return a new, initialized calendar event
 */
struct ast_calendar_event *ast_calendar_event_alloc(struct ast_calendar *cal);

/*! \brief Allocate an astobj2 container for ast_calendar_event objects
 *
 * \return a new event container
 */
struct ao2_container *ast_calendar_event_container_alloc(void);

/*! \brief Add an event to the list of events for a calendar
 *
 * \param cal calendar containing the events to be merged
 * \param new_events an oa2 container of events to be merged into cal->events
 */
void ast_calendar_merge_events(struct ast_calendar *cal, struct ao2_container *new_events);

/*! \brief Unreference an ast_calendar_event 
 *
 * \param event event to unref
 *
 * \return NULL
 */
struct ast_calendar_event *ast_calendar_unref_event(struct ast_calendar_event *event);

/*! \brief Remove all events from calendar 
 *
 * \param cal calendar whose events need to be cleared
 */
void ast_calendar_clear_events(struct ast_calendar *cal);

#endif /* _ASTERISK_CALENDAR_H */
