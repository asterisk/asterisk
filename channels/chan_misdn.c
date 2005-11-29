/*
 * Asterisk -- An open source telephony toolkit.
 * 
 * Copyright (C) 2004, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
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
 *
 */

/*!
 * \file
 *
 * \brief the chan_misdn channel driver for Asterisk
 * \author Christian Richter <crich@beronet.com>
 *
 * \ingroup channel_drivers
 */

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>

#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/io.h>
#include <asterisk/frame.h>
#include <asterisk/translate.h>
#include <asterisk/cli.h>
#include <asterisk/musiconhold.h>
#include <asterisk/dsp.h>
#include <asterisk/translate.h>
#include <asterisk/config.h>
#include <asterisk/file.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/app.h>
#include <asterisk/features.h>

#include "chan_misdn_config.h"
#include "isdn_lib.h"

ast_mutex_t release_lock_mutex;

#define release_lock ast_mutex_lock(&release_lock_mutex)
#define release_unlock ast_mutex_unlock(&release_lock_mutex)


/* BEGIN: chan_misdn.h */

enum misdn_chan_state {
	MISDN_NOTHING,		/*!< at beginning */
	MISDN_WAITING4DIGS, /*!<  when waiting for infos */
	MISDN_EXTCANTMATCH, /*!<  when asterisk couldnt match our ext */
	MISDN_DIALING, /*!<  when pbx_start */
	MISDN_PROGRESS, /*!<  we got a progress */
	MISDN_CALLING, /*!<  when misdn_call is called */
	MISDN_CALLING_ACKNOWLEDGE, /*!<  when we get SETUP_ACK */
	MISDN_ALERTING, /*!<  when Alerting */
	MISDN_BUSY, /*!<  when BUSY */
	MISDN_CONNECTED, /*!<  when connected */
	MISDN_BRIDGED, /*!<  when bridged */
	MISDN_CLEANING, /*!< when hangup from * but we were connected before */
	MISDN_HUNGUP_FROM_MISDN, /*!< when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	MISDN_HUNGUP_FROM_AST, /*!< when DISCONNECT/RELEASE/REL_COMP came out of */
	/* misdn_hangup */
	MISDN_HOLDED, /*!< if this chan is holded */
	MISDN_HOLD_DISCONNECT /*!< if this chan is holded */
  
};

#define ORG_AST 1
#define ORG_MISDN 2

struct chan_list {
  
	ast_mutex_t lock;

	enum misdn_chan_state state;
	int holded; 
	int orginator;

	int norxtone;
	int notxtone; 

	int pipe[2];
	char ast_rd_buf[4096];
	struct ast_frame frame;

	int faxdetect;
	int faxhandled;

	int ast_dsp;
	
	struct ast_dsp *dsp;
	struct ast_trans_pvt *trans;
  
	struct ast_channel * ast;
  
	struct misdn_bchannel *bc;
	struct misdn_bchannel *holded_bc;

	unsigned int l3id;
	int addr;
	
	struct chan_list *peer;
	struct chan_list *next;
	struct chan_list *prev;
	struct chan_list *first;
};

struct robin_list {
	char *group;
	int port;
	int channel;
	struct robin_list *next;
	struct robin_list *prev;
};
static struct robin_list *robin = NULL;

static inline void free_robin_list_r (struct robin_list *r)
{
	if (r) {
		if (r->next) free_robin_list_r(r->next);
		if (r->group) free(r->group);
		free(r);
	}
}

static void free_robin_list ( void )
{
	free_robin_list_r(robin);
}

struct robin_list* get_robin_position (char *group) 
{
	struct robin_list *iter = robin;
	for (; iter; iter = iter->next) {
		if (!strcasecmp(iter->group, group))
			return iter;
	}
	struct robin_list *new = (struct robin_list *)calloc(1, sizeof(struct robin_list));
	new->group = strndup(group, strlen(group));
	new->channel = 1;
	if (robin) {
		new->next = robin;
		robin->prev = new;
	}
	robin = new;
	return robin;
}

struct ast_channel *misdn_new(struct chan_list *cl, int state, char * name, char * context, char *exten, char *callerid, int format, int port, int c);
void send_digit_to_chan(struct chan_list *cl, char digit );


#define AST_CID_P(ast) ast->cid.cid_num
#define AST_BRIDGED_P(ast) ast_bridged_channel(ast) 
#define AST_LOAD_CFG ast_config_load
#define AST_DESTROY_CFG ast_config_destroy

#define MISDN_ASTERISK_TECH_PVT(ast) ast->tech_pvt
#define MISDN_ASTERISK_PVT(ast) 1
#define MISDN_ASTERISK_TYPE(ast) ast->tech->type

/* END: chan_misdn.h */

#include <asterisk/strings.h>

/* #define MISDN_DEBUG 1 */

static  char *desc = "Channel driver for mISDN Support (Bri/Pri)";
static  char *type = "mISDN";

int tracing = 0 ;

static int usecnt=0;

char **misdn_key_vector=NULL;
int misdn_key_vector_size=0;

/* Only alaw and mulaw is allowed for now */
static int prefformat =  AST_FORMAT_ALAW ; /*  AST_FORMAT_SLINEAR ;  AST_FORMAT_ULAW | */

static ast_mutex_t usecnt_lock; 

int *misdn_debug;
int *misdn_debug_only;
int max_ports;

struct chan_list dummy_cl;

struct chan_list *cl_te=NULL;
ast_mutex_t cl_te_lock;

enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data);

void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel*bc);

void cl_queue_chan(struct chan_list **list, struct chan_list *chan);
void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan);
struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc);
void chan_misdn_log(int level, int port, char *tmpl, ...);
void chan_misdn_trace_call(struct ast_channel *chan, int debug, char *tmpl, ...);

static int start_bc_tones(struct chan_list *cl);
static int stop_bc_tones(struct chan_list *cl);
static void release_chan(struct misdn_bchannel *bc);

static int misdn_set_opt_exec(struct ast_channel *chan, void *data);
static int misdn_facility_exec(struct ast_channel *chan, void *data);

/*************** Helpers *****************/

static struct chan_list * get_chan_by_ast(struct ast_channel *ast)
{
	struct chan_list *tmp;
  
	for (tmp=cl_te; tmp; tmp = tmp->next) {
		if ( tmp->ast == ast ) return tmp;
	}
  
	return NULL;
}

static struct chan_list * get_chan_by_ast_name(char *name)
{
	struct chan_list *tmp;
  
	for (tmp=cl_te; tmp; tmp = tmp->next) {
		if ( tmp->ast  && strcmp(tmp->ast->name,name) == 0) return tmp;
	}
  
	return NULL;
}

static char* tone2str(struct misdn_bchannel *bc)
{
	static struct {
		char name[16];
		enum tone_e tone;
	} *tone, tone_buf[] = {
		{"NOTONE",TONE_NONE},
		{"DIAL",TONE_DIAL},
		{"BUSY",TONE_BUSY},
		{"ALERT",TONE_ALERTING},
		{"",TONE_NONE}
	};
  
  
	for (tone=&tone_buf[0]; tone->name[0]; tone++) {
		if (tone->tone == bc->tone) return tone->name;
	}
	return NULL;
}

static char *bearer2str(int cap) {
	static char *bearers[]={
		"Speech",
		"Audio 3.1k",
		"Unres Digital",
		"Res Digital",
		"Unknown Bearer"
	};
	
	switch (cap) {
	case INFO_CAPABILITY_SPEECH:
		return bearers[0];
		break;
	case INFO_CAPABILITY_AUDIO_3_1K:
		return bearers[1];
		break;
	case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		return bearers[2];
		break;
	case INFO_CAPABILITY_DIGITAL_RESTRICTED:
		return bearers[3];
		break;
	default:
		return bearers[4];
		break;
	}
}

static void print_bearer(struct misdn_bchannel *bc) 
{
	
	chan_misdn_log(2, bc->port, " --> Bearer: %s\n",bearer2str(bc->capability));
	
	switch(bc->law) {
	case INFO_CODEC_ALAW:
		chan_misdn_log(2, bc->port, " --> Codec: Alaw\n");
		break;
	case INFO_CODEC_ULAW:
		chan_misdn_log(2, bc->port, " --> Codec: Ulaw\n");
		break;
	}
}
/*************** Helpers END *************/

void send_digit_to_chan(struct chan_list *cl, char digit )
{
	static const char* dtmf_tones[] = {
		"!941+1336/100,!0/100",	/* 0 */
		"!697+1209/100,!0/100",	/* 1 */
		"!697+1336/100,!0/100",	/* 2 */
		"!697+1477/100,!0/100",	/* 3 */
		"!770+1209/100,!0/100",	/* 4 */
		"!770+1336/100,!0/100",	/* 5 */
		"!770+1477/100,!0/100",	/* 6 */
		"!852+1209/100,!0/100",	/* 7 */
		"!852+1336/100,!0/100",	/* 8 */
		"!852+1477/100,!0/100",	/* 9 */
		"!697+1633/100,!0/100",	/* A */
		"!770+1633/100,!0/100",	/* B */
		"!852+1633/100,!0/100",	/* C */
		"!941+1633/100,!0/100",	/* D */
		"!941+1209/100,!0/100",	/* * */
		"!941+1477/100,!0/100" };	/* # */
	struct ast_channel *chan=cl->ast; 
  
	if (digit >= '0' && digit <='9')
		ast_playtones_start(chan,0,dtmf_tones[digit-'0'], 0);
	else if (digit >= 'A' && digit <= 'D')
		ast_playtones_start(chan,0,dtmf_tones[digit-'A'+10], 0);
	else if (digit == '*')
		ast_playtones_start(chan,0,dtmf_tones[14], 0);
	else if (digit == '#')
		ast_playtones_start(chan,0,dtmf_tones[15], 0);
	else {
		/* not handled */
		ast_log(LOG_DEBUG, "Unable to handle DTMF tone '%c' for '%s'\n", digit, chan->name);
    
    
	}
}
/*** CLI HANDLING ***/
static int misdn_set_debug(int fd, int argc, char *argv[])
{
	if (argc != 4 && argc != 5 && argc != 6 && argc != 7)
		return RESULT_SHOWUSAGE; 

	int level = atoi(argv[3]);

	switch (argc) {
		case 4:	
		case 5: {
					int only = 0;
					if (argc == 5) {
						if (strncasecmp(argv[4], "only", strlen(argv[4])))
							return RESULT_SHOWUSAGE;
						else
							only = 1;
					}
					int i;
					for (i=0; i<=max_ports; i++) {
						misdn_debug[i] = level;
						misdn_debug_only[i] = only;
					}
					ast_cli(fd, "changing debug level for all ports to %d%s\n",misdn_debug[0], only?" (only)":"");
				}
				break;
		case 6: 
		case 7: {
					if (strncasecmp(argv[4], "port", strlen(argv[4])))
						return RESULT_SHOWUSAGE;
					int port = atoi(argv[5]);
					if (port <= 0 || port > max_ports) {
						switch (max_ports) {
							case 0:
								ast_cli(fd, "port number not valid! no ports available so you won't get lucky with any number here...\n");
								break;
							case 1:
								ast_cli(fd, "port number not valid! only port 1 is availble.\n");
								break;
							default:
								ast_cli(fd, "port number not valid! only ports 1 to %d are available.\n", max_ports);
							}
							return 0;
					}
					if (argc == 7) {
						if (strncasecmp(argv[6], "only", strlen(argv[6])))
							return RESULT_SHOWUSAGE;
						else
							misdn_debug_only[port] = 1;
					} else
						misdn_debug_only[port] = 0;
					misdn_debug[port] = level;
					ast_cli(fd, "changing debug level to %d%s for port %d\n", misdn_debug[port], misdn_debug_only[port]?" (only)":"", port);
				}
	}
	return 0;
}


static int misdn_set_crypt_debug(int fd, int argc, char *argv[])
{
	if (argc != 5 )return RESULT_SHOWUSAGE; 

	return 0;
}


static int misdn_restart_port (int fd, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_restart(port);

	return 0;
}

static int misdn_port_up (int fd, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	port = atoi(argv[3]);
	
	misdn_lib_get_port_up(port);
  
	return 0;
}


static int misdn_show_config (int fd, int argc, char *argv[])
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int linebreak;

	int onlyport = -1;
	if (argc >= 4) {
		if (!sscanf(argv[3], "%d", &onlyport) || onlyport < 0) {
			ast_cli(fd, "Unknown option: %s\n", argv[3]);
			return RESULT_SHOWUSAGE;
		}
	}
	
	if (argc == 3 || onlyport == 0) {
		ast_cli(fd,"Misdn General-Config: \n"); 
		ast_cli(fd," ->  VERSION: " CHAN_MISDN_VERSION "\n");
		
		for (elem = MISDN_GEN_FIRST + 1, linebreak = 1; elem < MISDN_GEN_LAST; elem++, linebreak++) {
			misdn_cfg_get_config_string( 0, elem, buffer, BUFFERSIZE);
			ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
		}
	}

	if (onlyport < 0) {
		int port = misdn_cfg_get_next_port(0);
		for (; port > 0; port = misdn_cfg_get_next_port(port)) {
			ast_cli(fd, "\n[PORT %d]\n", port);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string( port, elem, buffer, BUFFERSIZE);
				ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}	
			ast_cli(fd, "\n");
		}
	}
	
	if (onlyport > 0) {
		if (misdn_cfg_is_port_valid(onlyport)) {
			ast_cli(fd, "[PORT %d]\n", onlyport);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string( onlyport, elem, buffer, BUFFERSIZE);
				ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}	
			ast_cli(fd, "\n");
		} else {
			ast_cli(fd, "Port %d is not active!\n", onlyport);
		}
	}
	return 0;
}



struct state_struct {
	enum misdn_chan_state state;
	char txt[255] ;
} ;

struct state_struct state_array[] = {
	{MISDN_NOTHING,"NOTHING"}, /* at beginning */
	{MISDN_WAITING4DIGS,"WAITING4DIGS"}, /*  when waiting for infos */
	{MISDN_EXTCANTMATCH,"EXTCANTMATCH"}, /*  when asterisk couldnt match our ext */
	{MISDN_DIALING,"DIALING"}, /*  when pbx_start */
	{MISDN_PROGRESS,"PROGRESS"}, /*  when pbx_start */
	{MISDN_CALLING,"CALLING"}, /*  when misdn_call is called */
	{MISDN_ALERTING,"ALERTING"}, /*  when Alerting */
	{MISDN_BUSY,"BUSY"}, /*  when BUSY */
	{MISDN_CONNECTED,"CONNECTED"}, /*  when connected */
	{MISDN_BRIDGED,"BRIDGED"}, /*  when bridged */
	{MISDN_CLEANING,"CLEANING"}, /* when hangup from * but we were connected before */
	{MISDN_HUNGUP_FROM_MISDN,"HUNGUP_FROM_MISDN"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HOLDED,"HOLDED"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HOLD_DISCONNECT,"HOLD_DISCONNECT"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HUNGUP_FROM_AST,"HUNGUP_FROM_AST"} /* when DISCONNECT/RELEASE/REL_COMP came out of */
	/* misdn_hangup */
};




char *misdn_get_ch_state(struct chan_list *p) 
{
	int i;
	if( !p) return NULL;
  
	for (i=0; i< sizeof(state_array)/sizeof(struct state_struct); i++) {
		if ( state_array[i].state == p->state) return state_array[i].txt; 
	}
  
	return NULL;
}

static int misdn_reload (int fd, int argc, char *argv[])
{
	int i, cfg_debug;
	
	ast_cli(fd, "Reloading mISDN Config\n");
	chan_misdn_log(0, 0, "Dynamic Crypting Activation is not support during reload at the moment\n");
	
	free_robin_list();

	misdn_cfg_reload();

	{
		char tempbuf[BUFFERSIZE];
		misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, tempbuf, BUFFERSIZE);
		if (strlen(tempbuf))
			tracing = 1;
	}

	misdn_cfg_get( 0, MISDN_GEN_DEBUG, &cfg_debug, sizeof(int));
	for (i = 0;  i <= max_ports; i++) {
		misdn_debug[i] = cfg_debug;
		misdn_debug_only[i] = 0;
	}
	
	return 0;
}

static void print_bc_info (int fd, struct chan_list* help, struct misdn_bchannel* bc)
{
	struct ast_channel *ast=help->ast;
	ast_cli(fd,
		"* Pid:%d Prt:%d Ch:%d Mode:%s Org:%s dad:%s oad:%s ctx:%s state:%s\n",
		bc->pid, bc->port, bc->channel,
		bc->nt?"NT":"TE",
		help->orginator == ORG_AST?"*":"I",
		ast?ast->exten:NULL,
		ast?AST_CID_P(ast):NULL,
		ast?ast->context:NULL,
		misdn_get_ch_state(help)
		);
	if (misdn_debug[bc->port] > 0)
		ast_cli(fd,
			"  --> astname: %s\n"
			"  --> ch_l3id: %x\n"
			"  --> ch_addr: %x\n"
			"  --> bc_addr: %x\n"
			"  --> bc_l3id: %x\n"
			"  --> tone: %s\n"
			"  --> display: %s\n"
			"  --> activated: %d\n"
			"  --> capability: %s\n"
			"  --> echo_cancel: %d\n"
			"  --> notone : rx %d tx:%d\n"
			"  --> bc_hold: %d holded_bc :%d\n",
			help->ast->name,
			help->l3id,
			help->addr,
			bc->addr,
			bc?bc->l3_id:-1,
			tone2str(bc),
			bc->display,
			
			bc->active,
			bearer2str(bc->capability),
			bc->ec_enable,
			help->norxtone,help->notxtone,
			bc->holded, help->holded_bc?1:0
			);
  
}


static int misdn_show_cls (int fd, int argc, char *argv[])
{
	struct chan_list *help=cl_te;
  
	ast_cli(fd,"Chan List: %p\n",cl_te); 
  
	for (;help; help=help->next) {
		struct misdn_bchannel *bc=help->bc;   
		struct ast_channel *ast=help->ast;
		if (misdn_debug[0] > 2) ast_cli(fd, "Bc:%p Ast:%p\n", bc, ast);
		if (bc) {
			print_bc_info(fd, help, bc);
		} else if ( (bc=help->holded_bc) ) {
			chan_misdn_log(0, 0, "ITS A HOLDED BC:\n");
			print_bc_info(fd, help,  bc);
		} else {
			ast_cli(fd,"* Channel in unknown STATE !!! Exten:%s, Callerid:%s\n", ast->exten, AST_CID_P(ast));
		}
	}
  
  
	return 0;
}



static int misdn_show_cl (int fd, int argc, char *argv[])
{
	struct chan_list *help=cl_te;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	for (;help; help=help->next) {
		struct misdn_bchannel *bc=help->bc;   
		struct ast_channel *ast=help->ast;
    
		if (bc && ast) {
			if (!strcasecmp(ast->name,argv[3])) {
				print_bc_info(fd, help, bc);
				break; 
			}
		} 
	}
  
  
	return 0;
}

ast_mutex_t lock;
int MAXTICS=8;

static int misdn_set_tics (int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	MAXTICS=atoi(argv[3]);
  
	return 0;
}



static int misdn_show_stacks (int fd, int argc, char *argv[])
{
	int port;
	
	ast_cli(fd, "BEGIN STACK_LIST:\n");
	for (port=misdn_cfg_get_next_port(0); port > 0;
	     port=misdn_cfg_get_next_port(port)) {
		char buf[128];
		get_show_stack_details(port,buf);
		ast_cli(fd,"  %s  Debug:%d%s\n", buf, misdn_debug[port], misdn_debug_only[port]?"(only)":"");
	}
		

	return 0;

}

static int misdn_show_port (int fd, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);
  
	ast_cli(fd, "BEGIN STACK_LIST:\n");

	char buf[128];
	get_show_stack_details(port,buf);
	ast_cli(fd,"  %s  Debug:%d%s\n",buf, misdn_debug[port], misdn_debug_only[port]?"(only)":"");

	
	return 0;
}

static int misdn_send_cd (int fd, int argc, char *argv[])
{
	char *channame; 
	char *nr; 
  
	if (argc != 5)
		return RESULT_SHOWUSAGE;
  
	channame = argv[3];
	nr = argv[4];
	
	ast_cli(fd, "Sending Calldeflection (%s) to %s\n",nr, channame);
	
	{
		struct chan_list *tmp=get_chan_by_ast_name(channame);
		
		if (!tmp) {
			ast_cli(fd, "Sending CD with nr %s to %s failed Channel does not exist\n",nr, channame);
			return 0; 
		} else {
			
			misdn_lib_send_facility(tmp->bc, FACILITY_CALLDEFLECT, nr);
		}
	}
  
	return 0; 
}



static int misdn_send_digit (int fd, int argc, char *argv[])
{
	char *channame; 
	char *msg; 
  
	if (argc != 5)
		return RESULT_SHOWUSAGE;
  
	channame = argv[3];
	msg = argv[4];

	ast_cli(fd, "Sending %s to %s\n",msg, channame);
  
	{
		struct chan_list *tmp=get_chan_by_ast_name(channame);
    
		if (!tmp) {
			ast_cli(fd, "Sending %s to %s failed Channel does not exist\n",msg, channame);
			return 0; 
		} else {
#if 1
			int i;
			int msglen = strlen(msg);
			for (i=0; i<msglen; i++) {
				ast_cli(fd, "Sending: %c\n",msg[i]);
				send_digit_to_chan(tmp, msg[i]);
				/* res = ast_safe_sleep(tmp->ast, 250); */
				usleep(250000);
				/* res = ast_waitfor(tmp->ast,100); */
			}
#else
			int res;
			res = ast_dtmf_stream(tmp->ast,NULL,msg,250);
#endif
		}
	}
  
	return 0; 
}

static int misdn_toggle_echocancel (int fd, int argc, char *argv[])
{
	char *channame; 

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	channame = argv[3];
  
	ast_cli(fd, "Toggling EchoCancel on %s\n", channame);
  
	{
		struct chan_list *tmp=get_chan_by_ast_name(channame);
    
		if (!tmp) {
			ast_cli(fd, "Toggling EchoCancel %s failed Channel does not exist\n", channame);
			return 0; 
		} else {
			tmp->bc->ec_enable=tmp->bc->ec_enable?0:1;

			if (tmp->bc->ec_enable) {
				manager_ec_enable(tmp->bc);
			} else {
				manager_ec_disable(tmp->bc);
			}
		}
	}
  
	return 0; 
}



static int misdn_send_display (int fd, int argc, char *argv[])
{
	char *channame; 
	char *msg; 
  
	if (argc != 5)
		return RESULT_SHOWUSAGE;
  
	channame = argv[3];
	msg = argv[4];

	ast_cli(fd, "Sending %s to %s\n",msg, channame);
	{
		struct chan_list *tmp;
		tmp=get_chan_by_ast_name(channame);
    
		if (tmp && tmp->bc) {
			int l = sizeof(tmp->bc->display);
			strncpy(tmp->bc->display, msg, l);
			tmp->bc->display[l-1] = 0;
			misdn_lib_send_event(tmp->bc, EVENT_INFORMATION);
		} else {
			ast_cli(fd,"No such channel %s\n",channame);
			return RESULT_FAILURE;
		}
	}

	return RESULT_SUCCESS ;
}




static char *complete_ch_helper(char *line, char *word, int pos, int state, int rpos)
{
	struct ast_channel *c;
	int which=0;
	char *ret;
	if (pos != rpos)
		return NULL;
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strncasecmp(word, c->name, strlen(word))) {
			if (++which > state)
				break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (c) {
		ret = strdup(c->name);
		ast_mutex_unlock(&c->lock);
	} else
		ret = NULL;
	return ret;
}

static char *complete_ch(char *line, char *word, int pos, int state)
{
	return complete_ch_helper(line, word, pos, state, 3);
}

static char *complete_debug_port (char *line, char *word, int pos, int state)
{
	if (state)
		return NULL;

	switch (pos) {
	case 4: if (*word == 'p')
				return strdup("port");
			else if (*word == 'o')
				return strdup("only");
			break;
	case 6: if (*word == 'o')
				return strdup("only");
			break;
	}
	return NULL;
}

static struct ast_cli_entry cli_send_cd =
{ {"misdn","send","calldeflect", NULL},
  misdn_send_cd,
  "Sends CallDeflection to mISDN Channel", 
  "Usage: misdn send calldeflect <channel> \"<nr>\" \n",
  complete_ch
};


static struct ast_cli_entry cli_send_digit =
{ {"misdn","send","digit", NULL},
  misdn_send_digit,
  "Sends DTMF Digit to mISDN Channel", 
  "Usage: misdn send digit <channel> \"<msg>\" \n"
  "       Send <digit> to <channel> as DTMF Tone\n"
  "       when channel is a mISDN channel\n",
  complete_ch
};


static struct ast_cli_entry cli_toggle_echocancel =
{ {"misdn","toggle","echocancel", NULL},
  misdn_toggle_echocancel,
  "Toggles EchoCancel on mISDN Channel", 
  "Usage: misdn toggle echocancel <channel>\n", 
  complete_ch
};



static struct ast_cli_entry cli_send_display =
{ {"misdn","send","display", NULL},
  misdn_send_display,
  "Sends Text to mISDN Channel", 
  "Usage: misdn send display <channel> \"<msg>\" \n"
  "       Send <msg> to <channel> as Display Message\n"
  "       when channel is a mISDN channel\n",
  complete_ch
};


static struct ast_cli_entry cli_show_config =
{ {"misdn","show","config", NULL},
  misdn_show_config,
  "Shows internal mISDN config, read from cfg-file", 
  "Usage: misdn show config [port | 0]\n       use 0 to only print the general config.\n"
};
 

static struct ast_cli_entry cli_reload =
{ {"misdn","reload", NULL},
  misdn_reload,
  "Reloads internal mISDN config, read from cfg-file", 
  "Usage: misdn reload\n"
};

static struct ast_cli_entry cli_set_tics =
{ {"misdn","set","tics", NULL},
  misdn_set_tics,
  "", 
  "\n"
};


static struct ast_cli_entry cli_show_cls =
{ {"misdn","show","channels", NULL},
  misdn_show_cls,
  "Shows internal mISDN chan_list", 
  "Usage: misdn show channels\n"
};

static struct ast_cli_entry cli_show_cl =
{ {"misdn","show","channel", NULL},
  misdn_show_cl,
  "Shows internal mISDN chan_list", 
  "Usage: misdn show channels\n",
  complete_ch
};



static struct ast_cli_entry cli_restart_port =
{ {"misdn","restart","port", NULL},
  misdn_restart_port,
  "Restarts the given port", 
  "Usage: misdn restart port\n"
};


static struct ast_cli_entry cli_port_up =
{ {"misdn","port","up", NULL},
  misdn_port_up,
  "Tries to establish L1 on the given port", 
  "Usage: misdn port up <port>\n"
};


static struct ast_cli_entry cli_show_stacks =
{ {"misdn","show","stacks", NULL},
  misdn_show_stacks,
  "Shows internal mISDN stack_list", 
  "Usage: misdn show stacks\n"
};

static struct ast_cli_entry cli_show_port =
{ {"misdn","show","port", NULL},
  misdn_show_port,
  "Shows detailed information for given port", 
  "Usage: misdn show port <port>\n"
};



static struct ast_cli_entry cli_set_debug =
{ {"misdn","set","debug", NULL},
  misdn_set_debug,
  "Sets Debuglevel of chan_misdn",
  "Usage: misdn set debug <level> [only] | [port <port> [only]]\n",
  complete_debug_port
};

static struct ast_cli_entry cli_set_crypt_debug =
{ {"misdn","set","crypt","debug", NULL},
  misdn_set_crypt_debug,
  "Sets CryptDebuglevel of chan_misdn, at the moment, level={1,2}", 
  "Usage: misdn set crypt debug <level>\n"
};
/*** CLI END ***/


/*****************************/
/*** AST Indications Start ***/
/*****************************/

static int misdn_call(struct ast_channel *ast, char *dest, int timeout)
{
	int port=0;
	int r;
	struct chan_list *ch=MISDN_ASTERISK_TECH_PVT(ast);
	struct misdn_bchannel *newbc;
	char *opts=NULL;
	char dest_cp[256];
	
	{
		strncpy(dest_cp,dest,sizeof(dest_cp)-1);
		dest_cp[sizeof(dest_cp)]=0;
		opts=strchr(dest_cp,'/');
		if ( opts && (opts=strchr(++opts,'/')) ) {
			if (opts) {
				opts++;
				if (!*opts) opts=NULL;
			}
		}
	}
	
	if (!ast) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on ast_channel *ast where ast == NULL\n");
		return -1;
	}

	if (((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) || !dest  ) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause=41;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}


	if (!ch) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause=41;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	newbc=ch->bc;
	
	if (!newbc) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause=41;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	port=newbc->port;

	
	chan_misdn_log(1, 0, "* CALL: %s\n",dest);
	
	chan_misdn_log(1, port, " --> * dad:%s tech:%s ctx:%s\n",ast->exten,ast->name, ast->context);
	
	{
		char context[BUFFERSIZE];
		
		misdn_cfg_get( port, MISDN_CFG_CONTEXT, context, sizeof(ast->context));
		{
			int l = sizeof(ast->context);
			strncpy(ast->context,context, l);
			ast->context[l-1] = 0;
		}
		chan_misdn_log(2, port, " --> * Setting Context to %s\n",context);
		misdn_cfg_get( port, MISDN_CFG_LANGUAGE, ast->language, BUFFERSIZE);
		
		misdn_cfg_get( port, MISDN_CFG_TXGAIN, &newbc->txgain, sizeof(int));
		misdn_cfg_get( port, MISDN_CFG_RXGAIN, &newbc->rxgain, sizeof(int));
		
		misdn_cfg_get( port, MISDN_CFG_TE_CHOOSE_CHANNEL, &(newbc->te_choose_channel), sizeof(int));
		

		{
			char callerid[BUFFERSIZE];
			misdn_cfg_get( port, MISDN_CFG_CALLERID, callerid, BUFFERSIZE);
			if ( ! ast_strlen_zero(callerid) ) {
				chan_misdn_log(1, port, " --> * Setting Cid to %s\n", callerid);
				{
					int l = sizeof(newbc->oad);
					strncpy(newbc->oad,callerid, l);
					newbc->oad[l-1] = 0;
				}
				
				misdn_cfg_get( port, MISDN_CFG_DIALPLAN, &newbc->dnumplan, sizeof(int));
				switch (newbc->dnumplan) {
				case NUMPLAN_INTERNATIONAL:
				case NUMPLAN_NATIONAL:
				case NUMPLAN_SUBSCRIBER:
				case NUMPLAN_UNKNOWN:
					/* Maybe we should cut off the prefix if present ? */
					break;
				default:
					chan_misdn_log(0, port, " --> !!!! Wrong dialplan setting, please see the misdn.conf sample file\n ");
					break;
				}
				
			}
		}
		
		/* Will be overridden by asterisk in head! */
		{
			int pres;
			
			misdn_cfg_get( port, MISDN_CFG_PRES, &pres, sizeof(int));
			newbc->pres=pres?0:1;
			
		}
		
		int def_callingpres;
		misdn_cfg_get( port, MISDN_CFG_USE_CALLINGPRES, &def_callingpres, sizeof(int));
		if ( def_callingpres) {
			switch (ast->cid.cid_pres){
			case AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
				newbc->pres=1;
				break;
				
			case AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
				newbc->pres=0;
				break;
			default:
				newbc->pres=0;
			}
		}

		
		{
			int ec, ectr;
			
			misdn_cfg_get( port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
			
			misdn_cfg_get( port, MISDN_CFG_ECHOTRAINING, &ectr, sizeof(int));
			if (ec == 1 ) {
				newbc->ec_enable=1;
			} else if ( ec > 1 ) {
				newbc->ec_enable=1;
				newbc->ec_deftaps=ec;
			}

			if ( !ectr ) {
				newbc->ec_training=0;
			}
		}
		
	} 
	
	chan_misdn_log(3, port, " --> * adding2newbc ext %s\n",ast->exten);
	if (ast->exten) {
		int l = sizeof(newbc->dad);
		strncpy(newbc->dad,ast->exten, l);
		newbc->dad[l-1] = 0;
	}
	newbc->rad[0]=0;
	chan_misdn_log(3, port, " --> * adding2newbc callerid %s\n",AST_CID_P(ast));
	if (ast_strlen_zero(newbc->oad) && AST_CID_P(ast) ) {
		if (AST_CID_P(ast)) {
			int l = sizeof(newbc->oad);
			strncpy(newbc->oad,AST_CID_P(ast), l);
			newbc->oad[l-1] = 0;
		}
	}
	
	{
		struct chan_list *ch=MISDN_ASTERISK_TECH_PVT(ast);
		if (!ch) { ast_verbose("No chan_list in misdn_call"); return -1;}
		ch->bc = newbc;
		ch->orginator=ORG_AST;
		ch->ast = ast;
		
		MISDN_ASTERISK_TECH_PVT(ast) = ch ;
		
		
      
		newbc->capability=ast->transfercapability;
		pbx_builtin_setvar_helper(ast,"TRANSFERCAPABILITY",ast_transfercapability2str(newbc->capability));
		if ( ast->transfercapability == INFO_CAPABILITY_DIGITAL_UNRESTRICTED) {
			chan_misdn_log(2, port, " --> * Call with flag Digital\n");
		}
		

		/* Finally The Options Override Everything */
		if (opts)
			misdn_set_opt_exec(ast,opts);
		else
			chan_misdn_log(1,0,"NO OPTS GIVEN\n");
		
		
		cl_queue_chan(&cl_te, ch) ;
		ch->state=MISDN_CALLING;

		chan_misdn_trace_call(ast,1,"*->I: EVENT_CALL\n" );
		
		r=misdn_lib_send_event( newbc, EVENT_SETUP );
		
		/** we should have l3id after sending setup **/
		ch->l3id=newbc->l3_id;
	}
	
	if ( r == -ENOCHAN  ) {
		chan_misdn_log(0, port, " --> * Theres no Channel at the moment .. !\n");
		chan_misdn_log(1, port, " --> * SEND: State Down pid:%d\n",newbc?newbc->pid:-1);
		ast->hangupcause=34;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	chan_misdn_log(1, port, " --> * SEND: State Dialing pid:%d\n",newbc?newbc->pid:1);

	ast_setstate(ast, AST_STATE_DIALING);
	
	ast->hangupcause=16;
	return 0; 
}


int misdn_answer(struct ast_channel *ast)
{
	struct chan_list *p;

	
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) return -1;
	p = MISDN_ASTERISK_TECH_PVT(ast) ;
	
	chan_misdn_trace_call(ast,1,"*->I: EVENT_ANSWER\n");
	
	chan_misdn_log(1, p? (p->bc? p->bc->port : 0) : 0, "* ANSWER:\n");
	
	if (!p) {
		ast_log(LOG_WARNING, " --> Channel not connected ??\n");
		ast_queue_hangup(ast);
	}

	if (!p->bc) {
		chan_misdn_log(1, 0, " --> Got Answer, but theres no bc obj ??\n");

		ast_queue_hangup(ast);
	}

	{
		char *tmp_key = pbx_builtin_getvar_helper(p->ast, "CRYPT_KEY");
		
		if (tmp_key ) {
			chan_misdn_log(1, p->bc->port, " --> Connection will be BF crypted\n");
			{
				int l = sizeof(p->bc->crypt_key);
				strncpy(p->bc->crypt_key,tmp_key, l);
				p->bc->crypt_key[l-1] = 0;
			}
		} else {
			chan_misdn_log(3, p->bc->port, " --> Connection is without BF encryption\n");
		}
    
	}
	
	p->state = MISDN_CONNECTED;
	misdn_lib_send_event( p->bc, EVENT_CONNECT);
	start_bc_tones(p);
	
  
	return 0;
}

int misdn_digit(struct ast_channel *ast, char digit )
{
	struct chan_list *p;
	
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) return -1;
	p = MISDN_ASTERISK_TECH_PVT(ast) ;
	

	struct misdn_bchannel *bc=p->bc;
	chan_misdn_log(1, bc?bc->port:0, "* IND : Digit %c\n",digit);
	
	if (!bc) {
		ast_log(LOG_WARNING, " --> !! Got Digit Event withut having bchannel Object\n");
		return -1;
	}
	
	switch (p->state ) {
		case MISDN_CALLING:
		{
			
			char buf[8];
			buf[0]=digit;
			buf[1]=0;
			
			int l = sizeof(bc->infos_pending);
			strncat(bc->infos_pending,buf,l);
			bc->infos_pending[l-1] = 0;
		}
		break;
		case MISDN_CALLING_ACKNOWLEDGE:
		{
			bc->info_dad[0]=digit;
			bc->info_dad[1]=0;
			
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad,bc->info_dad, l - strlen(bc->dad));
				bc->dad[l-1] = 0;
		}
			{
				int l = sizeof(p->ast->exten);
				strncpy(p->ast->exten, bc->dad, l);
				p->ast->exten[l-1] = 0;
			}
			
			misdn_lib_send_event( bc, EVENT_INFORMATION);
		}
		break;
		
		default:
			if ( bc->send_dtmf ) {
				send_digit_to_chan(p,digit);
			}
		break;
	}
	
	return 0;
}


int misdn_fixup(struct ast_channel *oldast, struct ast_channel *ast)
{
	struct chan_list *p;
	
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) return -1;
	p = MISDN_ASTERISK_TECH_PVT(ast) ;
	
	chan_misdn_log(1, p->bc?p->bc->port:0, "* IND: Got Fixup State:%s Holded:%d L3id:%x\n", misdn_get_ch_state(p), p->holded, p->l3id);
	
	p->ast = ast ;
	p->state=MISDN_CONNECTED;
  
	return 0;
}


int misdn_transfer (struct ast_channel *ast, char *dest)
{
	struct chan_list *p;
	
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) return -1;
	p = MISDN_ASTERISK_TECH_PVT(ast) ;

	chan_misdn_log(1, p->bc?p->bc->port:0, "* IND : Got Transfer %s\n",dest);
	return 0;
}



int misdn_indication(struct ast_channel *ast, int cond)
{
	struct chan_list *p;

  
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) {
		ast_log(LOG_WARNING, "Returnded -1 in misdn_indication\n");
		return -1;
	}
	p = MISDN_ASTERISK_TECH_PVT(ast) ;
	
	if (!p->bc ) {
		chan_misdn_log(1, 0, "* IND : Indication from %s\n",ast->exten);
		ast_log(LOG_WARNING, "Private Pointer but no bc ?\n");
		return -1;
	}
	
	chan_misdn_log(1, p->bc->port, "* IND : Indication from %s\n",ast->exten);
	
	switch (cond) {
	case AST_CONTROL_BUSY:
		chan_misdn_log(1, p->bc->port, "* IND :\tbusy\n");
		chan_misdn_log(1, p->bc->port, " --> * SEND: State Busy pid:%d\n",p->bc?p->bc->pid:-1);
		ast_setstate(ast,AST_STATE_BUSY);
		
		p->bc->out_cause=17;
		if (p->state != MISDN_CONNECTED) {
			misdn_lib_send_event( p->bc, EVENT_DISCONNECT);
			manager_send_tone(p->bc, TONE_BUSY);
		} else {
			chan_misdn_log(0, p->bc->port, " --> !! Got Busy in Connected State !?! port:%d ast:%s\n",
				       p->bc->port, ast->name);
		}
		break;
	case AST_CONTROL_RING:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tring pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_RINGING:
		if ( p->state == MISDN_ALERTING) {
			chan_misdn_log(1, p->bc->port, " --> * IND :\tringing pid:%d but I ws Ringing before, so ignoreing it\n",p->bc?p->bc->pid:-1);
			break;
		}
		p->state=MISDN_ALERTING;
		
		chan_misdn_log(1, p->bc->port, " --> * IND :\tringing pid:%d\n",p->bc?p->bc->pid:-1);
		
		misdn_lib_send_event( p->bc, EVENT_ALERTING);
		
		manager_send_tone(p->bc, TONE_ALERTING);
		chan_misdn_log(1, p->bc->port, " --> * SEND: State Ring pid:%d\n",p->bc?p->bc->pid:-1);
		ast_setstate(ast,AST_STATE_RINGING);
		break;
		
	case AST_CONTROL_ANSWER:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tanswer pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_TAKEOFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\ttakeoffhook pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_OFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\toffhook pid:%d\n",p->bc?p->bc->pid:-1);
		break; 
	case AST_CONTROL_FLASH:
		chan_misdn_log(1, p->bc->port, " --> *\tflash pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_PROGRESS:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tprogress pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_CONGESTION:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tcongestion pid:%d\n",p->bc?p->bc->pid:-1);

		p->bc->out_cause=42;
		if (p->state != MISDN_CONNECTED) {
			start_bc_tones(p);
			//misdn_lib_send_event( p->bc, EVENT_RELEASE_COMPLETE);
			misdn_lib_send_event( p->bc, EVENT_RELEASE);
		} else {
			misdn_lib_send_event( p->bc, EVENT_DISCONNECT);
		}
		if (p->bc->nt) {
			manager_send_tone(p->bc, TONE_BUSY);
		}
		break;
	case -1 :
		chan_misdn_log(1, p->bc->port, " --> * IND :\t-1! pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_HOLD:
		chan_misdn_log(1, p->bc->port, " --> *\tHOLD pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_UNHOLD:
		chan_misdn_log(1, p->bc->port, " --> *\tUNHOLD pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	default:
		ast_log(LOG_WARNING, " --> * Unknown Indication:%d pid:%d\n",cond,p->bc?p->bc->pid:-1);
	}
  
	return 0;
}

int misdn_hangup(struct ast_channel *ast)
{
	struct chan_list *p;
	struct misdn_bchannel *bc=NULL;
	
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) return -1;
	p = MISDN_ASTERISK_TECH_PVT(ast) ;

	release_lock;

	chan_misdn_trace_call(ast,1,"*->I: EVENT_HANGUP cause=%d\n",ast->hangupcause);
	
	ast_log(LOG_DEBUG, "misdn_hangup(%s)\n", ast->name);
	
	if (!p) {
		chan_misdn_log(3, 0, "misdn_hangup called, without chan_list obj.\n");
		release_unlock;
		return 0 ;
	}
	

	
	MISDN_ASTERISK_TECH_PVT(ast)=NULL;
	p->ast=NULL;

	if (ast->_state == AST_STATE_RESERVED) {
		/* between request and call */
		MISDN_ASTERISK_TECH_PVT(ast)=NULL;
		release_unlock;
		
		cl_dequeue_chan(&cl_te, p);
		free(p);

		misdn_lib_release(bc);
		
		return 0;
	}

	stop_bc_tones(p);
	
	release_unlock;
	
	
	bc=p->bc;
	
	if (!bc) {
		ast_log(LOG_WARNING,"Hangup with private but no bc ?\n");
		return 0;
	}
	
	{
		char *varcause=NULL;
		bc->cause=ast->hangupcause?ast->hangupcause:16;
		
		if ( (varcause=pbx_builtin_getvar_helper(ast, "HANGUPCAUSE")) ||
		     (varcause=pbx_builtin_getvar_helper(ast, "PRI_CAUSE"))) {
			int tmpcause=atoi(varcause);
			bc->out_cause=tmpcause?tmpcause:16;
		}
    
		chan_misdn_log(1, bc->port, "* IND : HANGUP\tpid:%d ctx:%s dad:%s oad:%s State:%s\n",p->bc?p->bc->pid:-1, ast->context, ast->exten, AST_CID_P(ast), misdn_get_ch_state(p));
		chan_misdn_log(2, bc->port, " --> l3id:%x\n",p->l3id);
		chan_misdn_log(1, bc->port, " --> cause:%d\n",bc->cause);
		chan_misdn_log(1, bc->port, " --> out_cause:%d\n",bc->out_cause);
		
		switch (p->state) {
		case MISDN_CALLING:
			p->state=MISDN_CLEANING;
			misdn_lib_send_event( bc, EVENT_RELEASE_COMPLETE);
			break;
		case MISDN_HOLDED:
		case MISDN_DIALING:
			start_bc_tones(p);
			manager_send_tone(bc, TONE_BUSY);
			p->state=MISDN_CLEANING;
			
			misdn_lib_send_event( bc, EVENT_RELEASE_COMPLETE);
      
			break;
      
		case MISDN_ALERTING:
			chan_misdn_log(2, bc->port, " --> * State Alerting\n");

			if (p->orginator != ORG_AST) 
				manager_send_tone(bc, TONE_BUSY);
      
			p->state=MISDN_CLEANING;
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
			break;
		case MISDN_CONNECTED:
			/*  Alerting or Disconect */
			chan_misdn_log(2, bc->port, " --> * State Connected\n");
			start_bc_tones(p);
			manager_send_tone(bc, TONE_BUSY);
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
      
			p->state=MISDN_CLEANING; /* MISDN_HUNGUP_FROM_AST; */
			break;

		case MISDN_CLEANING:
			break;
      
		case MISDN_HOLD_DISCONNECT:
			/* need to send release here */
			chan_misdn_log(2, bc->port, " --> state HOLD_DISC\n");
			chan_misdn_log(1, bc->port, " --> cause %d\n",bc->cause);
			chan_misdn_log(1, bc->port, " --> out_cause %d\n",bc->out_cause);
			
			misdn_lib_send_event(bc,EVENT_RELEASE);
			break;
		default:
			/*  Alerting or Disconect */
			if (bc->nt)
				misdn_lib_send_event(bc, EVENT_RELEASE);
			else
				misdn_lib_send_event(bc, EVENT_DISCONNECT);
			p->state=MISDN_CLEANING; /* MISDN_HUNGUP_FROM_AST; */
		}
    
	}
	
	chan_misdn_log(1, bc->port, "Channel: %s hanguped\n",ast->name);
	
	return 0;
}

struct ast_frame  *misdn_read(struct ast_channel *ast)
{
	struct chan_list *tmp;
	
	char blah[255];
	int len =0 ;
	
	if (!ast) return NULL;
	tmp = MISDN_ASTERISK_TECH_PVT(ast);
	if (!tmp) return NULL;
	if (!tmp->bc) return NULL;
	
	
	read(tmp->pipe[0],blah,sizeof(blah));
	
	
	len = misdn_ibuf_usedcount(tmp->bc->astbuf);

	/*shrinken len if necessary, we transmit at maximum 4k*/
	len = len<=sizeof(tmp->ast_rd_buf)?len:sizeof(tmp->ast_rd_buf);
	
	misdn_ibuf_memcpy_r(tmp->ast_rd_buf, tmp->bc->astbuf,len);
	
	tmp->frame.frametype  = AST_FRAME_VOICE;
	tmp->frame.subclass = AST_FORMAT_ALAW;
	tmp->frame.datalen = len;
	tmp->frame.samples = len ;
	tmp->frame.mallocd =0 ;
	tmp->frame.offset= 0 ;
	tmp->frame.src = NULL;
	tmp->frame.data = tmp->ast_rd_buf ;

	chan_misdn_trace_call(tmp->ast,3,"*->I: EVENT_READ len=%d\n",len);
	
	return &tmp->frame;
}

int misdn_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct chan_list *p;
	int i  = 0;
	
	if (!ast || ! MISDN_ASTERISK_PVT(ast)) return -1;
	p = MISDN_ASTERISK_TECH_PVT(ast) ;
	
	if (!p->bc ) {
		ast_log(LOG_WARNING, "private but no bc\n");
		return -1;
	}
	
	if (p->bc->tone != TONE_NONE)
		manager_send_tone(p->bc,TONE_NONE);
	
	
	if (p->holded ) {
		chan_misdn_log(5, p->bc->port, "misdn_write: Returning because holded\n");
		return 0;
	}
	
	if (p->notxtone) {
		chan_misdn_log(5, p->bc->port, "misdn_write: Returning because notxone\n");
		return 0;
	}
	
	if ( !(frame->subclass & prefformat)) {
		chan_misdn_log(0, p->bc->port, "Got Unsupported Frame with Format:%d\n", frame->subclass);
	}
	
	
#if MISDN_DEBUG
	{
		int i, max=5>frame->samples?frame->samples:5;
		
		printf("write2mISDN %p %d bytes: ", p, frame->samples);
		
		for (i=0; i<  max ; i++) printf("%2.2x ",((char*) frame->data)[i]);
		printf ("\n");
	}
#endif
	chan_misdn_trace_call(ast,3,"*->I: EVENT_WRITE len=%d\n",frame->samples);
	
	i= manager_tx2misdn_frm(p->bc, frame->data, frame->samples);
	
	return 0;
}


enum ast_bridge_result  misdn_bridge (struct ast_channel *c0,
				      struct ast_channel *c1, int flags,
				      struct ast_frame **fo,
				      struct ast_channel **rc,
				      int timeoutms)

{
	struct chan_list *ch1,*ch2;
	struct ast_channel *carr[2], *who;
	int to=-1;
	struct ast_frame *f;
  
	ch1=get_chan_by_ast(c0);
	ch2=get_chan_by_ast(c1);

	carr[0]=c0;
	carr[1]=c1;
  
  
	if (ch1 && ch2 ) ;
	else
		return -1;
  

	int bridging;
	misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
	if (bridging) {
		int ecwb;
		misdn_cfg_get( ch1->bc->port, MISDN_CFG_ECHOCANCELWHENBRIDGED, &ecwb, sizeof(int));
		if ( !ecwb ) {
			chan_misdn_log(0, ch1->bc->port, "Disabling Echo Cancellor when Bridged\n");
			ch1->bc->ec_enable=0;
			manager_ec_disable(ch1->bc);
		}
		misdn_cfg_get( ch2->bc->port, MISDN_CFG_ECHOCANCELWHENBRIDGED, &ecwb, sizeof(int));
		if ( !ecwb ) {
			chan_misdn_log(0, ch2->bc->port, "Disabling Echo Cancellor when Bridged\n");
			ch2->bc->ec_enable=0;
			manager_ec_disable(ch2->bc);
		}
		
		/* trying to make a mISDN_dsp conference */
		chan_misdn_log(0, ch1->bc->port, "I SEND: Making conference with Number:%d\n", (ch1->bc->pid<<1) +1);

		misdn_lib_bridge(ch1->bc,ch2->bc);
	}
	
	chan_misdn_log(1, ch1->bc->port, "* Makeing Native Bridge between %s and %s\n", ch1->bc->oad, ch2->bc->oad);
  
	while(1) {
		to=-1;
		who = ast_waitfor_n(carr, 2, &to);

		if (!who) {
			ast_log(LOG_DEBUG,"misdn_bridge: empty read\n");
			continue;
		}
		f = ast_read(who);
    
		if (!f || f->frametype == AST_FRAME_CONTROL) {
			/* got hangup .. */
			*fo=f;
			*rc=who;
      
			break;
		}
    
    
		if (who == c0) {
			ast_write(c1,f);
		}
		else {
			ast_write(c0,f);
		}
    
	}
  
	if (bridging) {
		misdn_lib_split_bridge(ch1->bc,ch2->bc);
	}
  
	return 0;
}

/** AST INDICATIONS END **/

static int start_bc_tones(struct chan_list* cl)
{
	manager_bchannel_activate(cl->bc);
	manager_send_tone(cl->bc ,TONE_NONE);
	cl->notxtone=0;
	cl->norxtone=0;
	return 0;
}

static int stop_bc_tones(struct chan_list *cl)
{
	if (cl->bc) {
		manager_bchannel_deactivate(cl->bc);
	}
	cl->notxtone=1;
	cl->norxtone=1;
	
	return 0;
}


struct chan_list *init_chan_list(void)
{
	struct chan_list *cl=malloc(sizeof(struct chan_list));
	
	if (!cl) {
		chan_misdn_log(0, 0, "misdn_request: malloc failed!");
		return NULL;
	}
	
	memset(cl,0,sizeof(struct chan_list));
	
	return cl;
	
}

static struct ast_channel *misdn_request(const char *type, int format, void *data, int *cause)

{
	struct ast_channel *tmp = NULL;
	char group[BUFFERSIZE]="";
	char buf[128];
	char buf2[128], *ext=NULL, *port_str;
	char *tokb=NULL, *p=NULL;
	int channel=0, port=0;
	struct misdn_bchannel *newbc = NULL;
	
	struct chan_list *cl=init_chan_list();
	
	sprintf(buf,"%s/%s",type,(char*)data);
	strncpy(buf2,data, 128);
	buf2[127] = 0;
	port_str=strtok_r(buf2,"/", &tokb);

	ext=strtok_r(NULL,"/", &tokb);
	
	if (!ext) {
		ast_log(LOG_WARNING, " --> ! IND : CALL dad:%s WITH WRONG ARGS, check extension.conf\n",ext);
		
		return NULL;
	}
 	
	if (port_str) {
		if (port_str[0]=='g' && port_str[1]==':' ) {
			/* We make a group call lets checkout which ports are in my group */
			port_str += 2;
			strncpy(group, port_str, BUFFERSIZE);
			group[127] = 0;
			chan_misdn_log(2, 0, " --> Group Call group: %s\n",group);
		} 
		else if ((p = strchr(port_str, ':'))) {
			// we have a preselected channel
			*p = 0;
			channel = atoi(++p);
			port = atoi(port_str);
			chan_misdn_log(2, port, " --> Call on preselected Channel (%d) on Port %d\n", channel, port);
		}
		else {
			port = atoi(port_str);
		}
		
		
	} else {
		ast_log(LOG_WARNING, " --> ! IND : CALL dad:%s WITHOUT PORT/Group, check extension.conf\n",ext);
		return NULL;
	}

	if (!ast_strlen_zero(group)) {
	
		char cfg_group[BUFFERSIZE];
		struct robin_list *rr = NULL;

		if (misdn_cfg_is_group_method(group, METHOD_ROUND_ROBIN)) {
			chan_misdn_log(4, port, " --> STARTING ROUND ROBIN...");
			rr = get_robin_position(group);
		}
		
		if (rr) {
			int robin_channel = rr->channel;
			int port_start;
			int next_chan = 1;

			do {
				port_start = 0;
				for (port = misdn_cfg_get_next_port_spin(rr->port); port > 0 && port != port_start;
					 port = misdn_cfg_get_next_port_spin(port)) {

					if (!port_start)
						port_start = port;

					if (port >= port_start)
						next_chan = 1;
					
					if (port < port_start && next_chan) {
						if (++robin_channel >= MAX_BCHANS) {
							robin_channel = 1;
						}
						next_chan = 0;
					}

					misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);
					
					if (!strcasecmp(cfg_group, group)) {
						int l1, port_up;
					
						misdn_cfg_get( 0, MISDN_GEN_L1_INFO_OK, &l1, sizeof(l1));
						port_up = misdn_lib_port_up(port);
						
						if ((l1 && port_up) || !l1)	{
							newbc = misdn_lib_get_free_bc(port, robin_channel);
							if (newbc) {
								chan_misdn_log(4, port, " Success! Found port:%d channel:%d\n", newbc->port, newbc->channel);
								if (port_up)
									chan_misdn_log(4, port, "def_l1:%d, portup:%d\n", l1, port_up);
								rr->port = newbc->port;
								rr->channel = newbc->channel;
								break;
							}
						}
					}
				}
			} while (!newbc && robin_channel != rr->channel);
			
			if (!newbc)
				chan_misdn_log(4, port, " Failed! No free channel in group %d!", group);
		}
		
		else {		
			for (port=misdn_cfg_get_next_port(0); port > 0;
				 port=misdn_cfg_get_next_port(port)) {
				
				misdn_cfg_get( port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);
				
				if (!strcasecmp(cfg_group, group)) {
					int l1, port_up;
					
					misdn_cfg_get( 0, MISDN_GEN_L1_INFO_OK, &l1, sizeof(l1));
					port_up = misdn_lib_port_up(port);

					chan_misdn_log(4, port, "def_l1:%d, portup:%d\n", l1, port_up);
					
					if ((l1 && port_up) || !l1)	{
						newbc = misdn_lib_get_free_bc(port, 0);
						if (newbc)
							break;
					}
				}
			}
		}
		
	} else {
		if (channel)
			chan_misdn_log(1, port," --> preselected_channel: %d\n",channel);
		newbc = misdn_lib_get_free_bc(port, channel);
	}
	
	if (!newbc) {
		chan_misdn_log(1, 0, " --> ! No free channel chan ext:%s even after Group Call\n",ext);
		chan_misdn_log(1, 0, " --> SEND: State Down\n");
		return NULL;
	}
	
	cl->bc=newbc;
	
	tmp = misdn_new(cl, AST_STATE_RESERVED, buf, "default", ext, ext, format, port, channel);
	
	return tmp;
}


struct ast_channel_tech misdn_tech = {
	.type="mISDN",
	.description="Channel driver for mISDN Support (Bri/Pri)",
	.capabilities= AST_FORMAT_ALAW ,
	.requester=misdn_request,
	.send_digit=misdn_digit,
	.call=misdn_call,
	.bridge=misdn_bridge, 
	.hangup=misdn_hangup,
	.answer=misdn_answer,
	.read=misdn_read,
	.write=misdn_write,
	.indicate=misdn_indication,
	.fixup=misdn_fixup,
	.properties=0
	/* .transfer=misdn_transfer */
};

struct ast_channel_tech misdn_tech_wo_bridge = {
	.type="mISDN",
	.description="Channel driver for mISDN Support (Bri/Pri)",
	.capabilities=AST_FORMAT_ALAW ,
	.requester=misdn_request,
	.send_digit=misdn_digit,
	.call=misdn_call,
	.hangup=misdn_hangup,
	.answer=misdn_answer,
	.read=misdn_read,
	.write=misdn_write,
	.indicate=misdn_indication,
	.fixup=misdn_fixup,
	.properties=0
	/* .transfer=misdn_transfer */
};


unsigned long glob_channel=0;

struct ast_channel *misdn_new(struct chan_list *chlist, int state, char * name, char * context, char *exten, char *callerid, int format, int port, int c)
{
	struct ast_channel *tmp;
	
	tmp = ast_channel_alloc(0);
	
	if (tmp) {
		chan_misdn_log(2, 0, " --> * NEW CHANNEL dad:%s oad:%s ctx:%s\n",exten,callerid, context);
		
		
		if (c<=0) {
			c=glob_channel++;
			snprintf(tmp->name, sizeof(tmp->name), "%s/%d-u%d",
				 type, port, c);
		} else {
			snprintf(tmp->name, sizeof(tmp->name), "%s/%d-%d",
				 type, port, c);
		}
		
		tmp->type = type;
    
		tmp->nativeformats = prefformat;
		tmp->readformat = format;
		tmp->writeformat = format;
    
		tmp->tech_pvt = chlist;

		int bridging;
		misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
		if (bridging)
			tmp->tech = &misdn_tech;
		else
			tmp->tech = &misdn_tech_wo_bridge;
    
    
		tmp->writeformat = format;
		tmp->readformat = format;
		tmp->priority=1;
    
    
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, exten,  sizeof(tmp->exten) - 1);

		if (callerid) {
			char *cid_name, *cid_num;
      
			ast_callerid_parse(callerid, &cid_name, &cid_num);
			if (cid_name)
				tmp->cid.cid_name=strdup(cid_name);
			if (cid_num)
				tmp->cid.cid_num=strdup(cid_num);
		}

		{
			if (pipe(chlist->pipe)<0)
				perror("Pipe failed\n");
			
			tmp->fds[0]=chlist->pipe[0];
			
		}
		
		if (chlist->bc) {
			int port=chlist->bc->port;
			misdn_cfg_get( port, MISDN_CFG_LANGUAGE, tmp->language, sizeof(tmp->language));
			
			{
				char buf[256];
				ast_group_t pg,cg;
				
				misdn_cfg_get(port, MISDN_CFG_PICKUPGROUP, &pg, sizeof(pg));
				misdn_cfg_get(port, MISDN_CFG_CALLGROUP, &cg, sizeof(cg));
				
				chan_misdn_log(2, port, " --> * CallGrp:%s PickupGrp:%s\n",ast_print_group(buf,sizeof(buf),cg),ast_print_group(buf,sizeof(buf),pg));
				tmp->pickupgroup=pg;
				tmp->callgroup=cg;
			}
			misdn_cfg_get(port, MISDN_CFG_TXGAIN, &chlist->bc->txgain, sizeof(int));
			misdn_cfg_get(port, MISDN_CFG_RXGAIN, &chlist->bc->rxgain, sizeof(int));
			chan_misdn_log(2, port, " --> rxgain:%d txgain:%d\n",chlist->bc->rxgain,chlist->bc->txgain);
			
		} else {
			chan_misdn_log(3, 0, " --> Not Setting Pickupgroup, we have no bc yet\n");
		}
		
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		else
			tmp->rings = 0;
	} else {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
		chan_misdn_log(0,0,"Unable to allocate channel structure\n");
	}
	
	return tmp;
}



int misdn_tx2ast_frm(struct chan_list * tmp, char * buf,  int len )
{
	struct ast_frame frame;
	
	/* If in hold state we drop frame .. */
	if (tmp->holded ) return 0;

	switch(tmp->state) {
	case MISDN_CLEANING:
	case MISDN_EXTCANTMATCH:
	case MISDN_WAITING4DIGS:
		return 0;
	default:
		break;
	}
	
	if (tmp->norxtone) {
		chan_misdn_log(3, tmp->bc->port, "misdn_tx2ast_frm: Returning because norxtone\n");
		return 0;
	}
	
	frame.frametype  = AST_FRAME_VOICE;
	frame.subclass = AST_FORMAT_ALAW;
	frame.datalen = len;
	frame.samples = len ;
	frame.mallocd =0 ;
	frame.offset= 0 ;
	frame.src = NULL;
	frame.data = buf ;
	
	if (tmp->faxdetect || tmp->ast_dsp ) {
		struct ast_frame *f,*f2;
		if (tmp->trans)
			f2=ast_translate(tmp->trans, &frame,0);
		else {
			chan_misdn_log(0, tmp->bc->port, "No T-Path found\n");
			return 0;
		}
		
		f = ast_dsp_process(tmp->ast, tmp->dsp, f2);
		if (f && (f->frametype == AST_FRAME_DTMF)) {
			ast_log(LOG_DEBUG, "Detected inband DTMF digit: %c", f->subclass);
			if (f->subclass == 'f' && tmp->faxdetect) {
				/* Fax tone -- Handle and return NULL */
				struct ast_channel *ast = tmp->ast;
				if (!tmp->faxhandled) {
					tmp->faxhandled++;
					if (strcmp(ast->exten, "fax")) {
						if (ast_exists_extension(ast, ast_strlen_zero(ast->macrocontext)? ast->context : ast->macrocontext, "fax", 1, AST_CID_P(ast))) {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension\n", ast->name);
							/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
							pbx_builtin_setvar_helper(ast,"FAXEXTEN",ast->exten);
							if (ast_async_goto(ast, ast->context, "fax", 1))
								ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, ast->context);
						} else
							ast_log(LOG_NOTICE, "Fax detected, but no fax extension ctx:%s exten:%s\n",ast->context, ast->exten);
					} else
						ast_log(LOG_DEBUG, "Already in a fax extension, not redirecting\n");
				} else
					ast_log(LOG_DEBUG, "Fax already handled\n");
				frame.frametype = AST_FRAME_NULL;
				frame.subclass = 0;
				f = &frame;
			}  else if ( tmp->ast_dsp) {
				struct ast_frame fr;
				memset(&fr, 0 , sizeof(fr));
				fr.frametype = AST_FRAME_DTMF;
				fr.subclass = f->subclass ;
				fr.src=NULL;
				fr.data = NULL ;
				fr.datalen = 0;
				fr.samples = 0 ;
				fr.mallocd =0 ;
				fr.offset= 0 ;
				
				chan_misdn_log(2, tmp->bc->port, " --> * SEND: DTMF (AST_DSP) :%c\n",f->subclass);
				ast_queue_frame(tmp->ast, &fr);
				
				frame.frametype = AST_FRAME_NULL;
				frame.subclass = 0;
				f = &frame;
			}
		}
	}
	
	if (tmp && tmp->ast && MISDN_ASTERISK_PVT (tmp->ast) && MISDN_ASTERISK_TECH_PVT(tmp->ast) ) {
#if MISDN_DEBUG
		int i, max=5>len?len:5;
    
		printf("write2* %p %d bytes: ",tmp, len);
    
		for (i=0; i<  max ; i++) printf("%2.2x ",((char*) frame.data)[i]);
		printf ("\n");
#endif
		chan_misdn_log(9, tmp->bc->port, "Queueing %d bytes 2 Asterisk\n",len);
		
		ast_queue_frame(tmp->ast,&frame);
		
	}  else {
		ast_log (LOG_WARNING, "No ast || ast->pvt || ch\n");
	}
	
	return 0;
}

/** Channel Queue ***/

struct chan_list *find_chan_by_l3id(struct chan_list *list, unsigned long l3id)
{
	struct chan_list *help=list;
	for (;help; help=help->next) {
		if (help->l3id == l3id ) return help;
	}
  
	chan_misdn_log(4, list? (list->bc? list->bc->port : 0) : 0, "$$$ find_chan: No channel found with l3id:%x\n",l3id);
  
	return NULL;
}

struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help=list;
	for (;help; help=help->next) {
		if (help->bc == bc) return help;
	}
  
	chan_misdn_log(4, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n",bc->oad,bc->dad);
  
	return NULL;
}


struct chan_list *find_holded(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help=list;
	
	chan_misdn_log(4, bc->port, "$$$ find_holded: channel:%d oad:%s dad:%s\n",bc->channel, bc->oad,bc->dad);
	for (;help; help=help->next) {
		chan_misdn_log(4, bc->port, "$$$ find_holded: --> holded:%d channel:%d\n",help->bc->holded, help->bc->channel);
		if (help->bc->port == bc->port
		    && help->bc->holded ) return help;
	}
	
	chan_misdn_log(4, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n",bc->oad,bc->dad);
  
	return NULL;
}

void cl_queue_chan(struct chan_list **list, struct chan_list *chan)
{
	chan_misdn_log(4, chan->bc? chan->bc->port : 0, "* Queuing chan %p\n",chan);
  
	ast_mutex_lock(&cl_te_lock);
	if (!*list) {
		*list = chan;
	} else {
		struct chan_list *help=*list;
		for (;help->next; help=help->next); 
		help->next=chan;
	}
	chan->next=NULL;
	ast_mutex_unlock(&cl_te_lock);
}

void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan) 
{
	if (chan->dsp) 
		ast_dsp_free(chan->dsp);
	if (chan->trans)
		ast_translator_free_path(chan->trans);
	

	ast_mutex_lock(&cl_te_lock);
	if (!*list) {
		ast_mutex_unlock(&cl_te_lock);
		return;
	}
  
	if (*list == chan) {
		*list=(*list)->next;
		ast_mutex_unlock(&cl_te_lock);
		return ;
	}
  
	{
		struct chan_list *help=*list;
		for (;help->next; help=help->next) {
			if (help->next == chan) {
				help->next=help->next->next;
				ast_mutex_unlock(&cl_te_lock);
				return;
			}
		}
	}
	
	ast_mutex_unlock(&cl_te_lock);
}

/** Channel Queue End **/



/** Isdn asks us to release channel, pendant to misdn_hangup **/
static void release_chan(struct misdn_bchannel *bc) {
	struct ast_channel *ast=NULL;
	
	{
		struct chan_list *ch=find_chan_by_bc(cl_te, bc);
		if (!ch) ch=find_chan_by_l3id (cl_te, bc->l3_id);
		
		release_lock;
		if (ch->ast) {
			ast=ch->ast;
		} 
		release_unlock;
		
		chan_misdn_log(1, bc->port, "Trying to Release bc with l3id: %x\n",bc->l3_id);
		if (ch) {
			if (ast)
				chan_misdn_trace_call(ast,1,"I->*: EVENT_RELEASE\n");
			
			close(ch->pipe[0]);
			close(ch->pipe[1]);
			
			if (ast && MISDN_ASTERISK_PVT(ast)) {
				chan_misdn_log(1, bc->port, "* RELEASING CHANNEL pid:%d ctx:%s dad:%s oad:%s state: %s\n",bc?bc->pid:-1, ast->context, ast->exten,AST_CID_P(ast),misdn_get_ch_state(ch));
				chan_misdn_log(3, bc->port, " --> * State Down\n");
				/* copy cause */
				send_cause2ast(ast,bc);
				
				MISDN_ASTERISK_TECH_PVT(ast)=NULL;
				
      
				if (ast->_state != AST_STATE_RESERVED) {
					chan_misdn_log(3, bc->port, " --> Setting AST State to down\n");
					ast_setstate(ast, AST_STATE_DOWN);
				}
				
				switch(ch->state) {
				case MISDN_EXTCANTMATCH:
				case MISDN_WAITING4DIGS:
				{
					chan_misdn_log(3,  bc->port, " --> * State Wait4dig | ExtCantMatch\n");
					ast_hangup(ast);
				}
				break;
				
				case MISDN_DIALING:
				case MISDN_CALLING_ACKNOWLEDGE:
				case MISDN_PROGRESS:
					chan_misdn_log(2,  bc->port, "* --> In State Dialin\n");
					chan_misdn_log(2,  bc->port, "* --> Queue Hangup\n");
					

					ast_queue_hangup(ast);
					break;
				case MISDN_CALLING:
					
					chan_misdn_log(2,  bc->port, "* --> In State Callin\n");
					
					if (!bc->nt) {
						chan_misdn_log(2,  bc->port, "* --> Queue Hangup\n");
						ast_queue_hangup(ast);
					} else {
						chan_misdn_log(2,  bc->port, "* --> Hangup\n");
						ast_queue_hangup(ast);
						//ast_hangup(ast);
					}
					break;
					
				case MISDN_CLEANING:
					/* this state comes out of ast so we mustnt call a ast function ! */
					chan_misdn_log(2,  bc->port, "* --> In StateCleaning\n");
					break;
				case MISDN_HOLD_DISCONNECT:
					chan_misdn_log(2,  bc->port, "* --> In HOLD_DISC\n");
					break;
				default:
					chan_misdn_log(2,  bc->port, "* --> In State Default\n");
					chan_misdn_log(2,  bc->port, "* --> Queue Hangup\n");
	
					
					if (ast && MISDN_ASTERISK_PVT(ast)) {
						ast_queue_hangup(ast);
					} else {
						chan_misdn_log (0,  bc->port, "!! Not really queued!\n");
					}
				}
			}
			cl_dequeue_chan(&cl_te, ch);
			
			free(ch);
		} else {
			/* chan is already cleaned, so exiting  */
		}
	}
}
/*** release end **/

void misdn_transfer_bc(struct chan_list *tmp_ch, struct chan_list *holded_chan)
{
	chan_misdn_log(4,0,"TRANSFERING %s to %s\n",holded_chan->ast->name, tmp_ch->ast->name);
	
	tmp_ch->state=MISDN_HOLD_DISCONNECT;
  
	ast_moh_stop(AST_BRIDGED_P(holded_chan->ast));

	holded_chan->state=MISDN_CONNECTED;
	holded_chan->holded=0;
	misdn_lib_transfer(holded_chan->bc?holded_chan->bc:holded_chan->holded_bc);
	
	ast_channel_masquerade(holded_chan->ast, AST_BRIDGED_P(tmp_ch->ast));
}


void do_immediate_setup(struct misdn_bchannel *bc,struct chan_list *ch , struct ast_channel *ast)
{
	char predial[256]="";
	char *p = predial;
  
	struct ast_frame fr;
  
	strncpy(predial, ast->exten, sizeof(predial) -1 );
  
	ch->state=MISDN_DIALING;
	
	if (bc->nt) {
		int ret; 
		ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
	} else {
		int ret;
		if ( misdn_lib_is_ptp(bc->port)) {
			ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
		} else {
			ret = misdn_lib_send_event(bc, EVENT_PROCEEDING );
		}
	}

	manager_send_tone(bc,TONE_DIAL);  
  
	chan_misdn_log(1, bc->port, "* Starting Ast ctx:%s dad:%s oad:%s with 's' extension\n", ast->context, ast->exten, AST_CID_P(ast));
  
	strncpy(ast->exten,"s", 2);
  
	if (ast_pbx_start(ast)<0) {
		ast=NULL;
		manager_send_tone(bc,TONE_BUSY);
		if (bc->nt)
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
		else
			misdn_lib_send_event(bc, EVENT_DISCONNECT );
	}
  
  
	while (!ast_strlen_zero(p) ) {
		fr.frametype = AST_FRAME_DTMF;
		fr.subclass = *p ;
		fr.src=NULL;
		fr.data = NULL ;
		fr.datalen = 0;
		fr.samples = 0 ;
		fr.mallocd =0 ;
		fr.offset= 0 ;

		if (ch->ast && MISDN_ASTERISK_PVT(ch->ast) && MISDN_ASTERISK_TECH_PVT(ch->ast)) {
			ast_queue_frame(ch->ast, &fr);
		}
		p++;
	}
}



void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel*bc) {
	
	ast->hangupcause=bc->cause;
	
	switch ( bc->cause) {
		
	case 1: /** Congestion Cases **/
	case 2:
	case 3:
 	case 4:
 	case 22:
 	case 27:
		chan_misdn_log(1, bc?bc->port:0, " --> * SEND: Queue Congestion pid:%d\n", bc?bc->pid:-1);
		
		ast_queue_control(ast, AST_CONTROL_CONGESTION);
		break;
		
	case 21:
	case 17: /* user busy */
		chan_misdn_log(1,  bc?bc->port:0, " --> * SEND: Queue Busy pid:%d\n", bc?bc->pid:-1);
		
		ast_queue_control(ast, AST_CONTROL_BUSY);
		
		break;
	}
}

/************************************************************/
/*  Receive Events from isdn_lib  here                     */
/************************************************************/
enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data)
{
	struct chan_list *ch=find_chan_by_bc(cl_te, bc);
	
	if (!ch)
		ch=find_chan_by_l3id(cl_te, bc->l3_id);
	
	if (event != EVENT_BCHAN_DATA) { /*  Debug Only Non-Bchan */
		chan_misdn_log(1, bc->port, "I IND :%s oad:%s dad:%s port:%d\n", manager_isdn_get_info(event), bc->oad, bc->dad, bc->port);
		misdn_lib_log_ies(bc);
	}
	
	if (event != EVENT_SETUP) {
		if (!ch) {
			if (event != EVENT_CLEANUP )
				ast_log(LOG_WARNING, "Chan not existing at the moment bc->l3id:%x bc:%p event:%s port:%d channel:%d\n",bc->l3_id, bc, manager_isdn_get_info( event), bc->port,bc->channel);
			return -1;
		}
	}
	
	if (ch ) {
		switch (event) {
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
		case EVENT_CLEANUP:
			break;
		default:
			if ( !ch->ast  || !MISDN_ASTERISK_PVT(ch->ast) || !MISDN_ASTERISK_TECH_PVT(ch->ast)) {
				if (event!=EVENT_BCHAN_DATA)
					ast_log(LOG_WARNING, "No Ast or No private Pointer in Event (%d:%s)\n", event, manager_isdn_get_info(event));
				return -1;
			}
		}
	}
	
	
	switch (event) {
	case EVENT_NEW_L3ID:
		ch->l3id=bc->l3_id;
		break;

	case EVENT_NEW_BC:
		if (bc)
			ch->bc=bc;
		break;
		
	case EVENT_DTMF_TONE:
	{
		/*  sending INFOS as DTMF-Frames :) */
		struct ast_frame fr;
		memset(&fr, 0 , sizeof(fr));
		fr.frametype = AST_FRAME_DTMF;
		fr.subclass = bc->dtmf ;
		fr.src=NULL;
		fr.data = NULL ;
		fr.datalen = 0;
		fr.samples = 0 ;
		fr.mallocd =0 ;
		fr.offset= 0 ;
		
		chan_misdn_log(2, bc->port, " --> DTMF:%c\n", bc->dtmf);
		
		ast_queue_frame(ch->ast, &fr);
	}
	break;
	case EVENT_STATUS:
		break;
    
	case EVENT_INFORMATION:
	{
		int stop_tone;
		misdn_cfg_get( 0, MISDN_GEN_STOP_TONE, &stop_tone, sizeof(int));
		if ( stop_tone && bc->tone != TONE_NONE) {
			manager_send_tone(bc,TONE_NONE);
		}
		
		if (ch->state == MISDN_WAITING4DIGS ) {
			/*  Ok, incomplete Setup, waiting till extension exists */
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad,bc->info_dad, l);
				bc->dad[l-1] = 0;
			}
			
			
			{
				int l = sizeof(ch->ast->exten);
				strncpy(ch->ast->exten, bc->dad, l);
				ch->ast->exten[l-1] = 0;
			}
/*			chan_misdn_log(5, bc->port, "Can Match Extension: dad:%s oad:%s\n",bc->dad,bc->oad);*/
			
			char bc_context[BUFFERSIZE];
			misdn_cfg_get( bc->port, MISDN_CFG_CONTEXT, bc_context, BUFFERSIZE);
			if(!ast_canmatch_extension(ch->ast, bc_context, bc->dad, 1, bc->oad)) {
				chan_misdn_log(0, bc->port, "Extension can never match, so disconnecting\n");
				manager_send_tone(bc,TONE_BUSY);
				ch->state=MISDN_EXTCANTMATCH;
				bc->out_cause=1;
				if (bc->nt)
					misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
				else
					misdn_lib_send_event(bc, EVENT_DISCONNECT );
				break;
			}
			if (ast_exists_extension(ch->ast, bc_context, bc->dad, 1, bc->oad)) {
				ch->state=MISDN_DIALING;
	  
				manager_send_tone(bc,TONE_NONE);
/*				chan_misdn_log(1, bc->port, " --> * Starting Ast ctx:%s\n", ch->ast->context);*/
				if (ast_pbx_start(ch->ast)<0) {
					chan_misdn_log(0, bc->port, "ast_pbx_start returned < 0 in INFO\n");
					manager_send_tone(bc,TONE_BUSY);
					if (bc->nt)
						misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
					else
						misdn_lib_send_event(bc, EVENT_DISCONNECT );
				}
			}
	
		} else {
			/*  sending INFOS as DTMF-Frames :) */
			struct ast_frame fr;
			fr.frametype = AST_FRAME_DTMF;
			fr.subclass = bc->info_dad[0] ;
			fr.src=NULL;
			fr.data = NULL ;
			fr.datalen = 0;
			fr.samples = 0 ;
			fr.mallocd =0 ;
			fr.offset= 0 ;

			
			int digits;
			misdn_cfg_get( 0, MISDN_GEN_APPEND_DIGITS2EXTEN, &digits, sizeof(int));
			if (ch->state != MISDN_CONNECTED ) {
				if (digits) {
					int l = sizeof(bc->dad);
					strncat(bc->dad,bc->info_dad, l);
					bc->dad[l-1] = 0;
					l = sizeof(ch->ast->exten);
					strncpy(ch->ast->exten, bc->dad, l);
					ch->ast->exten[l-1] = 0;

					ast_cdr_update(ch->ast);
				}
				
				ast_queue_frame(ch->ast, &fr);
			}
			
		}
	}
	break;
	case EVENT_SETUP:
	{
		struct chan_list *ch=find_chan_by_bc(cl_te, bc);
		if (ch && ch->state != MISDN_NOTHING ) {
			chan_misdn_log(1, bc->port, " --> Ignoring Call we have already one\n");
			return RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE; /*  Ignore MSNs which are not in our List */
		}
	}
	
	int msn_valid = misdn_cfg_is_msn_valid(bc->port, bc->dad);
	if (!bc->nt && ! msn_valid) {
		chan_misdn_log(1, bc->port, " --> Ignoring Call, its not in our MSN List\n");
		return RESPONSE_IGNORE_SETUP; /*  Ignore MSNs which are not in our List */
	}
	
	print_bearer(bc);
    
	{
		struct chan_list *ch=init_chan_list();
		struct ast_channel *chan;
		char name[128];
		if (!ch) { chan_misdn_log(0, bc->port, "cb_events: malloc for chan_list failed!\n"); return 0;}
		
		ch->bc = bc;
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		ch->orginator = ORG_MISDN;

		
		{
			char prefix[BUFFERSIZE]="";
			switch( bc->onumplan ) {
			case NUMPLAN_INTERNATIONAL:
				misdn_cfg_get( bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
				break;
	  
			case NUMPLAN_NATIONAL:
				misdn_cfg_get( bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
				break;
	  
	  
			case NUMPLAN_SUBSCRIBER:
				/* dunno what to do here ? */
				break;

			case NUMPLAN_UNKNOWN:
				break;
			default:
				break;
			}

			{
				int l = strlen(prefix) + strlen(bc->oad);
				char tmp[l+1];
				strcpy(tmp,prefix);
				strcat(tmp,bc->oad);
				strcpy(bc->oad,tmp);
			}
			
			if (!ast_strlen_zero(bc->oad))
				sprintf(name,"mISDN/%d/%s",bc->port,bc->oad);
			else
				sprintf(name,"mISDN/%d",bc->port);


			if (!ast_strlen_zero(bc->dad)) {
				strncpy(bc->orig_dad,bc->dad, sizeof(bc->orig_dad));
				bc->orig_dad[sizeof(bc->orig_dad)-1] = 0;
			}
			
			if ( ast_strlen_zero(bc->dad) && !ast_strlen_zero(bc->keypad)) {
				strncpy(bc->dad,bc->keypad, sizeof(bc->dad));
				bc->dad[sizeof(bc->dad)-1] = 0;
			}
			prefix[0] = 0;
			
			switch( bc->dnumplan ) {
			case NUMPLAN_INTERNATIONAL:
				misdn_cfg_get( bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
				break;
				
			case NUMPLAN_NATIONAL:
				misdn_cfg_get( bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
				break;
				
				
			case NUMPLAN_SUBSCRIBER:
				/* dunno what to do here ? */
				break;
				
			case NUMPLAN_UNKNOWN:
				break;
			default:
				break;
			}
		
			{
				int l = strlen(prefix) + strlen(bc->dad);
				char tmp[l+1];
				strcpy(tmp,prefix);
				strcat(tmp,bc->dad);
				strcpy(bc->dad,tmp);
			}
			
			char bc_context[BUFFERSIZE];
			misdn_cfg_get( bc->port, MISDN_CFG_CONTEXT, bc_context, BUFFERSIZE);
			chan=misdn_new(ch, AST_STATE_RING,name ,bc_context, bc->dad, bc->oad, AST_FORMAT_ALAW, bc->port, bc->channel);
			
			if (!chan) {
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
				return 0;
			}
			
			ch->ast = chan;
			pbx_builtin_setvar_helper(ch->ast,"REDIRECTING_NUMBER",bc->rad);
			
		}

		

		chan_misdn_trace_call(chan,1,"I->*: EVENT_SETUP\n");
		
		if ( bc->pres ) {
			chan->cid.cid_pres=AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		}  else {
			chan->cid.cid_pres=AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
		}
      
		pbx_builtin_setvar_helper(chan, "TRANSFERCAPABILITY", ast_transfercapability2str(bc->capability));
		chan->transfercapability=bc->capability;
      
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
			pbx_builtin_setvar_helper(chan,"CALLTYPE","DIGITAL");
			break;
		default:
			pbx_builtin_setvar_helper(chan,"CALLTYPE","SPEECH");
		}

		/** queue new chan **/
		cl_queue_chan(&cl_te, ch) ;

		/* Check for Pickup Request first */
		if (!strcmp(chan->exten, ast_pickup_ext())) {
			int ret;/** Sending SETUP_ACK**/
			ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
	
			if (ast_pickup_call(chan)) {
				ast_hangup(chan);
			} else {
				ch->state = MISDN_CALLING_ACKNOWLEDGE;
	  
				ch->ast=NULL;
	  
				ast_setstate(chan, AST_STATE_DOWN);
				ast_hangup(chan); 
	  
				break;
			}
		}
		/*
		  added support for s extension hope it will help those poor cretains
		  which haven't overlap dial.
		*/
		{
			
			misdn_cfg_get( bc->port, MISDN_CFG_LANGUAGE, chan->language, sizeof(chan->language));
			int ai;
			misdn_cfg_get( bc->port, MISDN_CFG_ALWAYS_IMMEDIATE, &ai, sizeof(ai));
			if ( ai ) {
				do_immediate_setup(bc, ch , chan);
				break;
			}

			int immediate;
			misdn_cfg_get( bc->port, MISDN_CFG_IMMEDIATE, &immediate, sizeof(int));
			
			if (ast_strlen_zero(bc->orig_dad) && immediate ) {
				do_immediate_setup(bc, ch , chan);
				break;
			}
			
		}

		/** Now after we've finished configuring our channel object
		    we'll jump into the dialplan **/
		
		char bc_context[BUFFERSIZE];
		misdn_cfg_get( bc->port, MISDN_CFG_CONTEXT, bc_context, BUFFERSIZE);
		if(!ast_canmatch_extension(ch->ast, bc_context, bc->dad, 1, bc->oad)) {
			chan_misdn_log(0, bc->port, "Extension can never match, so disconnecting\n");
			manager_send_tone(bc,TONE_BUSY);
			ch->state=MISDN_EXTCANTMATCH;
			bc->out_cause=1;
			if (bc->nt)
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
			else
				misdn_lib_send_event(bc, EVENT_DISCONNECT );
			break;
		}
		
		if (ast_exists_extension(ch->ast, bc_context, bc->dad, 1, bc->oad)) {
			ch->state=MISDN_DIALING;
	
			if (bc->nt) {
				int ret; 
				ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			} else {
				int ret;
				ret= misdn_lib_send_event(bc, EVENT_PROCEEDING );
			}
	
			if (ast_pbx_start(chan)<0) {
				chan_misdn_log(0, bc->port, "ast_pbx_start returned <0 in SETUP\n");
				chan=NULL;
				manager_send_tone(bc,TONE_BUSY);
				if (bc->nt)
					misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
				else
					misdn_lib_send_event(bc, EVENT_DISCONNECT );
			}
		} else {
			int ret= misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			if (ret == -ENOCHAN) {
				ast_log(LOG_WARNING,"Channel was catched, before we could Acknowledge\n");
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			}
			/*  send tone to phone :) */
			
			int stop_tone;
			misdn_cfg_get( 0, MISDN_GEN_STOP_TONE, &stop_tone, sizeof(int));
			if ( (!ast_strlen_zero(bc->dad)) && stop_tone ) 
				manager_send_tone(bc,TONE_NONE);
			else
				manager_send_tone(bc,TONE_DIAL);
	
			ch->state=MISDN_WAITING4DIGS;
		}
      
	}
	break;
	case EVENT_SETUP_ACKNOWLEDGE:
	{
		ch->state = MISDN_CALLING_ACKNOWLEDGE;
		if (!ast_strlen_zero(bc->infos_pending)) {
			/* TX Pending Infos */
			
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad,bc->infos_pending, l - strlen(bc->dad));
				bc->dad[l-1] = 0;
			}	
			{
				int l = sizeof(ch->ast->exten);
				strncpy(ch->ast->exten, bc->dad, l);
				ch->ast->exten[l-1] = 0;
			}
			{
				int l = sizeof(bc->info_dad);
				strncpy(bc->info_dad, bc->infos_pending, l);
				bc->info_dad[l-1] = 0;
			}
			strncpy(bc->infos_pending,"", 1);

			misdn_lib_send_event(bc, EVENT_INFORMATION);
		}
	}
	break;
	case EVENT_PROCEEDING:
	{
		
		if ( misdn_cap_is_speech(bc->capability) &&
		     misdn_inband_avail(bc) ) {
			start_bc_tones(ch);
		}
	}
	break;
	case EVENT_PROGRESS:
		if (!bc->nt ) {
			if ( misdn_cap_is_speech(bc->capability) &&
			     misdn_inband_avail(bc)
				) {
				start_bc_tones(ch);
			}
			
			ast_queue_control(ch->ast, AST_CONTROL_PROGRESS);
			
			ch->state=MISDN_PROGRESS;
		}
		break;
		
		
	case EVENT_ALERTING:
	{
		ch->state = MISDN_ALERTING;
		
		chan_misdn_trace_call(ch->ast,1,"I->*: EVENT_ALERTING\n");
		 
		 ast_queue_control(ch->ast, AST_CONTROL_RINGING);
		 ast_setstate(ch->ast, AST_STATE_RINGING);
		 
		 if ( misdn_cap_is_speech(bc->capability) && misdn_inband_avail(bc)) {
			 start_bc_tones(ch);
		 }
	}
	break;
	case EVENT_CONNECT:
		misdn_lib_send_event(bc,EVENT_CONNECT_ACKNOWLEDGE);
	case EVENT_CONNECT_ACKNOWLEDGE:
	{
		bc->state=STATE_CONNECTED;
		
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		
		start_bc_tones(ch);
		
		chan_misdn_trace_call(ch->ast,1,"I->*: EVENT_CONNECT\n");
		
		ch->state = MISDN_CONNECTED;
		ast_queue_control(ch->ast, AST_CONTROL_ANSWER);
	}
	break;
	case EVENT_DISCONNECT:
	{
		
		struct chan_list *holded_ch=find_holded(cl_te, bc);
		
		if (holded_ch ) {
			if  (ch->state == MISDN_CONNECTED ) {
				misdn_transfer_bc(ch, holded_ch) ;
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
				break;
			}
		}

		send_cause2ast(ch->ast,bc);

		if (!misdn_inband_avail(bc)) {
			/* If Inband Avail. In TE, wait till we get hangup from ast. */
			stop_bc_tones(ch);
		}
		bc->out_cause=16;
		misdn_lib_send_event(bc,EVENT_RELEASE);
		
	}
	break;
	
	case EVENT_RELEASE:
		{
			
			switch ( bc->cause) {
				
			case -1:
				/*
				  OK, it really sucks, this is a RELEASE from NT-Stack So we take
				  it and return easylie, It seems that we've send a DISCONNECT
				  before, so we should RELEASE_COMPLETE after that Disconnect
				  (looks like ALERTING State at misdn_hangup !!
				*/
				return RESPONSE_OK;
				break;
			}
			
			
			bc->out_cause=16;
			
			stop_bc_tones(ch);
			release_chan(bc);
		}
		break;
	case EVENT_RELEASE_COMPLETE:
	{
		stop_bc_tones(ch);
		release_chan(bc);
	}
	break;
	
	case EVENT_BCHAN_DATA:
	{
		chan_misdn_trace_call(ch->ast,3,"I->*: EVENT_B_DATA len=%d\n",bc->bframe_len);
		
		if ( !misdn_cap_is_speech(ch->bc->capability) || bc->nojitter) {
			misdn_tx2ast_frm(ch, bc->bframe, bc->bframe_len );
		} else {
			int len=bc->bframe_len;
			int free=misdn_ibuf_freecount(bc->astbuf);
			
			
			if (bc->bframe_len > free) {
				ast_log(LOG_DEBUG, "sbuf overflow!\n");
				len=misdn_ibuf_freecount(bc->astbuf);

				if (len == 0) {
					ast_log(LOG_WARNING, "BCHAN_DATA: write buffer overflow port:%d channel:%d!\n",bc->port,bc->channel);
				}
			}
			
			misdn_ibuf_memcpy_w(bc->astbuf, bc->bframe, len);
			
			{
				char blah[1]="\0";
#ifdef FLATTEN_JITTER
				{
					struct timeval tv;
					gettimeofday(&tv,NULL);
					
					if (tv.tv_usec % 10000 > 0 ) {
						write(ch->pipe[1], blah,sizeof(blah));
						bc->time_usec=tv.tv_usec;
					}
				}
#else
				write(ch->pipe[1], blah,sizeof(blah));
#endif
				
				
			}
		}
		
	}
	break;
	case EVENT_TIMEOUT:
		break; /* Ignore now .. */
		{
			switch (ch->state) {
			case MISDN_CALLING:
				chan_misdn_log(0, bc?bc->port:0, "GOT TIMOUT AT CALING pid:%d\n", bc?bc->pid:-1);
					break;
			case MISDN_DIALING:
			case MISDN_PROGRESS:
				break;
			default:
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			}
		}
		break;
	case EVENT_CLEANUP:
	{
		stop_bc_tones(ch);
		release_chan(bc);
	}
	break;
    
	/***************************/
	/** Suplementary Services **/
	/***************************/
	case EVENT_RETRIEVE:
	{
		struct ast_channel *hold_ast=AST_BRIDGED_P(ch->ast);
		ch->state = MISDN_CONNECTED;
		
		//ast_moh_stop(ch->ast);
		//start_bc_tones(ch);
		if (hold_ast) {
			ast_moh_stop(hold_ast);
		}
		
		if ( misdn_lib_send_event(bc, EVENT_RETRIEVE_ACKNOWLEDGE) < 0)
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
		
		
	}
	break;
    
	case EVENT_HOLD:
	{
		int hold_allowed;
		misdn_cfg_get( bc->port, MISDN_CFG_HOLD_ALLOWED, &hold_allowed, sizeof(int));
		
		if (!hold_allowed) {
			chan_misdn_log(0, bc->port, "Hold not allowed on port:%d\n", bc->port);
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			break;
		}

		{
			struct chan_list *holded_ch=find_holded(cl_te, bc);
			if (holded_ch) {
				misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
				chan_misdn_log(0, bc->port, "We can't use RETRIEVE at the moment due to mISDN bug!\n");
				break;
			}
		}
		
		if (AST_BRIDGED_P(ch->ast)){
			ch->state = MISDN_HOLDED;
			ch->l3id = bc->l3_id;
			
			ast_moh_start(AST_BRIDGED_P(ch->ast), NULL);
			misdn_lib_send_event(bc, EVENT_HOLD_ACKNOWLEDGE);
		} else {
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			chan_misdn_log(0, bc->port, "We aren't bridged to anybody\n");
		}
	} 
	break;
	default:
		ast_log(LOG_WARNING, "Got Unknown Event\n");
		break;
	}
	
	return RESPONSE_OK;
}

/** TE STUFF END **/

/******************************************
 *
 *   Asterisk Channel Endpoint END
 *
 *
 *******************************************/


int clearl3_true ( void ) {
	int default_clearl3;
	misdn_cfg_get( 0, MISDN_GEN_CLEAR_L3, &default_clearl3, sizeof(int));
	return default_clearl3;
}

int g_config_initialized=0;

int load_module(void)
{
	int i;
	
	char ports[256]="";
	
	max_ports=misdn_lib_maxports_get();
	
	if (max_ports<=0) {
		ast_log(LOG_ERROR, "Unable to initialize mISDN\n");
		return -1;
	}
	
	
	misdn_cfg_init(max_ports);
	g_config_initialized=1;
	
	misdn_debug = (int *)malloc(sizeof(int) * (max_ports+1));
	misdn_cfg_get( 0, MISDN_GEN_DEBUG, &misdn_debug[0], sizeof(int));
	for (i = 1; i <= max_ports; i++)
		misdn_debug[i] = misdn_debug[0];
	misdn_debug_only = (int *)calloc(max_ports + 1, sizeof(int));

	
	{
		char tempbuf[BUFFERSIZE];
		misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, tempbuf, BUFFERSIZE);
		if (strlen(tempbuf))
			tracing = 1;
	}

	ast_mutex_init(&cl_te_lock);
	ast_mutex_init(&release_lock_mutex);

	misdn_cfg_get_ports_string(ports);
	if (strlen(ports))
		chan_misdn_log(0, 0, "Got: %s from get_ports\n",ports);
	
	{
		struct misdn_lib_iface iface = {
			.cb_event = cb_events,
			.cb_log = chan_misdn_log,
			.cb_clearl3_true = clearl3_true
		};
		if (misdn_lib_init(ports, &iface, NULL))
			chan_misdn_log(0, 0, "No te ports initialized\n");
	}


	{
		if (ast_channel_register(&misdn_tech)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
			unload_module();
			return -1;
		}
	}
  
	ast_cli_register(&cli_send_display);
	ast_cli_register(&cli_send_cd);
	ast_cli_register(&cli_send_digit);
	ast_cli_register(&cli_toggle_echocancel);
	ast_cli_register(&cli_set_tics);

	ast_cli_register(&cli_show_cls);
	ast_cli_register(&cli_show_cl);
	ast_cli_register(&cli_show_config);
	ast_cli_register(&cli_show_port);
	ast_cli_register(&cli_show_stacks);

	ast_cli_register(&cli_restart_port);
	ast_cli_register(&cli_port_up);
	ast_cli_register(&cli_set_debug);
	ast_cli_register(&cli_set_crypt_debug);
	ast_cli_register(&cli_reload);

  
	ast_register_application("misdn_set_opt", misdn_set_opt_exec, "misdn_set_flags",
				 "misdn_set_opt(:<opt><optarg>:<opt><optarg>..):\n"
				 "Sets mISDN opts. and optargs\n"
				 "\n"
		);

	
	ast_register_application("misdn_facility", misdn_facility_exec, "misdn_facility",
				 "misdn_facility(<FACILITY_TYPE>|<ARG1>|..)\n"
				 "Sends the Facility Message FACILITY_TYPE with \n"
				 "the given Arguments to the current ISDN Channel\n"
				 "Supported Facilities are:\n"
				 "\n"
				 "type=calldeflect args=Nr where to deflect\n"
				 "\n"
		);
  
	chan_misdn_log(0, 0, "-- mISDN Channel Driver Registred -- (BE AWARE THIS DRIVER IS EXPERIMENTAL!)\n");

	return 0;
}



int unload_module(void)
{
	/* First, take us out of the channel loop */
	chan_misdn_log(0, 0, "-- Unregistering mISDN Channel Driver --\n");

	if (!g_config_initialized) return 0;
	
	ast_cli_unregister(&cli_send_display);
	
	ast_cli_unregister(&cli_send_cd);
	
	ast_cli_unregister(&cli_send_digit);
	ast_cli_unregister(&cli_toggle_echocancel);
	ast_cli_unregister(&cli_set_tics);
  
	ast_cli_unregister(&cli_show_cls);
	ast_cli_unregister(&cli_show_cl);
	ast_cli_unregister(&cli_show_config);
	ast_cli_unregister(&cli_show_port);
	ast_cli_unregister(&cli_show_stacks);
	ast_cli_unregister(&cli_restart_port);
	ast_cli_unregister(&cli_port_up);
	ast_cli_unregister(&cli_set_debug);
	ast_cli_unregister(&cli_set_crypt_debug);
	ast_cli_unregister(&cli_reload);
	/* ast_unregister_application("misdn_crypt"); */
	ast_unregister_application("misdn_set_opt");
	ast_unregister_application("misdn_facility");
  
	ast_channel_unregister(&misdn_tech);

	free_robin_list();
	misdn_cfg_destroy();
	misdn_lib_destroy();
  
	if (misdn_debug)
		free(misdn_debug);
	if (misdn_debug_only)
		free(misdn_debug_only);
	
	return 0;
}

int usecount(void)
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description(void)
{
	return desc;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}

void chan_misdn_log(int level, int port, char *tmpl, ...)
{
	if (! ((0 <= port) && (port <= max_ports))) {
		ast_log(LOG_WARNING, "cb_log called with out-of-range port number! (%d)\n", port);
		return;
	}
		
	va_list ap;
	char buf[1024];
  
	va_start(ap, tmpl);
	vsnprintf( buf, 1023, tmpl, ap );
	va_end(ap);
	
	if (misdn_debug_only[port] ? (level==1 && misdn_debug[port]) || (level==misdn_debug[port]) : level <= misdn_debug[port]) {
		ast_console_puts(buf);
	}
	
	if (level <= misdn_debug[0] && tracing) {
		time_t tm = time(NULL);
		char *tmp=ctime(&tm),*p;
		char file[BUFFERSIZE];
		misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, file, BUFFERSIZE);
		FILE *fp= fopen(file, "a+");

		p=strchr(tmp,'\n');
		if (p) *p=':';
    
		if (!fp) {
			ast_console_puts("Error opening Tracefile: ");
			ast_console_puts(strerror(errno));
			ast_console_puts("\n");
			return ;
		}
		
		fputs(tmp,fp);
		fputs(" ", fp);
		fputs(buf, fp);

		fclose(fp);
	}
}


void chan_misdn_trace_call(struct ast_channel *chan, int debug, char *tmpl, ...)
{
	va_list ap;
	char buf[1024];
	char name[1024];

	int trace;
	misdn_cfg_get( 0, MISDN_GEN_TRACE_CALLS, &trace, sizeof(int));
	if (!trace) return ;
	
	if (misdn_debug[0] < debug) return ; 
	
	char tracedir[BUFFERSIZE];
	misdn_cfg_get( 0, MISDN_GEN_TRACE_DIR, tracedir, BUFFERSIZE);
	sprintf(name,"%s/%s.%s",tracedir, chan->uniqueid, chan->cid.cid_num );
	
	va_start(ap, tmpl);
	
	vsprintf( buf, tmpl, ap );
	
	va_end(ap);
	
	time_t tm = time(NULL);
	char *tmp=ctime(&tm),*p;
	FILE *fp= fopen(name, "a");
	int fd;
	
	if (!fp) {
		ast_console_puts("Error opening Tracefile");
		ast_console_puts(strerror(errno));
		ast_console_puts("\n");
		return ;
	}
	
	fd=fileno(fp) ;
	
	flock(fd, LOCK_EX);
	
	p=strchr(tmp,'\n');
	if (p) *p=':';
	
	
	
	fputs(tmp,fp);
	fputs(" ", fp);
	fputs(buf, fp);

	flock(fd, LOCK_UN);
	
	fclose(fp);
	
}


/*** SOME APPS ;)***/

static int misdn_facility_exec(struct ast_channel *chan, void *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *tok, *tokb;
	

	if (strcasecmp(MISDN_ASTERISK_TYPE(chan),"mISDN")) {
		ast_log(LOG_WARNING, "misdn_facility makes only sense with chan_misdn channels!\n");
		return -1;
	}
	
	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "misdn_facility Requires arguments\n");
		return -1;
	}
	
	tok=strtok_r((char*)data,"|", &tokb) ;
	
	if (!tok) {
		ast_log(LOG_WARNING, "misdn_facility Requires arguments\n");
		return -1;
	}
	
	if (!strcasecmp(tok,"calldeflect")) {
		tok=strtok_r(NULL,"|", &tokb) ;
		
		if (!tok) {
			ast_log(LOG_WARNING, "Facility: Call Defl Requires arguments\n");
		}
		
		misdn_lib_send_facility(ch->bc, FACILITY_CALLDEFLECT, tok);
		
	} else {
		ast_log(LOG_WARNING, "Unknown Facility: %s\n",tok);
	}
	
	return 0;
	
}


static int misdn_set_opt_exec(struct ast_channel *chan, void *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *tok,*tokb;
	int  keyidx=0;
	int rxgain=0;
	int txgain=0;

	if (strcasecmp(MISDN_ASTERISK_TYPE(chan),"mISDN")) {
		ast_log(LOG_WARNING, "misdn_set_opt makes only sense with chan_misdn channels!\n");
		return -1;
	}
	
	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "misdn_set_opt Requires arguments\n");
		return -1;
	}

	for (tok=strtok_r((char*)data, ":",&tokb);
	     tok;
	     tok=strtok_r(NULL,":",&tokb) ) {
		int neglect=0;
		
		if (tok[0] == '!' ) {
			neglect=1;
			tok++;
		}
		
		switch(tok[0]) {
			
		case 'd' :
			strncpy(ch->bc->display,++tok,84);
			chan_misdn_log(1, ch->bc->port, "SETOPT: Display:%s\n",ch->bc->display);
			break;
			
		case 'n':
			chan_misdn_log(1, ch->bc->port, "SETOPT: No DSP\n");
			ch->bc->nodsp=1;
			break;

		case 'j':
			chan_misdn_log(1, ch->bc->port, "SETOPT: No jitter\n");
			ch->bc->nojitter=1;
			break;
      
		case 'v':
			tok++;

			switch ( tok[0] ) {
			case 'r' :
				rxgain=atoi(++tok);
				if (rxgain<-8) rxgain=-8;
				if (rxgain>8) rxgain=8;
				ch->bc->rxgain=rxgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n",rxgain);
				break;
			case 't':
				txgain=atoi(++tok);
				if (txgain<-8) txgain=-8;
				if (txgain>8) txgain=8;
				ch->bc->txgain=txgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n",txgain);
				break;
			}
			break;
      
		case 'c':
			keyidx=atoi(++tok);
      
			if (keyidx > misdn_key_vector_size  || keyidx < 0 ) {
				ast_log(LOG_WARNING, "You entered the keyidx: %d but we have only %d keys\n",keyidx, misdn_key_vector_size );
				continue; 
			}
      
			{
				int l = sizeof(ch->bc->crypt_key);
				strncpy(ch->bc->crypt_key, misdn_key_vector[keyidx], l);
				ch->bc->crypt_key[l-1] = 0;
			}
			chan_misdn_log(0, ch->bc->port, "SETOPT: crypt with key:%s\n",misdn_key_vector[keyidx]);
			break;

		case 'e':
			chan_misdn_log(1, ch->bc->port, "SETOPT: EchoCancel\n");
			
			if (neglect) {
				ch->bc->ec_enable=0;
			} else {
				ch->bc->ec_enable=1;
				ch->bc->orig=ch->orginator;
				tok++;
				if (tok) {
					ch->bc->ec_deftaps=atoi(tok);
				}
			}
			
			break;
      
		case 'h':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Digital\n");
			if (strlen(tok) > 1 && tok[1]=='1') {
				chan_misdn_log(1, ch->bc->port, "SETOPT: Digital TRANS_DIGITAL\n");
				ch->bc->async=1;
				ch->bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			} else {
				ch->bc->async=0;
				ch->bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			}
			break;
            
		case 's':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Send DTMF\n");
			ch->bc->send_dtmf=1;
			break;
			
		case 'f':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Faxdetect\n");
			ch->faxdetect=1;
			break;

		case 'a':
			chan_misdn_log(1, ch->bc->port, "SETOPT: AST_DSP (for DTMF)\n");
			ch->ast_dsp=1;
			break;

		case 'p':
			chan_misdn_log(1, ch->bc->port, "SETOPT: callerpres: %s\n",&tok[1]);
			/* CRICH: callingpres!!! */
			if (strstr(tok,"allowed") ) {
				ch->bc->pres=0;
			} else if (strstr(tok,"not_screened")) {
				ch->bc->pres=1;
			}
			
			
			break;
      
      
		default:
			break;
		}
	}
	
	if (ch->faxdetect || ch->ast_dsp) {
		
		if (!ch->dsp) ch->dsp = ast_dsp_new();
		if (ch->dsp) ast_dsp_set_features(ch->dsp, DSP_FEATURE_DTMF_DETECT| DSP_FEATURE_FAX_DETECT);
		if (!ch->trans) ch->trans=ast_translator_build_path(AST_FORMAT_SLINEAR, AST_FORMAT_ALAW);
	}

	if (ch->ast_dsp) {
		chan_misdn_log(1,ch->bc->port,"SETOPT: with AST_DSP we deactivate mISDN_dsp\n");
		ch->bc->nodsp=1;
		ch->bc->nojitter=1;
	}
	
	return 0;
}


