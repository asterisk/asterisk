/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Michael Kuron
 *
 * Michael Kuron <m.kuron@gmx.de>
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
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/aoc.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

static pj_xml_attr *aoc_xml_create_attr(pj_pool_t *pool, pj_xml_node *node,
	const char *name, const char *value)
{
	pj_xml_attr *attr;

	attr = PJ_POOL_ALLOC_T(pool, pj_xml_attr);

	pj_strdup2(pool, &attr->name, name);
	pj_strdup2(pool, &attr->value, value);

	pj_xml_add_attr(node, attr);
	return attr;
}

static pj_xml_node *aoc_xml_create_node(pj_pool_t *pool, pj_xml_node *parent,
	const char *name)
{
	pj_xml_node *node;

	node = PJ_POOL_ZALLOC_T(pool, pj_xml_node);

	pj_list_init(&node->attr_head);
	pj_list_init(&node->node_head);

	pj_strdup2(pool, &node->name, name);

	if (parent) {
		pj_xml_add_node(parent, node);
	}

	return node;
}

static void aoc_xml_set_node_content(pj_pool_t *pool, pj_xml_node *node,
	const char *content)
{
	pj_strdup2(pool, &node->content, content);
}

static char * aoc_format_amount(pj_pool_t *pool, unsigned int amount,
		enum ast_aoc_currency_multiplier multiplier)
{
	const size_t amount_max_size = 16;
	char *amount_str;

	amount_str = pj_pool_alloc(pool, amount_max_size);

	switch (multiplier) {
	case AST_AOC_MULT_ONETHOUSANDTH:
		pj_ansi_snprintf(amount_str, amount_max_size, "%.3f", amount*0.001f);
		break;
	case AST_AOC_MULT_ONEHUNDREDTH:
		pj_ansi_snprintf(amount_str, amount_max_size, "%.2f", amount*0.01f);
		break;
	case AST_AOC_MULT_ONETENTH:
		pj_ansi_snprintf(amount_str, amount_max_size, "%.1f", amount*0.1f);
		break;
	case AST_AOC_MULT_ONE:
		pj_ansi_snprintf(amount_str, amount_max_size, "%d", amount);
		break;
	case AST_AOC_MULT_TEN:
		pj_ansi_snprintf(amount_str, amount_max_size, "%d", amount*10);
		break;
	case AST_AOC_MULT_HUNDRED:
		pj_ansi_snprintf(amount_str, amount_max_size, "%d", amount*100);
		break;
	case AST_AOC_MULT_THOUSAND:
		pj_ansi_snprintf(amount_str, amount_max_size, "%d", amount*1000);
		break;
	default:
		pj_ansi_snprintf(amount_str, amount_max_size, "%d", amount);
	}

	return amount_str;
}

static const char *aoc_time_scale_str(enum ast_aoc_time_scale value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_TIME_SCALE_HUNDREDTH_SECOND:
		str = "one-hundredth-second";
		break;
	case AST_AOC_TIME_SCALE_TENTH_SECOND:
		str = "one-tenth-second";
		break;
	case AST_AOC_TIME_SCALE_SECOND:
		str = "one-second";
		break;
	case AST_AOC_TIME_SCALE_TEN_SECOND:
		str = "ten-seconds";
		break;
	case AST_AOC_TIME_SCALE_MINUTE:
		str = "one-minute";
		break;
	case AST_AOC_TIME_SCALE_HOUR:
		str = "one-hour";
		break;
	case AST_AOC_TIME_SCALE_DAY:
		str = "twenty-four-hours";
		break;
	}
	return str;
}

static void aoc_datastore_destroy(void *obj)
{
	char *xml = obj;
	ast_free(xml);
}

static const struct ast_datastore_info aoc_s_datastore = {
	.type = "AOC-S",
	.destroy = aoc_datastore_destroy,
};

static const struct ast_datastore_info aoc_d_datastore = {
	.type = "AOC-D",
	.destroy = aoc_datastore_destroy,
};

static const struct ast_datastore_info aoc_e_datastore = {
	.type = "AOC-E",
	.destroy = aoc_datastore_destroy,
};

struct aoc_data {
	struct ast_sip_session *session;
	struct ast_aoc_decoded *decoded;
	enum ast_channel_state channel_state;
};

static void aoc_release_pool(void * data)
{
	pj_pool_t *pool = data;
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
}

static int aoc_send_as_xml(void * data)
{
	RAII_VAR(struct aoc_data *, adata, data, ao2_cleanup);
	RAII_VAR(pj_pool_t *, pool, NULL, aoc_release_pool);

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "AOC", 2048, 512);

	if (!pool) {
		ast_log(LOG_ERROR, "Could not create a memory pool for AOC XML\n");
		return 1;
	}

	if (ast_aoc_get_msg_type(adata->decoded) == AST_AOC_D ||
			ast_aoc_get_msg_type(adata->decoded) == AST_AOC_E) {
		pj_xml_node *aoc;
		pj_xml_node *aoc_type;
		pj_xml_node *charging_info = NULL;
		pj_xml_node *charges;
		pj_xml_node *charge;
		char *xml;
		size_t size;
		const size_t xml_max_size = 512;

		aoc = aoc_xml_create_node(pool, NULL, "aoc");
		aoc_xml_create_attr(pool, aoc, "xmlns",
				"http://uri.etsi.org/ngn/params/xml/simservs/aoc");
		aoc_type = aoc_xml_create_node(pool, aoc,
				ast_aoc_get_msg_type(adata->decoded) == AST_AOC_D ? "aoc-d" : "aoc-e");
		if (ast_aoc_get_msg_type(adata->decoded) == AST_AOC_D) {
			charging_info = aoc_xml_create_node(pool, aoc_type, "charging-info");
			aoc_xml_set_node_content(pool, charging_info,
					ast_aoc_get_total_type(adata->decoded) == AST_AOC_SUBTOTAL ? "subtotal" : "total");
		}
		charges = aoc_xml_create_node(pool, aoc_type, "recorded-charges");

		if (ast_aoc_get_charge_type(adata->decoded) == AST_AOC_CHARGE_FREE) {
			charge = aoc_xml_create_node(pool, charges, "free-charge");
		} else if (ast_aoc_get_charge_type(adata->decoded) == AST_AOC_CHARGE_CURRENCY ||
				ast_aoc_get_charge_type(adata->decoded) == AST_AOC_CHARGE_UNIT) {
			charge = aoc_xml_create_node(pool, charges, "recorded-currency-units");
		} else {
			charge = aoc_xml_create_node(pool, charges, "not-available");
		}

		if (ast_aoc_get_charge_type(adata->decoded) == AST_AOC_CHARGE_CURRENCY) {
			const char *currency;
			pj_xml_node *amount;
			char *amount_str;

			currency = ast_aoc_get_currency_name(adata->decoded);
			if (!ast_strlen_zero(currency)) {
				pj_xml_node *currency_id;

				currency_id = aoc_xml_create_node(pool, charge, "currency-id");
				aoc_xml_set_node_content(pool, currency_id, currency);
			}

			amount = aoc_xml_create_node(pool, charge, "currency-amount");
			amount_str = aoc_format_amount(pool, ast_aoc_get_currency_amount(adata->decoded),
					ast_aoc_get_currency_multiplier(adata->decoded));
			aoc_xml_set_node_content(pool, amount, amount_str);
		} else if (ast_aoc_get_charge_type(adata->decoded) == AST_AOC_CHARGE_UNIT) {
			pj_xml_node *currency_id;
			const struct ast_aoc_unit_entry *unit_entry;

			currency_id = aoc_xml_create_node(pool, charge, "currency-id");
			aoc_xml_set_node_content(pool, currency_id, "UNIT");

			unit_entry = ast_aoc_get_unit_info(adata->decoded, 0);
			if (unit_entry) {
				pj_xml_node *amount;
				char *amount_str;

				amount = aoc_xml_create_node(pool, charge, "currency-amount");
				amount_str = aoc_format_amount(pool, unit_entry->amount,
						AST_AOC_MULT_ONE);
				aoc_xml_set_node_content(pool, amount, amount_str);
			}
		}

		xml = pj_pool_alloc(pool, xml_max_size);
		size = pj_xml_print(aoc, xml, xml_max_size - 1, PJ_TRUE);
		if (size >= xml_max_size) {
			ast_log(LOG_ERROR, "aoc+xml body text too large\n");
			return 1;
		}
		xml[size] = 0;

		if (ast_aoc_get_msg_type(adata->decoded) == AST_AOC_D) {
			RAII_VAR(struct ast_datastore *, datastore,
					ast_sip_session_get_datastore(adata->session, aoc_d_datastore.type),
					ao2_cleanup);
			struct pjsip_tx_data *tdata;
			struct ast_sip_body body = {
				.type = "application",
				.subtype = "vnd.etsi.aoc+xml",
				.body_text = xml
			};

			if (ast_sip_create_request("INFO", adata->session->inv_session->dlg,
					adata->session->endpoint, NULL, NULL, &tdata)) {
				ast_log(LOG_ERROR, "Could not create AOC INFO request\n");
				return 1;
			}
			if (ast_sip_add_body(tdata, &body)) {
				ast_log(LOG_ERROR, "Could not add body to AOC INFO request\n");
				pjsip_tx_data_dec_ref(tdata);
				return 1;
			}
			ast_sip_session_send_request(adata->session, tdata);

			if (!datastore) {
				datastore = ast_sip_session_alloc_datastore(&aoc_d_datastore, aoc_d_datastore.type);
				if (!datastore) {
					ast_log(LOG_ERROR, "Unable to create datastore for AOC-D.\n");
					return 1;
				}
				datastore->data = NULL;
				if (ast_sip_session_add_datastore(adata->session, datastore)) {
					ast_log(LOG_ERROR, "Unable to create datastore for AOC-D.\n");
					return 1;
				}
			} else {
				ast_free(datastore->data);
			}

			aoc_xml_set_node_content(pool, charging_info, "total");
			size = pj_xml_print(aoc, xml, xml_max_size - 1, PJ_TRUE);
			xml[size] = 0;
			datastore->data = ast_strdup(xml);
		} else if (ast_aoc_get_msg_type(adata->decoded) == AST_AOC_E) {
			RAII_VAR(struct ast_datastore *, datastore,
					ast_sip_session_get_datastore(adata->session, aoc_e_datastore.type),
					ao2_cleanup);
			if (!datastore) {
				datastore = ast_sip_session_alloc_datastore(&aoc_e_datastore, aoc_e_datastore.type);
				if (!datastore) {
					ast_log(LOG_ERROR, "Unable to create datastore for AOC-E.\n");
					return 1;
				}
				datastore->data = NULL;
				if (ast_sip_session_add_datastore(adata->session, datastore)) {
					ast_log(LOG_ERROR, "Unable to create datastore for AOC-E.\n");
					return 1;
				}
			} else {
				ast_free(datastore->data);
			}
			datastore->data = ast_strdup(xml);
		}
	} else if (ast_aoc_get_msg_type(adata->decoded) == AST_AOC_S) {
		pj_xml_node *aoc;
		pj_xml_node *aoc_type;
		pj_xml_node *charged_items;
		const struct ast_aoc_s_entry *entry;
		int idx;
		char *xml;
		size_t size;
		const size_t xml_max_size = 1024;

		aoc = aoc_xml_create_node(pool, NULL, "aoc");
		aoc_xml_create_attr(pool, aoc, "xmlns",
				"http://uri.etsi.org/ngn/params/xml/simservs/aoc");
		aoc_type = aoc_xml_create_node(pool, aoc, "aoc-s");
		charged_items = aoc_xml_create_node(pool, aoc_type, "charged-items");

		for (idx = 0; idx < ast_aoc_s_get_count(adata->decoded); idx++) {
			pj_xml_node *charged_item;
			pj_xml_node *charge;

			if (!(entry = ast_aoc_s_get_rate_info(adata->decoded, idx))) {
				break;
			}

			if (entry->charged_item == AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION) {
				charged_item = aoc_xml_create_node(pool, charged_items, "basic");
			} else if (entry->charged_item == AST_AOC_CHARGED_ITEM_CALL_ATTEMPT) {
				charged_item = aoc_xml_create_node(pool, charged_items,
						"communication-attempt");
			} else if (entry->charged_item == AST_AOC_CHARGED_ITEM_CALL_SETUP) {
				charged_item = aoc_xml_create_node(pool, charged_items,
						"communication-setup");
			} else {
				continue;
			}

			if (entry->rate_type == AST_AOC_RATE_TYPE_FREE) {
				charge = aoc_xml_create_node(pool, charged_item, "free-charge");
			} else if (entry->rate_type == AST_AOC_RATE_TYPE_FLAT) {
				charge = aoc_xml_create_node(pool, charged_item, "flat-rate");
			} else if (entry->rate_type == AST_AOC_RATE_TYPE_DURATION &&
					entry->charged_item == AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION) {
				charge = aoc_xml_create_node(pool, charged_item, "price-time");
			} else {
				continue;
			}

			if (entry->rate_type == AST_AOC_RATE_TYPE_DURATION ||
					entry->rate_type == AST_AOC_RATE_TYPE_FLAT) {
				const char *currency;
				pj_xml_node *amount;
				uint32_t amount_val;
				enum ast_aoc_currency_multiplier multiplier_val;
				char *amount_str;

				currency = (entry->rate_type == AST_AOC_RATE_TYPE_DURATION ?
						entry->rate.duration.currency_name :
						entry->rate.flat.currency_name);
				if (!ast_strlen_zero(currency)) {
					pj_xml_node *currency_id;

					currency_id = aoc_xml_create_node(pool, charge, "currency-id");
					aoc_xml_set_node_content(pool, currency_id, currency);
				}

				amount = aoc_xml_create_node(pool, charge, "currency-amount");
				amount_val = (entry->rate_type == AST_AOC_RATE_TYPE_DURATION ?
						entry->rate.duration.amount : entry->rate.flat.amount);
				multiplier_val = (entry->rate_type == AST_AOC_RATE_TYPE_DURATION ?
						entry->rate.duration.multiplier : entry->rate.flat.multiplier);
				amount_str = aoc_format_amount(pool, amount_val, multiplier_val);
				aoc_xml_set_node_content(pool, amount, amount_str);
			}

			if (entry->rate_type == AST_AOC_RATE_TYPE_DURATION) {
				pj_xml_node *length_time_unit;
				pj_xml_node *time_unit;
				char *time_str;
				pj_xml_node *scale;
				pj_xml_node *charging_type;

				length_time_unit = aoc_xml_create_node(pool, charge, "length-time-unit");
				time_unit = aoc_xml_create_node(pool, length_time_unit, "time-unit");
				time_str = aoc_format_amount(pool, entry->rate.duration.time,
						AST_AOC_MULT_ONE);
				aoc_xml_set_node_content(pool, time_unit, time_str);
				scale = aoc_xml_create_node(pool, length_time_unit, "scale");
				aoc_xml_set_node_content(pool, scale,
						aoc_time_scale_str(entry->rate.duration.time_scale));
				charging_type = aoc_xml_create_node(pool, charge, "charging-type");
				aoc_xml_set_node_content(pool, charging_type,
						entry->rate.duration.charging_type ? "step-function" :
						"continuous");
			}
		}

		xml = pj_pool_alloc(pool, xml_max_size);
		size = pj_xml_print(aoc, xml, xml_max_size - 1, PJ_TRUE);
		if (size >= xml_max_size) {
			ast_log(LOG_ERROR, "aoc+xml body text too large\n");
			return 1;
		}
		xml[size] = 0;

		if (adata->channel_state == AST_STATE_UP ||
				adata->session->call_direction == AST_SIP_SESSION_OUTGOING_CALL) {
			struct pjsip_tx_data *tdata;
			struct ast_sip_body body = {
				.type = "application",
				.subtype = "vnd.etsi.aoc+xml",
				.body_text = xml
			};

			if (ast_sip_create_request("INFO", adata->session->inv_session->dlg,
					adata->session->endpoint, NULL, NULL, &tdata)) {
				ast_log(LOG_ERROR, "Could not create AOC INFO request\n");
				return 1;
			}
			if (ast_sip_add_body(tdata, &body)) {
				ast_log(LOG_ERROR, "Could not add body to AOC INFO request\n");
				pjsip_tx_data_dec_ref(tdata);
				return 1;
			}
			ast_sip_session_send_request(adata->session, tdata);
		} else {
			RAII_VAR(struct ast_datastore *, datastore,
					ast_sip_session_get_datastore(adata->session, aoc_s_datastore.type),
					ao2_cleanup);
			if (!datastore) {
				datastore = ast_sip_session_alloc_datastore(&aoc_s_datastore, aoc_s_datastore.type);
				if (!datastore) {
					ast_log(LOG_ERROR, "Unable to create datastore for AOC-S.\n");
					return 1;
				}
				if (ast_sip_session_add_datastore(adata->session, datastore)) {
					ast_log(LOG_ERROR, "Unable to create datastore for AOC-S.\n");
					return 1;
				}
			} else {
				ast_free(datastore->data);
			}
			datastore->data = ast_strdup(xml);
		}
	}

	return 0;
}

static void aoc_data_destroy(void * data)
{
	struct aoc_data *adata = data;

	ast_aoc_destroy_decoded(adata->decoded);
	ao2_cleanup(adata->session);
}

static struct ast_frame *aoc_framehook(struct ast_channel *ast, struct ast_frame *f,
		enum ast_framehook_event event, void *data)
{
	struct ast_sip_channel_pvt *channel;
	struct aoc_data *adata;

	if (!f || f->frametype != AST_FRAME_CONTROL || event != AST_FRAMEHOOK_EVENT_WRITE ||
			f->subclass.integer != AST_CONTROL_AOC) {
		return f;
	}

	adata = ao2_alloc(sizeof(struct aoc_data), aoc_data_destroy);
	if (!adata) {
		ast_log(LOG_ERROR, "Failed to allocate AOC data\n");
		return f;
	}

	adata->decoded = ast_aoc_decode((struct ast_aoc_encoded *) f->data.ptr, f->datalen, ast);
	if (!adata->decoded) {
		ast_log(LOG_ERROR, "Error decoding indicated AOC data\n");
		ao2_ref(adata, -1);
		return f;
	}

	channel = ast_channel_tech_pvt(ast);
	adata->session = ao2_bump(channel->session);
	adata->channel_state = ast_channel_state(ast);

	if (ast_sip_push_task(adata->session->serializer, aoc_send_as_xml, adata)) {
		ast_log(LOG_ERROR, "Unable to send AOC XML for channel %s\n", ast_channel_name(ast));
		ao2_ref(adata, -1);
	}
	return &ast_null_frame;
}

static int aoc_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL) ? 1 : 0;
}

static void aoc_attach_framehook(struct ast_sip_session *session)
{
	int framehook_id;
	static struct ast_framehook_interface hook = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = aoc_framehook,
		.consume_cb = aoc_consume,
	};

	if (!session->channel || !session->endpoint->send_aoc) {
		return;
	}

	ast_channel_lock(session->channel);

	framehook_id = ast_framehook_attach(session->channel, &hook);
	if (framehook_id < 0) {
		ast_log(LOG_WARNING, "Could not attach AOC Frame hook, AOC will be unavailable on '%s'\n",
			ast_channel_name(session->channel));
	}

	ast_channel_unlock(session->channel);
}

static int aoc_incoming_invite_request(struct ast_sip_session *session,
		struct pjsip_rx_data *rdata)
{
	aoc_attach_framehook(session);
	return 0;
}

static void aoc_outgoing_invite_request(struct ast_sip_session *session,
		struct pjsip_tx_data *tdata)
{
	aoc_attach_framehook(session);
}

static void aoc_bye_outgoing_response(struct ast_sip_session *session,
		struct pjsip_tx_data *tdata)
{
	struct ast_sip_body body = {
		.type = "application",
		.subtype = "vnd.etsi.aoc+xml",
	};
	RAII_VAR(struct ast_datastore *, datastore_d, ast_sip_session_get_datastore(session,
			aoc_d_datastore.type), ao2_cleanup);
	RAII_VAR(struct ast_datastore *, datastore_e, ast_sip_session_get_datastore(session,
			aoc_e_datastore.type), ao2_cleanup);

	if (datastore_e) {
		body.body_text = datastore_e->data;
	} else if (datastore_d) {
		body.body_text = datastore_d->data;
	}
	else {
		return;
	}

	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_ERROR, "Could not add body to AOC INFO request\n");
	}
}

static void aoc_bye_outgoing_request(struct ast_sip_session *session,
		struct pjsip_tx_data *tdata)
{
	struct ast_sip_body body = {
		.type = "application",
		.subtype = "vnd.etsi.aoc+xml",
	};
	RAII_VAR(struct ast_datastore *, datastore_d, ast_sip_session_get_datastore(session,
			aoc_d_datastore.type), ao2_cleanup);
	RAII_VAR(struct ast_datastore *, datastore_e, ast_sip_session_get_datastore(session,
			aoc_e_datastore.type), ao2_cleanup);

	if (datastore_e) {
		body.body_text = datastore_e->data;
	} else if (datastore_d) {
		body.body_text = datastore_d->data;
	}
	else {
		return;
	}

	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_ERROR, "Could not add body to AOC INFO request\n");
	}
}

static void aoc_invite_outgoing_response(struct ast_sip_session *session,
		struct pjsip_tx_data *tdata)
{
	pjsip_msg_body *multipart_body;
	pjsip_multipart_part *part;
	pj_str_t body_text;
	pj_str_t type;
	pj_str_t subtype;
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session,
			aoc_s_datastore.type), ao2_cleanup);

	if (tdata->msg->line.status.code != 180 && tdata->msg->line.status.code != 183 &&
			tdata->msg->line.status.code != 200) {
		return;
	}

	if (!datastore) {
		return;
	}

	if (tdata->msg->body && pjsip_media_type_cmp(&tdata->msg->body->content_type,
			&pjsip_media_type_multipart_mixed, 0) == 0) {
		multipart_body = tdata->msg->body;
	} else {
		pjsip_sdp_info *tdata_sdp_info;

		tdata_sdp_info = pjsip_tdata_get_sdp_info(tdata);
		if (tdata_sdp_info->sdp) {
			pj_status_t rc;

			rc = pjsip_create_multipart_sdp_body(tdata->pool, tdata_sdp_info->sdp,
					&multipart_body);
			if (rc != PJ_SUCCESS) {
				ast_log(LOG_ERROR, "Unable to create sdp multipart body\n");
				return;
			}
		} else {
			multipart_body = pjsip_multipart_create(tdata->pool,
					&pjsip_media_type_multipart_mixed, NULL);
		}
	}

	part = pjsip_multipart_create_part(tdata->pool);
	pj_strdup2(tdata->pool, &body_text, datastore->data);
	pj_cstr(&type, "application");
	pj_cstr(&subtype, "vnd.etsi.aoc+xml");
	part->body = pjsip_msg_body_create(tdata->pool, &type, &subtype, &body_text);
	pjsip_multipart_add_part(tdata->pool, multipart_body, part);

	tdata->msg->body = multipart_body;
}

static struct ast_sip_session_supplement aoc_bye_supplement = {
	.method = "BYE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_LAST,
	.outgoing_request = aoc_bye_outgoing_request,
	.outgoing_response = aoc_bye_outgoing_response,
};

static struct ast_sip_session_supplement aoc_invite_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_LAST,
	.incoming_request = aoc_incoming_invite_request,
	.outgoing_request = aoc_outgoing_invite_request,
	.outgoing_response = aoc_invite_outgoing_response,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&aoc_bye_supplement);
	ast_sip_session_register_supplement(&aoc_invite_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&aoc_bye_supplement);
	ast_sip_session_unregister_supplement(&aoc_invite_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP AOC Support",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_pjsip",
);
