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
 * \brief Generic Advice of Charge encode and decode routines
 *
 * \author David Vossel <dvossel@digium.com>
 */

#ifndef _AST_AOC_H_
#define _AST_AOC_H_

#include "asterisk/channel.h"

#define AOC_CURRENCY_NAME_SIZE (10 + 1)

/*! \brief Defines the currency multiplier for an aoc message. */
enum ast_aoc_currency_multiplier {
	AST_AOC_MULT_ONETHOUSANDTH = 1,
	AST_AOC_MULT_ONEHUNDREDTH,
	AST_AOC_MULT_ONETENTH,
	AST_AOC_MULT_ONE,
	AST_AOC_MULT_TEN,
	AST_AOC_MULT_HUNDRED,
	AST_AOC_MULT_THOUSAND,
	AST_AOC_MULT_NUM_ENTRIES, /* must remain the last item in enum, this is not a valid type */
};

/*!
 * \brief Defines the billing id options for an aoc message.
 * \note  AOC-D is limited to NORMAL, REVERSE_CHARGE, and CREDIT_CARD.
 */
enum ast_aoc_billing_id {
	AST_AOC_BILLING_NA = 0,
	AST_AOC_BILLING_NORMAL,
	AST_AOC_BILLING_REVERSE_CHARGE,
	AST_AOC_BILLING_CREDIT_CARD,
	AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL,
	AST_AOC_BILLING_CALL_FWD_BUSY,
	AST_AOC_BILLING_CALL_FWD_NO_REPLY,
	AST_AOC_BILLING_CALL_DEFLECTION,
	AST_AOC_BILLING_CALL_TRANSFER,
	AST_AOC_BILLING_NUM_ENTRIES /* must remain the last item in enum, not a valid billing id */
};

enum ast_aoc_type {
	AST_AOC_REQUEST = 0,
	AST_AOC_S,
	AST_AOC_D,
	AST_AOC_E, /* aoc-e must remain the last item in this enum */
};

enum ast_aoc_charge_type {
	AST_AOC_CHARGE_NA = 0,
	AST_AOC_CHARGE_FREE,
	AST_AOC_CHARGE_CURRENCY,
	AST_AOC_CHARGE_UNIT, /* unit must remain the last item in enum */
};

enum ast_aoc_request {
	AST_AOC_REQUEST_S = (1 << 0),
	AST_AOC_REQUEST_D = (1 << 1),
	AST_AOC_REQUEST_E = (1 << 2),
};

enum ast_aoc_total_type {
	AST_AOC_TOTAL = 0,
	AST_AOC_SUBTOTAL = 1,
};

enum ast_aoc_time_scale {
	AST_AOC_TIME_SCALE_HUNDREDTH_SECOND,
	AST_AOC_TIME_SCALE_TENTH_SECOND,
	AST_AOC_TIME_SCALE_SECOND,
	AST_AOC_TIME_SCALE_TEN_SECOND,
	AST_AOC_TIME_SCALE_MINUTE,
	AST_AOC_TIME_SCALE_HOUR,
	AST_AOC_TIME_SCALE_DAY,
};

struct ast_aoc_time {
	/*! LengthOfTimeUnit (Not valid if length is zero.) */
	uint32_t length;
	uint16_t scale;
};

struct ast_aoc_duration_rate {
	uint32_t amount;
	uint32_t time;
	/*! Not present if the granularity time is zero. */
	uint32_t granularity_time;

	uint16_t multiplier;
	uint16_t time_scale;
	uint16_t granularity_time_scale;

	/*! Name of currency involved.  Null terminated. */
	char currency_name[AOC_CURRENCY_NAME_SIZE];

	/*!
	 * \brief Charging interval type
	 * \details
	 * continuousCharging(0),
	 * stepFunction(1)
	 */
	uint8_t charging_type;
};

enum ast_aoc_volume_unit {
	AST_AOC_VOLUME_UNIT_OCTET,
	AST_AOC_VOLUME_UNIT_SEGMENT,
	AST_AOC_VOLUME_UNIT_MESSAGE,
};

struct ast_aoc_volume_rate {
	uint32_t amount;
	uint16_t multiplier;
	uint16_t volume_unit;
	char currency_name[AOC_CURRENCY_NAME_SIZE];
};

struct ast_aoc_flat_rate {
	uint32_t amount;
	uint16_t multiplier;
	/*! Name of currency involved.  Null terminated. */
	char currency_name[AOC_CURRENCY_NAME_SIZE];
};

enum ast_aoc_s_charged_item {
	AST_AOC_CHARGED_ITEM_NA,
	AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT,
	AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION,
	AST_AOC_CHARGED_ITEM_CALL_ATTEMPT,
	AST_AOC_CHARGED_ITEM_CALL_SETUP,
	AST_AOC_CHARGED_ITEM_USER_USER_INFO,
	AST_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE,
};

enum ast_aoc_s_rate_type {
	AST_AOC_RATE_TYPE_NA,
	AST_AOC_RATE_TYPE_FREE,
	AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING,
	AST_AOC_RATE_TYPE_DURATION,
	AST_AOC_RATE_TYPE_FLAT,
	AST_AOC_RATE_TYPE_VOLUME,
	AST_AOC_RATE_TYPE_SPECIAL_CODE,
};

struct ast_aoc_s_entry {
	uint16_t charged_item;
	uint16_t rate_type;

	/*! \brief Charge rate being applied. */
	union {
		struct ast_aoc_duration_rate duration;
		struct ast_aoc_flat_rate flat;
		struct ast_aoc_volume_rate volume;
		uint16_t special_code; /* 1...10 */
	} rate;
} __attribute__((packed));

struct ast_aoc_unit_entry {
	char valid_amount;
	unsigned int amount;
	char valid_type;
	unsigned int type; /* 1 - 16 by ETSI standard */
};

enum AST_AOC_CHARGING_ASSOCIATION {
	AST_AOC_CHARGING_ASSOCIATION_NA,
	AST_AOC_CHARGING_ASSOCIATION_NUMBER,
	AST_AOC_CHARGING_ASSOCIATION_ID,
};
struct ast_aoc_charging_association_number {
	uint8_t plan;
	char number[32];
} __attribute__((packed));
struct ast_aoc_charging_association {
	union {
		int32_t id;
		struct ast_aoc_charging_association_number number;
	} charge;
	/*! \see enum AST_AOC_CHARGING_ASSOCIATION */
	uint8_t charging_type;
} __attribute__((packed));

/*! \brief AOC Payload Header. Holds all the encoded AOC data to pass on the wire */
struct ast_aoc_encoded;

/*! \brief Decoded AOC data. This value is used to set all the values in an AOC message before encoding.*/
struct ast_aoc_decoded;

/*!
 * \brief creates a ast_aoc_decode object of a specific message type
 * \since 1.8
 *
 * \param msg_type AOC-D, AOC-E, or AOC Request
 * \param charge_type this is ignored if message type is not AOC-D or AOC-E.
 * \param requests flags.  This defines the types of AOC requested. This
 *        field should only be set when the message type is AOC Request,
 *        the value is ignored otherwise.
 *
 * \retval heap allocated ast_aoc_decoded object ptr on success
 * \retval NULL failure
 */
struct ast_aoc_decoded *ast_aoc_create(const enum ast_aoc_type msg_type,
		const enum ast_aoc_charge_type charge_type,
		const enum ast_aoc_request requests);


/*! \brief free an ast_aoc_decoded object */
void *ast_aoc_destroy_decoded(struct ast_aoc_decoded *decoded);

/*! \brief free an ast_aoc_encoded object */
void *ast_aoc_destroy_encoded(struct ast_aoc_encoded *encoded);

/*!
 * \brief decodes an encoded aoc payload.
 * \since 1.8
 *
 * \param encoded the encoded payload to decode.
 * \param size total size of encoded payload
 * \param chan ast channel, Optional for DEBUG output purposes
 *
 * \retval heap allocated ast_aoc_decoded object ptr on success
 * \retval NULL failure
 */
struct ast_aoc_decoded *ast_aoc_decode(struct ast_aoc_encoded *encoded, size_t size, struct ast_channel *chan);

/*!
 * \brief encodes a decoded aoc structure so it can be passed on the wire
 * \since 1.8
 *
 * \param decoded the decoded struct to be encoded
 * \param out_size output parameter representing size of encoded data
 * \param chan ast channel, Optional for DEBUG output purposes
 *
 * \retval pointer to encoded data
 * \retval NULL failure
 */
struct ast_aoc_encoded *ast_aoc_encode(struct ast_aoc_decoded *decoded, size_t *out_size, struct ast_channel *chan);

/*!
 * \brief Sets the type of total for a AOC-D message
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 * \param type total type: TOTAL or SUBTOTAL
 *
 * \note If this value is not set, the default for the message is TOTAL
 *
 * \retval 0 success
 */
int ast_aoc_set_total_type(struct ast_aoc_decoded *decoded, const enum ast_aoc_total_type type);

/*!
 * \brief Sets the currency values for a AOC-D or AOC-E message
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 * \param amount currency amount REQUIRED
 * \param multiplier currency multiplier REQUIRED, 0 or undefined value defaults to AST_AOC_MULT_ONE.
 * \param name currency name OPTIONAL
 *
 * \retval 0 success
 */
int ast_aoc_set_currency_info(struct ast_aoc_decoded *decoded,
		const unsigned int amount,
		const enum ast_aoc_currency_multiplier multiplier,
		const char *name);

/*!
 * \brief Adds a unit entry into the list of units
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 * \param amount_is_present set this if the number of units is actually present.
 * \param amount number of units
 * \param type_is_present set this if the type value is present
 * \param type unit type
 *
 * \note If neither the amount nor the type is present, the entry will
 * not be added.
 *
 * \retval 0 success
 */
int ast_aoc_add_unit_entry(struct ast_aoc_decoded *decoded,
		const unsigned int amount_is_present,
		const unsigned int amount,
		const unsigned int type_is_present,
		const unsigned int type);

/*!
 * \brief set the billing id for a AOC-D or AST_AOC_E message
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 * \param id billing id
 *
 * \retval 0 success
 */
int ast_aoc_set_billing_id(struct ast_aoc_decoded *decoded, const enum ast_aoc_billing_id id);

/*!
 * \brief set the charging association id for an AST_AOC_E message
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 * \param id charging association identifier
 *
 * \note If the association number was set, this will override that value. Only the id OR the
 *       number can be set at a time, not both.
 *
 * \retval 0 success
 */
int ast_aoc_set_association_id(struct ast_aoc_decoded *decoded, const int id);

/*!
 * \brief set the charging association number for an AOC-E message
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 * \param num charging association number
 * \param plan charging association number plan and type-of-number fields
 *
 * \note If the association id was set, this will override that value. Only the id OR the
 *       number can be set at a time, not both.
 *
 * \retval 0 success
 */
int ast_aoc_set_association_number(struct ast_aoc_decoded *decoded, const char *num, uint8_t plan);

/*!
 * \brief Mark the AST_AOC_REQUEST message as a termination request.
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to set values on
 *
 * \note A termination request indicates that the call has terminated,
 * but that the other side is waiting for a short period of time before
 * hanging up so it can get the final AOC-E message.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_set_termination_request(struct ast_aoc_decoded *decoded);

/*!
 * \brief Add AOC-S duration rate entry
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param charged_item ast_aoc_s_charged_item
 * \param amount currency amount
 * \param multiplier currency multiplier
 * \param currency_name truncated after 10 characters
 * \param time
 * \param time_scale from ast_aoc_time_scale enum
 * \param granularity_time (optional, set to 0 if not present);
 * \param granularity_time_scale (optional, set to 0 if not present);
 * \param step_function  set to 1 if this is to use a step function, 0 if continuious
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_rate_duration(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	unsigned int amount,
	enum ast_aoc_currency_multiplier multiplier,
	const char *currency_name,
	unsigned long time,
	enum ast_aoc_time_scale time_scale,
	unsigned long granularity_time,
	enum ast_aoc_time_scale granularity_time_scale,
	int step_function);

/*!
 * \brief Add AOC-S flat rate entry
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param charged_item ast_aoc_s_charged_item
 * \param amount currency amount
 * \param multiplier currency multiplier
 * \param currency_name truncated after 10 characters
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_rate_flat(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	unsigned int amount,
	enum ast_aoc_currency_multiplier multiplier,
	const char *currency_name);

/*!
 * \brief Add AOC-S volume rate entry
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param charged_item ast_aoc_s_charged_item
 * \param volume_unit from ast_aoc_volume_unit enum
 * \param amount currency amount
 * \param multiplier currency multiplier
 * \param currency_name truncated after 10 characters
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_rate_volume(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	enum ast_aoc_volume_unit volume_unit,
	unsigned int amount,
	enum ast_aoc_currency_multiplier multiplier,
	const char *currency_name);

/*!
 * \brief Add AOC-S special rate entry
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param charged_item ast_aoc_s_charged_item
 * \param code special charging code
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_rate_special_charge_code(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item,
	unsigned int code);

/*!
 * \brief Add AOC-S indicating charge item is free
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param charged_item ast_aoc_s_charged_item
 * \param from_beginning TRUE if the rate is free from beginning.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_rate_free(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item, int from_beginning);

/*!
 * \brief Add AOC-S entry indicating charge item is not available
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param charged_item ast_aoc_s_charged_item
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_rate_na(struct ast_aoc_decoded *decoded,
	enum ast_aoc_s_charged_item charged_item);

/*!
 * \brief Add AOC-S special arrangement entry
 * \since 1.8
 *
 * \param decoded aoc decoded object to add entry to
 * \param code special arrangement code
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_s_add_special_arrangement(struct ast_aoc_decoded *decoded,
	unsigned int code);

/*!
 * \brief Convert decoded aoc msg to string representation
 * \since 1.8
 *
 * \param decoded ast_aoc_decoded struct to convert to string
 * \param msg dynamic heap allocated ast_str object to store string representation in
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_aoc_decoded2str(const struct ast_aoc_decoded *decoded, struct ast_str **msg);

/*!
 * \brief generate AOC manager event for an AOC-S, AOC-D, or AOC-E msg
 * \pre chan is locked
 */
int ast_aoc_manager_event(const struct ast_aoc_decoded *decoded, struct ast_channel *chan);

/*! \brief get the message type, AOC-D, AOC-E, or AOC Request */
enum ast_aoc_type ast_aoc_get_msg_type(struct ast_aoc_decoded *decoded);

/*! \brief get the charging type for an AOC-D or AOC-E message */
enum ast_aoc_charge_type ast_aoc_get_charge_type(struct ast_aoc_decoded *decoded);

/*! \brief get the types of AOC requested for when message type is AOC Request */
enum ast_aoc_request ast_aoc_get_request(struct ast_aoc_decoded *decoded);

/*! \brief get the type of total for a AOC-D message */
enum ast_aoc_total_type ast_aoc_get_total_type(struct ast_aoc_decoded *decoded);

/*! \brief get the currency amount for AOC-D and AOC-E messages*/
unsigned int ast_aoc_get_currency_amount(struct ast_aoc_decoded *decoded);

/*! \brief get the number rates associated with an AOC-S message */
unsigned int ast_aoc_s_get_count(struct ast_aoc_decoded *decoded);

/*!
 * \brief get a specific AOC-S rate entry.
 * \since 1.8
 *
 * \note This can be used in conjunction with ast_aoc_s_get_count to create
 *       a unit entry iterator.
 */
const struct ast_aoc_s_entry *ast_aoc_s_get_rate_info(struct ast_aoc_decoded *decoded, unsigned int entry_number);

/*! \brief get the number of unit entries for AOC-D and AOC-E messages*/
unsigned int ast_aoc_get_unit_count(struct ast_aoc_decoded *decoded);

/*!
 * \brief get a specific unit entry.
 * \since 1.8
 *
 * \note This can be used in conjunction with ast_aoc_get_unit_count to create
 *       a unit entry iterator.
 */
const struct ast_aoc_unit_entry *ast_aoc_get_unit_info(struct ast_aoc_decoded *decoded, unsigned int entry_number);

/*! \brief get the currency multiplier for AOC-D and AOC-E messages */
enum ast_aoc_currency_multiplier ast_aoc_get_currency_multiplier(struct ast_aoc_decoded *decoded);

/*! \brief get the currency multiplier for AOC-D and AOC-E messages in decimal format */
const char *ast_aoc_get_currency_multiplier_decimal(struct ast_aoc_decoded *decoded);

/*! \brief get the currency name for AOC-D and AOC-E messages*/
const char *ast_aoc_get_currency_name(struct ast_aoc_decoded *decoded);

/*! \brief get the billing id for AOC-D and AOC-E messages*/
enum ast_aoc_billing_id ast_aoc_get_billing_id(struct ast_aoc_decoded *decoded);

/*! \brief get the charging association info for AOC-E messages*/
const struct ast_aoc_charging_association *ast_aoc_get_association_info(struct ast_aoc_decoded *decoded);

/*!
 * \brief get whether or not the AST_AOC_REQUEST message as a termination request.
 * \since 1.8
 *
 * \note a termination request indicates that the call has terminated,
 *       but that the other side is waiting for a short period of time
 *       before hanging up so it can get the final AOC-E message.
 *
 * \param decoded ast_aoc_decoded struct to get values on
 *
 * \retval 0 not a termination request
 * \retval 1 is a termination request
 */
int ast_aoc_get_termination_request(struct ast_aoc_decoded *decoded);

/*!
 * \brief test aoc encode decode routines.
 * \since 1.8
 *
 * \note  This function verifies that a decoded message matches itself after
 *        the encode decode routine.
 */
int ast_aoc_test_encode_decode_match(struct ast_aoc_decoded *decoded);

/*! \brief enable aoc cli options */
int ast_aoc_cli_init(void);

#endif	/* _AST_AOC_H_ */
