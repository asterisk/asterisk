/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Digium, Inc.
 *
 * Jan Kalab <pitlicek@gmail.com>
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
 * \brief Resource for handling MS Exchange Web Service calendars
 */

/*** MODULEINFO
	<depend>neon29</depend>
	<support_level>core</support_level>
***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <ne_request.h>
#include <ne_session.h>
#include <ne_uri.h>
#include <ne_socket.h>
#include <ne_auth.h>
#include <ne_xml.h>
#include <ne_xmlreq.h>
#include <ne_utils.h>
#include <ne_redirect.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/calendar.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/astobj2.h"

static void *ewscal_load_calendar(void *data);
static void *unref_ewscal(void *obj);
static int ewscal_write_event(struct ast_calendar_event *event);

static struct ast_calendar_tech ewscal_tech = {
	.type = "ews",
	.description = "MS Exchange Web Service calendars",
	.module = AST_MODULE,
	.load_calendar = ewscal_load_calendar,
	.unref_calendar = unref_ewscal,
	.write_event = ewscal_write_event,
};

enum xml_op {
	XML_OP_FIND = 100,
	XML_OP_GET,
	XML_OP_CREATE,
};

struct calendar_id {
	struct ast_str *id;
	AST_LIST_ENTRY(calendar_id) next;
};

struct xml_context {
	ne_xml_parser *parser;
	struct ast_str *cdata;
	struct ast_calendar_event *event;
	enum xml_op op;
	struct ewscal_pvt *pvt;
	AST_LIST_HEAD_NOLOCK(ids, calendar_id) ids;
};

/* Important states of XML parsing */
enum {
	XML_EVENT_CALENDAR_ITEM = 9,
	XML_EVENT_NAME = 10,
	XML_EVENT_DESCRIPTION,
	XML_EVENT_START,
	XML_EVENT_END,
	XML_EVENT_BUSY,
	XML_EVENT_ORGANIZER,
	XML_EVENT_LOCATION,
	XML_EVENT_ATTENDEE_LIST,
	XML_EVENT_ATTENDEE,
	XML_EVENT_MAILBOX,
	XML_EVENT_EMAIL_ADDRESS,
	XML_EVENT_CATEGORIES,
	XML_EVENT_CATEGORY,
	XML_EVENT_IMPORTANCE,
};

struct ewscal_pvt {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(url);
		AST_STRING_FIELD(user);
		AST_STRING_FIELD(secret);
	);
	struct ast_calendar *owner;
	ne_uri uri;
	ne_session *session;
	struct ao2_container *events;
	unsigned int items;
};

static void ewscal_destructor(void *obj)
{
	struct ewscal_pvt *pvt = obj;

	ast_debug(1, "Destroying pvt for Exchange Web Service calendar %s\n", "pvt->owner->name");
	if (pvt->session) {
		ne_session_destroy(pvt->session);
	}
	ast_string_field_free_memory(pvt);

	ao2_callback(pvt->events, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	ao2_ref(pvt->events, -1);
}

static void *unref_ewscal(void *obj)
{
	struct ewscal_pvt *pvt = obj;

	ast_debug(5, "EWS: unref_ewscal()\n");
	ao2_ref(pvt, -1);
	return NULL;
}

static int auth_credentials(void *userdata, const char *realm, int attempts, char *username, char *secret)
{
	struct ewscal_pvt *pvt = userdata;

	if (attempts > 1) {
		ast_log(LOG_WARNING, "Invalid username or password for Exchange Web Service calendar '%s'\n", pvt->owner->name);
		return -1;
	}

	ne_strnzcpy(username, pvt->user, NE_ABUFSIZ);
	ne_strnzcpy(secret, pvt->secret, NE_ABUFSIZ);

	return 0;
}

static int ssl_verify(void *userdata, int failures, const ne_ssl_certificate *cert)
{
	struct ewscal_pvt *pvt = userdata;
	if (failures & NE_SSL_UNTRUSTED) {
		ast_log(LOG_WARNING, "Untrusted SSL certificate for calendar %s!\n", pvt->owner->name);
		return 0;
	}
	return 1;	/* NE_SSL_NOTYETVALID, NE_SSL_EXPIRED, NE_SSL_IDMISMATCH */
}

static time_t mstime_to_time_t(char *mstime)
{
	struct ast_tm tm;
	struct timeval tv;

	if (ast_strptime(mstime, "%FT%TZ", &tm)) {
		tv = ast_mktime(&tm, "UTC");
		return tv.tv_sec;
	}
	return 0;
}

static int startelm(void *userdata, int parent, const char *nspace, const char *name, const char **atts)
{
	struct xml_context *ctx = userdata;

	ast_debug(5, "EWS: XML: Start: %s\n", name);
	if (ctx->op == XML_OP_CREATE) {
		return NE_XML_DECLINE;
	}

	/* Nodes needed for traversing until CalendarItem is found */
	if (!strcmp(name, "Envelope") ||
		(!strcmp(name, "Body") && parent != XML_EVENT_CALENDAR_ITEM) ||
		!strcmp(name, "FindItemResponse") ||
		!strcmp(name, "GetItemResponse") ||
		!strcmp(name, "CreateItemResponse") ||
		!strcmp(name, "ResponseMessages") ||
		!strcmp(name, "FindItemResponseMessage") || !strcmp(name, "GetItemResponseMessage") ||
		!strcmp(name, "Items")
	) {
		return 1;
	} else if (!strcmp(name, "RootFolder")) {
		/* Get number of events */
		unsigned int items;

		ast_debug(3, "EWS: XML: <RootFolder>\n");
		if (sscanf(ne_xml_get_attr(ctx->parser, atts, NULL, "TotalItemsInView"), "%u", &items) != 1) {
			/* Couldn't read enything */
			ne_xml_set_error(ctx->parser, "Could't read number of events.");
			return NE_XML_ABORT;
		}

		ast_debug(3, "EWS: %u calendar items to load\n", items);
		ctx->pvt->items = items;
		if (items < 1) {
			/* Stop processing XML if there are no events */
			ast_calendar_merge_events(ctx->pvt->owner, ctx->pvt->events);
			return NE_XML_DECLINE;
		}
		return 1;
	} else if (!strcmp(name, "CalendarItem")) {
		/* Event start */
		ast_debug(3, "EWS: XML: <CalendarItem>\n");
		if (!(ctx->pvt && ctx->pvt->owner)) {
			ast_log(LOG_ERROR, "Require a private structure with an owner\n");
			return NE_XML_ABORT;
		}

		ctx->event = ast_calendar_event_alloc(ctx->pvt->owner);
		if (!ctx->event) {
			ast_log(LOG_ERROR, "Could not allocate an event!\n");
			return NE_XML_ABORT;
		}

		ctx->cdata = ast_str_create(64);
		if (!ctx->cdata) {
			ast_log(LOG_ERROR, "Could not allocate CDATA!\n");
			return NE_XML_ABORT;
		}

		return XML_EVENT_CALENDAR_ITEM;
	} else if (!strcmp(name, "ItemId")) {
		/* Event UID */
		if (ctx->op == XML_OP_FIND) {
			struct calendar_id *id;
			if (!(id = ast_calloc(1, sizeof(*id)))) {
				return NE_XML_ABORT;
			}
			if (!(id->id = ast_str_create(256))) {
				ast_free(id);
				return NE_XML_ABORT;
			}
			ast_str_set(&id->id, 0, "%s", ne_xml_get_attr(ctx->parser, atts, NULL, "Id"));
			AST_LIST_INSERT_TAIL(&ctx->ids, id, next);
			ast_debug(3, "EWS_FIND: XML: UID: %s\n", ast_str_buffer(id->id));
		} else {
			ast_debug(3, "EWS_GET: XML: UID: %s\n", ne_xml_get_attr(ctx->parser, atts, NULL, "Id"));
			ast_string_field_set(ctx->event, uid, ne_xml_get_attr(ctx->parser, atts, NULL, "Id"));
		}
		return XML_EVENT_NAME;
	} else if (!strcmp(name, "Subject")) {
		/* Event name */
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_NAME;
	} else if (!strcmp(name, "Body") && parent == XML_EVENT_CALENDAR_ITEM) {
		/* Event body/description */
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_DESCRIPTION;
	} else if (!strcmp(name, "Start")) {
		/* Event start time */
		return XML_EVENT_START;
	} else if (!strcmp(name, "End")) {
		/* Event end time */
		return XML_EVENT_END;
	} else if (!strcmp(name, "LegacyFreeBusyStatus")) {
		/* Event busy state */
		return XML_EVENT_BUSY;
	} else if (!strcmp(name, "Organizer") ||
			(parent == XML_EVENT_ORGANIZER && (!strcmp(name, "Mailbox") ||
			!strcmp(name, "Name")))) {
		/* Event organizer */
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_ORGANIZER;
	} else if (!strcmp(name, "Location")) {
		/* Event location */
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_LOCATION;
	} else if (!strcmp(name, "Categories")) {
		/* Event categories */
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_CATEGORIES;
	} else if (parent == XML_EVENT_CATEGORIES && !strcmp(name, "String")) {
		/* Event category */
		return XML_EVENT_CATEGORY;
	} else if (!strcmp(name, "Importance")) {
		/* Event importance (priority) */
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_IMPORTANCE;
	} else if (!strcmp(name, "RequiredAttendees") || !strcmp(name, "OptionalAttendees")) {
		return XML_EVENT_ATTENDEE_LIST;
	} else if (!strcmp(name, "Attendee") && parent == XML_EVENT_ATTENDEE_LIST) {
		return XML_EVENT_ATTENDEE;
	} else if (!strcmp(name, "Mailbox") && parent == XML_EVENT_ATTENDEE) {
		return XML_EVENT_MAILBOX;
	} else if (!strcmp(name, "EmailAddress") && parent == XML_EVENT_MAILBOX) {
		if (!ctx->cdata) {
			return NE_XML_ABORT;
		}
		ast_str_reset(ctx->cdata);
		return XML_EVENT_EMAIL_ADDRESS;
	}

	return NE_XML_DECLINE;
}

static int cdata(void *userdata, int state, const char *cdata, size_t len)
{
	struct xml_context *ctx = userdata;
	char data[len + 1];

	/* !!! DON'T USE AST_STRING_FIELD FUNCTIONS HERE, JUST COLLECT CTX->CDATA !!! */
	if (state < XML_EVENT_NAME || ctx->op == XML_OP_CREATE) {
		return 0;
	}

	if (!ctx->event) {
		ast_log(LOG_ERROR, "Parsing event data, but event object does not exist!\n");
		return 1;
	}

	if (!ctx->cdata) {
		ast_log(LOG_ERROR, "String for storing CDATA is unitialized!\n");
		return 1;
	}

	ast_copy_string(data, cdata, len + 1);

	switch (state) {
	case XML_EVENT_START:
		ctx->event->start = mstime_to_time_t(data);
		break;
	case XML_EVENT_END:
		ctx->event->end = mstime_to_time_t(data);
		break;
	case XML_EVENT_BUSY:
		if (!strcmp(data, "Busy") || !strcmp(data, "OOF")) {
			ast_debug(3, "EWS: XML: Busy: yes\n");
			ctx->event->busy_state = AST_CALENDAR_BS_BUSY;
		}
		else if (!strcmp(data, "Tentative")) {
			ast_debug(3, "EWS: XML: Busy: tentative\n");
			ctx->event->busy_state = AST_CALENDAR_BS_BUSY_TENTATIVE;
		}
		else {
			ast_debug(3, "EWS: XML: Busy: no\n");
			ctx->event->busy_state = AST_CALENDAR_BS_FREE;
		}
		break;
	case XML_EVENT_CATEGORY:
		if (ast_str_strlen(ctx->cdata) == 0) {
			ast_str_set(&ctx->cdata, 0, "%s", data);
		} else {
			ast_str_append(&ctx->cdata, 0, ",%s", data);
		}
		break;
	default:
		ast_str_append(&ctx->cdata, 0, "%s", data);
	}

	ast_debug(5, "EWS: XML: CDATA: %s\n", ast_str_buffer(ctx->cdata));

	return 0;
}

static int endelm(void *userdata, int state, const char *nspace, const char *name)
{
	struct xml_context *ctx = userdata;

	ast_debug(5, "EWS: XML: End:   %s\n", name);
	if (ctx->op == XML_OP_FIND || ctx->op == XML_OP_CREATE) {
		return NE_XML_DECLINE;
	}

	if (!strcmp(name, "Subject")) {
		/* Event name end*/
		ast_string_field_set(ctx->event, summary, ast_str_buffer(ctx->cdata));
		ast_debug(3, "EWS: XML: Summary: %s\n", ctx->event->summary);
		ast_str_reset(ctx->cdata);
	} else if (!strcmp(name, "Body") && state == XML_EVENT_DESCRIPTION) {
		/* Event body/description end */
		ast_string_field_set(ctx->event, description, ast_str_buffer(ctx->cdata));
		ast_debug(3, "EWS: XML: Description: %s\n", ctx->event->description);
		ast_str_reset(ctx->cdata);
	} else if (!strcmp(name, "Organizer")) {
		/* Event organizer end */
		ast_string_field_set(ctx->event, organizer, ast_str_buffer(ctx->cdata));
		ast_debug(3, "EWS: XML: Organizer: %s\n", ctx->event->organizer);
		ast_str_reset(ctx->cdata);
	} else if (!strcmp(name, "Location")) {
		/* Event location end */
		ast_string_field_set(ctx->event, location, ast_str_buffer(ctx->cdata));
		ast_debug(3, "EWS: XML: Location: %s\n", ctx->event->location);
		ast_str_reset(ctx->cdata);
	} else if (!strcmp(name, "Categories")) {
		/* Event categories end */
		ast_string_field_set(ctx->event, categories, ast_str_buffer(ctx->cdata));
		ast_debug(3, "EWS: XML: Categories: %s\n", ctx->event->categories);
		ast_str_reset(ctx->cdata);
	} else if (!strcmp(name, "Importance")) {
		/* Event importance end */
		if (!strcmp(ast_str_buffer(ctx->cdata), "Low")) {
			ctx->event->priority = 9;
		} else if (!strcmp(ast_str_buffer(ctx->cdata), "Normal")) {
			ctx->event->priority = 5;
		} else if (!strcmp(ast_str_buffer(ctx->cdata), "High")) {
			ctx->event->priority = 1;
		}
		ast_debug(3, "EWS: XML: Importance: %s (%d)\n", ast_str_buffer(ctx->cdata), ctx->event->priority);
		ast_str_reset(ctx->cdata);
	} else if (state == XML_EVENT_EMAIL_ADDRESS) {
		struct ast_calendar_attendee *attendee;

		if (!(attendee = ast_calloc(1, sizeof(*attendee)))) {
			ctx->event = ast_calendar_unref_event(ctx->event);
			return  1;
		}

		if (ast_str_strlen(ctx->cdata)) {
			attendee->data = ast_strdup(ast_str_buffer(ctx->cdata));
			AST_LIST_INSERT_TAIL(&ctx->event->attendees, attendee, next);
		} else {
			ast_free(attendee);
		}
		ast_debug(3, "EWS: XML: attendee address '%s'\n", ast_str_buffer(ctx->cdata));
		ast_str_reset(ctx->cdata);
	} else if (!strcmp(name, "CalendarItem")) {
		/* Event end */
		ast_debug(3, "EWS: XML: </CalendarItem>\n");
		ast_free(ctx->cdata);
		if (ctx->event) {
			ao2_link(ctx->pvt->events, ctx->event);
			ctx->event = ast_calendar_unref_event(ctx->event);
		} else {
			ast_log(LOG_ERROR, "Event data ended in XML, but event object does not exist!\n");
			return 1;
		}
	} else if (!strcmp(name, "Envelope")) {
		/* Events end */
		ast_debug(3, "EWS: XML: %d of %u event(s) has been parsed…\n", ao2_container_count(ctx->pvt->events), ctx->pvt->items);
		if (ao2_container_count(ctx->pvt->events) >= ctx->pvt->items) {
			ast_debug(3, "EWS: XML: All events has been parsed, merging…\n");
			ast_calendar_merge_events(ctx->pvt->owner, ctx->pvt->events);
		}
	}

	return 0;
}

static const char *mstime(time_t t, char *buf, size_t buflen)
{
	struct timeval tv = {
		.tv_sec = t,
	};
	struct ast_tm tm;

	ast_localtime(&tv, &tm, "utc");
	ast_strftime(buf, buflen, "%FT%TZ", &tm);

	return S_OR(buf, "");
}

static const char *msstatus(enum ast_calendar_busy_state state)
{
	switch (state) {
	case AST_CALENDAR_BS_BUSY_TENTATIVE:
		return "Tentative";
	case AST_CALENDAR_BS_BUSY:
		return "Busy";
	case AST_CALENDAR_BS_FREE:
		return "Free";
	default:
		return "";
	}
}

static const char *get_soap_action(enum xml_op op)
{
	switch (op) {
	case XML_OP_FIND:
		return "\"http://schemas.microsoft.com/exchange/services/2006/messages/FindItem\"";
	case XML_OP_GET:
		return "\"http://schemas.microsoft.com/exchange/services/2006/messages/GetItem\"";
	case XML_OP_CREATE:
		return "\"http://schemas.microsoft.com/exchange/services/2006/messages/CreateItem\"";
	}

	return "";
}

static int send_ews_request_and_parse(struct ast_str *request, struct xml_context *ctx)
{
	int ret;
	ne_request *req;
	ne_xml_parser *parser;

	ast_debug(3, "EWS: HTTP request...\n");
	if (!(ctx && ctx->pvt)) {
		ast_log(LOG_ERROR, "There is no private!\n");
		return -1;
	}

	if (!ast_str_strlen(request)) {
		ast_log(LOG_ERROR, "No request to send!\n");
		return -1;
	}

	ast_debug(3, "%s\n", ast_str_buffer(request));

	/* Prepare HTTP POST request */
	req = ne_request_create(ctx->pvt->session, "POST", ctx->pvt->uri.path);
	ne_set_request_flag(req, NE_REQFLAG_IDEMPOTENT, 0);

	/* Set headers--should be application/soap+xml, but MS… :/ */
	ne_add_request_header(req, "Content-Type", "text/xml; charset=utf-8");
	ne_add_request_header(req, "SOAPAction", get_soap_action(ctx->op));

	/* Set body to SOAP request */
	ne_set_request_body_buffer(req, ast_str_buffer(request), ast_str_strlen(request));

	/* Prepare XML parser */
	parser = ne_xml_create();
	ctx->parser = parser;
	ne_xml_push_handler(parser, startelm, cdata, endelm, ctx);	/* Callbacks */

	/* Dispatch request and parse response as XML */
	ret = ne_xml_dispatch_request(req, parser);
	if (ret != NE_OK) { /* Error handling */
		ast_log(LOG_WARNING, "Unable to communicate with Exchange Web Service at '%s': %s\n", ctx->pvt->url, ne_get_error(ctx->pvt->session));
		ne_request_destroy(req);
		ne_xml_destroy(parser);
		return -1;
	}

	/* Cleanup */
	ne_request_destroy(req);
	ne_xml_destroy(parser);

	return 0;
}

static int ewscal_write_event(struct ast_calendar_event *event)
{
	struct ast_str *request;
	struct ewscal_pvt *pvt = event->owner->tech_pvt;
	char start[21], end[21];
	struct xml_context ctx = {
		.op = XML_OP_CREATE,
		.pvt = pvt,
	};
	int ret;
	char *category, *categories;

	if (!pvt) {
		return -1;
	}

	if (!(request = ast_str_create(1024))) {
		return -1;
	}

	ast_str_set(&request, 0,
		"<soap:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
			"xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
			"xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
			"xmlns:t=\"http://schemas.microsoft.com/exchange/services/2006/types\">"
			"<soap:Body>"
			"<CreateItem xmlns=\"http://schemas.microsoft.com/exchange/services/2006/messages\" "
				"xmlns:t=\"http://schemas.microsoft.com/exchange/services/2006/types\" "
				"SendMeetingInvitations=\"SendToNone\" >"
				"<SavedItemFolderId>"
					"<t:DistinguishedFolderId Id=\"calendar\"/>"
				"</SavedItemFolderId>"
				"<Items>"
					"<t:CalendarItem xmlns=\"http://schemas.microsoft.com/exchange/services/2006/types\">"
						"<Subject>%s</Subject>"
						"<Body BodyType=\"Text\">%s</Body>"
						"<ReminderIsSet>false</ReminderIsSet>"
						"<Start>%s</Start>"
						"<End>%s</End>"
						"<IsAllDayEvent>false</IsAllDayEvent>"
						"<LegacyFreeBusyStatus>%s</LegacyFreeBusyStatus>"
						"<Location>%s</Location>",
		event->summary,
		event->description,
		mstime(event->start, start, sizeof(start)),
		mstime(event->end, end, sizeof(end)),
		msstatus(event->busy_state),
		event->location
	);
	/* Event priority */
	switch (event->priority) {
	case 1:
	case 2:
	case 3:
	case 4:
		ast_str_append(&request, 0, "<Importance>High</Importance>");
		break;
	case 5:
		ast_str_append(&request, 0, "<Importance>Normal</Importance>");
		break;
	case 6:
	case 7:
	case 8:
	case 9:
		ast_str_append(&request, 0, "<Importance>Low</Importance>");
		break;
	}
	/* Event categories*/
	if (strlen(event->categories) > 0) {
		ast_str_append(&request, 0, "<Categories>");
		categories = ast_strdupa(event->categories);	/* Duplicate string, since strsep() is destructive */
		category = strsep(&categories, ",");
		while (category != NULL) {
			ast_str_append(&request, 0, "<String>%s</String>", category);
			category = strsep(&categories, ",");
		}
		ast_str_append(&request, 0, "</Categories>");
	}
	/* Finish request */
	ast_str_append(&request, 0, "</t:CalendarItem></Items></CreateItem></soap:Body></soap:Envelope>");

	ret = send_ews_request_and_parse(request, &ctx);

	ast_free(request);

	return ret;
}

static struct calendar_id *get_ewscal_ids_for(struct ewscal_pvt *pvt)
{
	char start[21], end[21];
	struct ast_tm tm;
	struct timeval tv;
	struct ast_str *request;
	struct xml_context ctx = {
		.op = XML_OP_FIND,
		.pvt = pvt,
	};

	ast_debug(5, "EWS: get_ewscal_ids_for()\n");

	if (!pvt) {
		ast_log(LOG_ERROR, "There is no private!\n");
		return NULL;
	}

	/* Prepare timeframe strings */
	tv = ast_tvnow();
	ast_localtime(&tv, &tm, "UTC");
	ast_strftime(start, sizeof(start), "%FT%TZ", &tm);
	tv.tv_sec += 60 * pvt->owner->timeframe;
	ast_localtime(&tv, &tm, "UTC");
	ast_strftime(end, sizeof(end), "%FT%TZ", &tm);

	/* Prepare SOAP request */
	if (!(request = ast_str_create(512))) {
		return NULL;
	}

	ast_str_set(&request, 0,
		"<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"xmlns:ns1=\"http://schemas.microsoft.com/exchange/services/2006/types\" "
		"xmlns:ns2=\"http://schemas.microsoft.com/exchange/services/2006/messages\">"
			"<SOAP-ENV:Body>"
				"<ns2:FindItem Traversal=\"Shallow\">"
					"<ns2:ItemShape>"
						"<ns1:BaseShape>IdOnly</ns1:BaseShape>"
					"</ns2:ItemShape>"
					"<ns2:CalendarView StartDate=\"%s\" EndDate=\"%s\"/>"	/* Timeframe */
					"<ns2:ParentFolderIds>"
						"<ns1:DistinguishedFolderId Id=\"calendar\"/>"
					"</ns2:ParentFolderIds>"
				"</ns2:FindItem>"
			"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		start, end	/* Timeframe */
	);

	AST_LIST_HEAD_INIT_NOLOCK(&ctx.ids);

	/* Dispatch request and parse response as XML */
	if (send_ews_request_and_parse(request, &ctx)) {
		ast_free(request);
		return NULL;
	}

	/* Cleanup */
	ast_free(request);

	return AST_LIST_FIRST(&ctx.ids);
}

static int parse_ewscal_id(struct ewscal_pvt *pvt, const char *id) {
	struct ast_str *request;
	struct xml_context ctx = {
		.pvt = pvt,
		.op = XML_OP_GET,
	};

	if (!(request = ast_str_create(512))) {
		return -1;
	}

	ast_str_set(&request, 0,
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"xmlns:t=\"http://schemas.microsoft.com/exchange/services/2006/types\">"
		"<soap:Body>"
			"<GetItem xmlns=\"http://schemas.microsoft.com/exchange/services/2006/messages\">"
				"<ItemShape>"
					"<t:BaseShape>AllProperties</t:BaseShape>"
				"</ItemShape>"
				"<ItemIds>"
					"<t:ItemId Id=\"%s\"/>"
				"</ItemIds>"
			"</GetItem>"
		"</soap:Body>"
		"</soap:Envelope>", id
	);

	if (send_ews_request_and_parse(request, &ctx)) {
		ast_free(request);
		return -1;
	}

	ast_free(request);

	return 0;
}

static int update_ewscal(struct ewscal_pvt *pvt)
{
	struct calendar_id *id_head;
	struct calendar_id *iter;

	if (!(id_head = get_ewscal_ids_for(pvt))) {
		return 0;
	}

	for (iter = id_head; iter; iter = AST_LIST_NEXT(iter, next)) {
		parse_ewscal_id(pvt, ast_str_buffer(iter->id));
		ast_free(iter->id);
		ast_free(iter);
	}

	return 0;
}

static void *ewscal_load_calendar(void *void_data)
{
	struct ewscal_pvt *pvt;
	const struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_calendar *cal = void_data;
	ast_mutex_t refreshlock;

	ast_debug(5, "EWS: ewscal_load_calendar()\n");

	if (!(cal && (cfg = ast_calendar_config_acquire()))) {
		ast_log(LOG_ERROR, "You must enable calendar support for res_ewscal to load\n");
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

	if (!(pvt = ao2_alloc(sizeof(*pvt), ewscal_destructor))) {
		ast_log(LOG_ERROR, "Could not allocate ewscal_pvt structure for calendar: %s\n", cal->name);
		ast_calendar_config_release();
		return NULL;
	}

	pvt->owner = cal;

	if (!(pvt->events = ast_calendar_event_container_alloc())) {
		ast_log(LOG_ERROR, "Could not allocate space for fetching events for calendar: %s\n", cal->name);
		pvt = unref_ewscal(pvt);
		ao2_unlock(cal);
		ast_calendar_config_release();
		return NULL;
	}

	if (ast_string_field_init(pvt, 32)) {
		ast_log(LOG_ERROR, "Couldn't allocate string field space for calendar: %s\n", cal->name);
		pvt = unref_ewscal(pvt);
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
		ast_log(LOG_WARNING, "No URL was specified for Exchange Web Service calendar '%s' - skipping.\n", cal->name);
		pvt = unref_ewscal(pvt);
		ao2_unlock(cal);
		return NULL;
	}

	if (ne_uri_parse(pvt->url, &pvt->uri) || pvt->uri.host == NULL || pvt->uri.path == NULL) {
		ast_log(LOG_WARNING, "Could not parse url '%s' for Exchange Web Service calendar '%s' - skipping.\n", pvt->url, cal->name);
		pvt = unref_ewscal(pvt);
		ao2_unlock(cal);
		return NULL;
	}

	if (pvt->uri.scheme == NULL) {
		pvt->uri.scheme = "http";
	}

	if (pvt->uri.port == 0) {
		pvt->uri.port = ne_uri_defaultport(pvt->uri.scheme);
	}

	ast_debug(3, "ne_uri.scheme	= %s\n", pvt->uri.scheme);
	ast_debug(3, "ne_uri.host	= %s\n", pvt->uri.host);
	ast_debug(3, "ne_uri.port	= %u\n", pvt->uri.port);
	ast_debug(3, "ne_uri.path	= %s\n", pvt->uri.path);
	ast_debug(3, "user		= %s\n", pvt->user);
	ast_debug(3, "secret		= %s\n", pvt->secret);

	pvt->session = ne_session_create(pvt->uri.scheme, pvt->uri.host, pvt->uri.port);
	ne_redirect_register(pvt->session);
	ne_set_server_auth(pvt->session, auth_credentials, pvt);
	ne_set_useragent(pvt->session, "Asterisk");

	if (!strcasecmp(pvt->uri.scheme, "https")) {
		ne_ssl_trust_default_ca(pvt->session);
		ne_ssl_set_verify(pvt->session, ssl_verify, pvt);
	}

	cal->tech_pvt = pvt;

	ast_mutex_init(&refreshlock);

	/* Load it the first time */
	update_ewscal(pvt);

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

		update_ewscal(pvt);
	}

	return NULL;
}

static int load_module(void)
{
	/* Actualy, 0.29.1 is required (because of NTLM authentication), but this
	 * function does not support matching patch version.
	 *
	 * The ne_version_match function returns non-zero if the library
	 * version is not of major version major, or the minor version
	 * is less than minor. For neon versions 0.x, every minor
	 * version is assumed to be incompatible with every other minor
	 * version.
	 *
	 * I.e. for version 1.2..1.9 we would do ne_version_match(1, 2)
	 * but for version 0.29 and 0.30 we need two checks. */
	if (ne_version_match(0, 29) && ne_version_match(0, 30)) {
		ast_log(LOG_ERROR, "Exchange Web Service calendar module require neon >= 0.29.1, but %s is installed.\n", ne_version_string());
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_calendar_register(&ewscal_tech) && (ne_sock_init() == 0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ne_sock_exit();
	ast_calendar_unregister(&ewscal_tech);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk MS Exchange Web Service Calendar Integration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEVSTATE_PLUGIN,
);
