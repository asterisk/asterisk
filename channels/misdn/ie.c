
/*
 * Chan_Misdn -- Channel Driver for Asterisk
 *
 * Interface to mISDN
 *
 * Copyright (C) 2005, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
 *
 * heaviliy patched from jollys ie.cpp, jolly gave me ALL
 * rights for this code, i can even have my own copyright on it.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*
  the pointer of enc_ie_* always points to the IE itself
  if qi is not NULL (TE-mode), offset is set
*/


#include <string.h>

#include <mISDNuser/mISDNlib.h>
#include <mISDNuser/isdn_net.h>
#include <mISDNuser/l3dss1.h>
#include <mISDNuser/net_l3.h>



#define MISDN_IE_DEBG 0

/* support stuff */
static void strnncpy(unsigned char *dest, unsigned char *src, int len, int dst_len)
{
	if (len > dst_len-1)
		len = dst_len-1;
	strncpy((char *)dest, (char *)src, len);
	dest[len] = '\0';
}


/* IE_COMPLETE */
void enc_ie_complete(unsigned char **ntmode, msg_t *msg, int complete, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);

	if (complete<0 || complete>1)
	{
		printf("%s: ERROR: complete(%d) is out of range.\n", __FUNCTION__, complete);
		return;
	}

	if (complete)
		if (MISDN_IE_DEBG) printf("    complete=%d\n", complete);

	if (complete)
	{
		p = msg_put(msg, 1);
		if (nt)
		{
			*ntmode = p;
		} else
			qi->QI_ELEMENT(sending_complete) = p - (unsigned char *)qi - sizeof(Q931_info_t);

		p[0] = IE_COMPLETE;
	}
}

void dec_ie_complete(unsigned char *p, Q931_info_t *qi, int *complete, int nt, struct misdn_bchannel *bc)
{
	*complete = 0;
	if (!nt)
	{
		if (qi->QI_ELEMENT(sending_complete))
			*complete = 1;
	} else
		if (p)
			*complete = 1;

	if (*complete)
		if (MISDN_IE_DEBG) printf("    complete=%d\n", *complete);
}


/* IE_BEARER */
void enc_ie_bearer(unsigned char **ntmode, msg_t *msg, int coding, int capability, int mode, int rate, int multi, int user, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (coding<0 || coding>3)
	{
		printf("%s: ERROR: coding(%d) is out of range.\n", __FUNCTION__, coding);
		return;
	}
	if (capability<0 || capability>31)
	{
		printf("%s: ERROR: capability(%d) is out of range.\n", __FUNCTION__, capability);
		return;
	}
	if (mode<0 || mode>3)
	{
		printf("%s: ERROR: mode(%d) is out of range.\n", __FUNCTION__, mode);
		return;
	}
	if (rate<0 || rate>31)
	{
		printf("%s: ERROR: rate(%d) is out of range.\n", __FUNCTION__, rate);
		return;
	}
	if (multi>127)
	{
		printf("%s: ERROR: multi(%d) is out of range.\n", __FUNCTION__, multi);
		return;
	}
	if (user>31)
	{
		printf("%s: ERROR: user L1(%d) is out of range.\n", __FUNCTION__, rate);
		return;
	}
	if (rate!=24 && multi>=0)
	{
		printf("%s: WARNING: multi(%d) is only possible if rate(%d) would be 24.\n", __FUNCTION__, multi, rate);
		multi = -1;
	}

	if (MISDN_IE_DEBG) printf("    coding=%d capability=%d mode=%d rate=%d multi=%d user=%d\n", coding, capability, mode, rate, multi, user);

	l = 2 + (multi>=0) + (user>=0);
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(bearer_capability) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_BEARER;
	p[1] = l;
	p[2] = 0x80 + (coding<<5) + capability;
	p[3] = 0x80 + (mode<<5) + rate;
	if (multi >= 0)
		p[4] = 0x80 + multi;
	if (user >= 0)
		p[4+(multi>=0)] = 0xa0 + user;
}

void dec_ie_bearer(unsigned char *p, Q931_info_t *qi, int *coding, int *capability, int *mode, int *rate, int *multi, int *user, 
		   int *async, int *urate, int *stopbits, int *dbits, int *parity, int nt, struct misdn_bchannel *bc)
{
	int octet;
	*coding = -1;
	*capability = -1;
	*mode = -1;
	*rate = -1;
	*multi = -1;
	*user = -1;
	*async = -1;
	*urate = -1;
	*stopbits = -1;
	*dbits = -1;
	*parity = -1;

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(llc))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(llc) + 1;
		else if (qi->QI_ELEMENT(bearer_capability))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(bearer_capability) + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*coding = (p[1]&0x60) >> 5;
	*capability = p[1] & 0x1f;
	octet = 2;
	if (!(p[1] & 0x80))
		octet++;

	if (p[0] < octet)
		goto done;

	*mode = (p[octet]&0x60) >> 5;
	*rate = p[octet] & 0x1f;

	octet++;

	if (p[0] < octet)
		goto done;

	if (*rate == 0x18) {
		/* Rate multiplier only present if 64Kb/s base rate */
		*multi = p[octet++] & 0x7f;
	}

	if (p[0] < octet)
		goto done;

	/* Start L1 info */
	if ((p[octet] & 0x60) == 0x20) {
		*user = p[octet] & 0x1f;

		if (p[0] <= octet)
			goto done;
		
		if (p[octet++] & 0x80)
			goto l2;

		*async = !!(p[octet] & 0x40);
		/* 0x20 is inband negotiation */
		*urate = p[octet] & 0x1f;

		if (p[0] <= octet)
			goto done;
		
		if (p[octet++] & 0x80)
			goto l2;

		/* Ignore next byte for now: Intermediate rate, NIC, flow control */

		if (p[0] <= octet)
			goto done;
		
		if (p[octet++] & 0x80)
			goto l2;

		/* And the next one. Header, multiframe, mode, assignor/ee, negotiation */

		if (p[0] <= octet)
			goto done;
		
		if (!p[octet++] & 0x80)
			goto l2;

		/* Wheee. V.110 speed information */

		*stopbits = (p[octet] & 0x60) >> 5;
		*dbits = (p[octet] & 0x18) >> 3; 
		*parity = p[octet] & 7;

		octet++;
	}
 l2: /* Nobody seems to want the rest so we don't bother (yet) */
 done:		
	if (MISDN_IE_DEBG) printf("    coding=%d capability=%d mode=%d rate=%d multi=%d user=%d async=%d urate=%d stopbits=%d dbits=%d parity=%d\n", *coding, *capability, *mode, *rate, *multi, *user, *async, *urate, *stopbits, *dbits, *parity);
}


/* IE_CALL_ID */
void enc_ie_call_id(unsigned char **ntmode, msg_t *msg, unsigned char *callid, int callid_len, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	char debug[25];
	int i;

	if (!callid || callid_len<=0)
	{
		return;
	}
	if (callid_len>8)
	{
		printf("%s: ERROR: callid_len(%d) is out of range.\n", __FUNCTION__, callid_len);
		return;
	}

	i = 0;
	while(i < callid_len)
	{
		if (MISDN_IE_DEBG) printf(debug+(i*3), " %02x", callid[i]);
		i++;
	}
		
	if (MISDN_IE_DEBG) printf("    callid%s\n", debug);

	l = callid_len;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(call_id) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CALL_ID;
	p[1] = l;
	memcpy(p+2, callid, callid_len);
}

void dec_ie_call_id(unsigned char *p, Q931_info_t *qi, unsigned char *callid, int *callid_len, int nt, struct misdn_bchannel *bc)
{
	char debug[25];
	int i;

	*callid_len = -1;

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(call_id))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(call_id) + 1;
	}
	if (!p)
		return;
	if (p[0] > 8)
	{
		printf("%s: ERROR: IE too long (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*callid_len = p[0];
	memcpy(callid, p+1, *callid_len);

	i = 0;
	while(i < *callid_len)
	{
		if (MISDN_IE_DEBG) printf(debug+(i*3), " %02x", callid[i]);
		i++;
	}
		
	if (MISDN_IE_DEBG) printf("    callid%s\n", debug);
}


/* IE_CALLED_PN */
void enc_ie_called_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, unsigned char *number, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		printf("%s: ERROR: type(%d) is out of range.\n", __FUNCTION__, type);
		return;
	}
	if (plan<0 || plan>15)
	{
		printf("%s: ERROR: plan(%d) is out of range.\n", __FUNCTION__, plan);
		return;
	}
	if (!number[0])
	{
		printf("%s: ERROR: number is not given.\n", __FUNCTION__);
		return;
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d number='%s'\n", type, plan, number);

	l = 1+strlen((char *)number);
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(called_nr) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CALLED_PN;
	p[1] = l;
	p[2] = 0x80 + (type<<4) + plan;
	strncpy((char *)p+3, (char *)number, strlen((char *)number));
}

void dec_ie_called_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, unsigned char *number, int number_len, int nt, struct misdn_bchannel *bc)
{
	*type = -1;
	*plan = -1;
	*number = '\0';

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(called_nr))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(called_nr) + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	strnncpy(number, p+2, p[0]-1, number_len);

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d number='%s'\n", *type, *plan, number);
}


/* IE_CALLING_PN */
void enc_ie_calling_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, unsigned char *number, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		printf("%s: ERROR: type(%d) is out of range.\n", __FUNCTION__, type);
		return;
	}
	if (plan<0 || plan>15)
	{
		printf("%s: ERROR: plan(%d) is out of range.\n", __FUNCTION__, plan);
		return;
	}
	if (present>3)
	{
		printf("%s: ERROR: present(%d) is out of range.\n", __FUNCTION__, present);
		return;
	}
	if (present >= 0) if (screen<0 || screen>3)
	{
		printf("%s: ERROR: screen(%d) is out of range.\n", __FUNCTION__, screen);
		return;
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d screen=%d number='%s'\n", type, plan, present, screen, number);

	l = 1;
	if (number) if (number[0])
		l += strlen((char *)number);
	if (present >= 0)
		l += 1;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(calling_nr) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CALLING_PN;
	p[1] = l;
	if (present >= 0)
	{
		p[2] = 0x00 + (type<<4) + plan;
		p[3] = 0x80 + (present<<5) + screen;
		if (number) if (number[0])
			strncpy((char *)p+4, (char *)number, strlen((char *)number));
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number) if (number[0])
			strncpy((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void dec_ie_calling_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len, int nt, struct misdn_bchannel *bc)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*screen = -1;
	*number = '\0';

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(calling_nr))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(calling_nr) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		if (p[0] < 2)
		{
			printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
			return;
		}
		*present = (p[2]&0x60) >> 5;
		*screen = p[2] & 0x3;
		strnncpy(number, p+3, p[0]-2, number_len);
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
 		/* SPECIAL workarround for IBT software bug */ 
		/* if (number[0]==0x80) */
		/*  strcpy((char *)number, (char *)number+1); */
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d screen=%d number='%s'\n", *type, *plan, *present, *screen, number);
}


/* IE_CONNECTED_PN */
void enc_ie_connected_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, unsigned char *number, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		printf("%s: ERROR: type(%d) is out of range.\n", __FUNCTION__, type);
		return;
	}
	if (plan<0 || plan>15)
	{
		printf("%s: ERROR: plan(%d) is out of range.\n", __FUNCTION__, plan);
		return;
	}
	if (present>3)
	{
		printf("%s: ERROR: present(%d) is out of range.\n", __FUNCTION__, present);
		return;
	}
	if (present >= 0) if (screen<0 || screen>3)
	{
		printf("%s: ERROR: screen(%d) is out of range.\n", __FUNCTION__, screen);
		return;
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d screen=%d number='%s'\n", type, plan, present, screen, number);

	l = 1;
	if (number) if (number[0])
		l += strlen((char *)number);
	if (present >= 0)
		l += 1;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(connected_nr) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CONNECT_PN;
	p[1] = l;
	if (present >= 0)
	{
		p[2] = 0x00 + (type<<4) + plan;
		p[3] = 0x80 + (present<<5) + screen;
		if (number) if (number[0])
			strncpy((char *)p+4, (char *)number, strlen((char *)number));
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number) if (number[0])
			strncpy((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void dec_ie_connected_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len, int nt, struct misdn_bchannel *bc)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*screen = -1;
	*number = '\0';

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(connected_nr))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(connected_nr) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		if (p[0] < 2)
		{
			printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
			return;
		}
		*present = (p[2]&0x60) >> 5;
		*screen = p[2] & 0x3;
		strnncpy(number, p+3, p[0]-2, number_len);
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d screen=%d number='%s'\n", *type, *plan, *present, *screen, number);
}


/* IE_CAUSE */
void enc_ie_cause(unsigned char **ntmode, msg_t *msg, int location, int cause, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (location<0 || location>7)
	{
		printf("%s: ERROR: location(%d) is out of range.\n", __FUNCTION__, location);
		return;
	}
	if (cause<0 || cause>127)
	{
		printf("%s: ERROR: cause(%d) is out of range.\n", __FUNCTION__, cause);
		return;
	}

	if (MISDN_IE_DEBG) printf("    location=%d cause=%d\n", location, cause);

	l = 2;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(cause) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CAUSE;
	p[1] = l;
	p[2] = 0x80 + location;
	p[3] = 0x80 + cause;
}
void enc_ie_cause_standalone(unsigned char **ntmode, msg_t *msg, int location, int cause, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p = msg_put(msg, 4);
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	if (ntmode)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(cause) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CAUSE;
	p[1] = 2;
	p[2] = 0x80 + location;
	p[3] = 0x80 + cause;
}


void dec_ie_cause(unsigned char *p, Q931_info_t *qi, int *location, int *cause, int nt, struct misdn_bchannel *bc)
{
	*location = -1;
	*cause = -1;

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(cause))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(cause) + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*location = p[1] & 0x0f;
	*cause = p[2] & 0x7f;

	if (MISDN_IE_DEBG) printf("    location=%d cause=%d\n", *location, *cause);
}


/* IE_CHANNEL_ID */
void enc_ie_channel_id(unsigned char **ntmode, msg_t *msg, int exclusive, int channel, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;
	struct misdn_stack *stack=get_stack_by_bc(bc);
	int pri = stack->pri;
	
	if (exclusive<0 || exclusive>1)
	{
		printf("%s: ERROR: exclusive(%d) is out of range.\n", __FUNCTION__, exclusive);
		return;
	}
	if ((channel<0 || channel>0xff)
	    || (!pri && (channel>2 && channel<0xff))
	    || (pri && (channel>31 && channel<0xff))
	    || (pri && channel==16))
	{
		printf("%s: ERROR: channel(%d) is out of range.\n", __FUNCTION__, channel);
		return;
	}

	/* if (MISDN_IE_DEBG) printf("    exclusive=%d channel=%d\n", exclusive, channel); */
	

	if (!pri)
	{
		/* BRI */
		l = 1;
		p = msg_put(msg, l+2);
		if (nt)
			*ntmode = p+1;
		else
			qi->QI_ELEMENT(channel_id) = p - (unsigned char *)qi - sizeof(Q931_info_t);
		p[0] = IE_CHANNEL_ID;
		p[1] = l;
		if (channel == 0xff)
			channel = 3;
		p[2] = 0x80 + (exclusive<<3) + channel;
		/* printf("    exclusive=%d channel=%d\n", exclusive, channel); */
	} else
	{
		/* PRI */
		if (channel == 0) /* no channel */
			return; /* IE not present */
/* 		if (MISDN_IE_DEBG) printf("channel = %d\n", channel); */
		if (channel == 0xff) /* any channel */
		{
			l = 1;
			p = msg_put(msg, l+2);
			if (nt)
				*ntmode = p+1;
			else
				qi->QI_ELEMENT(channel_id) = p - (unsigned char *)qi - sizeof(Q931_info_t);
			p[0] = IE_CHANNEL_ID;
			p[1] = l;
			p[2] = 0x80 + 0x20 + 0x03;
/* 			if (MISDN_IE_DEBG) printf("%02x\n", p[2]); */
			return; /* end */
		}
		l = 3;
		p = msg_put(msg, l+2);
		if (nt)
			*ntmode = p+1;
		else
			qi->QI_ELEMENT(channel_id) = p - (unsigned char *)qi - sizeof(Q931_info_t);
		p[0] = IE_CHANNEL_ID;
		p[1] = l;
		p[2] = 0x80 + 0x20 + (exclusive<<3) + 0x01;
		p[3] = 0x80 + 3; /* CCITT, Number, B-type */
		p[4] = 0x80 + channel;
/* 		if (MISDN_IE_DEBG) printf("%02x %02x %02x\n", p[2], p[3], p[4]); */
	}
}

void dec_ie_channel_id(unsigned char *p, Q931_info_t *qi, int *exclusive, int *channel, int nt, struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	int pri =stack->pri;

	*exclusive = -1;
	*channel = -1;

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(channel_id))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(channel_id) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	if (p[1] & 0x40)
	{
		printf("%s: ERROR: refering to channels of other interfaces is not supported.\n", __FUNCTION__);
		return;
	}
	if (p[1] & 0x04)
	{
		printf("%s: ERROR: using d-channel is not supported.\n", __FUNCTION__);
		return;
	}

	*exclusive = (p[1]&0x08) >> 3;
	if (!pri)
	{
		/* BRI */
		if (p[1] & 0x20)
		{
			printf("%s: ERROR: extended channel ID with non PRI interface.\n", __FUNCTION__);
			return;
		}
		*channel = p[1] & 0x03;
		if (*channel == 3)
			*channel = 0xff;
	} else
	{
		/* PRI */
		if (p[0] < 1)
		{
			printf("%s: ERROR: IE too short for PRI (%d).\n", __FUNCTION__, p[0]);
			return;
		}
		if (!(p[1] & 0x20))
		{
			printf("%s: ERROR: basic channel ID with PRI interface.\n", __FUNCTION__);
			return;
		}
		if ((p[1]&0x03) == 0x00)
		{
			/* no channel */
			*channel = 0;
			return;
		}
		if ((p[1]&0x03) == 0x03)
		{
			/* any channel */
			*channel = 0xff;
			return;
		}
		if (p[0] < 3)
		{
			printf("%s: ERROR: IE too short for PRI with channel(%d).\n", __FUNCTION__, p[0]);
			return;
		}
		if (p[2] & 0x10)
		{
			printf("%s: ERROR: channel map not supported.\n", __FUNCTION__);
			return;
		}
		*channel = p[3] & 0x7f;
		if ( (*channel<1) | (*channel==16) | (*channel>31))
		{
			printf("%s: ERROR: PRI interface channel out of range (%d).\n", __FUNCTION__, *channel);
			return;
		}
/* 		if (MISDN_IE_DEBG) printf("%02x %02x %02x\n", p[1], p[2], p[3]); */
	}

	if (MISDN_IE_DEBG) printf("    exclusive=%d channel=%d\n", *exclusive, *channel);
}


/* IE_DATE */
void enc_ie_date(unsigned char **ntmode, msg_t *msg, time_t ti, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	struct tm *tm;

	tm = localtime(&ti);
	if (!tm)
	{
		printf("%s: ERROR: gettimeofday() returned NULL.\n", __FUNCTION__);
		return;
	}

	if (MISDN_IE_DEBG) printf("    year=%d month=%d day=%d hour=%d minute=%d\n", tm->tm_year%100, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min);

	l = 5;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(date) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_DATE;
	p[1] = l;
	p[2] = tm->tm_year % 100;
	p[3] = tm->tm_mon + 1;
	p[4] = tm->tm_mday;
	p[5] = tm->tm_hour;
	p[6] = tm->tm_min;
}


/* IE_DISPLAY */
void enc_ie_display(unsigned char **ntmode, msg_t *msg, unsigned char *display, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (!display[0])
	{
		printf("%s: ERROR: display text not given.\n", __FUNCTION__);
		return;
	}

	if (strlen((char *)display) > 80)
	{
		printf("%s: WARNING: display text too long (max 80 chars), cutting.\n", __FUNCTION__);
		display[80] = '\0';
	}

	/* if (MISDN_IE_DEBG) printf("    display='%s' (len=%d)\n", display, strlen((char *)display)); */

	l = strlen((char *)display);
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(display) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_DISPLAY;
	p[1] = l;
	strncpy((char *)p+2, (char *)display, strlen((char *)display));
}

void dec_ie_display(unsigned char *p, Q931_info_t *qi, unsigned char *display, int display_len, int nt, struct misdn_bchannel *bc)
{
	*display = '\0';

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(display))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(display) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	strnncpy(display, p+1, p[0], display_len);

	if (MISDN_IE_DEBG) printf("    display='%s'\n", display);
}


/* IE_KEYPAD */
void enc_ie_keypad(unsigned char **ntmode, msg_t *msg, unsigned char *keypad, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (!keypad[0])
	{
		printf("%s: ERROR: keypad info not given.\n", __FUNCTION__);
		return;
	}

	if (MISDN_IE_DEBG) printf("    keypad='%s'\n", keypad);

	l = strlen((char *)keypad);
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(keypad) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_KEYPAD;
	p[1] = l;
	strncpy((char *)p+2, (char *)keypad, strlen((char *)keypad));
}

void dec_ie_keypad(unsigned char *p, Q931_info_t *qi, unsigned char *keypad, int keypad_len, int nt, struct misdn_bchannel *bc)
{
	*keypad = '\0';

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(keypad))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(keypad) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	strnncpy(keypad, p+1, p[0], keypad_len);

	if (MISDN_IE_DEBG) printf("    keypad='%s'\n", keypad);
}


/* IE_NOTIFY */
void enc_ie_notify(unsigned char **ntmode, msg_t *msg, int notify, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (notify<0 || notify>0x7f)
	{
		printf("%s: ERROR: notify(%d) is out of range.\n", __FUNCTION__, notify);
		return;
	}

	if (MISDN_IE_DEBG) printf("    notify=%d\n", notify);

	l = 1;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(notify) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_NOTIFY;
	p[1] = l;
	p[2] = 0x80 + notify;
}

void dec_ie_notify(unsigned char *p, Q931_info_t *qi, int *notify, int nt, struct misdn_bchannel *bc)
{
	*notify = -1;

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(notify))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(notify) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*notify = p[1] & 0x7f;

	if (MISDN_IE_DEBG) printf("    notify=%d\n", *notify);
}


/* IE_PROGRESS */
void enc_ie_progress(unsigned char **ntmode, msg_t *msg, int coding, int location, int progress, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (coding<0 || coding>0x03)
	{
		printf("%s: ERROR: coding(%d) is out of range.\n", __FUNCTION__, coding);
		return;
	}
	if (location<0 || location>0x0f)
	{
		printf("%s: ERROR: location(%d) is out of range.\n", __FUNCTION__, location);
		return;
	}
	if (progress<0 || progress>0x7f)
	{
		printf("%s: ERROR: progress(%d) is out of range.\n", __FUNCTION__, progress);
		return;
	}

	if (MISDN_IE_DEBG) printf("    coding=%d location=%d progress=%d\n", coding, location, progress);

	l = 2;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(progress) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_PROGRESS;
	p[1] = l;
	p[2] = 0x80 + (coding<<5) + location;
	p[3] = 0x80 + progress;
}

void dec_ie_progress(unsigned char *p, Q931_info_t *qi, int *coding, int *location, int *progress, int nt, struct misdn_bchannel *bc)
{
	*coding = -1;
	*location = -1;
	//*progress = -1;
	*progress = 0;
	
	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(progress))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(progress) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*coding = (p[1]&0x60) >> 5;
	*location = p[1] & 0x0f;
	*progress = p[2] & 0x7f;

	//if (MISDN_IE_DEBG) printf("    coding=%d location=%d progress=%d\n", *coding, *location, *progress);
	if (1) printf("    coding=%d location=%d progress=%d\n", *coding, *location, *progress);
}


/* IE_REDIR_NR (redirecting = during MT_SETUP) */
void enc_ie_redir_nr(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, int reason, unsigned char *number, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		printf("%s: ERROR: type(%d) is out of range.\n", __FUNCTION__, type);
		return;
	}
	if (plan<0 || plan>15)
	{
		printf("%s: ERROR: plan(%d) is out of range.\n", __FUNCTION__, plan);
		return;
	}
	if (present > 3)
	{
		printf("%s: ERROR: present(%d) is out of range.\n", __FUNCTION__, present);
		return;
	}
	if (present >= 0) if (screen<0 || screen>3)
	{
		printf("%s: ERROR: screen(%d) is out of range.\n", __FUNCTION__, screen);
		return;
	}
	if (reason > 0x0f)
	{
		printf("%s: ERROR: reason(%d) is out of range.\n", __FUNCTION__, reason);
		return;
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d screen=%d readon=%d number='%s'\n", type, plan, present, screen, reason, number);

	l = 1;
	if (number)
		l += strlen((char *)number);
	if (present >= 0)
	{
		l += 1;
		if (reason >= 0)
			l += 1;
	}
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(redirect_nr) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_REDIR_NR;
	p[1] = l;
	if (present >= 0)
	{
		if (reason >= 0)
		{
			p[2] = 0x00 + (type<<4) + plan;
			p[3] = 0x00 + (present<<5) + screen;
			p[4] = 0x80 + reason;
			if (number)
				strncpy((char *)p+5, (char *)number, strlen((char *)number));
		} else
		{
			p[2] = 0x00 + (type<<4) + plan;
			p[3] = 0x80 + (present<<5) + screen;
			if (number)
				strncpy((char *)p+4, (char *)number, strlen((char *)number));
		}
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number) if (number[0])
			strncpy((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void dec_ie_redir_nr(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, int *reason, unsigned char *number, int number_len, int nt, struct misdn_bchannel *bc)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*screen = -1;
	*reason = -1;
	*number = '\0';

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(redirect_nr))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(redirect_nr) + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		*present = (p[2]&0x60) >> 5;
		*screen = p[2] & 0x3;
		if (!(p[2] & 0x80))
		{
			*reason = p[3] & 0x0f;
			strnncpy(number, p+4, p[0]-3, number_len);
		} else
		{
			strnncpy(number, p+3, p[0]-2, number_len);
		}
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d screen=%d reason=%d number='%s'\n", *type, *plan, *present, *screen, *reason, number);
}


/* IE_REDIR_DN (redirection = during MT_NOTIFY) */
void enc_ie_redir_dn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, unsigned char *number, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
/* 	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN); */
	int l;

	if (type<0 || type>7)
	{
		printf("%s: ERROR: type(%d) is out of range.\n", __FUNCTION__, type);
		return;
	}
	if (plan<0 || plan>15)
	{
		printf("%s: ERROR: plan(%d) is out of range.\n", __FUNCTION__, plan);
		return;
	}
	if (present > 3)
	{
		printf("%s: ERROR: present(%d) is out of range.\n", __FUNCTION__, present);
		return;
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d number='%s'\n", type, plan, present, number);

	l = 1;
	if (number)
		l += strlen((char *)number);
	if (present >= 0)
		l += 1;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
/* #warning REINSERT redir_dn, when included in te-mode */
		/*qi->QI_ELEMENT(redir_dn) = p - (unsigned char *)qi - sizeof(Q931_info_t)*/;
	p[0] = IE_REDIR_DN;
	p[1] = l;
	if (present >= 0)
	{
		p[2] = 0x00 + (type<<4) + plan;
		p[3] = 0x80 + (present<<5);
		if (number)
			strncpy((char *)p+4, (char *)number, strlen((char *)number));
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number)
			strncpy((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void dec_ie_redir_dn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, unsigned char *number, int number_len, int nt, struct misdn_bchannel *bc)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*number = '\0';

	if (!nt)
	{
		p = NULL;
/* #warning REINSERT redir_dn, when included in te-mode */
/* 		if (qi->QI_ELEMENT(redir_dn)) */
/* 			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(redir_dn) + 1; */
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		printf("%s: ERROR: IE too short (%d).\n", __FUNCTION__, p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		*present = (p[2]&0x60) >> 5;
		strnncpy(number, p+3, p[0]-2, number_len);
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	if (MISDN_IE_DEBG) printf("    type=%d plan=%d present=%d number='%s'\n", *type, *plan, *present, number);
}



/* IE_USERUSER */
void enc_ie_useruser(unsigned char **ntmode, msg_t *msg, int protocol, unsigned char *user, int user_len, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	char debug[768];
	int i;

	if (protocol<0 || protocol>127)
	{
		printf("%s: ERROR: protocol(%d) is out of range.\n", __FUNCTION__, protocol);
		return;
	}
	if (!user || user_len<=0)
	{
		return;
	}

	i = 0;
	while(i < user_len)
	{
		if (MISDN_IE_DEBG) printf(debug+(i*3), " %02x", user[i]);
		i++;
	}
		
	if (MISDN_IE_DEBG) printf("    protocol=%d user-user%s\n", protocol, debug);

	l = user_len;
	p = msg_put(msg, l+3);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(useruser) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_USER_USER;
	p[1] = l;
	p[2] = 0x80 + protocol;
	memcpy(p+3, user, user_len);
}

void dec_ie_useruser(unsigned char *p, Q931_info_t *qi, int *protocol, unsigned char *user, int *user_len, int nt, struct misdn_bchannel *bc)
{
	char debug[768];
	int i;

	*user_len = 0;
	*protocol = -1;

	if (!nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(useruser))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(useruser) + 1;
	}
	if (!p)
		return;

	*user_len = p[0]-1;
	if (p[0] < 1)
		return;
	*protocol = p[1];
	memcpy(user, p+2, (*user_len<=128)?*(user_len):128); /* clip to 128 maximum */

	i = 0;
	while(i < *user_len)
	{
		if (MISDN_IE_DEBG) printf(debug+(i*3), " %02x", user[i]);
		i++;
	}
	debug[i*3] = '\0';
		
	if (MISDN_IE_DEBG) printf("    protocol=%d user-user%s\n", *protocol, debug);
}




