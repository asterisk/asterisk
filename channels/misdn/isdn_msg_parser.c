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


#include "isdn_lib_intern.h"


#include "isdn_lib.h"

#include "ie.c"

#include "fac.h"

void parse_proceeding (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CALL_PROCEEDING_t *proceeding=(CALL_PROCEEDING_t*)((unsigned long)msg->data+ HEADER_LEN);
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	{
		int  exclusive, channel;
		dec_ie_channel_id(proceeding->CHANNEL_ID, (Q931_info_t *)proceeding, &exclusive, &channel, nt,bc);
		
		if (channel==0xff) /* any channel */
			channel=-1;
    
		/*  ALERT: is that everytime true ?  */

		if (channel > 0 && stack->nt) 
			bc->channel = channel;
	}
	
	dec_ie_progress(proceeding->PROGRESS, (Q931_info_t *)proceeding, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
	
	
#if DEBUG 
	printf("Parsing PROCEEDING Msg\n"); 
#endif
}
msg_t *build_proceeding (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt)
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CALL_PROCEEDING_t *proceeding;
	msg_t *msg =(msg_t*)create_l3msg(CC_PROCEEDING | REQUEST, MT_CALL_PROCEEDING,  bc?bc->l3_id:-1, sizeof(CALL_PROCEEDING_t) ,nt); 
  
	proceeding=(CALL_PROCEEDING_t*)((msg->data+HEADER_LEN));

	enc_ie_channel_id(&proceeding->CHANNEL_ID, msg, 1,bc->channel, nt,bc);
  
	if (nt) 
		enc_ie_progress(&proceeding->PROGRESS, msg, 0, nt?1:5, 8, nt,bc);
  

#if DEBUG 
	printf("Building PROCEEDING Msg\n"); 
#endif
	return msg; 
}

void parse_alerting (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN; 
	ALERTING_t *alerting=(ALERTING_t*)((unsigned long)(msg->data+HEADER_LEN));
	//Q931_info_t *qi=(Q931_info_t*)(msg->data+HEADER_LEN);
	
	dec_ie_progress(alerting->PROGRESS, (Q931_info_t *)alerting, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
	
#if DEBUG 
	printf("Parsing ALERTING Msg\n"); 
#endif

 
}
msg_t *build_alerting (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	ALERTING_t *alerting;
	msg_t *msg =(msg_t*)create_l3msg(CC_ALERTING | REQUEST, MT_ALERTING,  bc?bc->l3_id:-1, sizeof(ALERTING_t) ,nt); 
  
	alerting=(ALERTING_t*)((msg->data+HEADER_LEN)); 
  
	enc_ie_channel_id(&alerting->CHANNEL_ID, msg, 1,bc->channel, nt,bc);
	
	if (nt) 
		enc_ie_progress(&alerting->PROGRESS, msg, 0, nt?1:5, 8, nt,bc);
#if DEBUG 
	printf("Building ALERTING Msg\n"); 
#endif
	return msg; 
}


void parse_progress (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	PROGRESS_t *progress=(PROGRESS_t*)((unsigned long)(msg->data+HEADER_LEN)); 
	//Q931_info_t *qi=(Q931_info_t*)(msg->data+HEADER_LEN);  
	
	dec_ie_progress(progress->PROGRESS, (Q931_info_t *)progress, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
	
#if DEBUG 
	printf("Parsing PROGRESS Msg\n"); 
#endif
}

msg_t *build_progress (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	PROGRESS_t *progress;
	msg_t *msg =(msg_t*)create_l3msg(CC_PROGRESS | REQUEST, MT_PROGRESS,  bc?bc->l3_id:-1, sizeof(PROGRESS_t) ,nt); 
 
	progress=(PROGRESS_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building PROGRESS Msg\n"); 
#endif
	return msg; 
}

void parse_setup (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{ 
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_t *setup= (SETUP_t*)((unsigned long)msg->data+HEADER_LEN);
	Q931_info_t *qi=(Q931_info_t*)((unsigned long)msg->data+HEADER_LEN);

#if DEBUG 
	printf("Parsing SETUP Msg\n"); 
#endif
	{
		int type,plan,present, screen;
		char id[32];
		dec_ie_calling_pn(setup->CALLING_PN, qi, &type, &plan, &present, &screen, (unsigned char *)id, sizeof(id), nt,bc);

		bc->onumplan=type; 
		strcpy(bc->oad, id);
		switch (present) {
		case 0:
//			cb_log(3, bc->stack->port, " --> Pres:0\n");
			bc->pres=0; /* screened */
			break;
		case 1:
//			cb_log(3, bc->stack->port, " --> Pres:1\n");
			bc->pres=1; /* not screened */
			break;
		default:
//			cb_log(3, bc->stack->port, " --> Pres:%d\n",present);
			bc->pres=0;
		}
		switch (screen) {
		case 0:
//			cb_log(4, bc->stack->port, " --> Screen:0\n");
			break;
		default:
//			cb_log(4, bc->stack->port, " --> Screen:%d\n",screen);
			;
		} 
	}
	{
		int  type, plan;
		char number[32]; 
		dec_ie_called_pn(setup->CALLED_PN, (Q931_info_t *)setup, &type, &plan, (unsigned char *)number, sizeof(number), nt,bc);
		strcpy(bc->dad, number);
		bc->dnumplan=type; 
	}
	{
		char keypad[32];
		dec_ie_keypad(setup->KEYPAD, (Q931_info_t *)setup, (unsigned char *)keypad, sizeof(keypad), nt,bc);
		strcpy(bc->keypad, keypad);
	}

	{
		int  sending_complete;
		dec_ie_complete(setup->COMPLETE, (Q931_info_t *)setup, &sending_complete, nt,bc);
	}
  
	{
		int  type, plan, present, screen, reason;
		char id[32]; 
		dec_ie_redir_nr(setup->REDIR_NR, (Q931_info_t *)setup, &type, &plan, &present, &screen, &reason, (unsigned char *)id, sizeof(id), nt,bc);
    
		strcpy(bc->rad, id);
		bc->rnumplan=type; 
//		cb_log(3, bc->stack->port, " --> Redirecting number (REDIR_NR): '%s'\n", id);
	}
	{
		int  coding, capability, mode, rate, multi, user, async, urate, stopbits, dbits, parity;
		dec_ie_bearer(setup->BEARER, (Q931_info_t *)setup, &coding, &capability, &mode, &rate, &multi, &user, &async, &urate, &stopbits, &dbits, &parity, nt,bc);
		switch (capability) {
		case -1: bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED; 
//			cb_log(2, bc->stack->port, " --> cap -1 -> digital\n");
			break;
		case 0: bc->capability=INFO_CAPABILITY_SPEECH;
//			cb_log(2, bc->stack->port, " --> cap speech\n");
			break;
		case 8: bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			bc->user1 = user;
			bc->async = async;
			bc->urate = urate;
			
			bc->rate = rate;
			bc->mode = mode;
			
//			cb_log(2, bc->stack->port, " --> cap unres Digital (user l1 %d, async %d, user rate %d\n", user, async, urate);
			break;
		case 9: bc->capability=INFO_CAPABILITY_DIGITAL_RESTRICTED;
//			cb_log(2, bc->stack->port, " --> cap res Digital\n");
			break;
		default:
//			cb_log(2, bc->stack->port, " --> cap Else\n");
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
		if (channel==0xff) /* any channel */
			channel=-1;

		if (channel > 0) 
			bc->channel = channel;
	}
	
	dec_ie_progress(setup->PROGRESS, (Q931_info_t *)setup, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
	
}

#define ANY_CHANNEL 0xff /* IE attribut for 'any channel' */
msg_t *build_setup (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_t *setup;
	msg_t *msg =(msg_t*)create_l3msg(CC_SETUP | REQUEST, MT_SETUP,  bc?bc->l3_id:-1, sizeof(SETUP_t) ,nt); 
  
	setup=(SETUP_t*)((msg->data+HEADER_LEN)); 
  
//	cb_log(2, bc->stack->port, " --> oad %s dad %s channel %d\n",bc->oad, bc->dad,bc->channel);
	if (bc->channel == 0 || bc->channel == ANY_CHANNEL || bc->channel==-1)
		enc_ie_channel_id(&setup->CHANNEL_ID, msg, 0, bc->channel, nt,bc);
	else
		enc_ie_channel_id(&setup->CHANNEL_ID, msg, 1, bc->channel, nt,bc);
  
	{
		int type=bc->onumplan,plan=1,present=bc->pres,screen=bc->screen;
		enc_ie_calling_pn(&setup->CALLING_PN, msg, type, plan, present,
				  screen, bc->oad, nt, bc);
	}
  
	{
		if (bc->dad[0])
			enc_ie_called_pn(&setup->CALLED_PN, msg, bc->dnumplan, 1, bc->dad, nt,bc);
	}
  
	if (*bc->display) {
		enc_ie_display(&setup->DISPLAY, msg, bc->display, nt,bc);
	}
  
	{
		int coding=0, capability, mode=0 /*  2 for packet ! */
			,user, rate=0x10;
		switch (bc->capability) {
		case INFO_CAPABILITY_SPEECH: capability = 0;
//			cb_log(2, bc->stack->port, " --> Speech\n");
			break;
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED: capability = 8;
//			cb_log(2, bc->stack->port, " --> cap unres Digital\n");
			break;
		case INFO_CAPABILITY_DIGITAL_RESTRICTED: capability = 9;
//			cb_log(2, bc->stack->port, " --> cap res Digital\n");
			break;
		default:
//			cb_log(2, bc->stack->port, " --> cap Speech\n");
			capability=bc->capability; 
		}
		
		switch (bc->law) {
		case INFO_CODEC_ULAW: user=2;
//			cb_log(2, bc->stack->port, " --> Codec Ulaw\n");
			break;
		case INFO_CODEC_ALAW: user=3;
//			cb_log(2, bc->stack->port, " --> Codec Alaw\n");
			break;
		default:
			user=3;
		}
    
		enc_ie_bearer(&setup->BEARER, msg, coding, capability, mode, rate, -1, user, nt,bc);
	}
  
#if DEBUG 
	printf("Building SETUP Msg\n"); 
#endif
	return msg; 
}

void parse_connect (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CONNECT_t *connect=(CONNECT_t*)((unsigned long)(msg->data+HEADER_LEN));
  
	bc->ces = connect->ces;
	bc->ces = connect->ces;

	dec_ie_progress(connect->PROGRESS, (Q931_info_t *)connect, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
	
#if DEBUG 
	printf("Parsing CONNECT Msg\n"); 
#endif
}
msg_t *build_connect (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CONNECT_t *connect;
	msg_t *msg =(msg_t*)create_l3msg(CC_CONNECT | REQUEST, MT_CONNECT,  bc?bc->l3_id:-1, sizeof(CONNECT_t) ,nt); 
	
	cb_log(0,0,"BUILD_CONNECT: bc:%p bc->l3id:%d, nt:%d\n",bc,bc->l3_id,nt);

	connect=(CONNECT_t*)((msg->data+HEADER_LEN)); 

	if (nt) {
		time_t now;
		time(&now);
		enc_ie_date(&connect->DATE, msg, now, nt,bc);
	}
  
	{
		int type=0, plan=1, present=2, screen=0;
		enc_ie_connected_pn(&connect->CONNECT_PN, msg, type,plan, present, screen, (unsigned char*) bc->dad , nt , bc);
	}

#if DEBUG 
	printf("Building CONNECT Msg\n"); 
#endif
	return msg; 
}

void parse_setup_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_ACKNOWLEDGE_t *setup_acknowledge=(SETUP_ACKNOWLEDGE_t*)((unsigned long)(msg->data+HEADER_LEN));

	{
		int  exclusive, channel;
		dec_ie_channel_id(setup_acknowledge->CHANNEL_ID, (Q931_info_t *)setup_acknowledge, &exclusive, &channel, nt,bc);

		if (channel==0xff) /* any channel */
			channel=-1;

		if (channel > 0) 
			bc->channel = channel;
	}
	
	dec_ie_progress(setup_acknowledge->PROGRESS, (Q931_info_t *)setup_acknowledge, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
#if DEBUG 
	printf("Parsing SETUP_ACKNOWLEDGE Msg\n"); 
#endif

 
}
msg_t *build_setup_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SETUP_ACKNOWLEDGE_t *setup_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_SETUP_ACKNOWLEDGE | REQUEST, MT_SETUP_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(SETUP_ACKNOWLEDGE_t) ,nt); 
 
	setup_acknowledge=(SETUP_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN)); 
  
	enc_ie_channel_id(&setup_acknowledge->CHANNEL_ID, msg, 1,bc->channel, nt,bc);
  
	if (nt) 
		enc_ie_progress(&setup_acknowledge->PROGRESS, msg, 0, nt?1:5, 8, nt,bc);
  
#if DEBUG 
	printf("Building SETUP_ACKNOWLEDGE Msg\n"); 
#endif
	return msg; 
}

void parse_connect_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing CONNECT_ACKNOWLEDGE Msg\n"); 
#endif

 
}
msg_t *build_connect_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	CONNECT_ACKNOWLEDGE_t *connect_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_CONNECT | RESPONSE, MT_CONNECT,  bc?bc->l3_id:-1, sizeof(CONNECT_ACKNOWLEDGE_t) ,nt); 
 
	connect_acknowledge=(CONNECT_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN)); 
  
	enc_ie_channel_id(&connect_acknowledge->CHANNEL_ID, msg, 1, bc->channel, nt,bc);
  
#if DEBUG 
	printf("Building CONNECT_ACKNOWLEDGE Msg\n"); 
#endif
	return msg; 
}

void parse_user_information (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing USER_INFORMATION Msg\n"); 
#endif

 
}
msg_t *build_user_information (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	USER_INFORMATION_t *user_information;
	msg_t *msg =(msg_t*)create_l3msg(CC_USER_INFORMATION | REQUEST, MT_USER_INFORMATION,  bc?bc->l3_id:-1, sizeof(USER_INFORMATION_t) ,nt); 
 
	user_information=(USER_INFORMATION_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building USER_INFORMATION Msg\n"); 
#endif
	return msg; 
}

void parse_suspend_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing SUSPEND_REJECT Msg\n"); 
#endif

 
}
msg_t *build_suspend_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SUSPEND_REJECT_t *suspend_reject;
	msg_t *msg =(msg_t*)create_l3msg(CC_SUSPEND_REJECT | REQUEST, MT_SUSPEND_REJECT,  bc?bc->l3_id:-1, sizeof(SUSPEND_REJECT_t) ,nt); 
 
	suspend_reject=(SUSPEND_REJECT_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building SUSPEND_REJECT Msg\n"); 
#endif
	return msg; 
}

void parse_resume_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing RESUME_REJECT Msg\n"); 
#endif

 
}
msg_t *build_resume_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESUME_REJECT_t *resume_reject;
	msg_t *msg =(msg_t*)create_l3msg(CC_RESUME_REJECT | REQUEST, MT_RESUME_REJECT,  bc?bc->l3_id:-1, sizeof(RESUME_REJECT_t) ,nt); 
 
	resume_reject=(RESUME_REJECT_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building RESUME_REJECT Msg\n"); 
#endif
	return msg; 
}

void parse_hold (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing HOLD Msg\n"); 
#endif

 
}
msg_t *build_hold (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	HOLD_t *hold;
	msg_t *msg =(msg_t*)create_l3msg(CC_HOLD | REQUEST, MT_HOLD,  bc?bc->l3_id:-1, sizeof(HOLD_t) ,nt); 
 
	hold=(HOLD_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building HOLD Msg\n"); 
#endif
	return msg; 
}

void parse_suspend (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing SUSPEND Msg\n"); 
#endif

 
}
msg_t *build_suspend (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SUSPEND_t *suspend;
	msg_t *msg =(msg_t*)create_l3msg(CC_SUSPEND | REQUEST, MT_SUSPEND,  bc?bc->l3_id:-1, sizeof(SUSPEND_t) ,nt); 
 
	suspend=(SUSPEND_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building SUSPEND Msg\n"); 
#endif
	return msg; 
}

void parse_resume (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing RESUME Msg\n"); 
#endif

 
}
msg_t *build_resume (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESUME_t *resume;
	msg_t *msg =(msg_t*)create_l3msg(CC_RESUME | REQUEST, MT_RESUME,  bc?bc->l3_id:-1, sizeof(RESUME_t) ,nt); 
 
	resume=(RESUME_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building RESUME Msg\n"); 
#endif
	return msg; 
}

void parse_hold_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing HOLD_ACKNOWLEDGE Msg\n"); 
#endif

 
}
msg_t *build_hold_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	HOLD_ACKNOWLEDGE_t *hold_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_HOLD_ACKNOWLEDGE | REQUEST, MT_HOLD_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(HOLD_ACKNOWLEDGE_t) ,nt); 
 
	hold_acknowledge=(HOLD_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building HOLD_ACKNOWLEDGE Msg\n"); 
#endif
	return msg; 
}

void parse_suspend_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing SUSPEND_ACKNOWLEDGE Msg\n"); 
#endif

 
}
msg_t *build_suspend_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	SUSPEND_ACKNOWLEDGE_t *suspend_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_SUSPEND_ACKNOWLEDGE | REQUEST, MT_SUSPEND_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(SUSPEND_ACKNOWLEDGE_t) ,nt); 
 
	suspend_acknowledge=(SUSPEND_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building SUSPEND_ACKNOWLEDGE Msg\n"); 
#endif
	return msg; 
}

void parse_resume_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing RESUME_ACKNOWLEDGE Msg\n"); 
#endif

 
}
msg_t *build_resume_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESUME_ACKNOWLEDGE_t *resume_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_RESUME_ACKNOWLEDGE | REQUEST, MT_RESUME_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(RESUME_ACKNOWLEDGE_t) ,nt); 
 
	resume_acknowledge=(RESUME_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building RESUME_ACKNOWLEDGE Msg\n"); 
#endif
	return msg; 
}

void parse_hold_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing HOLD_REJECT Msg\n"); 
#endif

 
}
msg_t *build_hold_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	HOLD_REJECT_t *hold_reject;
	msg_t *msg =(msg_t*)create_l3msg(CC_HOLD_REJECT | REQUEST, MT_HOLD_REJECT,  bc?bc->l3_id:-1, sizeof(HOLD_REJECT_t) ,nt); 
 
	hold_reject=(HOLD_REJECT_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building HOLD_REJECT Msg\n"); 
#endif
	return msg; 
}

void parse_retrieve (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing RETRIEVE Msg\n"); 
#endif

 
}
msg_t *build_retrieve (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RETRIEVE_t *retrieve;
	msg_t *msg =(msg_t*)create_l3msg(CC_RETRIEVE | REQUEST, MT_RETRIEVE,  bc?bc->l3_id:-1, sizeof(RETRIEVE_t) ,nt); 
 
	retrieve=(RETRIEVE_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building RETRIEVE Msg\n"); 
#endif
	return msg; 
}

void parse_retrieve_acknowledge (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing RETRIEVE_ACKNOWLEDGE Msg\n"); 
#endif

 
}
msg_t *build_retrieve_acknowledge (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RETRIEVE_ACKNOWLEDGE_t *retrieve_acknowledge;
	msg_t *msg =(msg_t*)create_l3msg(CC_RETRIEVE_ACKNOWLEDGE | REQUEST, MT_RETRIEVE_ACKNOWLEDGE,  bc?bc->l3_id:-1, sizeof(RETRIEVE_ACKNOWLEDGE_t) ,nt); 
 
	retrieve_acknowledge=(RETRIEVE_ACKNOWLEDGE_t*)((msg->data+HEADER_LEN)); 

	enc_ie_channel_id(&retrieve_acknowledge->CHANNEL_ID, msg, 1, bc->channel, nt,bc);
#if DEBUG 
	printf("Building RETRIEVE_ACKNOWLEDGE Msg\n"); 
#endif
	return msg; 
}

void parse_retrieve_reject (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing RETRIEVE_REJECT Msg\n"); 
#endif

 
}
msg_t *build_retrieve_reject (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RETRIEVE_REJECT_t *retrieve_reject;
	msg_t *msg =(msg_t*)create_l3msg(CC_RETRIEVE_REJECT | REQUEST, MT_RETRIEVE_REJECT,  bc?bc->l3_id:-1, sizeof(RETRIEVE_REJECT_t) ,nt); 
 
	retrieve_reject=(RETRIEVE_REJECT_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building RETRIEVE_REJECT Msg\n"); 
#endif
	return msg; 
}

void parse_disconnect (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	DISCONNECT_t *disconnect=(DISCONNECT_t*)((unsigned long)(msg->data+HEADER_LEN));
	int location;
  
	dec_ie_cause(disconnect->CAUSE, (Q931_info_t *)(disconnect), &location, &bc->cause, nt,bc);

	dec_ie_progress(disconnect->PROGRESS, (Q931_info_t *)disconnect, &bc->progress_coding, &bc->progress_location, &bc->progress_indicator, nt, bc);
#if DEBUG 
	printf("Parsing DISCONNECT Msg\n"); 
#endif

 
}
msg_t *build_disconnect (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	DISCONNECT_t *disconnect;
	msg_t *msg =(msg_t*)create_l3msg(CC_DISCONNECT | REQUEST, MT_DISCONNECT,  bc?bc->l3_id:-1, sizeof(DISCONNECT_t) ,nt); 
	
	disconnect=(DISCONNECT_t*)((msg->data+HEADER_LEN)); 
	
	enc_ie_cause(&disconnect->CAUSE, msg, (nt)?1:0, bc->out_cause,nt,bc);
	if (nt) enc_ie_progress(&disconnect->PROGRESS, msg, 0, nt?1:5, 8 ,nt,bc);
  
#if DEBUG 
	printf("Building DISCONNECT Msg\n"); 
#endif
	return msg; 
}

void parse_restart (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESTART_t *restart=(RESTART_t*)((unsigned long)(msg->data+HEADER_LEN));

	struct misdn_stack *stack=get_stack_by_bc(bc);
	
#if DEBUG 
	printf("Parsing RESTART Msg\n");
#endif
  
	{
		int  exclusive, channel;
		dec_ie_channel_id(restart->CHANNEL_ID, (Q931_info_t *)restart, &exclusive, &channel, nt,bc);
		if (channel==0xff) /* any channel */
			channel=-1;
		cb_log(0, stack->port, "CC_RESTART Request on channel:%d on this port.\n");
	}
  
 
}
msg_t *build_restart (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RESTART_t *restart;
	msg_t *msg =(msg_t*)create_l3msg(CC_RESTART | REQUEST, MT_RESTART,  bc?bc->l3_id:-1, sizeof(RESTART_t) ,nt); 
 
	restart=(RESTART_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building RESTART Msg\n"); 
#endif
	return msg; 
}

void parse_release (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_t *release=(RELEASE_t*)((unsigned long)(msg->data+HEADER_LEN));
	int location;
  
	dec_ie_cause(release->CAUSE, (Q931_info_t *)(release), &location, &bc->cause, nt,bc);
#if DEBUG 
	printf("Parsing RELEASE Msg\n"); 
#endif

 
}
msg_t *build_release (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_t *release;
	msg_t *msg =(msg_t*)create_l3msg(CC_RELEASE | REQUEST, MT_RELEASE,  bc?bc->l3_id:-1, sizeof(RELEASE_t) ,nt); 
 
	release=(RELEASE_t*)((msg->data+HEADER_LEN)); 
  
  
	enc_ie_cause(&release->CAUSE, msg, nt?1:0, bc->out_cause, nt,bc);
  
#if DEBUG 
	printf("Building RELEASE Msg\n"); 
#endif
	return msg; 
}

void parse_release_complete (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_COMPLETE_t *release_complete=(RELEASE_COMPLETE_t*)((unsigned long)(msg->data+HEADER_LEN));
	int location;
	iframe_t *frm = (iframe_t*) msg->data;

	struct misdn_stack *stack=get_stack_by_bc(bc);
	
#ifdef MISDNUSER_JOLLY
	mISDNuser_head_t *hh;
	hh=(mISDNuser_head_t*)msg->data;
#else
	mISDN_head_t *hh;
	hh=(mISDN_head_t*)msg->data;
#endif
  
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
	dec_ie_cause(release_complete->CAUSE, (Q931_info_t *)(release_complete), &location, &bc->cause, nt,bc);

#if DEBUG 
	printf("Parsing RELEASE_COMPLETE Msg\n"); 
#endif
}

msg_t *build_release_complete (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	RELEASE_COMPLETE_t *release_complete;
	msg_t *msg =(msg_t*)create_l3msg(CC_RELEASE_COMPLETE | REQUEST, MT_RELEASE_COMPLETE,  bc?bc->l3_id:-1, sizeof(RELEASE_COMPLETE_t) ,nt); 
 
	release_complete=(RELEASE_COMPLETE_t*)((msg->data+HEADER_LEN)); 
	
	enc_ie_cause(&release_complete->CAUSE, msg, nt?1:0, bc->out_cause, nt,bc);
  
#if DEBUG 
	printf("Building RELEASE_COMPLETE Msg\n"); 
#endif
	return msg; 
}

void parse_facility (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	FACILITY_t *facility=(FACILITY_t*)((unsigned long)(msg->data+HEADER_LEN)); 
	Q931_info_t *qi=(Q931_info_t*)(msg->data+HEADER_LEN);  

#if DEBUG 
	printf("Parsing FACILITY Msg\n"); 
#endif

	{
		fac_dec(facility->FACILITY, qi, &bc->fac_type, &bc->fac, bc);
	}
}

msg_t *build_facility (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	FACILITY_t *facility;
	msg_t *msg =(msg_t*)create_l3msg(CC_FACILITY | REQUEST, MT_FACILITY,  bc?bc->l3_id:-1, sizeof(FACILITY_t) ,nt); 
 
	facility=(FACILITY_t*)((msg->data+HEADER_LEN)); 

	{
		if (*bc->display) {
			printf("Sending %s as Display\n", bc->display);
			enc_ie_display(&facility->DISPLAY, msg, bc->display, nt,bc);
		}
		
		
		fac_enc(&facility->FACILITY, msg, bc->out_fac_type, bc->out_fac,  bc);
		
	}
	
#if DEBUG 
	printf("Building FACILITY Msg\n"); 
#endif
	return msg; 
}

void parse_notify (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing NOTIFY Msg\n"); 
#endif
}

msg_t *build_notify (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	NOTIFY_t *notify;
	msg_t *msg =(msg_t*)create_l3msg(CC_NOTIFY | REQUEST, MT_NOTIFY,  bc?bc->l3_id:-1, sizeof(NOTIFY_t) ,nt); 
 
	notify=(NOTIFY_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building NOTIFY Msg\n"); 
#endif
	return msg; 
}

void parse_status_enquiry (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing STATUS_ENQUIRY Msg\n"); 
#endif
}

msg_t *build_status_enquiry (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	STATUS_ENQUIRY_t *status_enquiry;
	msg_t *msg =(msg_t*)create_l3msg(CC_STATUS_ENQUIRY | REQUEST, MT_STATUS_ENQUIRY,  bc?bc->l3_id:-1, sizeof(STATUS_ENQUIRY_t) ,nt); 
 
	status_enquiry=(STATUS_ENQUIRY_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building STATUS_ENQUIRY Msg\n"); 
#endif
	return msg; 
}

void parse_information (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	INFORMATION_t *information=(INFORMATION_t*)((unsigned long)(msg->data+HEADER_LEN));
	{
		int  type, plan;
		char number[32];
		char keypad[32];
		dec_ie_called_pn(information->CALLED_PN, (Q931_info_t *)information, &type, &plan, (unsigned char *)number, sizeof(number), nt, bc);
		dec_ie_keypad(information->KEYPAD, (Q931_info_t *)information, (unsigned char *)keypad, sizeof(keypad), nt, bc);
		strcpy(bc->info_dad, number);
		strcpy(bc->keypad,keypad);
	}
#if DEBUG 
	printf("Parsing INFORMATION Msg\n"); 
#endif
}

msg_t *build_information (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	INFORMATION_t *information;
	msg_t *msg =(msg_t*)create_l3msg(CC_INFORMATION | REQUEST, MT_INFORMATION,  bc?bc->l3_id:-1, sizeof(INFORMATION_t) ,nt); 
 
	information=(INFORMATION_t*)((msg->data+HEADER_LEN)); 
  
	{
		enc_ie_called_pn(&information->CALLED_PN, msg, 0, 1, bc->info_dad, nt,bc);
	}

	{
		if (*bc->display) {
			printf("Sending %s as Display\n", bc->display);
			enc_ie_display(&information->DISPLAY, msg, bc->display, nt,bc);
		}
	}
  
#if DEBUG 
	printf("Building INFORMATION Msg\n"); 
#endif
	return msg; 
}

void parse_status (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	STATUS_t *status=(STATUS_t*)((unsigned long)(msg->data+HEADER_LEN));
	int location;
  
	dec_ie_cause(status->CAUSE, (Q931_info_t *)(status), &location, &bc->cause, nt,bc);
	;

#if DEBUG 
	printf("Parsing STATUS Msg\n"); 
#endif
}

msg_t *build_status (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	STATUS_t *status;
	msg_t *msg =(msg_t*)create_l3msg(CC_STATUS | REQUEST, MT_STATUS,  bc?bc->l3_id:-1, sizeof(STATUS_t) ,nt); 
 
	status=(STATUS_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building STATUS Msg\n"); 
#endif
	return msg; 
}

void parse_timeout (struct isdn_msg msgs[], msg_t *msg, struct misdn_bchannel *bc, int nt) 
{
#if DEBUG 
	printf("Parsing STATUS Msg\n"); 
#endif 
}

msg_t *build_timeout (struct isdn_msg msgs[], struct misdn_bchannel *bc, int nt) 
{
	int HEADER_LEN = nt?mISDNUSER_HEAD_SIZE:mISDN_HEADER_LEN;
	STATUS_t *status;
	msg_t *msg =(msg_t*)create_l3msg(CC_STATUS | REQUEST, MT_STATUS,  bc?bc->l3_id:-1, sizeof(STATUS_t) ,nt); 
 
	status=(STATUS_t*)((msg->data+HEADER_LEN)); 

#if DEBUG 
	printf("Building STATUS Msg\n"); 
#endif
	return msg; 
}


/************************************/




/** Msg Array **/

struct isdn_msg msgs_g[] = {
	{CC_PROCEEDING,L3,EVENT_PROCEEDING,
	 parse_proceeding,build_proceeding,
	 "PROCEEDING"},
	{CC_ALERTING,L3,EVENT_ALERTING,
	 parse_alerting,build_alerting,
	 "ALERTING"},
	{CC_PROGRESS,L3,EVENT_PROGRESS,
	 parse_progress,build_progress,
	 "PROGRESS"},
	{CC_SETUP,L3,EVENT_SETUP,
	 parse_setup,build_setup,
	 "SETUP"},
	{CC_CONNECT,L3,EVENT_CONNECT,
	 parse_connect,build_connect,
	 "CONNECT"},
	{CC_SETUP_ACKNOWLEDGE,L3,EVENT_SETUP_ACKNOWLEDGE,
	 parse_setup_acknowledge,build_setup_acknowledge,
	 "SETUP_ACKNOWLEDGE"},
	{CC_CONNECT_ACKNOWLEDGE ,L3,EVENT_CONNECT_ACKNOWLEDGE ,
	 parse_connect_acknowledge ,build_connect_acknowledge,
	 "CONNECT_ACKNOWLEDGE "},
	{CC_USER_INFORMATION,L3,EVENT_USER_INFORMATION,
	 parse_user_information,build_user_information,
	 "USER_INFORMATION"},
	{CC_SUSPEND_REJECT,L3,EVENT_SUSPEND_REJECT,
	 parse_suspend_reject,build_suspend_reject,
	 "SUSPEND_REJECT"},
	{CC_RESUME_REJECT,L3,EVENT_RESUME_REJECT,
	 parse_resume_reject,build_resume_reject,
	 "RESUME_REJECT"},
	{CC_HOLD,L3,EVENT_HOLD,
	 parse_hold,build_hold,
	 "HOLD"},
	{CC_SUSPEND,L3,EVENT_SUSPEND,
	 parse_suspend,build_suspend,
	 "SUSPEND"},
	{CC_RESUME,L3,EVENT_RESUME,
	 parse_resume,build_resume,
	 "RESUME"},
	{CC_HOLD_ACKNOWLEDGE,L3,EVENT_HOLD_ACKNOWLEDGE,
	 parse_hold_acknowledge,build_hold_acknowledge,
	 "HOLD_ACKNOWLEDGE"},
	{CC_SUSPEND_ACKNOWLEDGE,L3,EVENT_SUSPEND_ACKNOWLEDGE,
	 parse_suspend_acknowledge,build_suspend_acknowledge,
	 "SUSPEND_ACKNOWLEDGE"},
	{CC_RESUME_ACKNOWLEDGE,L3,EVENT_RESUME_ACKNOWLEDGE,
	 parse_resume_acknowledge,build_resume_acknowledge,
	 "RESUME_ACKNOWLEDGE"},
	{CC_HOLD_REJECT,L3,EVENT_HOLD_REJECT,
	 parse_hold_reject,build_hold_reject,
	 "HOLD_REJECT"},
	{CC_RETRIEVE,L3,EVENT_RETRIEVE,
	 parse_retrieve,build_retrieve,
	 "RETRIEVE"},
	{CC_RETRIEVE_ACKNOWLEDGE,L3,EVENT_RETRIEVE_ACKNOWLEDGE,
	 parse_retrieve_acknowledge,build_retrieve_acknowledge,
	 "RETRIEVE_ACKNOWLEDGE"},
	{CC_RETRIEVE_REJECT,L3,EVENT_RETRIEVE_REJECT,
	 parse_retrieve_reject,build_retrieve_reject,
	 "RETRIEVE_REJECT"},
	{CC_DISCONNECT,L3,EVENT_DISCONNECT,
	 parse_disconnect,build_disconnect,
	 "DISCONNECT"},
	{CC_RESTART,L3,EVENT_RESTART,
	 parse_restart,build_restart,
	 "RESTART"},
	{CC_RELEASE,L3,EVENT_RELEASE,
	 parse_release,build_release,
	 "RELEASE"},
	{CC_RELEASE_COMPLETE,L3,EVENT_RELEASE_COMPLETE,
	 parse_release_complete,build_release_complete,
	 "RELEASE_COMPLETE"},
	{CC_FACILITY,L3,EVENT_FACILITY,
	 parse_facility,build_facility,
	 "FACILITY"},
	{CC_NOTIFY,L3,EVENT_NOTIFY,
	 parse_notify,build_notify,
	 "NOTIFY"},
	{CC_STATUS_ENQUIRY,L3,EVENT_STATUS_ENQUIRY,
	 parse_status_enquiry,build_status_enquiry,
	 "STATUS_ENQUIRY"},
	{CC_INFORMATION,L3,EVENT_INFORMATION,
	 parse_information,build_information,
	 "INFORMATION"},
	{CC_STATUS,L3,EVENT_STATUS,
	 parse_status,build_status,
	 "STATUS"},
	{CC_TIMEOUT,L3,EVENT_TIMEOUT,
	 parse_timeout,build_timeout,
	 "TIMEOUT"},
	{0,0,0,NULL,NULL,NULL}
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

	cb_log(4,0, "get_index: EVENT NOT FOUND!!\n");
	
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
char EVENT_BCHAN_DATA_INFO[] = "BCHAN_DATA";

char * isdn_get_info(struct isdn_msg msgs[], enum event_e event, int nt)
{
	int i=isdn_msg_get_index_by_event(msgs, event, nt);
	
	if(i>=0) return msgs[i].info;
	
	if (event == EVENT_CLEANUP) return EVENT_CLEAN_INFO;
	if (event == EVENT_DTMF_TONE) return EVENT_DTMF_TONE_INFO;
	if (event == EVENT_NEW_L3ID) return EVENT_NEW_L3ID_INFO;
	if (event == EVENT_NEW_BC) return EVENT_NEW_BC_INFO;
	if (event == EVENT_BCHAN_DATA) return EVENT_BCHAN_DATA_INFO;
	
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

