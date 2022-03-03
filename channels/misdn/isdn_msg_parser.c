/*
 * Chan_Misdn -- Channel Driver for Asterisk
 *
 * Interface to mISDN
 *
 * Copyright (C) 2004, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief Interface to mISDN - message parser
 * \author Christian Richter <crich@beronet.com>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "isdn_lib_intern.h"


#include "isdn_lib.h"

#include "ie.c"

/*!
 * \internal
 * \brief Build the name, number, name/number display message string
 *
 * \param display Display buffer to fill in
 * \param display_length Length of the display buffer to fill in
 * \param display_format Display format enumeration
 * \param name Name string to use
 * \param number Number string to use
 */
static void build_display_str(char *display, size_t display_length, int display_format, const char *name, const char *number)
{
	display[0] = 0;
	switch (display_format) {
	default:
	case 0:		/* none */
		break;

	case 1:		/* name */
		snprintf(display, display_length, "%s", name);
		break;

	case 2:		/* number */
		snprintf(display, display_length, "%s", number);
		break;

	case 3:		/* both */
		if (name[0] || number[0]) {
			snprintf(display, display_length, "\"%s\" <%s>", name, number);
		}
		break;
	}
}

/*!
 * \internal
 * \brief Encode the Facility IE and put it into the message structure.
 *
 * \param ntmode Where the encoded facility was put when in NT mode.
 * \param msg General message structure
 * \param fac Data to encode into the facility ie.
 * \param nt TRUE if in NT mode.
 */
static void enc_ie_facility(unsigned char **ntmode, msg_t *msg, struct FacParm *fac, int nt)
{
	int len;
	Q931_info_t *qi;
	unsigned char *p;
	unsigned char buf[256];

	len = encodeFac(buf, fac);
	if (len <= 0) {
		/*
		 * mISDN does not know how to build the requested facility structure
		 * Clear facility information
		 */
		fac->Function = Fac_None;
		return;
	}

	p = msg_put(msg, len);
	if (nt) {
		*ntmode = p + 1;
	} else {
		qi = (Q931_info_t *) (msg->data + mISDN_HEADER_LEN);
		qi->QI_ELEMENT(facility) = p - (unsigned char *) qi - sizeof(Q931_info_t);
	}

	memcpy(p, buf, len);

	/* Clear facility information */
	fac->Function = Fac_None;
}

/*!
 * \internal
 * \brief Decode the Facility IE.
 *
 * \param p Encoded facility ie data to decode. (NT mode)
 * \param qi Encoded facility ie data to decode. (TE mode)
 * \param fac Where to put the decoded facility ie data if it is available.
 * \param nt TRUE if in NT mode.
 * \param bc Associated B channel
 */
static void dec_ie_facility(unsigned char *p, Q931_info_t *qi, struct FacParm *fac, int nt, struct misdn_bchannel *bc)
{
	fac->Function = Fac_None;

	if (!nt) {
		p = NULL;
		if (qi->QI_ELEMENT(facility)) {
			p = (unsigned char *) qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(facility) + 1;
		}
	}
	if (!p) {
		return;
	}

	if (decodeFac(p, fac)) {
		cb_log(3, bc->port, "Decoding facility ie failed! Unrecognized facility message?\n");
	}
}



static void set_channel(struct misdn_bchannel *bc, int channel)
{

	cb_log(3,bc->port,"set_channel: bc->channel:%d channel:%d\n", bc->channel, channel);


	if (channel==0xff) {
		/* any channel */
		channel=-1;
	}

	/*  ALERT: is that everytime true ?  */
	if (channel > 0 && bc->nt ) {

		if (bc->channel && ( bc->channel != 0xff) ) {
			cb_log(0,bc->port,"We already have a channel (%d)\n", bc->channel);
		} else {
			bc->channel = channel;
			cb_event(EVENT_NEW_CHANNEL,bc,NULL);
		}
	}

	if (channel > 0 && !bc->nt ) {
		bc->channel = channel;
		cb_event(EVENT_NEW_CHANNEL,bc,NULL);
	}
}

static void parse_proceeding (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CALL_PROCEEDING_t *proceeding = (CALL_PROCEEDING_t *) (msg->data + HEADER_LEN);
	//struct misdn_stack *stack=get_stack_by_bc(bc);

	{
		int  exclusive, channel;
		dec_ie_channel_id(proceeding->CHANNEL_ID, (Q931_info_t *)proceeding, &exclusive, &channel, nt,bc);

		set_channel(bc,channel);

	}

	dec_ie_progress(proceeding->PROGRESS, (Q931_info_t *)proceeding, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);

	dec_ie_facility(proceeding->FACILITY, (Q931_info_t *) proceeding, &bc->fac_in, nt, bc);

	/* dec_ie_redir_dn */

#ifdef DEBUG
	printf("Parsing PROCEEDING Msg\n");
#endif
}
static msg_t *build_proceeding (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CALL_PROCEEDING_t *proceeding;
	msg_t *msg =(msg_t*)create_l3msg(CC_PROCEEDING | REQUEST, MT_CALL_PROCEEDING,  bc?bc->l3_id:-1, sizeof(CALL_PROCEEDING_t) ,nt);

	proceeding=(CALL_PROCEEDING_t*)((msg->data+HEADER_LEN));

	enc_ie_channel_id(&proceeding->CHANNEL_ID, msg, 1,bc->channel, nt,bc);

	if (nt)
		enc_ie_progress(&proceeding->PROGRESS, msg, 0, nt?1:5, 8, nt,bc);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&proceeding->FACILITY, msg, &bc->fac_out, nt);
	}

	/* enc_ie_redir_dn */

#ifdef DEBUG
	printf("Building PROCEEDING Msg\n");
#endif
	return msg;
}

static void parse_alerting (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	ALERTING_t *alerting = (ALERTING_t *) (msg->data + HEADER_LEN);
	//Q931_info_t *qi=(Q931_info_t*)(msg->data+HEADER_LEN);

	dec_ie_facility(alerting->FACILITY, (Q931_info_t *) alerting, &bc->fac_in, nt, bc);

	/* dec_ie_redir_dn */

	dec_ie_progress(alerting->PROGRESS, (Q931_info_t *)alerting, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);

#ifdef DEBUG
	printf("Parsing ALERTING Msg\n");
#endif


}

static msg_t *build_alerting (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	ALERTING_t *alerting;
	msg_t *msg =(msg_t*)create_l3msg(CC_ALERTING | REQUEST, MT_ALERTING,  bc?bc->l3_id:-1, sizeof(ALERTING_t) ,nt);

	alerting=(ALERTING_t*)((msg->data+HEADER_LEN));

	enc_ie_channel_id(&alerting->CHANNEL_ID, msg, 1,bc->channel, nt,bc);

	if (nt)
		enc_ie_progress(&alerting->PROGRESS, msg, 0, nt?1:5, 8, nt,bc);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&alerting->FACILITY, msg, &bc->fac_out, nt);
	}

	/* enc_ie_redir_dn */

#ifdef DEBUG
	printf("Building ALERTING Msg\n");
#endif
	return msg;
}


static void parse_progress (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	PROGRESS_t *progress = (PROGRESS_t *) (msg->data + HEADER_LEN);
	//Q931_info_t *qi=(Q931_info_t*)(msg->data+HEADER_LEN);

	dec_ie_progress(progress->PROGRESS, (Q931_info_t *)progress, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);

	dec_ie_facility(progress->FACILITY, (Q931_info_t *) progress, &bc->fac_in, nt, bc);

#ifdef DEBUG
	printf("Parsing PROGRESS Msg\n");
#endif
}

static msg_t *build_progress (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	PROGRESS_t *progress;
	msg_t *msg =(msg_t*)create_l3msg(CC_PROGRESS | REQUEST, MT_PROGRESS,  bc?bc->l3_id:-1, sizeof(PROGRESS_t) ,nt);

	progress=(PROGRESS_t*)((msg->data+HEADER_LEN));

	enc_ie_progress(&progress->PROGRESS, msg, 0, nt ? 1 : 5, 8, nt, bc);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&progress->FACILITY, msg, &bc->fac_out, nt);
	}

#ifdef DEBUG
	printf("Building PROGRESS Msg\n");
#endif
	return msg;
}

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Extract the SETUP message's BC, HLC, and LLC encoded ie contents.
 *
 * \param setup Indexed setup message contents
 * \param nt TRUE if in NT mode.
 * \param bc Associated B channel
 */
static void extract_setup_Bc_Hlc_Llc(SETUP_t *setup, int nt, struct misdn_bchannel *bc)
{
	__u8 *p;
	Q931_info_t *qi;

	qi = (Q931_info_t *) setup;

	/* Extract Bearer Capability */
	if (nt) {
		p = (__u8 *) setup->BEARER;
	} else {
		if (qi->QI_ELEMENT(bearer_capability)) {
			p = (__u8 *) qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(bearer_capability) + 1;
		} else {
			p = NULL;
		}
	}
	if (!p || *p == 0 || sizeof(bc->setup_bc_hlc_llc.Bc.Contents) < *p) {
		bc->setup_bc_hlc_llc.Bc.Length = 0;
	} else {
		bc->setup_bc_hlc_llc.Bc.Length = *p;
		memcpy(bc->setup_bc_hlc_llc.Bc.Contents, p + 1, *p);
	}

	/* Extract Low Layer Compatibility */
	if (nt) {
		p = (__u8 *) setup->LLC;
	} else {
		if (qi->QI_ELEMENT(llc)) {
			p = (__u8 *) qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(llc) + 1;
		} else {
			p = NULL;
		}
	}
	if (!p || *p == 0 || sizeof(bc->setup_bc_hlc_llc.Llc.Contents) < *p) {
		bc->setup_bc_hlc_llc.Llc.Length = 0;
	} else {
		bc->setup_bc_hlc_llc.Llc.Length = *p;
		memcpy(bc->setup_bc_hlc_llc.Llc.Contents, p + 1, *p);
	}

	/* Extract High Layer Compatibility */
	if (nt) {
		p = (__u8 *) setup->HLC;
	} else {
		if (qi->QI_ELEMENT(hlc)) {
			p = (__u8 *) qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(hlc) + 1;
		} else {
			p = NULL;
		}
	}
	if (!p || *p == 0 || sizeof(bc->setup_bc_hlc_llc.Hlc.Contents) < *p) {
		bc->setup_bc_hlc_llc.Hlc.Length = 0;
	} else {
		bc->setup_bc_hlc_llc.Hlc.Length = *p;
		memcpy(bc->setup_bc_hlc_llc.Hlc.Contents, p + 1, *p);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

static void parse_setup (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_t *setup = (SETUP_t *) (msg->data + HEADER_LEN);
	Q931_info_t *qi = (Q931_info_t *) (msg->data + HEADER_LEN);
	int type;
	int plan;
	int present;
	int screen;
	int reason;

#ifdef DEBUG
	printf("Parsing SETUP Msg\n");
#endif

	dec_ie_calling_pn(setup->CALLING_PN, qi, &type, &plan, &present, &screen, bc->caller.number, sizeof(bc->caller.number), nt, bc);
	bc->caller.number_type = type;
	bc->caller.number_plan = plan;
	switch (present) {
	default:
	case 0:
		bc->caller.presentation = 0;	/* presentation allowed */
		break;
	case 1:
		bc->caller.presentation = 1;	/* presentation restricted */
		break;
	case 2:
		bc->caller.presentation = 2;	/* Number not available */
		break;
	}
	if (0 <= screen) {
		bc->caller.screening = screen;
	} else {
		bc->caller.screening = 0;	/* Unscreened */
	}

	dec_ie_facility(setup->FACILITY, (Q931_info_t *) setup, &bc->fac_in, nt, bc);

	dec_ie_called_pn(setup->CALLED_PN, (Q931_info_t *) setup, &type, &plan, bc->dialed.number, sizeof(bc->dialed.number), nt, bc);
	bc->dialed.number_type = type;
	bc->dialed.number_plan = plan;

	dec_ie_keypad(setup->KEYPAD, (Q931_info_t *) setup, bc->keypad, sizeof(bc->keypad), nt, bc);

	dec_ie_complete(setup->COMPLETE, (Q931_info_t *) setup, &bc->sending_complete, nt, bc);

	dec_ie_redir_nr(setup->REDIR_NR, (Q931_info_t *) setup, &type, &plan, &present, &screen, &reason, bc->redirecting.from.number, sizeof(bc->redirecting.from.number), nt, bc);
	bc->redirecting.from.number_type = type;
	bc->redirecting.from.number_plan = plan;
	switch (present) {
	default:
	case 0:
		bc->redirecting.from.presentation = 0;	/* presentation allowed */
		break;
	case 1:
		bc->redirecting.from.presentation = 1;	/* presentation restricted */
		break;
	case 2:
		bc->redirecting.from.presentation = 2;	/* Number not available */
		break;
	}
	if (0 <= screen) {
		bc->redirecting.from.screening = screen;
	} else {
		bc->redirecting.from.screening = 0;	/* Unscreened */
	}
	if (0 <= reason) {
		bc->redirecting.reason = reason;
	} else {
		bc->redirecting.reason = mISDN_REDIRECTING_REASON_UNKNOWN;
	}

	{
		int  coding, capability, mode, rate, multi, user, async, urate, stopbits, dbits, parity;

		dec_ie_bearer(setup->BEARER, (Q931_info_t *)setup, &coding, &capability, &mode, &rate, &multi, &user, &async, &urate, &stopbits, &dbits, &parity, nt,bc);
		switch (capability) {
		case -1: bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			break;
		case 0: bc->capability=INFO_CAPABILITY_SPEECH;
			break;
		case 18: bc->capability=INFO_CAPABILITY_VIDEO;
			break;
		case 8: bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			bc->user1 = user;
			bc->urate = urate;

			bc->rate = rate;
			bc->mode = mode;
			break;
		case 9: bc->capability=INFO_CAPABILITY_DIGITAL_RESTRICTED;
			break;
		default:
			break;
		}

		switch(user) {
		case 2:
			bc->law=INFO_CODEC_ULAW;
			break;
		case 3:
			bc->law=INFO_CODEC_ALAW;
			break;
		default:
			bc->law=INFO_CODEC_ALAW;

		}

		bc->capability=capability;
	}
	{
		int  exclusive, channel;
		dec_ie_channel_id(setup->CHANNEL_ID, (Q931_info_t *)setup, &exclusive, &channel, nt,bc);

		set_channel(bc,channel);
	}

	{
		int  protocol ;
		dec_ie_useruser(setup->USER_USER, (Q931_info_t *)setup, &protocol, bc->uu, &bc->uulen, nt,bc);
		if (bc->uulen) cb_log(1, bc->port, "USERUSERINFO:%s\n", bc->uu);
		else
		cb_log(1, bc->port, "NO USERUSERINFO\n");
	}

	dec_ie_progress(setup->PROGRESS, (Q931_info_t *)setup, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);

#if defined(AST_MISDN_ENHANCEMENTS)
	extract_setup_Bc_Hlc_Llc(setup, nt, bc);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
}

#define ANY_CHANNEL 0xff /* IE attribute for 'any channel' */
static msg_t *build_setup (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_t *setup;
	msg_t *msg =(msg_t*)create_l3msg(CC_SETUP | REQUEST, MT_SETUP,  bc?bc->l3_id:-1, sizeof(SETUP_t) ,nt);
	int is_ptp;
	enum FacFunction fac_type;

	setup=(SETUP_t*)((msg->data+HEADER_LEN));

	if (bc->channel == 0 || bc->channel == ANY_CHANNEL || bc->channel==-1)
		enc_ie_channel_id(&setup->CHANNEL_ID, msg, 0, bc->channel, nt,bc);
	else
		enc_ie_channel_id(&setup->CHANNEL_ID, msg, 1, bc->channel, nt,bc);

	fac_type = bc->fac_out.Function;
	if (fac_type != Fac_None) {
		enc_ie_facility(&setup->FACILITY, msg, &bc->fac_out, nt);
	}

	enc_ie_calling_pn(&setup->CALLING_PN, msg, bc->caller.number_type, bc->caller.number_plan,
		bc->caller.presentation, bc->caller.screening, bc->caller.number, nt, bc);

	if (bc->dialed.number[0]) {
		enc_ie_called_pn(&setup->CALLED_PN, msg, bc->dialed.number_type, bc->dialed.number_plan, bc->dialed.number, nt, bc);
	}

	switch (bc->outgoing_colp) {
	case 0:/* pass */
	case 1:/* restricted */
		is_ptp = misdn_lib_is_ptp(bc->port);
		if (bc->redirecting.from.number[0]
			&& ((!is_ptp && nt)
				|| (is_ptp
#if defined(AST_MISDN_ENHANCEMENTS)
					/*
					 * There is no need to send out this ie when we are also sending
					 * a Fac_DivertingLegInformation2 as well.  The
					 * Fac_DivertingLegInformation2 supercedes the information in
					 * this ie.
					 */
					&& fac_type != Fac_DivertingLegInformation2
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
			))) {
#if 1
			/* ETSI and Q.952 do not define the screening field */
			enc_ie_redir_nr(&setup->REDIR_NR, msg, bc->redirecting.from.number_type,
				bc->redirecting.from.number_plan, bc->redirecting.from.presentation, 0,
				bc->redirecting.reason, bc->redirecting.from.number, nt, bc);
#else
			/* Q.931 defines the screening field */
			enc_ie_redir_nr(&setup->REDIR_NR, msg, bc->redirecting.from.number_type,
				bc->redirecting.from.number_plan, bc->redirecting.from.presentation,
				bc->redirecting.from.screening, bc->redirecting.reason,
				bc->redirecting.from.number, nt, bc);
#endif
		}
		break;
	default:
		break;
	}

	if (bc->keypad[0]) {
		enc_ie_keypad(&setup->KEYPAD, msg, bc->keypad, nt,bc);
	}



	if (*bc->display) {
		enc_ie_display(&setup->DISPLAY, msg, bc->display, nt, bc);
	} else if (nt && bc->caller.presentation == 0) {
		char display[sizeof(bc->display)];

		/* Presentation is allowed */
		build_display_str(display, sizeof(display), bc->display_setup, bc->caller.name, bc->caller.number);
		if (display[0]) {
			enc_ie_display(&setup->DISPLAY, msg, display, nt, bc);
		}
	}

	{
		int coding = 0;
		int capability;
		int mode = 0;	/* 2 for packet! */
		int user;
		int rate = 0x10;

		switch (bc->law) {
		case INFO_CODEC_ULAW: user=2;
			break;
		case INFO_CODEC_ALAW: user=3;
			break;
		default:
			user=3;
		}

		switch (bc->capability) {
		case INFO_CAPABILITY_SPEECH: capability = 0;
			break;
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED: capability = 8;
			user=-1;
			mode=bc->mode;
			rate=bc->rate;
			break;
		case INFO_CAPABILITY_DIGITAL_RESTRICTED: capability = 9;
			user=-1;
			break;
		default:
			capability=bc->capability;
		}

		enc_ie_bearer(&setup->BEARER, msg, coding, capability, mode, rate, -1, user, nt,bc);
	}

	if (bc->sending_complete) {
		enc_ie_complete(&setup->COMPLETE,msg, bc->sending_complete, nt, bc);
	}

	if (bc->uulen) {
		int  protocol=4;
		enc_ie_useruser(&setup->USER_USER, msg, protocol, bc->uu, bc->uulen, nt,bc);
		cb_log(1, bc->port, "ENCODING USERUSERINFO:%s\n", bc->uu);
	}

#if defined(AST_MISDN_ENHANCEMENTS)
	extract_setup_Bc_Hlc_Llc(setup, nt, bc);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#ifdef DEBUG
	printf("Building SETUP Msg\n");
#endif
	return msg;
}

static void parse_connect (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CONNECT_t *connect = (CONNECT_t *) (msg->data + HEADER_LEN);
	int type;
	int plan;
	int pres;
	int screen;

	bc->ces = connect->ces;

	dec_ie_progress(connect->PROGRESS, (Q931_info_t *)connect, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);

	dec_ie_connected_pn(connect->CONNECT_PN, (Q931_info_t *) connect, &type, &plan,
		&pres, &screen, bc->connected.number, sizeof(bc->connected.number), nt, bc);
	bc->connected.number_type = type;
	bc->connected.number_plan = plan;
	switch (pres) {
	default:
	case 0:
		bc->connected.presentation = 0;	/* presentation allowed */
		break;
	case 1:
		bc->connected.presentation = 1;	/* presentation restricted */
		break;
	case 2:
		bc->connected.presentation = 2;	/* Number not available */
		break;
	}
	if (0 <= screen) {
		bc->connected.screening = screen;
	} else {
		bc->connected.screening = 0;	/* Unscreened */
	}

	dec_ie_facility(connect->FACILITY, (Q931_info_t *) connect, &bc->fac_in, nt, bc);

	/*
		cb_log(1,bc->port,"CONNETED PN: %s cpn_dialplan:%d\n", connected_pn, type);
	*/

#ifdef DEBUG
	printf("Parsing CONNECT Msg\n");
#endif
}

static msg_t *build_connect (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CONNECT_t *connect;
	msg_t *msg =(msg_t*)create_l3msg(CC_CONNECT | REQUEST, MT_CONNECT,  bc?bc->l3_id:-1, sizeof(CONNECT_t) ,nt);

	cb_log(6,bc->port,"BUILD_CONNECT: bc:%p bc->l3id:%d, nt:%d\n",bc,bc->l3_id,nt);

	connect=(CONNECT_t*)((msg->data+HEADER_LEN));

	if (nt) {
		time_t now;
		time(&now);
		enc_ie_date(&connect->DATE, msg, now, nt,bc);
	}

	switch (bc->outgoing_colp) {
	case 0:/* pass */
	case 1:/* restricted */
		enc_ie_connected_pn(&connect->CONNECT_PN, msg, bc->connected.number_type,
			bc->connected.number_plan, bc->connected.presentation,
			bc->connected.screening, bc->connected.number, nt, bc);
		break;
	default:
		break;
	}

	if (nt && bc->connected.presentation == 0) {
		char display[sizeof(bc->display)];

		/* Presentation is allowed */
		build_display_str(display, sizeof(display), bc->display_connected, bc->connected.name, bc->connected.number);
		if (display[0]) {
			enc_ie_display(&connect->DISPLAY, msg, display, nt, bc);
		}
	}

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&connect->FACILITY, msg, &bc->fac_out, nt);
	}

#ifdef DEBUG
	printf("Building CONNECT Msg\n");
#endif
	return msg;
}

static void parse_setup_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_ACKNOWLEDGE_t *setup_acknowledge = (SETUP_ACKNOWLEDGE_t *) (msg->data + HEADER_LEN);

	{
		int  exclusive, channel;
		dec_ie_channel_id(setup_acknowledge->CHANNEL_ID, (Q931_info_t *)setup_acknowledge, &exclusive, &channel, nt,bc);


		set_channel(bc, channel);
	}

	dec_ie_progress(setup_acknowledge->PROGRESS, (Q931_info_t *)setup_acknowledge, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);

	dec_ie_facility(setup_acknowledge->FACILITY, (Q931_info_t *) setup_acknowledge, &bc->fac_in, nt, bc);

#ifdef DEBUG
	printf("Parsing SETUP_ACKNOWLEDGE Msg\n");
#endif


}

static msg_t *build_setup_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_ACKNOWLEDGE_t *setup_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_SETUP_ACKNOWLEDGE | REQUEST, MT_SETUP_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(SETUP_ACKNOWLEDGE_t) ,nt);

	setup_acknowledge=(SETUP_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN));

	enc_ie_channel_id(&setup_acknowledge->CHANNEL_ID, msg, 1,bc->channel, nt,bc);

	if (nt)
		enc_ie_progress(&setup_acknowledge->PROGRESS, msg, 0, nt?1:5, 8, nt,bc);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&setup_acknowledge->FACILITY, msg, &bc->fac_out, nt);
	}

#ifdef DEBUG
	printf("Building SETUP_ACKNOWLEDGE Msg\n");
#endif
	return msg;
}

static void parse_connect_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing CONNECT_ACKNOWLEDGE Msg\n");
#endif


}

static msg_t *build_connect_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CONNECT_ACKNOWLEDGE_t *connect_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_CONNECT | RESPONSE, MT_CONNECT,  bc?bc->l3_id:-1, sizeof(CONNECT_ACKNOWLEDGE_t) ,nt);

	connect_acknowledge=(CONNECT_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN));

	enc_ie_channel_id(&connect_acknowledge->CHANNEL_ID, msg, 1, bc->channel, nt,bc);

#ifdef DEBUG
	printf("Building CONNECT_ACKNOWLEDGE Msg\n");
#endif
	return msg;
}

static void parse_user_information (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing USER_INFORMATION Msg\n");
#endif


}

static msg_t *build_user_information (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_USER_INFORMATION | REQUEST, MT_USER_INFORMATION,  bc?bc->l3_id:-1, sizeof(USER_INFORMATION_t) ,nt);

#ifdef DEBUG
	printf("Building USER_INFORMATION Msg\n");
#endif
	return msg;
}

static void parse_suspend_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing SUSPEND_REJECT Msg\n");
#endif


}

static msg_t *build_suspend_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_SUSPEND_REJECT | REQUEST, MT_SUSPEND_REJECT,  bc?bc->l3_id:-1, sizeof(SUSPEND_REJECT_t) ,nt);

#ifdef DEBUG
	printf("Building SUSPEND_REJECT Msg\n");
#endif
	return msg;
}

static void parse_resume_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing RESUME_REJECT Msg\n");
#endif


}

static msg_t *build_resume_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_RESUME_REJECT | REQUEST, MT_RESUME_REJECT,  bc?bc->l3_id:-1, sizeof(RESUME_REJECT_t) ,nt);

#ifdef DEBUG
	printf("Building RESUME_REJECT Msg\n");
#endif
	return msg;
}

static void parse_hold (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing HOLD Msg\n");
#endif


}

static msg_t *build_hold (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_HOLD | REQUEST, MT_HOLD,  bc?bc->l3_id:-1, sizeof(HOLD_t) ,nt);

#ifdef DEBUG
	printf("Building HOLD Msg\n");
#endif
	return msg;
}

static void parse_suspend (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing SUSPEND Msg\n");
#endif


}

static msg_t *build_suspend (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_SUSPEND | REQUEST, MT_SUSPEND,  bc?bc->l3_id:-1, sizeof(SUSPEND_t) ,nt);

#ifdef DEBUG
	printf("Building SUSPEND Msg\n");
#endif
	return msg;
}

static void parse_resume (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing RESUME Msg\n");
#endif


}

static msg_t *build_resume (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_RESUME | REQUEST, MT_RESUME,  bc?bc->l3_id:-1, sizeof(RESUME_t) ,nt);

#ifdef DEBUG
	printf("Building RESUME Msg\n");
#endif
	return msg;
}

static void parse_hold_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing HOLD_ACKNOWLEDGE Msg\n");
#endif


}

static msg_t *build_hold_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_HOLD_ACKNOWLEDGE | REQUEST, MT_HOLD_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(HOLD_ACKNOWLEDGE_t) ,nt);

#ifdef DEBUG
	printf("Building HOLD_ACKNOWLEDGE Msg\n");
#endif
	return msg;
}

static void parse_suspend_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing SUSPEND_ACKNOWLEDGE Msg\n");
#endif


}

static msg_t *build_suspend_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_SUSPEND_ACKNOWLEDGE | REQUEST, MT_SUSPEND_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(SUSPEND_ACKNOWLEDGE_t) ,nt);

#ifdef DEBUG
	printf("Building SUSPEND_ACKNOWLEDGE Msg\n");
#endif
	return msg;
}

static void parse_resume_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing RESUME_ACKNOWLEDGE Msg\n");
#endif


}

static msg_t *build_resume_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_RESUME_ACKNOWLEDGE | REQUEST, MT_RESUME_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(RESUME_ACKNOWLEDGE_t) ,nt);

#ifdef DEBUG
	printf("Building RESUME_ACKNOWLEDGE Msg\n");
#endif
	return msg;
}

static void parse_hold_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing HOLD_REJECT Msg\n");
#endif


}

static msg_t *build_hold_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_HOLD_REJECT | REQUEST, MT_HOLD_REJECT,  bc?bc->l3_id:-1, sizeof(HOLD_REJECT_t) ,nt);

#ifdef DEBUG
	printf("Building HOLD_REJECT Msg\n");
#endif
	return msg;
}

static void parse_retrieve (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing RETRIEVE Msg\n");
#endif


}

static msg_t *build_retrieve (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_RETRIEVE | REQUEST, MT_RETRIEVE,  bc?bc->l3_id:-1, sizeof(RETRIEVE_t) ,nt);

#ifdef DEBUG
	printf("Building RETRIEVE Msg\n");
#endif
	return msg;
}

static void parse_retrieve_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing RETRIEVE_ACKNOWLEDGE Msg\n");
#endif


}

static msg_t *build_retrieve_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RETRIEVE_ACKNOWLEDGE_t *retrieve_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_RETRIEVE_ACKNOWLEDGE | REQUEST, MT_RETRIEVE_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(RETRIEVE_ACKNOWLEDGE_t) ,nt);

	retrieve_acknowledge=(RETRIEVE_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN));

	enc_ie_channel_id(&retrieve_acknowledge->CHANNEL_ID, msg, 1, bc->channel, nt,bc);
#ifdef DEBUG
	printf("Building RETRIEVE_ACKNOWLEDGE Msg\n");
#endif
	return msg;
}

static void parse_retrieve_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing RETRIEVE_REJECT Msg\n");
#endif


}

static msg_t *build_retrieve_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_RETRIEVE_REJECT | REQUEST, MT_RETRIEVE_REJECT,  bc?bc->l3_id:-1, sizeof(RETRIEVE_REJECT_t) ,nt);

#ifdef DEBUG
	printf("Building RETRIEVE_REJECT Msg\n");
#endif
	return msg;
}

static void parse_disconnect (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	DISCONNECT_t *disconnect = (DISCONNECT_t *) (msg->data + HEADER_LEN);
	int location;
 	int cause;
	dec_ie_cause(disconnect->CAUSE, (Q931_info_t *)(disconnect), &location, &cause, nt,bc);
	if (cause>0) bc->cause=cause;

	dec_ie_facility(disconnect->FACILITY, (Q931_info_t *) disconnect, &bc->fac_in, nt, bc);

	dec_ie_progress(disconnect->PROGRESS, (Q931_info_t *)disconnect, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
#ifdef DEBUG
	printf("Parsing DISCONNECT Msg\n");
#endif


}

static msg_t *build_disconnect (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	DISCONNECT_t *disconnect;
	msg_t *msg =(msg_t*)create_l3msg(CC_DISCONNECT | REQUEST, MT_DISCONNECT,  bc?bc->l3_id:-1, sizeof(DISCONNECT_t) ,nt);

	disconnect=(DISCONNECT_t*)((msg->data+HEADER_LEN));

	enc_ie_cause(&disconnect->CAUSE, msg, (nt)?1:0, bc->out_cause,nt,bc);
	if (nt) {
		enc_ie_progress(&disconnect->PROGRESS, msg, 0, nt ? 1 : 5, 8, nt, bc);
	}

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&disconnect->FACILITY, msg, &bc->fac_out, nt);
	}

	if (bc->uulen) {
		int  protocol=4;
		enc_ie_useruser(&disconnect->USER_USER, msg, protocol, bc->uu, bc->uulen, nt,bc);
		cb_log(1, bc->port, "ENCODING USERUSERINFO:%s\n", bc->uu);
	}

#ifdef DEBUG
	printf("Building DISCONNECT Msg\n");
#endif
	return msg;
}

static void parse_restart (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESTART_t *restart = (RESTART_t *) (msg->data + HEADER_LEN);

	struct misdn_stack *stack=get_stack_by_bc(bc);

#ifdef DEBUG
	printf("Parsing RESTART Msg\n");
#endif

	{
		int  exclusive;
		dec_ie_channel_id(restart->CHANNEL_ID, (Q931_info_t *)restart, &exclusive, &bc->restart_channel, nt,bc);
		cb_log(3, stack->port, "CC_RESTART Request on channel:%d on this port.\n", bc->restart_channel);
	}

}

static msg_t *build_restart (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESTART_t *restart;
	msg_t *msg =(msg_t*)create_l3msg(CC_RESTART | REQUEST, MT_RESTART,  bc?bc->l3_id:-1, sizeof(RESTART_t) ,nt);

	restart=(RESTART_t*)((msg->data+HEADER_LEN));

#ifdef DEBUG
	printf("Building RESTART Msg\n");
#endif

	if (bc->channel > 0) {
		enc_ie_channel_id(&restart->CHANNEL_ID, msg, 1,bc->channel, nt,bc);
		enc_ie_restart_ind(&restart->RESTART_IND, msg, 0x80, nt, bc);
	} else {
		enc_ie_restart_ind(&restart->RESTART_IND, msg, 0x87, nt, bc);
	}

	cb_log(0,bc->port, "Restarting channel %d\n", bc->channel);
	return msg;
}

static void parse_release (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_t *release = (RELEASE_t *) (msg->data + HEADER_LEN);
	int location;
	int cause;

	dec_ie_cause(release->CAUSE, (Q931_info_t *)(release), &location, &cause, nt,bc);
	if (cause>0) bc->cause=cause;

	dec_ie_facility(release->FACILITY, (Q931_info_t *) release, &bc->fac_in, nt, bc);

#ifdef DEBUG
	printf("Parsing RELEASE Msg\n");
#endif


}

static msg_t *build_release (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_t *release;
	msg_t *msg =(msg_t*)create_l3msg(CC_RELEASE | REQUEST, MT_RELEASE,  bc?bc->l3_id:-1, sizeof(RELEASE_t) ,nt);

	release=(RELEASE_t*)((msg->data+HEADER_LEN));

	if (bc->out_cause>= 0)
		enc_ie_cause(&release->CAUSE, msg, nt?1:0, bc->out_cause, nt,bc);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&release->FACILITY, msg, &bc->fac_out, nt);
	}

	if (bc->uulen) {
		int  protocol=4;
		enc_ie_useruser(&release->USER_USER, msg, protocol, bc->uu, bc->uulen, nt,bc);
		cb_log(1, bc->port, "ENCODING USERUSERINFO:%s\n", bc->uu);
	}

#ifdef DEBUG
	printf("Building RELEASE Msg\n");
#endif
	return msg;
}

static void parse_release_complete (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_COMPLETE_t *release_complete = (RELEASE_COMPLETE_t *) (msg->data + HEADER_LEN);
	int location;
	int cause;
	iframe_t *frm = (iframe_t*) msg->data;

	struct misdn_stack *stack=get_stack_by_bc(bc);
	mISDNuser_head_t *hh;
	hh=(mISDNuser_head_t*)msg->data;

	/*hh=(mISDN_head_t*)msg->data;
	mISDN_head_t *hh;*/

	if (nt) {
		if (hh->prim == (CC_RELEASE_COMPLETE|CONFIRM)) {
			cb_log(0, stack->port, "CC_RELEASE_COMPLETE|CONFIRM [NT] \n");
			return;
		}
	} else {
		if (frm->prim == (CC_RELEASE_COMPLETE|CONFIRM)) {
			cb_log(0, stack->port, "CC_RELEASE_COMPLETE|CONFIRM [TE] \n");
			return;
		}
	}
	dec_ie_cause(release_complete->CAUSE, (Q931_info_t *)(release_complete), &location, &cause, nt,bc);
	if (cause>0) bc->cause=cause;

	dec_ie_facility(release_complete->FACILITY, (Q931_info_t *) release_complete, &bc->fac_in, nt, bc);

#ifdef DEBUG
	printf("Parsing RELEASE_COMPLETE Msg\n");
#endif
}

static msg_t *build_release_complete (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_COMPLETE_t *release_complete;
	msg_t *msg =(msg_t*)create_l3msg(CC_RELEASE_COMPLETE | REQUEST, MT_RELEASE_COMPLETE,  bc?bc->l3_id:-1, sizeof(RELEASE_COMPLETE_t) ,nt);

	release_complete=(RELEASE_COMPLETE_t*)((msg->data+HEADER_LEN));

	enc_ie_cause(&release_complete->CAUSE, msg, nt?1:0, bc->out_cause, nt,bc);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&release_complete->FACILITY, msg, &bc->fac_out, nt);
	}

	if (bc->uulen) {
		int  protocol=4;
		enc_ie_useruser(&release_complete->USER_USER, msg, protocol, bc->uu, bc->uulen, nt,bc);
		cb_log(1, bc->port, "ENCODING USERUSERINFO:%s\n", bc->uu);
	}

#ifdef DEBUG
	printf("Building RELEASE_COMPLETE Msg\n");
#endif
	return msg;
}

static void parse_facility (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt ? mISDNUSER_HEAD_SIZE : mISDN_HEADER_LEN;
	FACILITY_t *facility = (FACILITY_t*)(msg->data+HEADER_LEN);
	Q931_info_t *qi = (Q931_info_t*)(msg->data+HEADER_LEN);
	unsigned char *p = NULL;
#if defined(AST_MISDN_ENHANCEMENTS)
	int description_code;
	int type;
	int plan;
	int present;
	char number[sizeof(bc->redirecting.to.number)];
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#ifdef DEBUG
	printf("Parsing FACILITY Msg\n");
#endif

	bc->fac_in.Function = Fac_None;

	if (!bc->nt) {
		if (qi->QI_ELEMENT(facility))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(facility) + 1;
	} else {
		p = facility->FACILITY;
	}
	if (!p)
		return;

	if (decodeFac(p, &bc->fac_in)) {
		cb_log(3, bc->port, "Decoding facility ie failed! Unrecognized facility message?\n");
	}

#if defined(AST_MISDN_ENHANCEMENTS)
	dec_ie_notify(facility->NOTIFY, qi, &description_code, nt, bc);
	if (description_code < 0) {
		bc->notify_description_code = mISDN_NOTIFY_CODE_INVALID;
	} else {
		bc->notify_description_code = description_code;
	}

	dec_ie_redir_dn(facility->REDIR_DN, qi, &type, &plan, &present, number, sizeof(number), nt, bc);
	if (0 <= type) {
		bc->redirecting.to_changed = 1;

		bc->redirecting.to.number_type = type;
		bc->redirecting.to.number_plan = plan;
		switch (present) {
		default:
		case 0:
			bc->redirecting.to.presentation = 0;	/* presentation allowed */
			break;
		case 1:
			bc->redirecting.to.presentation = 1;	/* presentation restricted */
			break;
		case 2:
			bc->redirecting.to.presentation = 2;	/* Number not available */
			break;
		}
		bc->redirecting.to.screening = 0;	/* Unscreened */
		strcpy(bc->redirecting.to.number, number);
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
}

static msg_t *build_facility (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int len;
	int HEADER_LEN;
	unsigned char *ie_fac;
	unsigned char fac_tmp[256];
	msg_t *msg;
	FACILITY_t *facility;
	Q931_info_t *qi;

#ifdef DEBUG
	printf("Building FACILITY Msg\n");
#endif

	len = encodeFac(fac_tmp, &(bc->fac_out));
	if (len <= 0) {
		/*
		 * mISDN does not know how to build the requested facility structure
		 * Clear facility information
		 */
		bc->fac_out.Function = Fac_None;

#if defined(AST_MISDN_ENHANCEMENTS)
		/* Clear other one shot information. */
		bc->notify_description_code = mISDN_NOTIFY_CODE_INVALID;
		bc->redirecting.to_changed = 0;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
		return NULL;
	}

	msg = (msg_t *) create_l3msg(CC_FACILITY | REQUEST, MT_FACILITY, bc ? bc->l3_id : -1, sizeof(FACILITY_t), nt);
	HEADER_LEN = nt ? mISDNUSER_HEAD_SIZE : mISDN_HEADER_LEN;
	facility = (FACILITY_t *) (msg->data + HEADER_LEN);

	ie_fac = msg_put(msg, len);
	if (bc->nt) {
		facility->FACILITY = ie_fac + 1;
	} else {
		qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
		qi->QI_ELEMENT(facility) = ie_fac - (unsigned char *)qi - sizeof(Q931_info_t);
	}

	memcpy(ie_fac, fac_tmp, len);

	/* Clear facility information */
	bc->fac_out.Function = Fac_None;

	if (*bc->display) {
#ifdef DEBUG
		printf("Sending %s as Display\n", bc->display);
#endif
		enc_ie_display(&facility->DISPLAY, msg, bc->display, nt,bc);
	}

#if defined(AST_MISDN_ENHANCEMENTS)
	if (bc->notify_description_code != mISDN_NOTIFY_CODE_INVALID) {
		enc_ie_notify(&facility->NOTIFY, msg, bc->notify_description_code, nt, bc);
		bc->notify_description_code = mISDN_NOTIFY_CODE_INVALID;
	}

	if (bc->redirecting.to_changed) {
		bc->redirecting.to_changed = 0;
		switch (bc->outgoing_colp) {
		case 0:/* pass */
		case 1:/* restricted */
			enc_ie_redir_dn(&facility->REDIR_DN, msg, bc->redirecting.to.number_type,
				bc->redirecting.to.number_plan, bc->redirecting.to.presentation,
				bc->redirecting.to.number, nt, bc);
			break;
		default:
			break;
		}
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	return msg;
}

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Parse a received REGISTER message
 *
 * \param msgs Search table entry that called us.
 * \param msg Received message contents
 * \param bc Associated B channel
 * \param nt TRUE if in NT mode.
 */
static void parse_register(struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN;
	REGISTER_t *reg;

	HEADER_LEN = nt ? mISDNUSER_HEAD_SIZE : mISDN_HEADER_LEN;
	reg = (REGISTER_t *) (msg->data + HEADER_LEN);

	/*
	 * A facility ie is optional.
	 * The peer may just be establishing a connection to send
	 * messages later.
	 */
	dec_ie_facility(reg->FACILITY, (Q931_info_t *) reg, &bc->fac_in, nt, bc);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Construct a REGISTER message
 *
 * \param msgs Search table entry that called us.
 * \param bc Associated B channel
 * \param nt TRUE if in NT mode.
 *
 * \return Allocated built message
 */
static msg_t *build_register(struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN;
	REGISTER_t *reg;
	msg_t *msg;

	msg = (msg_t *) create_l3msg(CC_REGISTER | REQUEST, MT_REGISTER,  bc ? bc->l3_id : -1, sizeof(REGISTER_t), nt);
	HEADER_LEN = nt ? mISDNUSER_HEAD_SIZE : mISDN_HEADER_LEN;
	reg = (REGISTER_t *) (msg->data + HEADER_LEN);

	if (bc->fac_out.Function != Fac_None) {
		enc_ie_facility(&reg->FACILITY, msg, &bc->fac_out, nt);
	}

	return msg;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

static void parse_notify (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt ? mISDNUSER_HEAD_SIZE : mISDN_HEADER_LEN;
	NOTIFY_t *notify = (NOTIFY_t *) (msg->data + HEADER_LEN);
	int description_code;
	int type;
	int plan;
	int present;
	char number[sizeof(bc->redirecting.to.number)];

#ifdef DEBUG
	printf("Parsing NOTIFY Msg\n");
#endif

	dec_ie_notify(notify->NOTIFY, (Q931_info_t *) notify, &description_code, nt, bc);
	if (description_code < 0) {
		bc->notify_description_code = mISDN_NOTIFY_CODE_INVALID;
	} else {
		bc->notify_description_code = description_code;
	}

	dec_ie_redir_dn(notify->REDIR_DN, (Q931_info_t *) notify, &type, &plan, &present, number, sizeof(number), nt, bc);
	if (0 <= type) {
		bc->redirecting.to_changed = 1;

		bc->redirecting.to.number_type = type;
		bc->redirecting.to.number_plan = plan;
		switch (present) {
		default:
		case 0:
			bc->redirecting.to.presentation = 0;	/* presentation allowed */
			break;
		case 1:
			bc->redirecting.to.presentation = 1;	/* presentation restricted */
			break;
		case 2:
			bc->redirecting.to.presentation = 2;	/* Number not available */
			break;
		}
		bc->redirecting.to.screening = 0;	/* Unscreened */
		strcpy(bc->redirecting.to.number, number);
	}
}

static msg_t *build_notify (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	NOTIFY_t *notify;
	msg_t *msg =(msg_t*)create_l3msg(CC_NOTIFY | REQUEST, MT_NOTIFY,  bc?bc->l3_id:-1, sizeof(NOTIFY_t) ,nt);

#ifdef DEBUG
	printf("Building NOTIFY Msg\n");
#endif

	notify = (NOTIFY_t *) (msg->data + HEADER_LEN);

	enc_ie_notify(&notify->NOTIFY, msg, bc->notify_description_code, nt, bc);
	bc->notify_description_code = mISDN_NOTIFY_CODE_INVALID;

	if (bc->redirecting.to_changed) {
		bc->redirecting.to_changed = 0;
		switch (bc->outgoing_colp) {
		case 0:/* pass */
		case 1:/* restricted */
			enc_ie_redir_dn(&notify->REDIR_DN, msg, bc->redirecting.to.number_type,
				bc->redirecting.to.number_plan, bc->redirecting.to.presentation,
				bc->redirecting.to.number, nt, bc);
			break;
		default:
			break;
		}
	}
	return msg;
}

static void parse_status_enquiry (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing STATUS_ENQUIRY Msg\n");
#endif
}

static msg_t *build_status_enquiry (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_STATUS_ENQUIRY | REQUEST, MT_STATUS_ENQUIRY,  bc?bc->l3_id:-1, sizeof(STATUS_ENQUIRY_t) ,nt);

#ifdef DEBUG
	printf("Building STATUS_ENQUIRY Msg\n");
#endif
	return msg;
}

static void parse_information (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	INFORMATION_t *information = (INFORMATION_t *) (msg->data + HEADER_LEN);
	int type, plan;

	dec_ie_called_pn(information->CALLED_PN, (Q931_info_t *) information, &type, &plan, bc->info_dad, sizeof(bc->info_dad), nt, bc);
	dec_ie_keypad(information->KEYPAD, (Q931_info_t *) information, bc->keypad, sizeof(bc->keypad), nt, bc);

#ifdef DEBUG
	printf("Parsing INFORMATION Msg\n");
#endif
}

static msg_t *build_information (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	INFORMATION_t *information;
	msg_t *msg =(msg_t*)create_l3msg(CC_INFORMATION | REQUEST, MT_INFORMATION,  bc?bc->l3_id:-1, sizeof(INFORMATION_t) ,nt);

	information=(INFORMATION_t*)((msg->data+HEADER_LEN));

	enc_ie_called_pn(&information->CALLED_PN, msg, 0, 1, bc->info_dad, nt,bc);

	{
		if (*bc->display) {
#ifdef DEBUG
			printf("Sending %s as Display\n", bc->display);
#endif
			enc_ie_display(&information->DISPLAY, msg, bc->display, nt,bc);
		}
	}

#ifdef DEBUG
	printf("Building INFORMATION Msg\n");
#endif
	return msg;
}

static void parse_status (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	STATUS_t *status = (STATUS_t *) (msg->data + HEADER_LEN);
	int location;
	int cause;

	dec_ie_cause(status->CAUSE, (Q931_info_t *)(status), &location, &cause, nt,bc);
	if (cause>0) bc->cause=cause;

#ifdef DEBUG
	printf("Parsing STATUS Msg\n");
#endif
}

static msg_t *build_status (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_STATUS | REQUEST, MT_STATUS,  bc?bc->l3_id:-1, sizeof(STATUS_t) ,nt);

#ifdef DEBUG
	printf("Building STATUS Msg\n");
#endif
	return msg;
}

static void parse_timeout (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
#ifdef DEBUG
	printf("Parsing STATUS Msg\n");
#endif
}

static msg_t *build_timeout (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	msg_t *msg =(msg_t*)create_l3msg(CC_STATUS | REQUEST, MT_STATUS,  bc?bc->l3_id:-1, sizeof(STATUS_t) ,nt);

#ifdef DEBUG
	printf("Building STATUS Msg\n");
#endif
	return msg;
}


/************************************/




/** Msg Array **/

struct isdn_msg msgs_g[] = {
/* *INDENT-OFF* */
	/* misdn_msg,               event,                      msg_parser,                 msg_builder,                info */
	{ CC_PROCEEDING,            EVENT_PROCEEDING,           parse_proceeding,           build_proceeding,           "PROCEEDING" },
	{ CC_ALERTING,              EVENT_ALERTING,             parse_alerting,             build_alerting,             "ALERTING" },
	{ CC_PROGRESS,              EVENT_PROGRESS,             parse_progress,             build_progress,             "PROGRESS" },
	{ CC_SETUP,                 EVENT_SETUP,                parse_setup,                build_setup,                "SETUP" },
#if defined(AST_MISDN_ENHANCEMENTS)
	{ CC_REGISTER,              EVENT_REGISTER,             parse_register,             build_register,             "REGISTER" },
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	{ CC_CONNECT,               EVENT_CONNECT,              parse_connect,              build_connect,              "CONNECT" },
	{ CC_SETUP_ACKNOWLEDGE,     EVENT_SETUP_ACKNOWLEDGE,    parse_setup_acknowledge,    build_setup_acknowledge,    "SETUP_ACKNOWLEDGE" },
	{ CC_CONNECT_ACKNOWLEDGE,   EVENT_CONNECT_ACKNOWLEDGE,  parse_connect_acknowledge,  build_connect_acknowledge,  "CONNECT_ACKNOWLEDGE " },
	{ CC_USER_INFORMATION,      EVENT_USER_INFORMATION,     parse_user_information,     build_user_information,     "USER_INFORMATION" },
	{ CC_SUSPEND_REJECT,        EVENT_SUSPEND_REJECT,       parse_suspend_reject,       build_suspend_reject,       "SUSPEND_REJECT" },
	{ CC_RESUME_REJECT,         EVENT_RESUME_REJECT,        parse_resume_reject,        build_resume_reject,        "RESUME_REJECT" },
	{ CC_HOLD,                  EVENT_HOLD,                 parse_hold,                 build_hold,                 "HOLD" },
	{ CC_SUSPEND,               EVENT_SUSPEND,              parse_suspend,              build_suspend,              "SUSPEND" },
	{ CC_RESUME,                EVENT_RESUME,               parse_resume,               build_resume,               "RESUME" },
	{ CC_HOLD_ACKNOWLEDGE,      EVENT_HOLD_ACKNOWLEDGE,     parse_hold_acknowledge,     build_hold_acknowledge,     "HOLD_ACKNOWLEDGE" },
	{ CC_SUSPEND_ACKNOWLEDGE,   EVENT_SUSPEND_ACKNOWLEDGE,  parse_suspend_acknowledge,  build_suspend_acknowledge,  "SUSPEND_ACKNOWLEDGE" },
	{ CC_RESUME_ACKNOWLEDGE,    EVENT_RESUME_ACKNOWLEDGE,   parse_resume_acknowledge,   build_resume_acknowledge,   "RESUME_ACKNOWLEDGE" },
	{ CC_HOLD_REJECT,           EVENT_HOLD_REJECT,          parse_hold_reject,          build_hold_reject,          "HOLD_REJECT" },
	{ CC_RETRIEVE,              EVENT_RETRIEVE,             parse_retrieve,             build_retrieve,             "RETRIEVE" },
	{ CC_RETRIEVE_ACKNOWLEDGE,  EVENT_RETRIEVE_ACKNOWLEDGE, parse_retrieve_acknowledge, build_retrieve_acknowledge, "RETRIEVE_ACKNOWLEDGE" },
	{ CC_RETRIEVE_REJECT,       EVENT_RETRIEVE_REJECT,      parse_retrieve_reject,      build_retrieve_reject,      "RETRIEVE_REJECT" },
	{ CC_DISCONNECT,            EVENT_DISCONNECT,           parse_disconnect,           build_disconnect,           "DISCONNECT" },
	{ CC_RESTART,               EVENT_RESTART,              parse_restart,              build_restart,              "RESTART" },
	{ CC_RELEASE,               EVENT_RELEASE,              parse_release,              build_release,              "RELEASE" },
	{ CC_RELEASE_COMPLETE,      EVENT_RELEASE_COMPLETE,     parse_release_complete,     build_release_complete,     "RELEASE_COMPLETE" },
	{ CC_FACILITY,              EVENT_FACILITY,             parse_facility,             build_facility,             "FACILITY" },
	{ CC_NOTIFY,                EVENT_NOTIFY,               parse_notify,               build_notify,               "NOTIFY" },
	{ CC_STATUS_ENQUIRY,        EVENT_STATUS_ENQUIRY,       parse_status_enquiry,       build_status_enquiry,       "STATUS_ENQUIRY" },
	{ CC_INFORMATION,           EVENT_INFORMATION,          parse_information,          build_information,          "INFORMATION" },
	{ CC_STATUS,                EVENT_STATUS,               parse_status,               build_status,               "STATUS" },
	{ CC_TIMEOUT,               EVENT_TIMEOUT,              parse_timeout,              build_timeout,              "TIMEOUT" },
	{ 0, 0, NULL, NULL, NULL }
/* *INDENT-ON* */
};

#define msgs_max (sizeof(msgs_g)/sizeof(struct isdn_msg))

/** INTERFACE FCTS ***/
int isdn_msg_get_index(struct isdn_msg msgs[], msg_t *msg, int nt)
{
	int i;

	if (nt){
		mISDNuser_head_t *hh = (mISDNuser_head_t*)msg->data;

		for (i=0; i< msgs_max -1; i++) {
			if ( (hh->prim&COMMAND_MASK)==(msgs[i].misdn_msg&COMMAND_MASK)) return i;
		}

	} else {
		iframe_t *frm = (iframe_t*)msg->data;

		for (i=0; i< msgs_max -1; i++)
			if ( (frm->prim&COMMAND_MASK)==(msgs[i].misdn_msg&COMMAND_MASK)) return i;
	}

	return -1;
}

int isdn_msg_get_index_by_event(struct isdn_msg msgs[], enum event_e event, int nt)
{
	int i;
	for (i=0; i< msgs_max; i++)
		if ( event == msgs[i].event) return i;

	cb_log(10,0, "get_index: event not found!\n");

	return -1;
}

enum event_e isdn_msg_get_event(struct isdn_msg msgs[], msg_t *msg, int nt)
{
	int i=isdn_msg_get_index(msgs, msg, nt);
	if(i>=0) return msgs[i].event;
	return EVENT_UNKNOWN;
}

char * isdn_msg_get_info(struct isdn_msg msgs[], msg_t *msg, int nt)
{
	int i=isdn_msg_get_index(msgs, msg, nt);
	if(i>=0) return msgs[i].info;
	return NULL;
}


char EVENT_CLEAN_INFO[] = "CLEAN_UP";
char EVENT_DTMF_TONE_INFO[] = "DTMF_TONE";
char EVENT_NEW_L3ID_INFO[] = "NEW_L3ID";
char EVENT_NEW_BC_INFO[] = "NEW_BC";
char EVENT_PORT_ALARM_INFO[] = "ALARM";
char EVENT_NEW_CHANNEL_INFO[] = "NEW_CHANNEL";
char EVENT_BCHAN_DATA_INFO[] = "BCHAN_DATA";
char EVENT_BCHAN_ACTIVATED_INFO[] = "BCHAN_ACTIVATED";
char EVENT_TONE_GENERATE_INFO[] = "TONE_GENERATE";
char EVENT_BCHAN_ERROR_INFO[] = "BCHAN_ERROR";

char * isdn_get_info(struct isdn_msg msgs[], enum event_e event, int nt)
{
	int i=isdn_msg_get_index_by_event(msgs, event, nt);

	if(i>=0) return msgs[i].info;

	if (event == EVENT_CLEANUP) return EVENT_CLEAN_INFO;
	if (event == EVENT_DTMF_TONE) return EVENT_DTMF_TONE_INFO;
	if (event == EVENT_NEW_L3ID) return EVENT_NEW_L3ID_INFO;
	if (event == EVENT_NEW_BC) return EVENT_NEW_BC_INFO;
	if (event == EVENT_NEW_CHANNEL) return EVENT_NEW_CHANNEL_INFO;
	if (event == EVENT_BCHAN_DATA) return EVENT_BCHAN_DATA_INFO;
	if (event == EVENT_BCHAN_ACTIVATED) return EVENT_BCHAN_ACTIVATED_INFO;
	if (event == EVENT_TONE_GENERATE) return EVENT_TONE_GENERATE_INFO;
	if (event == EVENT_PORT_ALARM) return EVENT_PORT_ALARM_INFO;
	if (event == EVENT_BCHAN_ERROR) return EVENT_BCHAN_ERROR_INFO;

	return NULL;
}

int isdn_msg_parse_event(struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt)
{
	int i=isdn_msg_get_index(msgs, msg, nt);
	if(i<0) return -1;

	msgs[i].msg_parser(msgs, msg, bc, nt);
	return 0;
}

msg_t * isdn_msg_build_event(struct isdn_msg msgs[], struct misdn_bchannel *bc, enum event_e event, int nt)
{
	int i=isdn_msg_get_index_by_event(msgs, event, nt);
	if(i<0) return NULL;

	return  msgs[i].msg_builder(msgs, bc, nt);
}
