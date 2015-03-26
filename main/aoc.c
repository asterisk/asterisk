/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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

/*!
 * \file
 * \brief generic AOC payload generation encoding and decoding
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/aoc.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/_private.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"

/* Encoded Payload Flags */
#define AST_AOC_ENCODED_TYPE_REQUEST    (0 << 0)
#define AST_AOC_ENCODED_TYPE_D          (1 << 0)
#define AST_AOC_ENCODED_TYPE_E          (2 << 0)
#define AST_AOC_ENCODED_TYPE_S          (3 << 0)

#define AST_AOC_ENCODED_REQUEST_S       (1 << 2)
#define AST_AOC_ENCODED_REQUEST_D       (1 << 3)
#define AST_AOC_ENCODED_REQUEST_E       (1 << 4)

#define AST_AOC_ENCODED_CHARGE_NA       (0 << 5)
#define AST_AOC_ENCODED_CHARGE_FREE     (1 << 5)
#define AST_AOC_ENCODED_CHARGE_CURRENCY (2 << 5)
#define AST_AOC_ENCODED_CHARGE_UNIT     (3 << 5)

#define AST_AOC_ENCODED_CHARGE_SUBTOTAL (1 << 7)
#define AST_AOC_ENCODED_CHARGE_TOTAL    (0 << 7)

#define AST_AOC_ENCODE_VERSION 1


static char aoc_debug_enabled = 0;
static void aoc_display_decoded_debug(const struct ast_aoc_decoded *decoded, int decoding, struct ast_channel *chan);
static int aoc_s_add_entry(struct ast_aoc_decoded *decoded, struct ast_aoc_s_entry *entry);

/* AOC Payload Header. Holds all the encoded AOC data to pass on the wire */
struct ast_aoc_encoded {
	uint8_t  version;
	uint8_t  flags;
	uint16_t datalen;
	unsigned char data[0];
};

/* Decoded AOC data */
struct ast_aoc_decoded {
	enum ast_aoc_type msg_type;
	enum ast_aoc_charge_type charge_type;
	enum ast_aoc_request request_flag;
	enum ast_aoc_total_type total_type;

	/* currency information */
	enum ast_aoc_currency_multiplier multiplier;
	unsigned int currency_amount;
	char currency_name[AOC_CURRENCY_NAME_SIZE];

	/* unit information */
	int unit_count;
	struct ast_aoc_unit_entry unit_list[32];

	/* Billing Id */
	enum ast_aoc_billing_id billing_id;

	/* Charging Association information */
	struct ast_aoc_charging_association charging_association;

	/* AOC-S charge information */
	int aoc_s_count;
	struct ast_aoc_s_entry aoc_s_entries[10];

	/* Is this an AOC Termination Request */
	char termination_request;
};

/*! \brief AOC Payload Information Elements */
enum AOC_IE {
	AOC_IE_CURRENCY = 1,
	AOC_IE_UNIT = 2,
	AOC_IE_BILLING = 3,
	AOC_IE_CHARGING_ASSOCIATION = 4,
	AOC_IE_RATE = 5,
	AOC_IE_TERMINATION_REQUEST = 6,
};

/*! \brief AOC IE payload header */
struct aoc_pl_ie_hdr {
	uint8_t ie_id;
	uint8_t datalen;
	char data[0];
} __attribute__((packed));

struct aoc_ie_currency {
	uint32_t amount;
	uint8_t  multiplier;
	char name[AOC_CURRENCY_NAME_SIZE];
} __attribute__((packed));

struct aoc_ie_unit {
	uint32_t amount;
	uint8_t valid_type;
	uint8_t valid_amount;
	uint8_t type;
} __attribute__((packed));

struct aoc_ie_billing {
	uint8_t id;
} __attribute__((packed));

struct aoc_ie_charging_association {
	struct ast_aoc_charging_association ca;
} __attribute__((packed));

struct aoc_ie_charging_rate {
	struct ast_aoc_s_entry entry;
} __attribute__((packed));

struct ast_aoc_decoded *ast_aoc_create(const enum ast_aoc_type msg_type,
		const enum ast_aoc_charge_type charge_type,
		const enum ast_aoc_request requests)
{
	struct ast_aoc_decoded *decoded = NULL;

	/* verify input */
	if (((unsigned int) charge_type > AST_AOC_CHARGE_UNIT) ||
		((unsigned int) msg_type > AST_AOC_E) ||
		((msg_type == AST_AOC_REQUEST) && !requests)) {

		ast_log(LOG_WARNING, "Failed to create ast_aoc_decoded object, invalid input\n");
		return NULL;
	}

	if (!(decoded = ast_calloc(1, sizeof(struct ast_aoc_decoded)))) {
		ast_log(LOG_WARNING, "Failed to create ast_aoc_decoded object \n");
		return NULL;
	}

	decoded->msg_type = msg_type;

	if (msg_type == AST_AOC_REQUEST) {
		decoded->request_flag = requests;
	} else if ((msg_type == AST_AOC_D) || (msg_type == AST_AOC_E)) {
		decoded->charge_type = charge_type;
	}

	return decoded;
}

void *ast_aoc_destroy_decoded(struct ast_aoc_decoded *decoded)
{
	ast_free(decoded);
	return NULL;
}

void *ast_aoc_destroy_encoded(struct ast_aoc_encoded *encoded)
{
	ast_free(encoded);
	return NULL;
}

static void aoc_parse_ie_charging_rate(struct ast_aoc_decoded *decoded, const struct aoc_ie_charging_rate *ie)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = ntohs(ie->entry.charged_item);
	entry.rate_type = ntohs(ie->entry.rate_type);

	switch (entry.rate_type) {
	case AST_AOC_RATE_TYPE_DURATION:
		entry.rate.duration.multiplier = ntohs(ie->entry.rate.duration.multiplier);
		entry.rate.duration.amount = ntohl(ie->entry.rate.duration.amount);
		entry.rate.duration.time = ntohl(ie->entry.rate.duration.time);
		entry.rate.duration.time_scale = ntohs(ie->entry.rate.duration.time_scale);
		entry.rate.duration.granularity_time = ntohl(ie->entry.rate.duration.granularity_time);
		entry.rate.duration.granularity_time_scale = ntohs(ie->entry.rate.duration.granularity_time_scale);
		entry.rate.duration.charging_type = ie->entry.rate.duration.charging_type; /* only one byte */

		if (!ast_strlen_zero(ie->entry.rate.duration.currency_name)) {
			ast_copy_string(entry.rate.duration.currency_name,
				ie->entry.rate.duration.currency_name,
				sizeof(entry.rate.duration.currency_name));
		}
		break;
	case AST_AOC_RATE_TYPE_FLAT:
		entry.rate.flat.multiplier = ntohs(ie->entry.rate.flat.multiplier);
		entry.rate.flat.amount = ntohl(ie->entry.rate.flat.amount);
		if (!ast_strlen_zero(ie->entry.rate.flat.currency_name)) {
			ast_copy_string(entry.rate.flat.currency_name,
				ie->entry.rate.flat.currency_name,
				sizeof(entry.rate.flat.currency_name));
		}
		break;
	case AST_AOC_RATE_TYPE_VOLUME:
		entry.rate.volume.multiplier = ntohs(ie->entry.rate.volume.multiplier);
		entry.rate.volume.amount = ntohl(ie->entry.rate.volume.amount);
		entry.rate.volume.volume_unit = ntohs(ie->entry.rate.volume.volume_unit);
		if (!ast_strlen_zero(ie->entry.rate.volume.currency_name)) {
			ast_copy_string(entry.rate.volume.currency_name,
				ie->entry.rate.volume.currency_name,
				sizeof(entry.rate.volume.currency_name));
		}
		break;
	case AST_AOC_RATE_TYPE_SPECIAL_CODE:
		entry.rate.special_code = ntohs(ie->entry.rate.special_code);
		break;
	}

	aoc_s_add_entry(decoded, &entry);
}

static int aoc_parse_ie(struct ast_aoc_decoded *decoded, unsigned char *data, unsigned int datalen)
{
	enum AOC_IE ie_id;
	unsigned int len;

	while (datalen >= 2) {
		ie_id = data[0];
		len = data[1];
		if (len > datalen -2) {
			ast_log(LOG_ERROR, "AOC information element length exceeds the total message size\n");
			return -1;
		}

		switch(ie_id) {
		case AOC_IE_CURRENCY:
			if (len == sizeof(struct aoc_ie_currency)) {
				struct aoc_ie_currency ie;
				memcpy(&ie, data + 2, len);
				decoded->currency_amount = ntohl(ie.amount);
				decoded->multiplier = ie.multiplier; /* only one byte */
				memcpy(decoded->currency_name, ie.name, sizeof(decoded->currency_name));
			} else {
				ast_log(LOG_WARNING, "Received invalid currency ie\n");
			}
			break;
		case AOC_IE_UNIT:
			if (len == sizeof(struct aoc_ie_unit)) {
				struct aoc_ie_unit ie;
				memcpy(&ie, data + 2, len);
				ast_aoc_add_unit_entry(decoded, ie.valid_amount, ntohl(ie.amount), ie.valid_type, ie.type);
			} else {
				ast_log(LOG_WARNING, "Received invalid unit ie\n");
			}
			break;
		case AOC_IE_BILLING:
			if (len == sizeof(struct aoc_ie_billing)) {
				struct aoc_ie_billing ie;
				memcpy(&ie, data + 2, len);
				decoded->billing_id = ie.id; /* only one byte */
			} else {
				ast_log(LOG_WARNING, "Received invalid billing ie\n");
			}
			break;
		case AOC_IE_CHARGING_ASSOCIATION:
			if (len == sizeof(struct aoc_ie_charging_association)) {
				memcpy(&decoded->charging_association, data + 2, sizeof(decoded->charging_association));
				/* everything in the charging_association struct is a single byte except for the id */
				if (decoded->charging_association.charging_type == AST_AOC_CHARGING_ASSOCIATION_ID) {
					decoded->charging_association.charge.id = ntohl(decoded->charging_association.charge.id);
				}
			} else {
				ast_log(LOG_WARNING, "Received invalid charging association ie\n");
			}
			break;
		case AOC_IE_RATE:
			if (len == sizeof(struct aoc_ie_charging_rate)) {
				struct aoc_ie_charging_rate ie;
				memcpy(&ie, data + 2, len);
				aoc_parse_ie_charging_rate(decoded, &ie);
			} else {
				ast_log(LOG_WARNING, "Received invalid charging rate ie\n");
			}
			break;
		case AOC_IE_TERMINATION_REQUEST:
			if (len == 0) {
				decoded->termination_request = 1;
			} else {
				ast_log(LOG_WARNING, "Received invalid termination request ie\n");
			}
			break;
		default:
			ast_log(LOG_WARNING, "Unknown AOC Information Element, ignoring.\n");
		}

		datalen -= (len + 2);
		data += (len + 2);
	}
	return 0;
}

struct ast_aoc_decoded *ast_aoc_decode(struct ast_aoc_encoded *encoded, size_t size, struct ast_channel *chan)
{
	struct ast_aoc_decoded *decoded;

	/* verify our encoded payload is actually large enough to hold all the ies */
	if ((size - (sizeof(struct ast_aoc_encoded)) != ntohs(encoded->datalen))) {
		ast_log(LOG_WARNING, "Corrupted aoc encoded object, can not decode\n");
		return NULL;
	}

	if (!(decoded = ast_calloc(1, sizeof(struct ast_aoc_decoded)))) {
		ast_log(LOG_WARNING, "Failed to create ast_aoc_decoded object \n");
		return NULL;
	}

	/* decode flags */

	if ((encoded->flags & AST_AOC_ENCODED_TYPE_S) == AST_AOC_ENCODED_TYPE_S) {
		decoded->msg_type = AST_AOC_S;
	} else if (encoded->flags & AST_AOC_ENCODED_TYPE_E) {
		decoded->msg_type = AST_AOC_E;
	} else if (encoded->flags & AST_AOC_ENCODED_TYPE_D) {
		decoded->msg_type = AST_AOC_D;
	} else {
		decoded->msg_type = AST_AOC_REQUEST;
	}

	if (decoded->msg_type == AST_AOC_REQUEST) {
		if (encoded->flags & AST_AOC_ENCODED_REQUEST_S) {
			decoded->request_flag |= AST_AOC_REQUEST_S;
		}
		if (encoded->flags & AST_AOC_ENCODED_REQUEST_D) {
			decoded->request_flag |= AST_AOC_REQUEST_D;
		}
		if (encoded->flags & AST_AOC_ENCODED_REQUEST_E) {
			decoded->request_flag |= AST_AOC_REQUEST_E;
		}
	} else if ((decoded->msg_type == AST_AOC_D) || (decoded->msg_type == AST_AOC_E)) {
		if ((encoded->flags & AST_AOC_ENCODED_CHARGE_UNIT) == AST_AOC_ENCODED_CHARGE_UNIT) {
			decoded->charge_type = AST_AOC_CHARGE_UNIT;
		} else if ((encoded->flags & AST_AOC_ENCODED_CHARGE_CURRENCY) == AST_AOC_ENCODED_CHARGE_CURRENCY) {
			decoded->charge_type = AST_AOC_CHARGE_CURRENCY;
		} else if ((encoded->flags & AST_AOC_ENCODED_CHARGE_FREE) == AST_AOC_ENCODED_CHARGE_FREE) {
			decoded->charge_type = AST_AOC_CHARGE_FREE;
		} else {
			decoded->charge_type = AST_AOC_CHARGE_NA;
		}

		if (encoded->flags & AST_AOC_ENCODED_CHARGE_SUBTOTAL) {
			decoded->total_type = AST_AOC_SUBTOTAL;
		}
	}

	/* decode information elements */
	aoc_parse_ie(decoded, encoded->data, ntohs(encoded->datalen));

	if (aoc_debug_enabled) {
		aoc_display_decoded_debug(decoded, 1, chan);
	}

	return decoded;
}

struct aoc_ie_data {
	unsigned char buf[1024];
	int pos;
};

/*!
 * \internal
 * \brief append an AOC information element
 * \note data is expected to already be in network byte order at this point
 */
static int aoc_append_ie(struct aoc_ie_data *ied, unsigned short ie_id, const void *data, unsigned short datalen)
{
	if (datalen > ((int)sizeof(ied->buf) - ied->pos)) {
		ast_log(LOG_WARNING, "Failure to append AOC information element, out of space \n");
		return -1;
	}
	ied->buf[ied->pos++] = ie_id;
	ied->buf[ied->pos++] = datalen;
	if (datalen) {
		memcpy(ied->buf + ied->pos, data, datalen);
		ied->pos += datalen;
	}
	return 0;
}

static void aoc_create_ie_data_charging_rate(const struct ast_aoc_s_entry *entry, struct aoc_ie_charging_rate *ie)
{
	ie->entry.charged_item = htons(entry->charged_item);
	ie->entry.rate_type = htons(entry->rate_type);

	switch (entry->rate_type) {
	case AST_AOC_RATE_TYPE_DURATION:
		ie->entry.rate.duration.multiplier = htons(entry->rate.duration.multiplier);
		ie->entry.rate.duration.amount = htonl(entry->rate.duration.amount);
		ie->entry.rate.duration.time = htonl(entry->rate.duration.time);
		ie->entry.rate.duration.time_scale = htons(entry->rate.duration.time_scale);
		ie->entry.rate.duration.granularity_time = htonl(entry->rate.duration.granularity_time);
		ie->entry.rate.duration.granularity_time_scale = htons(entry->rate.duration.granularity_time_scale);
		ie->entry.rate.duration.charging_type = entry->rate.duration.charging_type; /* only one byte */

		if (!ast_strlen_zero(entry->rate.duration.currency_name)) {
			ast_copy_string(ie->entry.rate.duration.currency_name,
				entry->rate.duration.currency_name,
				sizeof(ie->entry.rate.duration.currency_name));
		}
		break;
	case AST_AOC_RATE_TYPE_FLAT:
		ie->entry.rate.flat.multiplier = htons(entry->rate.flat.multiplier);
		ie->entry.rate.flat.amount = htonl(entry->rate.flat.amount);
		if (!ast_strlen_zero(entry->rate.flat.currency_name)) {
			ast_copy_string(ie->entry.rate.flat.currency_name,
				entry->rate.flat.currency_name,
				sizeof(ie->entry.rate.flat.currency_name));
		}
		break;
	case AST_AOC_RATE_TYPE_VOLUME:
		ie->entry.rate.volume.multiplier = htons(entry->rate.volume.multiplier);
		ie->entry.rate.volume.amount = htonl(entry->rate.volume.amount);
		ie->entry.rate.volume.volume_unit = htons(entry->rate.volume.volume_unit);
		if (!ast_strlen_zero(entry->rate.volume.currency_name)) {
			ast_copy_string(ie->entry.rate.volume.currency_name,
				entry->rate.volume.currency_name,
				sizeof(ie->entry.rate.volume.currency_name));
		}
		break;
	case AST_AOC_RATE_TYPE_SPECIAL_CODE:
		ie->entry.rate.special_code = htons(entry->rate.special_code);
		break;
	}

}
static void aoc_create_ie_data(struct ast_aoc_decoded *decoded, struct aoc_ie_data *ied)
{
	ied->pos = 0;

	if (decoded->currency_amount) {
		struct aoc_ie_currency ie = {
			.amount = htonl(decoded->currency_amount),
			.multiplier = decoded->multiplier, /* only one byte */
			.name = { 0, },
		};

		if (!ast_strlen_zero(decoded->currency_name)) {
			ast_copy_string(ie.name, decoded->currency_name, sizeof(ie.name));
		}

		aoc_append_ie(ied, AOC_IE_CURRENCY, (const void *) &ie, sizeof(ie));
	}

	if (decoded->unit_count) {
		struct aoc_ie_unit ie = { 0 };
		int i;

		for (i = 0; i < decoded->unit_count; i++) {
			ie.valid_amount = decoded->unit_list[i].valid_amount; /* only one byte */
			ie.amount = htonl(decoded->unit_list[i].amount);
			ie.valid_type = decoded->unit_list[i].valid_type; /* only one byte */
			ie.type = decoded->unit_list[i].type; /* only one byte */
			aoc_append_ie(ied, AOC_IE_UNIT, (const void *) &ie, sizeof(ie));
		}
	}

	if (decoded->billing_id) {
		struct aoc_ie_billing ie;
		ie.id = decoded->billing_id; /* only one byte */
		aoc_append_ie(ied, AOC_IE_BILLING, (const void *) &ie, sizeof(ie));
	}

	if (decoded->charging_association.charging_type != AST_AOC_CHARGING_ASSOCIATION_NA) {
		struct aoc_ie_charging_association ie;
		memset(&ie, 0, sizeof(ie));
		ie.ca.charging_type = decoded->charging_association.charging_type;   /* only one byte */
		if (decoded->charging_association.charging_type == AST_AOC_CHARGING_ASSOCIATION_NUMBER) {
			ie.ca.charge.number.plan = decoded->charging_association.charge.number.plan; /* only one byte */
			ast_copy_string(ie.ca.charge.number.number,
				decoded->charging_association.charge.number.number,
				sizeof(ie.ca.charge.number.number));
		} else if (decoded->charging_association.charging_type == AST_AOC_CHARGING_ASSOCIATION_ID) {
			ie.ca.charge.id = htonl(decoded->charging_association.charge.id);
		}
		aoc_append_ie(ied, AOC_IE_CHARGING_ASSOCIATION, (const void *) &ie, sizeof(ie));
	}

	if (decoded->aoc_s_count) {
		struct aoc_ie_charging_rate ie;
		int i;
		for (i = 0; i < decoded->aoc_s_count; i++) {
			memset(&ie, 0, sizeof(ie));
			aoc_create_ie_data_charging_rate(&decoded->aoc_s_entries[i], &ie);
			aoc_append_ie(ied, AOC_IE_RATE, (const void *) &ie, sizeof(ie));
		}
	}

	if (decoded->termination_request) {
		aoc_append_ie(ied, AOC_IE_TERMINATION_REQUEST, NULL, 0);
	}
}

struct ast_aoc_encoded *ast_aoc_encode(struct ast_aoc_decoded *decoded, size_t *out_size, struct ast_channel *chan)
{
	struct aoc_ie_data ied;
	struct ast_aoc_encoded *encoded = NULL;
	size_t size = 0;

	if (!decoded || !out_size) {
		return NULL;
	}

	*out_size = 0;

	/* create information element buffer before allocating the payload,
	 * by doing this the exact size of the payload + the id data can be
	 * allocated all at once. */
	aoc_create_ie_data(decoded, &ied);

	size = sizeof(struct ast_aoc_encoded) + ied.pos;

	if (!(encoded = ast_calloc(1, size))) {
		ast_log(LOG_WARNING, "Failed to create ast_aoc_encoded object during decode routine. \n");
		return NULL;
	}

	/* -- Set ie data buffer */
	if (ied.pos) {
		/* this is safe because encoded was allocated to fit this perfectly */
		memcpy(encoded->data, ied.buf, ied.pos);
		encoded->datalen = htons(ied.pos);
	}

	/* --- Set Flags --- */
	switch (decoded->msg_type) {
	case AST_AOC_S:
		encoded->flags = AST_AOC_ENCODED_TYPE_S;
		break;
	case AST_AOC_D:
		encoded->flags = AST_AOC_ENCODED_TYPE_D;
		break;
	case AST_AOC_E:
		encoded->flags = AST_AOC_ENCODED_TYPE_E;
		break;
	case AST_AOC_REQUEST:
		encoded->flags = AST_AOC_ENCODED_TYPE_REQUEST;
	default:
		break;
	}

	/* if it is type request, set the types requested, else set charge type */
	if (decoded->msg_type == AST_AOC_REQUEST) {
		if (decoded->request_flag & AST_AOC_REQUEST_S) {
			encoded->flags |= AST_AOC_ENCODED_REQUEST_S;
		}
		if (decoded->request_flag & AST_AOC_REQUEST_D) {
			encoded->flags |= AST_AOC_ENCODED_REQUEST_D;
		}
		if (decoded->request_flag & AST_AOC_REQUEST_E) {
			encoded->flags |= AST_AOC_ENCODED_REQUEST_E;
		}
	} else if ((decoded->msg_type == AST_AOC_D) || (decoded->msg_type == AST_AOC_E)) {
		switch (decoded->charge_type) {
		case AST_AOC_CHARGE_UNIT:
			encoded->flags |= AST_AOC_ENCODED_CHARGE_UNIT;
			break;
		case AST_AOC_CHARGE_CURRENCY:
			encoded->flags |= AST_AOC_ENCODED_CHARGE_CURRENCY;
			break;
		case AST_AOC_CHARGE_FREE:
			encoded->flags |= AST_AOC_ENCODED_CHARGE_FREE;
		case AST_AOC_CHARGE_NA:
		default:
			encoded->flags |= AST_AOC_ENCODED_CHARGE_NA;
			break;
		}

		if (decoded->total_type == AST_AOC_SUBTOTAL) {
			encoded->flags |= AST_AOC_ENCODED_CHARGE_SUBTOTAL;
		}
	}

	/* --- Set Version Number --- */
	encoded->version = AST_AOC_ENCODE_VERSION;

	/* set the output size  */
	*out_size = size;

	if (aoc_debug_enabled) {
		aoc_display_decoded_debug(decoded, 0, chan);
	}

	return encoded;
}

static int aoc_s_add_entry(struct ast_aoc_decoded *decoded, struct ast_aoc_s_entry *entry)
{
	if (decoded->aoc_s_count >= ARRAY_LEN(decoded->aoc_s_entries)) {
		return -1;
	}

	decoded->aoc_s_entries[decoded->aoc_s_count] = *entry;
	decoded->aoc_s_count++;

	return 0;
}


unsigned int ast_aoc_s_get_count(struct ast_aoc_decoded *decoded)
{
	return decoded->aoc_s_count;
}

const struct ast_aoc_s_entry *ast_aoc_s_get_rate_info(struct ast_aoc_decoded *decoded, unsigned int entry_number)
{
	if (entry_number >= decoded->aoc_s_count) {
		return NULL;
	}

	return (const struct ast_aoc_s_entry *) &decoded->aoc_s_entries[entry_number];
}

int ast_aoc_s_add_rate_duration(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	unsigned int amount,
	enum ast_aoc_currency_multiplier multiplier,
	const char *currency_name,
	unsigned long time,
	enum ast_aoc_time_scale time_scale,
	unsigned long granularity_time,
	enum ast_aoc_time_scale granularity_time_scale,
	int step_function)
{

	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = charged_item;
	entry.rate_type = AST_AOC_RATE_TYPE_DURATION;
	entry.rate.duration.amount = amount;
	entry.rate.duration.multiplier = multiplier;
	entry.rate.duration.time = time;
	entry.rate.duration.time_scale = time_scale;
	entry.rate.duration.granularity_time = granularity_time;
	entry.rate.duration.granularity_time_scale = granularity_time_scale;
	entry.rate.duration.charging_type = step_function ? 1 : 0;

	if (!ast_strlen_zero(currency_name)) {
		ast_copy_string(entry.rate.duration.currency_name, currency_name, sizeof(entry.rate.duration.currency_name));
	}

	return aoc_s_add_entry(decoded, &entry);
}

int ast_aoc_s_add_rate_flat(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	unsigned int amount,
	enum ast_aoc_currency_multiplier multiplier,
	const char *currency_name)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = charged_item;
	entry.rate_type = AST_AOC_RATE_TYPE_FLAT;
	entry.rate.flat.amount = amount;
	entry.rate.flat.multiplier = multiplier;

	if (!ast_strlen_zero(currency_name)) {
		ast_copy_string(entry.rate.flat.currency_name, currency_name, sizeof(entry.rate.flat.currency_name));
	}

	return aoc_s_add_entry(decoded, &entry);
}


int ast_aoc_s_add_rate_volume(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	enum ast_aoc_volume_unit volume_unit,
	unsigned int amount,
	enum ast_aoc_currency_multiplier multiplier,
	const char *currency_name)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = charged_item;
	entry.rate_type = AST_AOC_RATE_TYPE_VOLUME;
	entry.rate.volume.multiplier = multiplier;
	entry.rate.volume.amount = amount;
	entry.rate.volume.volume_unit = volume_unit;

	if (!ast_strlen_zero(currency_name)) {
		ast_copy_string(entry.rate.volume.currency_name, currency_name, sizeof(entry.rate.volume.currency_name));
	}

	return aoc_s_add_entry(decoded, &entry);
}

int ast_aoc_s_add_rate_special_charge_code(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	unsigned int code)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = charged_item;
	entry.rate_type = AST_AOC_RATE_TYPE_SPECIAL_CODE;
	entry.rate.special_code = code;

	return aoc_s_add_entry(decoded, &entry);
}

int ast_aoc_s_add_rate_free(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	int from_beginning)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = charged_item;
	entry.rate_type = from_beginning ? AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING : AST_AOC_RATE_TYPE_FREE;

	return aoc_s_add_entry(decoded, &entry);
}

int ast_aoc_s_add_rate_na(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = charged_item;
	entry.rate_type = AST_AOC_RATE_TYPE_NA;

	return aoc_s_add_entry(decoded, &entry);
}

int ast_aoc_s_add_special_arrangement(struct ast_aoc_decoded *decoded,
	unsigned int code)
{
	struct ast_aoc_s_entry entry = { 0, };

	entry.charged_item = AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT;
	entry.rate_type = AST_AOC_RATE_TYPE_SPECIAL_CODE;
	entry.rate.special_code = code;

	return aoc_s_add_entry(decoded, &entry);
}

enum ast_aoc_type ast_aoc_get_msg_type(struct ast_aoc_decoded *decoded)
{
	return decoded->msg_type;
}

enum ast_aoc_charge_type ast_aoc_get_charge_type(struct ast_aoc_decoded *decoded)
{
	return decoded->charge_type;
}

enum ast_aoc_request ast_aoc_get_request(struct ast_aoc_decoded *decoded)
{
	return decoded->request_flag;
}

int ast_aoc_set_total_type(struct ast_aoc_decoded *decoded,
	const enum ast_aoc_total_type type)
{
	decoded->total_type = type;
	return 0;
}

enum ast_aoc_total_type ast_aoc_get_total_type(struct ast_aoc_decoded *decoded)
{
	return decoded->total_type;
}

int ast_aoc_set_currency_info(struct ast_aoc_decoded *decoded,
		const unsigned int amount,
		const enum ast_aoc_currency_multiplier multiplier,
		const char *name)
{

	if (!ast_strlen_zero(name)) {
		ast_copy_string(decoded->currency_name, name, sizeof(decoded->currency_name));
	}

	decoded->currency_amount = amount;

	if (multiplier && (multiplier < AST_AOC_MULT_NUM_ENTRIES)) {
		decoded->multiplier = multiplier;
	} else {
		decoded->multiplier = AST_AOC_MULT_ONE;
	}

	return 0;
}

unsigned int ast_aoc_get_currency_amount(struct ast_aoc_decoded *decoded)
{
	return decoded->currency_amount;
}

enum ast_aoc_currency_multiplier ast_aoc_get_currency_multiplier(struct ast_aoc_decoded *decoded)
{
	return decoded->multiplier;
}

const char *ast_aoc_get_currency_multiplier_decimal(struct ast_aoc_decoded *decoded)
{
	switch (decoded->multiplier) {
	case AST_AOC_MULT_ONETHOUSANDTH:
		return "0.001";
	case AST_AOC_MULT_ONEHUNDREDTH:
		return "0.01";
	case AST_AOC_MULT_ONETENTH:
		return "0.1";
	case AST_AOC_MULT_ONE:
		return "1.0";
	case AST_AOC_MULT_TEN:
		return "10.0";
	case AST_AOC_MULT_HUNDRED:
		return "100.0";
	case AST_AOC_MULT_THOUSAND:
		return "1000.0";
	default:
		return "1.0";
	}
}

const char *ast_aoc_get_currency_name(struct ast_aoc_decoded *decoded)
{
	return decoded->currency_name;
}

int ast_aoc_add_unit_entry(struct ast_aoc_decoded *decoded,
		const unsigned int amount_is_present,
		const unsigned int amount,
		const unsigned int type_is_present,
		const unsigned int type)
{
	if ((decoded->msg_type == AST_AOC_REQUEST) ||
		(decoded->unit_count >= ARRAY_LEN(decoded->unit_list))) {
		return -1;
	}

	if (!amount_is_present && !type_is_present) {
		return -1;
	}

	decoded->unit_list[decoded->unit_count].valid_amount = amount_is_present;
	if (amount_is_present) {
		decoded->unit_list[decoded->unit_count].amount = amount;
	} else {
		decoded->unit_list[decoded->unit_count].amount = 0;
	}

	decoded->unit_list[decoded->unit_count].valid_type = type_is_present;
	if (type_is_present) {
		decoded->unit_list[decoded->unit_count].type = type;
	} else {
		decoded->unit_list[decoded->unit_count].type = 0;
	}
	decoded->unit_count++;

	return 0;
}

const struct ast_aoc_unit_entry *ast_aoc_get_unit_info(struct ast_aoc_decoded *decoded, unsigned int entry_number)
{
	if (entry_number >= decoded->unit_count) {
		return NULL;
	}

	return (const struct ast_aoc_unit_entry *) &decoded->unit_list[entry_number];
}

unsigned int ast_aoc_get_unit_count(struct ast_aoc_decoded *decoded)
{
	return decoded->unit_count;
}

int ast_aoc_set_billing_id(struct ast_aoc_decoded *decoded, const enum ast_aoc_billing_id id)
{
	if ((id >= AST_AOC_BILLING_NUM_ENTRIES) || (id < AST_AOC_BILLING_NA)) {
		return -1;
	}

	decoded->billing_id = id;

	return 0;
}

enum ast_aoc_billing_id ast_aoc_get_billing_id(struct ast_aoc_decoded *decoded)
{
	return decoded->billing_id;
}

int ast_aoc_set_association_id(struct ast_aoc_decoded *decoded, const int id)
{
	if (decoded->msg_type != AST_AOC_E) {
		return -1;
	}
	memset(&decoded->charging_association, 0, sizeof(decoded->charging_association));
	decoded->charging_association.charging_type = AST_AOC_CHARGING_ASSOCIATION_ID;
	decoded->charging_association.charge.id = id;
	return 0;
}

const struct ast_aoc_charging_association *ast_aoc_get_association_info(struct ast_aoc_decoded *decoded)
{
	return &decoded->charging_association;
}

int ast_aoc_set_association_number(struct ast_aoc_decoded *decoded, const char *num, uint8_t plan)
{
	if ((decoded->msg_type != AST_AOC_E) || ast_strlen_zero(num)) {
		return -1;
	}
	memset(&decoded->charging_association, 0, sizeof(decoded->charging_association));
	decoded->charging_association.charging_type = AST_AOC_CHARGING_ASSOCIATION_NUMBER;
	decoded->charging_association.charge.number.plan = plan;
	ast_copy_string(decoded->charging_association.charge.number.number, num, sizeof(decoded->charging_association.charge.number.number));

	return 0;
}

int ast_aoc_set_termination_request(struct ast_aoc_decoded *decoded)
{
	if (decoded->msg_type != AST_AOC_REQUEST) {
		return -1;
	}
	decoded->termination_request = 1;

	return 0;
}

int ast_aoc_get_termination_request(struct ast_aoc_decoded *decoded)
{
	return decoded->termination_request;
}

/*!
 * \internal
 * \brief Convert AST_AOC_VOLUME_UNIT to string.
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return String equivalent.
 */
static const char *aoc_volume_unit_str(enum ast_aoc_volume_unit value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_VOLUME_UNIT_OCTET:
		str = "Octet";
		break;
	case AST_AOC_VOLUME_UNIT_SEGMENT:
		str = "Segment";
		break;
	case AST_AOC_VOLUME_UNIT_MESSAGE:
		str = "Message";
		break;
	}
	return str;
}

/*!
 * \internal
 * \brief Convert ast_aoc_charged_item to string.
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return String equivalent.
 */
static const char *aoc_charged_item_str(enum ast_aoc_s_charged_item value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_CHARGED_ITEM_NA:
		str = "NotAvailable";
		break;
	case AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT:
		str = "SpecialArrangement";
		break;
	case AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION:
		str = "BasicCommunication";
		break;
	case AST_AOC_CHARGED_ITEM_CALL_ATTEMPT:
		str = "CallAttempt";
		break;
	case AST_AOC_CHARGED_ITEM_CALL_SETUP:
		str = "CallSetup";
		break;
	case AST_AOC_CHARGED_ITEM_USER_USER_INFO:
		str = "UserUserInfo";
		break;
	case AST_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE:
		str = "SupplementaryService";
		break;
	}
	return str;
}

/*!
 * \internal
 * \brief Convert ast_aoc_total_type to string.
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return String equivalent.
 */
static const char *aoc_type_of_totaling_str(enum ast_aoc_total_type value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_SUBTOTAL:
		str = "SubTotal";
		break;
	case AST_AOC_TOTAL:
		str = "Total";
		break;
	}
	return str;
}

/*!
 * \internal
 * \brief Convert ast_aoc_rate_type to string.
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return String equivalent.
 */
static const char *aoc_rate_type_str(enum ast_aoc_s_rate_type value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_RATE_TYPE_NA:
		str = "NotAvailable";
		break;
	case AST_AOC_RATE_TYPE_FREE:
		str = "Free";
		break;
	case AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING:
		str = "FreeFromBeginning";
		break;
	case AST_AOC_RATE_TYPE_DURATION:
		str = "Duration";
		break;
	case AST_AOC_RATE_TYPE_FLAT:
		str = "Flat";
		break;
	case AST_AOC_RATE_TYPE_VOLUME:
		str = "Volume";
		break;
	case AST_AOC_RATE_TYPE_SPECIAL_CODE:
		str = "SpecialCode";
		break;
	}
	return str;
}

/*!
 * \internal
 * \brief Convert AST_AOC_TIME_SCALE to string.
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return String equivalent.
 */
static const char *aoc_scale_str(enum ast_aoc_time_scale value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_TIME_SCALE_HUNDREDTH_SECOND:
		str = "OneHundredthSecond";
		break;
	case AST_AOC_TIME_SCALE_TENTH_SECOND:
		str = "OneTenthSecond";
		break;
	case AST_AOC_TIME_SCALE_SECOND:
		str = "Second";
		break;
	case AST_AOC_TIME_SCALE_TEN_SECOND:
		str = "TenSeconds";
		break;
	case AST_AOC_TIME_SCALE_MINUTE:
		str = "Minute";
		break;
	case AST_AOC_TIME_SCALE_HOUR:
		str = "Hour";
		break;
	case AST_AOC_TIME_SCALE_DAY:
		str = "Day";
		break;
	}
	return str;
}

static const char *aoc_charge_type_str(enum ast_aoc_charge_type value)
{
	const char *str;

	switch (value) {
	default:
	case AST_AOC_CHARGE_NA:
		str = "NotAvailable";
		break;
	case AST_AOC_CHARGE_FREE:
		str = "Free";
		break;
	case AST_AOC_CHARGE_CURRENCY:
		str = "Currency";
		break;
	case AST_AOC_CHARGE_UNIT:
		str = "Units";
		break;
	}

	return str;
}

static const char *aoc_multiplier_str(enum ast_aoc_currency_multiplier mult)
{
	switch (mult) {
	case AST_AOC_MULT_ONETHOUSANDTH:
		return "1/1000";
	case AST_AOC_MULT_ONEHUNDREDTH:
		return "1/100";
	case AST_AOC_MULT_ONETENTH:
		return "1/10";
	case AST_AOC_MULT_ONE:
		return "1";
	case AST_AOC_MULT_TEN:
		return "10";
	case AST_AOC_MULT_HUNDRED:
		return "100";
	case AST_AOC_MULT_THOUSAND:
		return "1000";
	case AST_AOC_MULT_NUM_ENTRIES:
		break;
	}
	return "1";
}

static const char *aoc_billingid_str(enum ast_aoc_billing_id billing_id)
{
	switch (billing_id) {
	case AST_AOC_BILLING_NORMAL:
		return "Normal";
	case AST_AOC_BILLING_REVERSE_CHARGE:
		return "Reverse";
	case AST_AOC_BILLING_CREDIT_CARD:
		return "CreditCard";
	case AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL:
		return "CallForwardingUnconditional";
	case AST_AOC_BILLING_CALL_FWD_BUSY:
		return "CallForwardingBusy";
	case AST_AOC_BILLING_CALL_FWD_NO_REPLY:
		return "CallForwardingNoReply";
	case AST_AOC_BILLING_CALL_DEFLECTION:
		return "CallDeflection";
	case AST_AOC_BILLING_CALL_TRANSFER:
		return "CallTransfer";
	case AST_AOC_BILLING_NA:
		return "NotAvailable";
	case AST_AOC_BILLING_NUM_ENTRIES:
		break;
	}
	return "NotAvailable";
}

int ast_aoc_test_encode_decode_match(struct ast_aoc_decoded *decoded)
{
	struct ast_aoc_decoded *new_decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t size;
	int res = 0;

	if (!(encoded = ast_aoc_encode(decoded, &size, NULL))) {
		return -1;
	}

	if (!(new_decoded = ast_aoc_decode(encoded, size, NULL))) {
		ast_free(encoded);
		return -1;
	}

	if (memcmp(new_decoded, decoded, sizeof(struct ast_aoc_decoded))) {
		res = -1;
	}

	ast_aoc_destroy_decoded(new_decoded);
	ast_aoc_destroy_encoded(encoded);
	return res;
}

static char *aoc_cli_debug_enable(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "aoc set debug";
		e->usage =
			"Usage: 'aoc set debug on' to enable aoc debug, 'aoc set debug off' to disable debug.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		if (a->argc != 4) {
			return CLI_SHOWUSAGE;
		} else if(ast_true(a->argv[3])) {
			ast_cli(a->fd, "aoc debug enabled\n");
			aoc_debug_enabled = 1;
		} else if (ast_false(a->argv[3])) {
			ast_cli(a->fd, "aoc debug disabled\n");
			aoc_debug_enabled = 0;
		} else {
			return CLI_SHOWUSAGE;
		}
	}

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Append the time structure to the event message string.
 * \since 1.8
 *
 * \param msg Event message string being built.
 * \param prefix Prefix to add to the amount lines.
 * \param name Name of the time structure to convert.
 * \param time Data to convert.
 * \param scale Data to convert.
 *
 * \return Nothing
 */
static void aoc_time_str(struct ast_str **msg, const char *prefix, const char *name, unsigned long time, enum ast_aoc_time_scale scale)
{
	ast_str_append(msg, 0, "%s/%s/Length: %lu\r\n", prefix, name, time);
	ast_str_append(msg, 0, "%s/%s/Scale: %s\r\n", prefix, name,
		aoc_scale_str(scale));
}

/*!
 * \internal
 * \brief Append the amount structure to the event message string.
 * \since 1.8
 *
 * \param msg Event message string being built.
 * \param prefix Prefix to add to the amount lines.
 * \param amount Data to convert.
 * \param multipler to convert
 *
 * \return Nothing
 */
static void aoc_amount_str(struct ast_str **msg, const char *prefix, unsigned int amount, enum ast_aoc_currency_multiplier mult)
{
	static const char name[] = "Amount";

	ast_str_append(msg, 0, "%s/%s/Cost: %u\r\n", prefix, name, amount);
	ast_str_append(msg, 0, "%s/%s/Multiplier: %s\r\n", prefix, name,
		aoc_multiplier_str(mult));
}

static void aoc_request_event(const struct ast_aoc_decoded *decoded, struct ast_channel *chan, struct ast_str **msg)
{
	if (chan) {
		ast_str_append(msg, 0, "Channel: %s\r\n", ast_channel_name(chan));
		ast_str_append(msg, 0, "UniqueID: %s\r\n", ast_channel_uniqueid(chan));
	}

	if (decoded->request_flag) {
		ast_str_append(msg, 0, "AOCRequest:");
		if (decoded->request_flag & AST_AOC_REQUEST_S) {
			ast_str_append(msg, 0, "S");
		}
		if (decoded->request_flag & AST_AOC_REQUEST_D) {
			ast_str_append(msg, 0, "D");
		}
		if (decoded->request_flag & AST_AOC_REQUEST_E) {
			ast_str_append(msg, 0, "E");
		}
		ast_str_append(msg, 0, "\r\n");

	} else {
		ast_str_append(msg, 0, "AOCRequest: NONE\r\n");
	}
}

static void aoc_s_event(const struct ast_aoc_decoded *decoded, struct ast_channel *owner, struct ast_str **msg)
{
	const char *rate_str;
	char prefix[32];
	int idx;

	if (owner) {
		ast_str_append(msg, 0, "Channel: %s\r\n", ast_channel_name(owner));
		ast_str_append(msg, 0, "UniqueID: %s\r\n", ast_channel_uniqueid(owner));
	}

	ast_str_append(msg, 0, "NumberRates: %d\r\n", decoded->aoc_s_count);
	for (idx = 0; idx < decoded->aoc_s_count; ++idx) {
		snprintf(prefix, sizeof(prefix), "Rate(%d)", idx);

		ast_str_append(msg, 0, "%s/Chargeable: %s\r\n", prefix,
			aoc_charged_item_str(decoded->aoc_s_entries[idx].charged_item));
		if (decoded->aoc_s_entries[idx].charged_item == AST_AOC_CHARGED_ITEM_NA) {
			continue;
		}
		rate_str = aoc_rate_type_str(decoded->aoc_s_entries[idx].rate_type);
		ast_str_append(msg, 0, "%s/Type: %s\r\n", prefix, rate_str);
		switch (decoded->aoc_s_entries[idx].rate_type) {
		case AST_AOC_RATE_TYPE_DURATION:
			strcat(prefix, "/");
			strcat(prefix, rate_str);
			ast_str_append(msg, 0, "%s/Currency: %s\r\n", prefix,
				decoded->aoc_s_entries[idx].rate.duration.currency_name);
			aoc_amount_str(msg, prefix,
				decoded->aoc_s_entries[idx].rate.duration.amount,
				decoded->aoc_s_entries[idx].rate.duration.multiplier);
			ast_str_append(msg, 0, "%s/ChargingType: %s\r\n", prefix,
				decoded->aoc_s_entries[idx].rate.duration.charging_type ?
				"StepFunction" : "ContinuousCharging");
			aoc_time_str(msg, prefix, "Time",
				decoded->aoc_s_entries[idx].rate.duration.time,
				decoded->aoc_s_entries[idx].rate.duration.time_scale);
			if (decoded->aoc_s_entries[idx].rate.duration.granularity_time) {
				aoc_time_str(msg, prefix, "Granularity",
					decoded->aoc_s_entries[idx].rate.duration.granularity_time,
					decoded->aoc_s_entries[idx].rate.duration.granularity_time_scale);
			}
			break;
		case AST_AOC_RATE_TYPE_FLAT:
			strcat(prefix, "/");
			strcat(prefix, rate_str);
			ast_str_append(msg, 0, "%s/Currency: %s\r\n", prefix,
				decoded->aoc_s_entries[idx].rate.flat.currency_name);
			aoc_amount_str(msg, prefix,
				decoded->aoc_s_entries[idx].rate.flat.amount,
				decoded->aoc_s_entries[idx].rate.flat.multiplier);
			break;
		case AST_AOC_RATE_TYPE_VOLUME:
			strcat(prefix, "/");
			strcat(prefix, rate_str);
			ast_str_append(msg, 0, "%s/Currency: %s\r\n", prefix,
				decoded->aoc_s_entries[idx].rate.volume.currency_name);
			aoc_amount_str(msg, prefix,
				decoded->aoc_s_entries[idx].rate.volume.amount,
				decoded->aoc_s_entries[idx].rate.volume.multiplier);
			ast_str_append(msg, 0, "%s/Unit: %s\r\n", prefix,
				aoc_volume_unit_str(decoded->aoc_s_entries[idx].rate.volume.volume_unit));
			break;
		case AST_AOC_RATE_TYPE_SPECIAL_CODE:
			ast_str_append(msg, 0, "%s/%s: %d\r\n", prefix, rate_str,
				decoded->aoc_s_entries[idx].rate.special_code);
			break;
		default:
			break;
		}
	}
}

static void aoc_d_event(const struct ast_aoc_decoded *decoded, struct ast_channel *chan, struct ast_str **msg)
{
	const char *charge_str;
	int idx;
	char prefix[32];

	if (chan) {
		ast_str_append(msg, 0, "Channel: %s\r\n", ast_channel_name(chan));
		ast_str_append(msg, 0, "UniqueID: %s\r\n", ast_channel_uniqueid(chan));
	}

	charge_str = aoc_charge_type_str(decoded->charge_type);
	ast_str_append(msg, 0, "Type: %s\r\n", charge_str);

	switch (decoded->charge_type) {
	case AST_AOC_CHARGE_CURRENCY:
	case AST_AOC_CHARGE_UNIT:
		ast_str_append(msg, 0, "BillingID: %s\r\n",
			aoc_billingid_str(decoded->billing_id));
		ast_str_append(msg, 0, "TypeOfCharging: %s\r\n",
			aoc_type_of_totaling_str(decoded->total_type));
		break;
	default:
		break;
	}

	switch (decoded->charge_type) {
	case AST_AOC_CHARGE_CURRENCY:
		ast_str_append(msg, 0, "%s: %s\r\n", charge_str,
			decoded->currency_name);
		aoc_amount_str(msg, charge_str,
			decoded->currency_amount,
			decoded->multiplier);
		break;
	case AST_AOC_CHARGE_UNIT:
		ast_str_append(msg, 0, "%s/NumberItems: %d\r\n", charge_str,
			decoded->unit_count);
		for (idx = 0; idx < decoded->unit_count; ++idx) {
			snprintf(prefix, sizeof(prefix), "%s/Item(%d)", charge_str, idx);
			if (decoded->unit_list[idx].valid_amount) {
				ast_str_append(msg, 0, "%s/NumberOf: %u\r\n", prefix,
					decoded->unit_list[idx].amount);
			}
			if (decoded->unit_list[idx].valid_type) {
				ast_str_append(msg, 0, "%s/TypeOf: %u\r\n", prefix,
					decoded->unit_list[idx].type);
			}
		}
		break;
	default:
		break;
	}
}

static void aoc_e_event(const struct ast_aoc_decoded *decoded, struct ast_channel *chan, struct ast_str **msg)
{
	const char *charge_str;
	int idx;
	char prefix[32];

	if (chan) {
		ast_str_append(msg, 0, "Channel: %s\r\n", ast_channel_name(chan));
		ast_str_append(msg, 0, "UniqueID: %s\r\n", ast_channel_uniqueid(chan));
	}

	charge_str = "ChargingAssociation";

	switch (decoded->charging_association.charging_type) {
	case AST_AOC_CHARGING_ASSOCIATION_NUMBER:
		snprintf(prefix, sizeof(prefix), "%s/Number", charge_str);
		ast_str_append(msg, 0, "%s: %s\r\n", prefix,
			decoded->charging_association.charge.number.number);
		ast_str_append(msg, 0, "%s/Plan: %d\r\n", prefix,
			decoded->charging_association.charge.number.plan);
		break;
	case AST_AOC_CHARGING_ASSOCIATION_ID:
		ast_str_append(msg, 0, "%s/ID: %d\r\n", charge_str, decoded->charging_association.charge.id);
		break;
	case AST_AOC_CHARGING_ASSOCIATION_NA:
	default:
		break;
	}

	charge_str = aoc_charge_type_str(decoded->charge_type);
	ast_str_append(msg, 0, "Type: %s\r\n", charge_str);
	switch (decoded->charge_type) {
	case AST_AOC_CHARGE_CURRENCY:
	case AST_AOC_CHARGE_UNIT:
		ast_str_append(msg, 0, "BillingID: %s\r\n",
			aoc_billingid_str(decoded->billing_id));
		break;
	default:
		break;
	}
	switch (decoded->charge_type) {
	case AST_AOC_CHARGE_CURRENCY:
		ast_str_append(msg, 0, "%s: %s\r\n", charge_str,
			decoded->currency_name);
		aoc_amount_str(msg, charge_str,
			decoded->currency_amount,
			decoded->multiplier);
		break;
	case AST_AOC_CHARGE_UNIT:
		ast_str_append(msg, 0, "%s/NumberItems: %d\r\n", charge_str,
			decoded->unit_count);
		for (idx = 0; idx < decoded->unit_count; ++idx) {
			snprintf(prefix, sizeof(prefix), "%s/Item(%d)", charge_str, idx);
			if (decoded->unit_list[idx].valid_amount) {
				ast_str_append(msg, 0, "%s/NumberOf: %u\r\n", prefix,
					decoded->unit_list[idx].amount);
			}
			if (decoded->unit_list[idx].valid_type) {
				ast_str_append(msg, 0, "%s/TypeOf: %u\r\n", prefix,
					decoded->unit_list[idx].type);
			}
		}
		break;
	default:
		break;
	}
}

int ast_aoc_manager_event(const struct ast_aoc_decoded *decoded, struct ast_channel *chan)
{
	struct ast_str *msg;

	if (!decoded || !(msg = ast_str_create(1024))) {
		return -1;
	}

	switch (decoded->msg_type) {
	case AST_AOC_S:
		if (chan) {
			aoc_s_event(decoded, chan, &msg);
			ast_manager_event(chan, EVENT_FLAG_AOC, "AOC-S", "%s", ast_str_buffer(msg));
		}
		break;
	case AST_AOC_D:
		if (chan) {
			aoc_d_event(decoded, chan, &msg);
			ast_manager_event(chan, EVENT_FLAG_AOC, "AOC-D", "%s", ast_str_buffer(msg));
		}
		break;
	case AST_AOC_E:
		{
			struct ast_channel *chans[1];
			aoc_e_event(decoded, chan, &msg);
			chans[0] = chan;
			ast_manager_event_multichan(EVENT_FLAG_AOC, "AOC-E", chan ? 1 : 0, chans, "%s", ast_str_buffer(msg));
		}
		break;
	default:
		/* events for AST_AOC_REQUEST are not generated here */
		break;
	}

	ast_free(msg);
	return 0;
}

int ast_aoc_decoded2str(const struct ast_aoc_decoded *decoded, struct ast_str **msg)
{
	if (!decoded || !msg) {
		return -1;
	}

	switch (decoded->msg_type) {
	case AST_AOC_S:
		ast_str_append(msg, 0, "AOC-S\r\n");
		aoc_s_event(decoded, NULL, msg);
		break;
	case AST_AOC_D:
		ast_str_append(msg, 0, "AOC-D\r\n");
		aoc_d_event(decoded, NULL, msg);
		break;
	case AST_AOC_E:
		ast_str_append(msg, 0, "AOC-E\r\n");
		aoc_e_event(decoded, NULL, msg);
		break;
	case AST_AOC_REQUEST:
		ast_str_append(msg, 0, "AOC-Request\r\n");
		aoc_request_event(decoded, NULL, msg);
		break;
	}

	return 0;
}

static void aoc_display_decoded_debug(const struct ast_aoc_decoded *decoded, int decoding, struct ast_channel *chan)
{
	struct ast_str *msg;

	if (!decoded || !(msg = ast_str_create(1024))) {
		return;
	}

	if (decoding) {
		ast_str_append(&msg, 0, "---- DECODED AOC MSG ----\r\n");
	} else {
		ast_str_append(&msg, 0, "---- ENCODED AOC MSG ----\r\n");
	}
	if (chan) {
		ast_str_append(&msg, 0, "CHANNEL: %s\r\n", ast_channel_name(chan));
	}

	if (ast_aoc_decoded2str(decoded, &msg)) {
		ast_free(msg);
		return;
	}

	ast_verb(1, "%s\r\n", ast_str_buffer(msg));
	ast_free(msg);
}

static struct ast_cli_entry aoc_cli[] = {
	AST_CLI_DEFINE(aoc_cli_debug_enable, "enable cli debugging of AOC messages"),
};

static void aoc_shutdown(void)
{
	ast_cli_unregister_multiple(aoc_cli, ARRAY_LEN(aoc_cli));
}
int ast_aoc_cli_init(void)
{
	ast_register_cleanup(aoc_shutdown);
	return ast_cli_register_multiple(aoc_cli, ARRAY_LEN(aoc_cli));
}
