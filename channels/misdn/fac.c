
#include "fac.h"
#include "asn1.h"

#if 0
+-------------------------------
| IE_IDENTIFIER
+-------------------------------
| {length}
+-------------------------------
|   +---------------------------
|   | SERVICE_DISCRIMINATOR
|   +---------------------------
|   | COMPONENT_TYPE_TAG
|   +---------------------------
|   | {length}
|   +---------------------------
|   |	+-----------------------
|   |   | INVOKE_IDENTIFIER_TAG (0x2)
|   |   +-----------------------
|   |   | {length}              (0x1)
|   |   +-----------------------
|   |   | {value}               (odd integer 0-127)
|   |   +-----------------------
|   |   +-----------------------
|   |   | OPERATION_VALUE_TAG   (0x2)
|   |   +-----------------------
|   |   | {length}              (0x1)
|   |   +-----------------------
|   |   | {value}
|   |   +-----------------------
|   |	+-----------------------
|   |	| ASN.1 data
+---+---+-----------------------
#endif

enum {
	SUPPLEMENTARY_SERVICE 	= 0x91,
} SERVICE_DISCRIMINATOR;

enum {
	INVOKE 					= 0xa1,
	RETURN_RESULT 			= 0xa2,
	RETURN_ERROR 			= 0xa3,
	REJECT 					= 0xa4,
} COMPONENT_TYPE_TAG;

enum {
	INVOKE_IDENTIFIER 		= 0x02,
	LINKED_IDENTIFIER 		= 0x80,
	NULL_IDENTIFIER 		= 0x05,
} INVOKE_IDENTIFIER_TAG;

enum {
	OPERATION_VALUE 		= 0x02,
} OPERATION_VALUE_TAG;

enum {
	VALUE_QUERY 			= 0x8c,
	SET_VALUE 				= 0x8d,
	REQUEST_FEATURE 		= 0x8f,
	ABORT 					= 0xbe,
	REDIRECT_CALL 			= 0xce,
	CALLING_PARTY_TO_HOLD 	= 0xcf,
	CALLING_PARTY_FROM_HOLD = 0x50,
	DROP_TARGET_PARTY 		= 0xd1,
	USER_DATA_TRANSFER 		= 0xd3,
	APP_SPECIFIC_STATUS 	= 0xd2,

	/* not from document */
	CALL_DEFLECT 			= 0x0d,
	AOC 					= 0x22,
} OPERATION_CODE;

enum {
	Q931_IE_TAG 			= 0x40,
} ARGUMENT_TAG;

#ifdef FACILITY_DEBUG
#define FAC_DUMP(fac,len,bc) fac_dump(fac,len,bc)
#include <ctype.h>
static void fac_dump (__u8 *facility, unsigned int fac_len, struct misdn_bchannel *bc)
{
	int i;
	cb_log(0, bc->port, "    --- facility dump start. length:%d\n", fac_len);
	for (i = 0; i < fac_len; ++i)
		if (isprint(facility[i]))
			cb_log(0, bc->port, "    --- %d: %04p (char:%c)\n", i, facility[i], facility[i]);
		else
			cb_log(0, bc->port, "    --- %d: %04p\n", i, facility[i]);
	cb_log(0, bc->port, "    --- facility dump end\n");
}
#else
#define FAC_DUMP(fac,len,bc)
#endif

/*
** Facility Encoding
*/

static int enc_fac_calldeflect (__u8 *dest, char *number, int pres)
{
	__u8 *body_len,
		 *p = dest,
		 *seq1, *seq2;

	*p++ = SUPPLEMENTARY_SERVICE;
	*p++ = INVOKE;

	body_len = p++;

	p += _enc_int(p, 0x1 /* some odd integer in (0..127) */, INVOKE_IDENTIFIER);
	p += _enc_int(p, CALL_DEFLECT, OPERATION_VALUE);
	p += enc_sequence_start(p, &seq1);
	  p += enc_sequence_start(p, &seq2);
	    p += _enc_num_string(p, number, strlen(number), ASN1_TAG_CONTEXT_SPECIFIC);
	  p += enc_sequence_end(p, seq2);
	  p += enc_bool(p, pres);
    p += enc_sequence_end(p, seq1);
	
	*body_len = p - &body_len[1];
	
	return p - dest;
}

static void enc_ie_facility (__u8 **ntmode, msg_t *msg, __u8 *facility, int facility_len, struct misdn_bchannel *bc)
{
	__u8 *ie_fac;
	
	Q931_info_t *qi;

	ie_fac = msg_put(msg, facility_len + 2);
	if (bc->nt) {
		*ntmode = ie_fac + 1;
	} else {
		qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
		qi->QI_ELEMENT(facility) = ie_fac - (__u8 *)qi - sizeof(Q931_info_t);
	}

	ie_fac[0] = IE_FACILITY;
	ie_fac[1] = facility_len;
	memcpy(ie_fac + 2, facility, facility_len);

	FAC_DUMP(ie_fac, facility_len + 2, bc);
}

void fac_enc (__u8 **ntmsg, msg_t *msg, enum facility_type type,  union facility fac, struct misdn_bchannel *bc)
{
	__u8 facility[256];
	int len;

	switch (type) {
	case FACILITY_CALLDEFLECT:
		len = enc_fac_calldeflect(facility, fac.calldeflect_nr, 1);
		enc_ie_facility(ntmsg, msg, facility, len, bc);
		break;
	case FACILITY_CENTREX:
	case FACILITY_NONE:
		break;
	}
}

/*
** Facility Decoding
*/

static int dec_fac_calldeflect (__u8 *p, int len, struct misdn_bchannel *bc)
{
	__u8 *end = p + len;
	int offset,
		pres;

	if ((offset = dec_sequence(p, end)) < 0)
		return -1;
	p += offset;

	if ((offset = dec_sequence(p, end)) < 0)
		return -1;
	p += offset;
	
	if ((offset = dec_num_string(p, end, bc->fac.calldeflect_nr)) < 0)
		return -1;
	p += offset;

	if ((offset = dec_bool(p, end, &pres)) < 0)
		return -1;

	cb_log(0, 0, "CALLDEFLECT: dest:%s pres:%s (not implemented yet)\n", bc->fac.calldeflect_nr, pres ? "yes" : "no");
	bc->fac_type = FACILITY_CALLDEFLECT;

	return 0;
}

void fac_dec (__u8 *p, Q931_info_t *qi, enum facility_type *type,  union facility *fac, struct misdn_bchannel *bc)
{
	int len,
		offset,
		inner_len,
		invoke_id,
		op_tag,
		op_val;
	__u8 *end,
				  *begin = p;

	if (!bc->nt) {
		if (qi->QI_ELEMENT(facility))
			p = (__u8 *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(facility) + 1;
		else
			p = NULL;
	}
	if (!p)
		return;

	offset = dec_len (p, &len);
	if (offset < 0) {
		cb_log(0, bc->port, "Could not decode FACILITY: dec_len failed!\n");
		return;
	}
	p += offset;
	end = p + len;

	FAC_DUMP(p, len, bc);

	if (len < 3 || p[0] != SUPPLEMENTARY_SERVICE || p[1] != INVOKE) {
		cb_log(0, bc->port, "Could not decode FACILITY: invalid or not supported!\n");
		return;
	}
	p += 2;

	offset = dec_len (p, &inner_len);
	if (offset < 0) {
		cb_log(0, bc->port, "Could not decode FACILITY: failed parsing inner length!\n");
		return;
	}
	p += offset;

	offset = dec_int (p, end, &invoke_id);
	if (offset < 0) {
		cb_log(0, bc->port, "Could not decode FACILITY: failed parsing invoke identifier!\n");
		return;
	}
	p += offset;

	offset = _dec_int (p, end, &op_val, &op_tag);
	if (offset < 0) {
		cb_log(0, bc->port, "Could not decode FACILITY: failed parsing operation value!\n");
		return;
	}
	p += offset;

	if (op_tag != OPERATION_VALUE || offset != 3) {
		cb_log(0, bc->port, "Could not decode FACILITY: operation value tag 0x%x unknown!\n", op_tag);
		return;
	}

	switch (op_val) {
	case CALL_DEFLECT:
		cb_log(0, bc->port, "FACILITY: Call Deflect\n");
		dec_fac_calldeflect(p, len - (p - begin) + 1, bc);
		break;
	case AOC:
		cb_log(0, bc->port, "FACILITY: AOC\n");
		break;
	default:
		cb_log(0, bc->port, "FACILITY unknown: operation value 0x%x, ignoring ...\n", op_val);
	}
}
