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
#include <signal.h>
#include <sys/file.h>
#include <semaphore.h>

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
#include <asterisk/sched.h>

#include <chan_misdn_config.h>
#include <isdn_lib.h>

char global_tracefile[BUFFERSIZE+1];

static int g_config_initialized=0;

struct misdn_jb{
	int size;
	int upper_threshold;
	char *samples, *ok;
	int wp,rp;
	int state_empty;
	int state_full;
	int state_buffer;
	int bytes_wrote;
	ast_mutex_t mutexjb;
};



/* allocates the jb-structure and initialise the elements*/
struct misdn_jb *misdn_jb_init(int size, int upper_threshold);

/* frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb);

/* fills the jitterbuffer with len data returns < 0 if there was an
error (bufferoverun). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len);

/* gets len bytes out of the jitterbuffer if available, else only the
available data is returned and the return value indicates the number
of data. */
int misdn_jb_empty(struct misdn_jb *jb, char *data, int len);




/* BEGIN: chan_misdn.h */

ast_mutex_t release_lock;


enum misdn_chan_state {
	MISDN_NOTHING=0,	/*!< at beginning */
	MISDN_WAITING4DIGS, /*!<  when waiting for infos */
	MISDN_EXTCANTMATCH, /*!<  when asterisk couldnt match our ext */
	MISDN_INCOMING_SETUP, /*!<  for incoming setups*/
	MISDN_DIALING, /*!<  when pbx_start */
	MISDN_PROGRESS, /*!<  we got a progress */
	MISDN_PROCEEDING, /*!<  we got a progress */
	MISDN_CALLING, /*!<  when misdn_call is called */
	MISDN_CALLING_ACKNOWLEDGE, /*!<  when we get SETUP_ACK */
	MISDN_ALERTING, /*!<  when Alerting */
	MISDN_BUSY, /*!<  when BUSY */
	MISDN_CONNECTED, /*!<  when connected */
	MISDN_PRECONNECTED, /*!<  when connected */
	MISDN_DISCONNECTED, /*!<  when connected */
	MISDN_RELEASED, /*!<  when connected */
	MISDN_BRIDGED, /*!<  when bridged */
	MISDN_CLEANING, /*!< when hangup from * but we were connected before */
	MISDN_HUNGUP_FROM_MISDN, /*!< when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	MISDN_HUNGUP_FROM_AST, /*!< when DISCONNECT/RELEASE/REL_COMP came out of */
	/* misdn_hangup */
	MISDN_HOLDED, /*!< if this chan is holded */
	MISDN_HOLD_DISCONNECT, /*!< if this chan is holded */
  
};

#define ORG_AST 1
#define ORG_MISDN 2

struct hold_info {
	int port;
	int channel;
};

struct chan_list {
  
	ast_mutex_t lock;

	char allowed_bearers[BUFFERSIZE+1];
	
	enum misdn_chan_state state;
	int need_queue_hangup;
	int need_hangup;
	int need_busy;
	
	int noautorespond_on_setup;
	
	int orginator;

	int norxtone;
	int notxtone; 

	int toggle_ec;
	
	int incoming_early_audio;

	int ignore_dtmf;

	int pipe[2];
	char ast_rd_buf[4096];
	struct ast_frame frame;

	int faxdetect;
	int faxhandled;

	int ast_dsp;

	int jb_len;
	int jb_upper_threshold;
	struct misdn_jb *jb;
	
	struct ast_dsp *dsp;
	struct ast_trans_pvt *trans;
  
	struct ast_channel * ast;

	int dummy;
  
	struct misdn_bchannel *bc;

	struct hold_info hold_info;

	unsigned int l3id;
	int addr;

	char context[BUFFERSIZE];

	int zero_read_cnt;
	int dropped_frame_cnt;

	int far_alerting;

	int nttimeout;

	int other_pid;
	struct chan_list *other_ch;

	const struct tone_zone_sound *ts;
	
	int overlap_dial;
	int overlap_dial_task;
	ast_mutex_t overlap_tv_lock;
	struct timeval overlap_tv;
  
	struct chan_list *peer;
	struct chan_list *next;
	struct chan_list *prev;
	struct chan_list *first;
};



void export_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);
void import_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);

struct robin_list {
	char *group;
	int port;
	int channel;
	struct robin_list *next;
	struct robin_list *prev;
};
static struct robin_list *robin = NULL;



static struct ast_frame *process_ast_dsp(struct chan_list *tmp, struct ast_frame *frame);



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
	robin = NULL;
}

static struct robin_list* get_robin_position (char *group) 
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


/* the main schedule context for stuff like l1 watcher, overlap dial, ... */
static struct sched_context *misdn_tasks = NULL;
static pthread_t misdn_tasks_thread;

static void chan_misdn_log(int level, int port, char *tmpl, ...);

static struct ast_channel *misdn_new(struct chan_list *cl, int state,  char *exten, char *callerid, int format, int port, int c);
static void send_digit_to_chan(struct chan_list *cl, char digit );

static void hangup_chan(struct chan_list *ch);
static int pbx_start_chan(struct chan_list *ch);

#define AST_CID_P(ast) ast->cid.cid_num
#define AST_BRIDGED_P(ast) ast_bridged_channel(ast) 
#define AST_LOAD_CFG ast_config_load
#define AST_DESTROY_CFG ast_config_destroy

#define MISDN_ASTERISK_TECH_PVT(ast) ast->tech_pvt
#define MISDN_ASTERISK_PVT(ast) 1

#include <asterisk/strings.h>

/* #define MISDN_DEBUG 1 */

static char *desc = "Channel driver for mISDN Support (Bri/Pri)";
static const char misdn_type[] = "mISDN";

static int tracing = 0 ;

static int usecnt=0;

/* Only alaw and mulaw is allowed for now */
static int prefformat =  AST_FORMAT_ALAW ; /*  AST_FORMAT_SLINEAR ;  AST_FORMAT_ULAW | */

static ast_mutex_t usecnt_lock; 

static int *misdn_debug;
static int *misdn_debug_only;
static int max_ports;

struct chan_list dummy_cl;

struct chan_list *cl_te=NULL;
ast_mutex_t cl_te_lock;

static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data);

static void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel*bc, struct chan_list *ch);

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan);
static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan);
static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc);
static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid);



static int dialtone_indicate(struct chan_list *cl);
static int hanguptone_indicate(struct chan_list *cl);
static int stop_indicate(struct chan_list *cl);

static int start_bc_tones(struct chan_list *cl);
static int stop_bc_tones(struct chan_list *cl);
static void release_chan(struct misdn_bchannel *bc);

static int misdn_check_l2l1(struct ast_channel *chan, void *data);
static int misdn_set_opt_exec(struct ast_channel *chan, void *data);
static int misdn_facility_exec(struct ast_channel *chan, void *data);

int chan_misdn_jb_empty(struct misdn_bchannel *bc, char *buf, int len);

#ifdef MISDN_1_2
static int update_pipeline_config(struct misdn_bchannel *bc);
#else
static int update_ec_config(struct misdn_bchannel *bc);
#endif

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



struct allowed_bearers {
	int cap;
	int val;
	char *name;
};

struct allowed_bearers allowed_bearers_array[]={
	{INFO_CAPABILITY_SPEECH,1,"speech"},
	{INFO_CAPABILITY_AUDIO_3_1K,2,"3_1khz"},
	{INFO_CAPABILITY_DIGITAL_UNRESTRICTED,4,"digital_unrestricted"},
	{INFO_CAPABILITY_DIGITAL_RESTRICTED,8,"digital_restriced"},
	{INFO_CAPABILITY_VIDEO,16,"video"}
};

static char *bearer2str(int cap) {
	static char *bearers[]={
		"Speech",
		"Audio 3.1k",
		"Unres Digital",
		"Res Digital",
		"Video",
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
	case INFO_CAPABILITY_VIDEO:
		return bearers[4];
		break;
	default:
		return bearers[5];
		break;
	}
}


static void print_facility( struct misdn_bchannel *bc)
{
	switch (bc->fac_type) {
	case FACILITY_CALLDEFLECT:
		chan_misdn_log(2,bc->port," --> calldeflect: %s\n",
			       bc->fac.calldeflect_nr);
		break;
	case FACILITY_CENTREX:
		chan_misdn_log(2,bc->port," --> centrex: %s\n",
			       bc->fac.cnip);
		break;
	default:
		chan_misdn_log(2,bc->port," --> unknown\n");
		
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

static void sighandler(int sig)
{}

static void* misdn_tasks_thread_func (void *data)
{
	int wait;
	struct sigaction sa;

	sa.sa_handler = sighandler;
	sa.sa_flags = SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGUSR1);
	sigaction(SIGUSR1, &sa, NULL);
	
	sem_post((sem_t *)data);

	while (1) {
		wait = ast_sched_wait(misdn_tasks);
		if (wait < 0)
			wait = 8000;
		if (poll(NULL, 0, wait) < 0)
			chan_misdn_log(4, 0, "Waking up misdn_tasks thread\n");
		ast_sched_runq(misdn_tasks);
	}
	return NULL;
}

static void misdn_tasks_init (void)
{
	sem_t blocker;
	int i = 5;

	if (sem_init(&blocker, 0, 0)) {
		perror("chan_misdn: Failed to initialize semaphore!");
		exit(1);
	}

	chan_misdn_log(4, 0, "Starting misdn_tasks thread\n");
	
	misdn_tasks = sched_context_create();
	pthread_create(&misdn_tasks_thread, NULL, misdn_tasks_thread_func, &blocker);

	while (sem_wait(&blocker) && --i);
	sem_destroy(&blocker);
}

static void misdn_tasks_destroy (void)
{
	if (misdn_tasks) {
		chan_misdn_log(4, 0, "Killing misdn_tasks thread\n");
		if ( pthread_cancel(misdn_tasks_thread) == 0 ) {
			cb_log(4, 0, "Joining misdn_tasks thread\n");
			pthread_join(misdn_tasks_thread, NULL);
		}
		sched_context_destroy(misdn_tasks);
	}
}

static inline void misdn_tasks_wakeup (void)
{
	pthread_kill(misdn_tasks_thread, SIGUSR1);
}

static inline int _misdn_tasks_add_variable (int timeout, ast_sched_cb callback, void *data, int variable)
{
	int task_id;

	if (!misdn_tasks) {
		misdn_tasks_init();
	}
	task_id = ast_sched_add_variable(misdn_tasks, timeout, callback, data, variable);
	misdn_tasks_wakeup();

	return task_id;
}

#if 0
static int misdn_tasks_add (int timeout, ast_sched_cb callback, void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 0);
}
#endif

static int misdn_tasks_add_variable (int timeout, ast_sched_cb callback, void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 1);
}

static void misdn_tasks_remove (int task_id)
{
	ast_sched_del(misdn_tasks, task_id);
}

static int misdn_overlap_dial_task (void *data)
{
	struct timeval tv_end, tv_now;
	int diff;
	struct chan_list *ch = (struct chan_list *)data;

	chan_misdn_log(4, ch->bc->port, "overlap dial task, chan_state: %d\n", ch->state);

	if (ch->state != MISDN_WAITING4DIGS) {
		ch->overlap_dial_task = -1;
		return 0;
	}
	
	ast_mutex_lock(&ch->overlap_tv_lock);
	tv_end = ch->overlap_tv;
	ast_mutex_unlock(&ch->overlap_tv_lock);
	
	tv_end.tv_sec += ch->overlap_dial;
	tv_now = ast_tvnow();

	diff = ast_tvdiff_ms(tv_end, tv_now);

	if (diff <= 100) {
		/* if we are 100ms near the timeout, we are satisfied.. */
		stop_indicate(ch);
		if (ast_exists_extension(ch->ast, ch->context, ch->bc->dad, 1, ch->bc->oad)) {
			ch->state=MISDN_DIALING;
			if (pbx_start_chan(ch) < 0) {
				chan_misdn_log(-1, ch->bc->port, "ast_pbx_start returned < 0 in misdn_overlap_dial_task\n");
				goto misdn_overlap_dial_task_disconnect;
			}
		} else {
misdn_overlap_dial_task_disconnect:
			hanguptone_indicate(ch);
			if (ch->bc->nt)
				misdn_lib_send_event(ch->bc, EVENT_RELEASE_COMPLETE );
			else
				misdn_lib_send_event(ch->bc, EVENT_RELEASE);
		}
		ch->overlap_dial_task = -1;
		return 0;
	} else
		return diff;
}

static void send_digit_to_chan(struct chan_list *cl, char digit )
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
	if (argc != 5) return RESULT_SHOWUSAGE; 

	return 0;
}


static int misdn_port_block(int fd, int argc, char *argv[])
{
	int port;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_block(port);

	return 0;
}

static int misdn_port_unblock(int fd, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_unblock(port);

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

static int misdn_restart_pid (int fd, int argc, char *argv[])
{
	int pid;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	pid = atoi(argv[3]);

	misdn_lib_pid_restart(pid);

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

static int misdn_port_down (int fd, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	port = atoi(argv[3]);
	
	misdn_lib_get_port_down(port);
  
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
		ast_cli(fd," -> Version: chan_misdn-" CHAN_MISDN_VERSION "\n");
		for (elem = MISDN_GEN_FIRST + 1, linebreak = 1; elem < MISDN_GEN_LAST; elem++, linebreak++) {
			misdn_cfg_get_config_string( 0, elem, buffer, BUFFERSIZE);
			ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
		}
		ast_cli(fd, "\n");
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

static struct state_struct state_array[] = {
	{MISDN_NOTHING,"NOTHING"}, /* at beginning */
	{MISDN_WAITING4DIGS,"WAITING4DIGS"}, /*  when waiting for infos */
	{MISDN_EXTCANTMATCH,"EXTCANTMATCH"}, /*  when asterisk couldnt match our ext */
	{MISDN_INCOMING_SETUP,"INCOMING SETUP"}, /*  when pbx_start */
	{MISDN_DIALING,"DIALING"}, /*  when pbx_start */
	{MISDN_PROGRESS,"PROGRESS"}, /*  when pbx_start */
	{MISDN_PROCEEDING,"PROCEEDING"}, /*  when pbx_start */
	{MISDN_CALLING,"CALLING"}, /*  when misdn_call is called */
	{MISDN_CALLING_ACKNOWLEDGE,"CALLING_ACKNOWLEDGE"}, /*  when misdn_call is called */
	{MISDN_ALERTING,"ALERTING"}, /*  when Alerting */
	{MISDN_BUSY,"BUSY"}, /*  when BUSY */
	{MISDN_CONNECTED,"CONNECTED"}, /*  when connected */
	{MISDN_PRECONNECTED,"PRECONNECTED"}, /*  when connected */
	{MISDN_DISCONNECTED,"DISCONNECTED"}, /*  when connected */
	{MISDN_RELEASED,"RELEASED"}, /*  when connected */
	{MISDN_BRIDGED,"BRIDGED"}, /*  when bridged */
	{MISDN_CLEANING,"CLEANING"}, /* when hangup from * but we were connected before */
	{MISDN_HUNGUP_FROM_MISDN,"HUNGUP_FROM_MISDN"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HOLDED,"HOLDED"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HOLD_DISCONNECT,"HOLD_DISCONNECT"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HUNGUP_FROM_AST,"HUNGUP_FROM_AST"} /* when DISCONNECT/RELEASE/REL_COMP came out of */
	/* misdn_hangup */
};

static char *misdn_get_ch_state(struct chan_list *p) 
{
	int i;
	static char state[8];
	
	if( !p) return NULL;
  
	for (i=0; i< sizeof(state_array)/sizeof(struct state_struct); i++) {
		if ( state_array[i].state == p->state) return state_array[i].txt; 
	}

 	sprintf(state,"%d",p->state) ;

	return state;
}



static void reload_config(void)
{
	int i, cfg_debug;

	if (!g_config_initialized) {
		ast_log(LOG_WARNING, "chan_misdn is not initialized properly, still reloading ?\n");
		return ;
	}
	
	free_robin_list();
	misdn_cfg_reload();
	misdn_cfg_update_ptp();
	misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);
	misdn_cfg_get( 0, MISDN_GEN_DEBUG, &cfg_debug, sizeof(int));

	for (i = 0;  i <= max_ports; i++) {
		misdn_debug[i] = cfg_debug;
		misdn_debug_only[i] = 0;
	}
}

static int misdn_reload (int fd, int argc, char *argv[])
{
	ast_cli(fd, "Reloading mISDN Config\n");
	reload_config();
	return 0;
}

static void print_bc_info (int fd, struct chan_list* help, struct misdn_bchannel* bc)
{
	struct ast_channel *ast=help->ast;
	ast_cli(fd,
		"* Pid:%d Prt:%d Ch:%d Mode:%s Org:%s dad:%s oad:%s rad:%s ctx:%s state:%s\n",

		bc->pid, bc->port, bc->channel,
		bc->nt?"NT":"TE",
		help->orginator == ORG_AST?"*":"I",
		ast?ast->exten:NULL,
		ast?AST_CID_P(ast):NULL,
		bc->rad,
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
			"  --> display: %s\n"
			"  --> activated: %d\n"
			"  --> state: %s\n"
			"  --> capability: %s\n"
#ifdef MISDN_1_2
			"  --> pipeline: %s\n"
#else
			"  --> echo_cancel: %d\n"
#endif
			"  --> notone : rx %d tx:%d\n"
			"  --> bc_hold: %d\n",
			help->ast->name,
			help->l3id,
			help->addr,
			bc->addr,
			bc?bc->l3_id:-1,
			bc->display,
			
			bc->active,
			bc_state2str(bc->bc_state),
			bearer2str(bc->capability),
#ifdef MISDN_1_2
			bc->pipeline,
#else
			bc->ec_enable,
#endif

			help->norxtone,help->notxtone,
			bc->holded
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
		} else {
			if (help->state == MISDN_HOLDED) {
				ast_cli(fd, "ITS A HOLDED BC:\n");
				ast_cli(fd, " --> l3_id: %x\n"
						" --> dad:%s oad:%s\n"
				
						,help->l3id
						,ast->exten
						,AST_CID_P(ast)
						);
			} else {
				ast_cli(fd,"* Channel in unknown STATE !!! Exten:%s, Callerid:%s\n", ast->exten, AST_CID_P(ast));
			}
		}
	}
  
 	misdn_dump_chanlist();
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

static int misdn_send_restart(int fd, int argc, char *argv[])
{
	int port;
	int channel;
	
	if ( (argc < 4) ||  (argc >  5) )
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	if (argc==5) {
		channel = atoi(argv[4]);
 		misdn_lib_send_restart(port, channel);
	} else
 		misdn_lib_send_restart(port, -1 );
	
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
			
			tmp->toggle_ec=tmp->toggle_ec?0:1;

			if (tmp->toggle_ec) {
#ifdef MISDN_1_2
				update_pipeline_config(tmp->bc);
#else
				update_ec_config(tmp->bc);
#endif
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
			ast_copy_string(tmp->bc->display, msg, sizeof(tmp->bc->display));
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

static struct ast_cli_entry cli_send_restart=
{ {"misdn","send","restart", NULL},
  misdn_send_restart,
  "Sends a restart for every bchannel on the given port", 
  "Usage: misdn send restart <port>\n"
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

static struct ast_cli_entry cli_port_block=
{ {"misdn","port","block", NULL},
  misdn_port_block,
  "Blocks the given port", 
  "Usage: misdn port block\n"
};

static struct ast_cli_entry cli_port_unblock=
{ {"misdn","port","unblock", NULL},
  misdn_port_unblock,
  "Unblocks the given port", 
  "Usage: misdn port unblock\n"
};


static struct ast_cli_entry cli_restart_port =
{ {"misdn","restart","port", NULL},
  misdn_restart_port,
  "Restarts the given port", 
  "Usage: misdn restart port\n"
};

static struct ast_cli_entry cli_restart_pid =
{ {"misdn","restart","pid", NULL},
  misdn_restart_pid,
  "Restarts the given pid", 
  "Usage: misdn restart pid\n"
};

static struct ast_cli_entry cli_port_up =
{ {"misdn","port","up", NULL},
  misdn_port_up,
  "Tries to establish L1 on the given port", 
  "Usage: misdn port up <port>\n"
};

static struct ast_cli_entry cli_port_down =
{ {"misdn","port","down", NULL},
  misdn_port_down,
  "Tries to deacivate the L1 on the given port", 
  "Usage: misdn port down <port>\n"
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


static int update_config (struct chan_list *ch, int orig) 
{
	if (!ch) {
		ast_log(LOG_WARNING, "Cannot configure without chanlist\n");
		return -1;
	}
	
	struct ast_channel *ast=ch->ast;
	struct misdn_bchannel *bc=ch->bc;
	if (! ast || ! bc ) {
		ast_log(LOG_WARNING, "Cannot configure without ast || bc\n");
		return -1;
	}
	
	int port=bc->port;
	
	chan_misdn_log(7,port,"update_config: Getting Config\n");

	int hdlc=0;
	misdn_cfg_get( port, MISDN_CFG_HDLC, &hdlc, sizeof(int));
	
	if (hdlc) {
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		case INFO_CAPABILITY_DIGITAL_RESTRICTED:
			chan_misdn_log(1,bc->port," --> CONF HDLC\n");
			bc->hdlc=1;
			break;
		}
		
	}
	
	
	int pres, screen;
			
	misdn_cfg_get( port, MISDN_CFG_PRES, &pres, sizeof(int));
	misdn_cfg_get( port, MISDN_CFG_SCREEN, &screen, sizeof(int));
	chan_misdn_log(2,port," --> pres: %d screen: %d\n",pres, screen);
		
	if ( (pres + screen) < 0 ) {

		chan_misdn_log(2,port," --> pres: %x\n", ast->cid.cid_pres);
			
		switch (ast->cid.cid_pres & 0x60){
				
		case AST_PRES_RESTRICTED:
			bc->pres=1;
			chan_misdn_log(2, port, " --> PRES: Restricted (0x1)\n");
			break;
				
				
		case AST_PRES_UNAVAILABLE:
			bc->pres=2;
			chan_misdn_log(2, port, " --> PRES: Unavailable (0x2)\n");
			break;
				
		default:
			bc->pres=0;
			chan_misdn_log(2, port, " --> PRES: Allowed (0x0)\n");
		}
			
		switch (ast->cid.cid_pres & 0x3){
				
		case AST_PRES_USER_NUMBER_UNSCREENED:
			bc->screen=0;
			chan_misdn_log(2, port, " --> SCREEN: Unscreened (0x0)\n");
			break;

		case AST_PRES_USER_NUMBER_PASSED_SCREEN:
			bc->screen=1;
			chan_misdn_log(2, port, " --> SCREEN: Passed Screen (0x1)\n");
			break;
		case AST_PRES_USER_NUMBER_FAILED_SCREEN:
			bc->screen=2;
			chan_misdn_log(2, port, " --> SCREEN: Failed Screen (0x2)\n");
			break;
				
		case AST_PRES_NETWORK_NUMBER:
			bc->screen=3;
			chan_misdn_log(2, port, " --> SCREEN: Network Nr. (0x3)\n");
			break;
				
		default:
			bc->screen=0;
			chan_misdn_log(2, port, " --> SCREEN: Unscreened (0x0)\n");
		}

			
	} else {
		bc->screen=screen;
		bc->pres=pres;
	}

	return 0;
	
}




void config_jitterbuffer(struct chan_list *ch)
{
	struct misdn_bchannel *bc=ch->bc;
	int len=ch->jb_len, threshold=ch->jb_upper_threshold;
	
	chan_misdn_log(5,bc->port, "config_jb: Called\n");
	
	if ( ! len ) {
		chan_misdn_log(1,bc->port, "config_jb: Deactivating Jitterbuffer\n");
		bc->nojitter=1;
	} else {
		
		if (len <=100 || len > 8000) {
			chan_misdn_log(0,bc->port,"config_jb: Jitterbuffer out of Bounds, setting to 1000\n");
			len=1000;
		}
		
		if ( threshold > len ) {
			chan_misdn_log(0,bc->port,"config_jb: Jitterbuffer Threshold > Jitterbuffer setting to Jitterbuffer -1\n");
		}
		
		if ( ch->jb) {
			cb_log(0,bc->port,"config_jb: We've got a Jitterbuffer Already on this port.\n");
			misdn_jb_destroy(ch->jb);
			ch->jb=NULL;
		}
		
		ch->jb=misdn_jb_init(len, threshold);

		if (!ch->jb ) 
			bc->nojitter=1;
	}
}


void debug_numplan(int port, int numplan, char *type)
{
	switch (numplan) {
	case NUMPLAN_INTERNATIONAL:
		chan_misdn_log(2, port, " --> %s: International\n",type);
		break;
	case NUMPLAN_NATIONAL:
		chan_misdn_log(2, port, " --> %s: National\n",type);
		break;
	case NUMPLAN_SUBSCRIBER:
		chan_misdn_log(2, port, " --> %s: Subscriber\n",type);
		break;
	case NUMPLAN_UNKNOWN:
		chan_misdn_log(2, port, " --> %s: Unknown\n",type);
		break;
		/* Maybe we should cut off the prefix if present ? */
	default:
		chan_misdn_log(0, port, " --> !!!! Wrong dialplan setting, please see the misdn.conf sample file\n ");
		break;
	}
}

#ifdef MISDN_1_2
static int update_pipeline_config(struct misdn_bchannel *bc)
{
	int ec;

	misdn_cfg_get(bc->port, MISDN_CFG_PIPELINE, bc->pipeline, sizeof(bc->pipeline));

	if (*bc->pipeline)
		return 0;

	misdn_cfg_get(bc->port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
	if (ec == 1)
		snprintf(bc->pipeline, sizeof(bc->pipeline) - 1, "mg2ec");
	else if (ec > 1)
		snprintf(bc->pipeline, sizeof(bc->pipeline) - 1, "mg2ec(deftaps=%d)", ec);

	return 0;
}
#else
static int update_ec_config(struct misdn_bchannel *bc)
{
	int ec;
	int port=bc->port;
		
	misdn_cfg_get( port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
	
	if (ec == 1 ) {
		bc->ec_enable=1;
	} else if ( ec > 1 ) {
		bc->ec_enable=1;
		bc->ec_deftaps=ec;
	}

	return 0;
}
#endif

static int read_config(struct chan_list *ch, int orig) {

	if (!ch) {
		ast_log(LOG_WARNING, "Cannot configure without chanlist\n");
		return -1;
	}

	struct ast_channel *ast=ch->ast;
	struct misdn_bchannel *bc=ch->bc;
	if (! ast || ! bc ) {
		ast_log(LOG_WARNING, "Cannot configure without ast || bc\n");
		return -1;
	}
	
	int port=bc->port;
	
	chan_misdn_log(5,port,"read_config: Getting Config\n");
	
	char lang[BUFFERSIZE+1];

	misdn_cfg_get( port, MISDN_CFG_LANGUAGE, lang, BUFFERSIZE);
	ast_copy_string(ast->language, lang, sizeof(ast->language));
	
	char musicclass[BUFFERSIZE+1];
	
	misdn_cfg_get( port, MISDN_CFG_MUSICCLASS, musicclass, BUFFERSIZE);
	ast_copy_string(ast->musicclass, musicclass, sizeof(ast->musicclass));
	
	misdn_cfg_get( port, MISDN_CFG_TXGAIN, &bc->txgain, sizeof(int));
	misdn_cfg_get( port, MISDN_CFG_RXGAIN, &bc->rxgain, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_INCOMING_EARLY_AUDIO, &ch->incoming_early_audio, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_SENDDTMF, &bc->send_dtmf, sizeof(int));

	misdn_cfg_get( port, MISDN_CFG_NEED_MORE_INFOS, &bc->need_more_infos, sizeof(int));
	misdn_cfg_get( port, MISDN_CFG_NTTIMEOUT, &ch->nttimeout, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_NOAUTORESPOND_ON_SETUP, &ch->noautorespond_on_setup, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_FAR_ALERTING, &ch->far_alerting, sizeof(int));

	misdn_cfg_get( port, MISDN_CFG_ALLOWED_BEARERS, &ch->allowed_bearers, BUFFERSIZE);
	
	
	int hdlc=0;
	misdn_cfg_get( port, MISDN_CFG_HDLC, &hdlc, sizeof(int));
	
	if (hdlc) {
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		case INFO_CAPABILITY_DIGITAL_RESTRICTED:
			chan_misdn_log(1,bc->port," --> CONF HDLC\n");
			bc->hdlc=1;
			break;
		}
		
	}
	/*Initialize new Jitterbuffer*/
	{
		misdn_cfg_get( port, MISDN_CFG_JITTERBUFFER, &ch->jb_len, sizeof(int));
		misdn_cfg_get( port, MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD, &ch->jb_upper_threshold, sizeof(int));
		
		config_jitterbuffer(ch);
	}
	
	misdn_cfg_get( bc->port, MISDN_CFG_CONTEXT, ch->context, sizeof(ch->context));
	
	ast_copy_string (ast->context,ch->context,sizeof(ast->context));	

#ifdef MISDN_1_2
	update_pipeline_config(bc);
#else
	update_ec_config(bc);
#endif

	{
		int eb3;
		
		misdn_cfg_get( bc->port, MISDN_CFG_EARLY_BCONNECT, &eb3, sizeof(int));
		bc->early_bconnect=eb3;
	}
	
	port=bc->port;
	
	{
		char buf[256];
		ast_group_t pg,cg;
		
		misdn_cfg_get(port, MISDN_CFG_PICKUPGROUP, &pg, sizeof(pg));
		misdn_cfg_get(port, MISDN_CFG_CALLGROUP, &cg, sizeof(cg));
		
		chan_misdn_log(5, port, " --> * CallGrp:%s PickupGrp:%s\n",ast_print_group(buf,sizeof(buf),cg),ast_print_group(buf,sizeof(buf),pg));
		ast->pickupgroup=pg;
		ast->callgroup=cg;
	}
	
	if ( orig  == ORG_AST) {
		misdn_cfg_get( port, MISDN_CFG_TE_CHOOSE_CHANNEL, &(bc->te_choose_channel), sizeof(int));
		
		{
			char callerid[BUFFERSIZE+1];
			misdn_cfg_get( port, MISDN_CFG_CALLERID, callerid, BUFFERSIZE);
			if ( ! ast_strlen_zero(callerid) ) {
				chan_misdn_log(1, port, " --> * Setting Cid to %s\n", callerid);
				{
					int l = sizeof(bc->oad);
					strncpy(bc->oad,callerid, l);
					bc->oad[l-1] = 0;
				}

			}

			
			misdn_cfg_get( port, MISDN_CFG_DIALPLAN, &bc->dnumplan, sizeof(int));
			misdn_cfg_get( port, MISDN_CFG_LOCALDIALPLAN, &bc->onumplan, sizeof(int));
			misdn_cfg_get( port, MISDN_CFG_CPNDIALPLAN, &bc->cpnnumplan, sizeof(int));
			debug_numplan(port, bc->dnumplan,"TON");
			debug_numplan(port, bc->onumplan,"LTON");
			debug_numplan(port, bc->cpnnumplan,"CTON");
		}

		ch->overlap_dial = 0;
	} else { /** ORIGINATOR MISDN **/
	
		misdn_cfg_get( port, MISDN_CFG_CPNDIALPLAN, &bc->cpnnumplan, sizeof(int));
		debug_numplan(port, bc->cpnnumplan,"CTON");
		
		char prefix[BUFFERSIZE+1]="";
		switch( bc->onumplan ) {
		case NUMPLAN_INTERNATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
			break;
			
		case NUMPLAN_NATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
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
		
		if (!ast_strlen_zero(bc->dad)) {
			ast_copy_string(bc->orig_dad,bc->dad, sizeof(bc->orig_dad));
		}
		
		if ( ast_strlen_zero(bc->dad) && !ast_strlen_zero(bc->keypad)) {
			ast_copy_string(bc->dad,bc->keypad, sizeof(bc->dad));
		}

		prefix[0] = 0;
		
		switch( bc->dnumplan ) {
		case NUMPLAN_INTERNATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
			break;
		case NUMPLAN_NATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
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
		
		if ( strcmp(bc->dad,ast->exten)) {
			ast_copy_string(ast->exten, bc->dad, sizeof(ast->exten));
		}
		
		ast_set_callerid(ast, bc->oad, NULL, bc->oad);
		
		if ( !ast_strlen_zero(bc->rad) ) 
			ast->cid.cid_rdnis=strdup(bc->rad);
		
		misdn_cfg_get(bc->port, MISDN_CFG_OVERLAP_DIAL, &ch->overlap_dial, sizeof(ch->overlap_dial));
		ast_mutex_init(&ch->overlap_tv_lock);
	} /* ORIG MISDN END */

	ch->overlap_dial_task = -1;
	
	return 0;
}


/*****************************/
/*** AST Indications Start ***/
/*****************************/

static int misdn_call(struct ast_channel *ast, char *dest, int timeout)
{
	int port=0;
	int r;
	struct chan_list *ch=MISDN_ASTERISK_TECH_PVT(ast);
	struct misdn_bchannel *newbc;
	char *opts=NULL, *ext;
	char dest_cp[256];
	
	{
		strncpy(dest_cp,dest,sizeof(dest_cp)-1);
		dest_cp[sizeof(dest_cp)]=0;

		ext=dest_cp;
		strsep(&ext,"/");
		if (ext) {
			opts=ext;
			strsep(&opts,"/");
		}  else {
			ast_log(LOG_WARNING, "Malformed dialstring\n");
			return -1;
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

	
	chan_misdn_log(1, port, "* CALL: %s\n",dest);
	
	chan_misdn_log(2, port, " --> * dad:%s tech:%s ctx:%s\n",ast->exten,ast->name, ast->context);
	
	chan_misdn_log(3, port, " --> * adding2newbc ext %s\n",ast->exten);
	if (ast->exten) {
		int l = sizeof(newbc->dad);
		strncpy(ast->exten,ext,sizeof(ast->exten));

		strncpy(newbc->dad,ext,l);

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
		if (!ch) { ast_verbose("No chan_list in misdn_call\n"); return -1;}
		
		newbc->capability=ast->transfercapability;
		pbx_builtin_setvar_helper(ast,"TRANSFERCAPABILITY",ast_transfercapability2str(newbc->capability));
		if ( ast->transfercapability == INFO_CAPABILITY_DIGITAL_UNRESTRICTED) {
			chan_misdn_log(2, port, " --> * Call with flag Digital\n");
		}
		

		/* update screening and presentation */ 
		update_config(ch,ORG_AST);
		
		/* fill in some ies from channel vary*/
		import_ch(ast, newbc, ch);
		
		/* Finally The Options Override Everything */
		if (opts)
			misdn_set_opt_exec(ast,opts);
		else
			chan_misdn_log(2,port,"NO OPTS GIVEN\n");

		/*check for bridging*/
		int bridging;
		misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
		if (bridging && ch->other_ch) {
#ifdef MISDN_1_2
			chan_misdn_log(1, port, "Disabling EC (aka Pipeline) on both Sides\n");
			*ch->bc->pipeline=0;
			*ch->other_ch->bc->pipeline=0;
#else
			chan_misdn_log(1, port, "Disabling EC on both Sides\n");
			ch->bc->ec_enable=0;
			ch->other_ch->bc->ec_enable=0;
#endif
		}
		
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
	
	chan_misdn_log(2, port, " --> * SEND: State Dialing pid:%d\n",newbc?newbc->pid:1);

	ast_setstate(ast, AST_STATE_DIALING);
	ast->hangupcause=16;
	
	if (newbc->nt) stop_bc_tones(ch);

	ch->state=MISDN_CALLING;
	
	return 0; 
}


static int misdn_answer(struct ast_channel *ast)
{
	struct chan_list *p;

	
	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast)) ) return -1;
	
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
		const char *tmp_key = pbx_builtin_getvar_helper(p->ast, "CRYPT_KEY");
		
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

	{
		const char *nodsp=pbx_builtin_getvar_helper(ast, "MISDN_DIGITAL_TRANS");
		if (nodsp) {
			chan_misdn_log(1, p->bc->port, " --> Connection is transparent digital\n");
			p->bc->nodsp=1;
			p->bc->hdlc=0;
			p->bc->nojitter=1;
		}
	}
	
	p->state = MISDN_CONNECTED;
	stop_indicate(p);

	if ( ast_strlen_zero(p->bc->cad) ) {
		chan_misdn_log(2,p->bc->port," --> empty cad using dad\n");
		ast_copy_string(p->bc->cad,p->bc->dad,sizeof(p->bc->cad));
	}

	misdn_lib_send_event( p->bc, EVENT_CONNECT);
	start_bc_tones(p);
	
	return 0;
}

static int misdn_digit(struct ast_channel *ast, char digit )
{
	struct chan_list *p;
	
	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast))) return -1;

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
			/* Do not send Digits in CONNECTED State, when
			 * the other side is too mISDN. */
			if (p->other_ch ) 
				return 0;

			if ( bc->send_dtmf ) 
				send_digit_to_chan(p,digit);
		break;
	}
	
	return 0;
}


static int misdn_fixup(struct ast_channel *oldast, struct ast_channel *ast)
{
	struct chan_list *p;
	
	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast) )) return -1;
	
	chan_misdn_log(1, p->bc?p->bc->port:0, "* IND: Got Fixup State:%s L3id:%x\n", misdn_get_ch_state(p), p->l3id);
	
	p->ast = ast ;
  
	return 0;
}



static int misdn_indication(struct ast_channel *ast, int cond)
{
	struct chan_list *p;

  
	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast))) {
		ast_log(LOG_WARNING, "Returnded -1 in misdn_indication\n");
		return -1;
	}
	
	if (!p->bc ) {
		chan_misdn_log(1, 0, "* IND : Indication from %s\n",ast->exten);
		ast_log(LOG_WARNING, "Private Pointer but no bc ?\n");
		return -1;
	}
	
	chan_misdn_log(5, p->bc->port, "* IND : Indication [%d] from %s\n",cond, ast->exten);
	
	switch (cond) {
	case AST_CONTROL_BUSY:
		chan_misdn_log(1, p->bc->port, "* IND :\tbusy pid:%d\n",p->bc?p->bc->pid:-1);
		ast_setstate(ast,AST_STATE_BUSY);

		p->bc->out_cause=17;
		if (p->state != MISDN_CONNECTED) {
			start_bc_tones(p);
			misdn_lib_send_event( p->bc, EVENT_DISCONNECT);
		} else {
			chan_misdn_log(-1, p->bc->port, " --> !! Got Busy in Connected State !?! ast:%s\n", ast->name);
		}
		return -1;
		break;
	case AST_CONTROL_RING:
		chan_misdn_log(1, p->bc->port, "* IND :\tring pid:%d\n",p->bc?p->bc->pid:-1);
		return -1;
		break;
		
	case AST_CONTROL_RINGING:
		chan_misdn_log(1, p->bc->port, "* IND :\tringing pid:%d\n",p->bc?p->bc->pid:-1);
		switch (p->state) {
			case MISDN_ALERTING:
				chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d but I was Ringing before, so ignoreing it\n",p->bc?p->bc->pid:-1);
				break;
			case MISDN_CONNECTED:
				chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d but Connected, so just send TONE_ALERTING without state changes \n",p->bc?p->bc->pid:-1);
				return -1;
				break;
			default:
				p->state=MISDN_ALERTING;
				chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d\n",p->bc?p->bc->pid:-1);
				misdn_lib_send_event( p->bc, EVENT_ALERTING);
			
				if (p->other_ch && p->other_ch->bc) {
					if (misdn_inband_avail(p->other_ch->bc)) {
						chan_misdn_log(2,p->bc->port, " --> other End is mISDN and has inband info available\n");
						break;
					}

					if (!p->other_ch->bc->nt) {
						chan_misdn_log(2,p->bc->port, " --> other End is mISDN TE so it has inband info for sure (?)\n");
						break;
					}
				}

				chan_misdn_log(3, p->bc->port, " --> * SEND: State Ring pid:%d\n",p->bc?p->bc->pid:-1);
				ast_setstate(ast,AST_STATE_RINGING);
			
				if ( !p->bc->nt && (p->orginator==ORG_MISDN) && !p->incoming_early_audio ) 
					chan_misdn_log(2,p->bc->port, " --> incoming_early_audio off\n");
				else 
					return -1;
		}
		break;
	case AST_CONTROL_ANSWER:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tanswer pid:%d\n",p->bc?p->bc->pid:-1);
		start_bc_tones(p);
		break;
	case AST_CONTROL_TAKEOFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\ttakeoffhook pid:%d\n",p->bc?p->bc->pid:-1);
		return -1;
		break;
	case AST_CONTROL_OFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\toffhook pid:%d\n",p->bc?p->bc->pid:-1);
		return -1;
		break; 
	case AST_CONTROL_FLASH:
		chan_misdn_log(1, p->bc->port, " --> *\tflash pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_PROGRESS:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tprogress pid:%d\n",p->bc?p->bc->pid:-1);
		misdn_lib_send_event( p->bc, EVENT_PROGRESS);
		break;
	case AST_CONTROL_PROCEEDING:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tproceeding pid:%d\n",p->bc?p->bc->pid:-1);
		misdn_lib_send_event( p->bc, EVENT_PROCEEDING);
		break;
	case AST_CONTROL_CONGESTION:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tcongestion pid:%d\n",p->bc?p->bc->pid:-1);

		p->bc->out_cause=42;
		start_bc_tones(p);
		misdn_lib_send_event( p->bc, EVENT_DISCONNECT);

		if (p->bc->nt) {
			hanguptone_indicate(p);
		}
		break;
	case -1 :
		chan_misdn_log(1, p->bc->port, " --> * IND :\t-1! (stop indication) pid:%d\n",p->bc?p->bc->pid:-1);
		
		stop_indicate(p);

		if (p->state == MISDN_CONNECTED) 
			start_bc_tones(p);

		break;

	case AST_CONTROL_HOLD:
		chan_misdn_log(1, p->bc->port, " --> *\tHOLD pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case AST_CONTROL_UNHOLD:
		chan_misdn_log(1, p->bc->port, " --> *\tUNHOLD pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	default:
		chan_misdn_log(1, p->bc->port, " --> * Unknown Indication:%d pid:%d\n",cond,p->bc?p->bc->pid:-1);
	}
  
	return 0;
}

static int misdn_hangup(struct ast_channel *ast)
{
	struct chan_list *p;
	struct misdn_bchannel *bc=NULL;
	
	ast_log(LOG_DEBUG, "misdn_hangup(%s)\n", ast->name);

	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast) ) ) return -1;
	
	if (!p) {
		chan_misdn_log(3, 0, "misdn_hangup called, without chan_list obj.\n");
		return 0 ;
	}
	
	bc=p->bc;


	
	MISDN_ASTERISK_TECH_PVT(ast)=NULL;
	p->ast=NULL;

	bc=p->bc;
	
	if (ast->_state == AST_STATE_RESERVED || 
		p->state == MISDN_NOTHING || 
		p->state == MISDN_HOLDED || 
		p->state == MISDN_HOLD_DISCONNECT ) {

		CLEAN_CH:
		/* between request and call */
		ast_log(LOG_DEBUG, "State Reserved (or nothing) => chanIsAvail\n");
		MISDN_ASTERISK_TECH_PVT(ast)=NULL;
		
		cl_dequeue_chan(&cl_te, p);
		
		close(p->pipe[0]);
		close(p->pipe[1]);
		
		free(p);
		if (bc)
			misdn_lib_release(bc);
		
		return 0;
	}

	if (!bc) {
		ast_log(LOG_WARNING,"Hangup with private but no bc ? state:%s l3id:%x\n", misdn_get_ch_state(p), p->l3id);
		goto CLEAN_CH;
	}


	p->need_hangup=0;
	p->need_queue_hangup=0;


	if (!p->bc->nt) 
		stop_bc_tones(p);

	
	{
		const char *varcause=NULL;
		bc->out_cause=ast->hangupcause?ast->hangupcause:16;
		
		if ( (varcause=pbx_builtin_getvar_helper(ast, "HANGUPCAUSE")) ||
		     (varcause=pbx_builtin_getvar_helper(ast, "PRI_CAUSE"))) {
			int tmpcause=atoi(varcause);
			bc->out_cause=tmpcause?tmpcause:16;
		}
    
		chan_misdn_log(1, bc->port, "* IND : HANGUP\tpid:%d ctx:%s dad:%s oad:%s State:%s\n",p->bc?p->bc->pid:-1, ast->context, ast->exten, AST_CID_P(ast), misdn_get_ch_state(p));
		chan_misdn_log(3, bc->port, " --> l3id:%x\n",p->l3id);
		chan_misdn_log(3, bc->port, " --> cause:%d\n",bc->cause);
		chan_misdn_log(2, bc->port, " --> out_cause:%d\n",bc->out_cause);
		chan_misdn_log(2, bc->port, " --> state:%s\n", misdn_get_ch_state(p));
		
		switch (p->state) {
		case MISDN_CALLING:
		case MISDN_INCOMING_SETUP:
			/* This is the only place in misdn_hangup, where we 
			 * can call release_chan, else it might create lot's of trouble
			 * */
			ast_log(LOG_NOTICE, "release channel, in CALLING/INCOMING_SETUP state.. no other events happened\n");
			release_chan(bc);

			p->state=MISDN_CLEANING;
			misdn_lib_send_event( bc, EVENT_RELEASE_COMPLETE);
			break;
		case MISDN_HOLDED:
		case MISDN_DIALING:
			start_bc_tones(p);
			hanguptone_indicate(p);
		
			if (bc->need_disconnect)
				misdn_lib_send_event( bc, EVENT_DISCONNECT);
			break;

		case MISDN_CALLING_ACKNOWLEDGE:
			start_bc_tones(p);
			hanguptone_indicate(p);
		
			if (bc->need_disconnect)
				misdn_lib_send_event( bc, EVENT_DISCONNECT);
			break;
      
		case MISDN_ALERTING:
		case MISDN_PROGRESS:
		case MISDN_PROCEEDING:
			if (p->orginator != ORG_AST) 
				hanguptone_indicate(p);
      
			/*p->state=MISDN_CLEANING;*/
			if (bc->need_disconnect)
				misdn_lib_send_event( bc, EVENT_DISCONNECT);
			break;
		case MISDN_CONNECTED:
		case MISDN_PRECONNECTED:
			/*  Alerting or Disconect */
			if (p->bc->nt) {
				start_bc_tones(p);
				hanguptone_indicate(p);
				p->bc->progress_indicator=8;
			}
			if (bc->need_disconnect)
				misdn_lib_send_event( bc, EVENT_DISCONNECT);

			/*p->state=MISDN_CLEANING;*/
			break;
		case MISDN_DISCONNECTED:
			misdn_lib_send_event( bc, EVENT_RELEASE);
			p->state=MISDN_CLEANING; /* MISDN_HUNGUP_FROM_AST; */
			break;

		case MISDN_RELEASED:
		case MISDN_CLEANING:
			p->state=MISDN_CLEANING;
			break;

		case MISDN_BUSY:
			break;
      
		case MISDN_HOLD_DISCONNECT:
			/* need to send release here */
			chan_misdn_log(1, bc->port, " --> cause %d\n",bc->cause);
			chan_misdn_log(1, bc->port, " --> out_cause %d\n",bc->out_cause);
			
			bc->out_cause=-1;
			misdn_lib_send_event(bc,EVENT_RELEASE);
			p->state=MISDN_CLEANING;
			break;
		default:
			if (bc->nt) {
				bc->out_cause=-1;
				misdn_lib_send_event(bc, EVENT_RELEASE);
				p->state=MISDN_CLEANING; 
			} else {
				if (bc->need_disconnect)
					misdn_lib_send_event(bc, EVENT_DISCONNECT);
			}
		}

		p->state=MISDN_CLEANING;
    
	}
	

	chan_misdn_log(3, bc->port, " --> Channel: %s hanguped new state:%s\n",ast->name,misdn_get_ch_state(p));
	
	return 0;
}

static struct ast_frame  *misdn_read(struct ast_channel *ast)
{
	struct chan_list *tmp;
	int len;
	
	if (!ast) {
		chan_misdn_log(1,0,"misdn_read called without ast\n");
		return NULL;
	}
	if (! (tmp=MISDN_ASTERISK_TECH_PVT(ast)) ) {
		chan_misdn_log(1,0,"misdn_read called without ast->pvt\n");
		return NULL;
	}

	if (!tmp->bc && !(tmp->state==MISDN_HOLDED)) {
		chan_misdn_log(1,0,"misdn_read called without bc\n");
		return NULL;
	}

	len=read(tmp->pipe[0],tmp->ast_rd_buf,sizeof(tmp->ast_rd_buf));
	
	if (len<=0) {
		/* we hangup here, since our pipe is closed */
		chan_misdn_log(2,tmp->bc->port,"misdn_read: Pipe closed, hanging up\n");
		return NULL;
	}

	tmp->frame.frametype  = AST_FRAME_VOICE;
	tmp->frame.subclass = AST_FORMAT_ALAW;
	tmp->frame.datalen = len;
	tmp->frame.samples = len ;
	tmp->frame.mallocd =0 ;
	tmp->frame.offset= 0 ;
	tmp->frame.delivery= ast_tv(0,0) ;
	tmp->frame.src = NULL;
	tmp->frame.data = tmp->ast_rd_buf ;
	
	if (tmp->faxdetect || tmp->ast_dsp ) {
		return process_ast_dsp(tmp, &tmp->frame);
	}
	
	return &tmp->frame;
}


static int misdn_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct chan_list *ch;
	int i  = 0;
	
	if (!ast || ! (ch=MISDN_ASTERISK_TECH_PVT(ast)) ) return -1;

	if (ch->state == MISDN_HOLDED) {
		chan_misdn_log(8, 0, "misdn_write: Returning because holded\n");
		return 0;
	}
	
	if (!ch->bc ) {

		ast_log(LOG_WARNING, "private but no bc\n");
		return -1;
	}
	
	if (ch->notxtone) {
		chan_misdn_log(9, ch->bc->port, "misdn_write: Returning because notxone\n");
		return 0;
	}


	if ( !frame->subclass) {
		chan_misdn_log(4, ch->bc->port, "misdn_write: * prods us\n");
		return 0;
	}
	
	if ( !(frame->subclass & prefformat)) {
		
		chan_misdn_log(-1, ch->bc->port, "Got Unsupported Frame with Format:%d\n", frame->subclass);
		return 0;
	}
	

	if ( !frame->samples ) {
		chan_misdn_log(4, ch->bc->port, "misdn_write: zero write\n");

		if (!strcmp(frame->src,"ast_prod")) {
			chan_misdn_log(1, ch->bc->port, "misdn_write: state (%s) prodded.\n", misdn_get_ch_state(ch));

			if (ch->ts) {
				chan_misdn_log(4,ch->bc->port,"Starting Playtones\n");
				misdn_lib_tone_generator_start(ch->bc);
			}
			return 0;
		}

		return -1;
	}

	if ( ! ch->bc->addr ) {
		chan_misdn_log(8, ch->bc->port, "misdn_write: no addr for bc dropping:%d\n", frame->samples);
		return 0;
	}
	
#if MISDN_DEBUG
	{
		int i, max=5>frame->samples?frame->samples:5;
		
		printf("write2mISDN %p %d bytes: ", p, frame->samples);
		
		for (i=0; i<  max ; i++) printf("%2.2x ",((char*) frame->data)[i]);
		printf ("\n");
	}
#endif


	switch (ch->bc->bc_state) {
		case BCHAN_ACTIVATED:
		case BCHAN_BRIDGED:
			break;
		default:
		if (!ch->dropped_frame_cnt)
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) droping: %d frames addr:%x exten:%s cid:%s ch->state:%s bc_state:%d l3id:%x\n",frame->samples,ch->bc->addr, ast->exten, ast->cid.cid_num,misdn_get_ch_state( ch), ch->bc->bc_state, ch->bc->l3_id);
		
		ch->dropped_frame_cnt++;
		if (ch->dropped_frame_cnt > 100) {
			ch->dropped_frame_cnt=0;
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) droping: %d frames addr:%x  dropped > 100 frames!\n",frame->samples,ch->bc->addr);

		}

		return 0;
	}

	chan_misdn_log(9, ch->bc->port, "Sending :%d bytes 2 MISDN\n",frame->samples);
	
	if ( !ch->bc->nojitter && misdn_cap_is_speech(ch->bc->capability) ) {
		/* Buffered Transmit (triggert by read from isdn side)*/
		if (misdn_jb_fill(ch->jb,frame->data,frame->samples) < 0) {
			if (ch->bc->active)
				cb_log(0,ch->bc->port,"Misdn Jitterbuffer Overflow.\n");
		}
		
	} else {
		/*transmit without jitterbuffer*/
		i=misdn_lib_tx2misdn_frm(ch->bc, frame->data, frame->samples);
	}

	
	
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

	int p1_b, p2_b;

	misdn_cfg_get(ch1->bc->port, MISDN_CFG_BRIDGING, &p1_b, sizeof(int));
	misdn_cfg_get(ch2->bc->port, MISDN_CFG_BRIDGING, &p2_b, sizeof(int));
	
	if ( ! p1_b || ! p2_b) {
		ast_log(LOG_NOTICE, "Falling back to Asterisk bridging\n");
		return AST_BRIDGE_FAILED;
	}

	int bridging;
	misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
	if (bridging) {
		/* trying to make a mISDN_dsp conference */
		chan_misdn_log(1, ch1->bc->port, "I SEND: Making conference with Number:%d\n", ch1->bc->pid +1);
		misdn_lib_bridge(ch1->bc,ch2->bc);
	}
	
	chan_misdn_log(1, ch1->bc->port, "* Making Native Bridge between %s and %s\n", ch1->bc->oad, ch2->bc->oad);


	if (! (flags&AST_BRIDGE_DTMF_CHANNEL_0) )
		ch1->ignore_dtmf=1;
	
	if (! (flags&AST_BRIDGE_DTMF_CHANNEL_1) )
		ch2->ignore_dtmf=1;
	
	
	while(1) {
		to=-1;
		who = ast_waitfor_n(carr, 2, &to);

		if (!who) {
			ast_log(LOG_NOTICE,"misdn_bridge: empty read, breaking out\n");
			break;
		}
		f = ast_read(who);
    
		if (!f || f->frametype == AST_FRAME_CONTROL) {
			/* got hangup .. */

			if (!f) 
				chan_misdn_log(4,ch1->bc->port,"Read Null Frame\n");
			else
				chan_misdn_log(4,ch1->bc->port,"Read Frame Controll class:%d\n",f->subclass);
			
			*fo=f;
			*rc=who;
      
			break;
		}
		
		if ( f->frametype == AST_FRAME_DTMF ) {
			chan_misdn_log(1,0,"Read DTMF %d from %s\n",f->subclass, who->exten);

			*fo=f;
			*rc=who;
			break;
		}
	
#if 0
		if (f->frametype == AST_FRAME_VOICE) {
			chan_misdn_log(1, ch1->bc->port, "I SEND: Splitting conference with Number:%d\n", ch1->bc->pid +1);
	
			continue;
		}
#endif

		if (who == c0) {
			ast_write(c1,f);
		}
		else {
			ast_write(c0,f);
		}
    
	}
	
	chan_misdn_log(1, ch1->bc->port, "I SEND: Splitting conference with Number:%d\n", ch1->bc->pid +1);
	
	misdn_lib_split_bridge(ch1->bc,ch2->bc);
	
	
	return AST_BRIDGE_COMPLETE;
}

/** AST INDICATIONS END **/

static int dialtone_indicate(struct chan_list *cl)
{
	const struct tone_zone_sound *ts= NULL;
	struct ast_channel *ast=cl->ast;

	if (!ast) {
		chan_misdn_log(0,cl->bc->port,"No Ast in dialtone_indicate\n");
		return -1;
	}

	int nd=0;
	misdn_cfg_get( cl->bc->port, MISDN_CFG_NODIALTONE, &nd, sizeof(nd));

	if (nd) {
		chan_misdn_log(1,cl->bc->port,"Not sending Dialtone, because config wants it\n");
		return 0;
	}
	
	chan_misdn_log(3,cl->bc->port," --> Dial\n");
	ts=ast_get_indication_tone(ast->zone,"dial");
	cl->ts=ts;	
	
	if (ts) {
		cl->notxtone=0;
		cl->norxtone=0;
		/* This prods us in misdn_write */
		ast_playtones_start(ast,0, ts->data, 0);
	}

	return 0;
}

static int hanguptone_indicate(struct chan_list *cl)
{
	misdn_lib_send_tone(cl->bc,TONE_HANGUP);
	return 0;
}

static int stop_indicate(struct chan_list *cl)
{
	struct ast_channel *ast=cl->ast;

	if (!ast) {
		chan_misdn_log(0,cl->bc->port,"No Ast in stop_indicate\n");
		return -1;
	}

	chan_misdn_log(3,cl->bc->port," --> None\n");
	misdn_lib_tone_generator_stop(cl->bc);
	ast_playtones_stop(ast);

	cl->ts=NULL;
	/*ast_deactivate_generator(ast);*/
	
	return 0;
}


static int start_bc_tones(struct chan_list* cl)
{
	misdn_lib_tone_generator_stop(cl->bc);
	cl->notxtone=0;
	cl->norxtone=0;
	return 0;
}

static int stop_bc_tones(struct chan_list *cl)
{
	if (!cl) return -1;

	cl->notxtone=1;
	cl->norxtone=1;
	
	return 0;
}


static struct chan_list *init_chan_list(int orig)
{
	struct chan_list *cl=malloc(sizeof(struct chan_list));
	
	if (!cl) {
		chan_misdn_log(-1, 0, "misdn_request: malloc failed!");
		return NULL;
	}
	
	memset(cl,0,sizeof(struct chan_list));

	cl->orginator=orig;
	cl->need_queue_hangup=1;
	cl->need_hangup=1;
	cl->need_busy=1;
	cl->overlap_dial_task=-1;
	
	return cl;
	
}

static struct ast_channel *misdn_request(const char *type, int format, void *data, int *cause)

{
	struct ast_channel *tmp = NULL;
	char group[BUFFERSIZE+1]="";
	char buf[128];
	char buf2[128], *ext=NULL, *port_str;
	char *tokb=NULL, *p=NULL;
	int channel=0, port=0;
	struct misdn_bchannel *newbc = NULL;
	int dec=0;
	
	struct chan_list *cl=init_chan_list(ORG_AST);
	
	sprintf(buf,"%s/%s",misdn_type,(char*)data);
	ast_copy_string(buf2,data, 128);
	
	port_str=strtok_r(buf2,"/", &tokb);

	ext=strtok_r(NULL,"/", &tokb);

	if (port_str) {
		if (port_str[0]=='g' && port_str[1]==':' ) {
			/* We make a group call lets checkout which ports are in my group */
			port_str += 2;
			strncpy(group, port_str, BUFFERSIZE);
			group[127] = 0;
			chan_misdn_log(2, 0, " --> Group Call group: %s\n",group);
		} 
		else if ((p = strchr(port_str, ':'))) {
			/* we have a preselected channel */
			*p = 0;
			channel = atoi(++p);
			port = atoi(port_str);
			chan_misdn_log(2, port, " --> Call on preselected Channel (%d).\n", channel);
		}
		else {
			port = atoi(port_str);
		}
	} else {
		ast_log(LOG_WARNING, " --> ! IND : CALL dad:%s WITHOUT PORT/Group, check extension.conf\n",ext);
		return NULL;
	}

	if (misdn_cfg_is_group_method(group, METHOD_STANDARD_DEC)) {
		chan_misdn_log(4, port, " --> STARTING STANDARDDEC...\n");
		dec=1;
	}

	if (!ast_strlen_zero(group)) {
	
		char cfg_group[BUFFERSIZE+1];
		struct robin_list *rr = NULL;

		if (misdn_cfg_is_group_method(group, METHOD_ROUND_ROBIN)) {
			chan_misdn_log(4, port, " --> STARTING ROUND ROBIN...\n");
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
					
					if (port <= port_start && next_chan) {
						int maxbchans=misdn_lib_get_maxchans(port);
						if (++robin_channel >= maxbchans) {
							robin_channel = 1;
						}
						next_chan = 0;
					}

					misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);
					
					if (!strcasecmp(cfg_group, group)) {
						int port_up;
						int check;
						misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(int));
						port_up = misdn_lib_port_up(port, check);

						if (check && !port_up) 
							chan_misdn_log(1,port,"L1 is not Up on this Port\n");
						
						if (check && port_up<0) {
							ast_log(LOG_WARNING,"This port (%d) is blocked\n", port);
						}
						
						
						if ( port_up>0 )	{
							newbc = misdn_lib_get_free_bc(port, robin_channel,0, 0);
							if (newbc) {
								chan_misdn_log(4, port, " Success! Found port:%d channel:%d\n", newbc->port, newbc->channel);
								if (port_up)
									chan_misdn_log(4, port, "portup:%d\n",  port_up);
								rr->port = newbc->port;
								rr->channel = newbc->channel;
								break;
							}
						}
					}
				}
			} while (!newbc && robin_channel != rr->channel);
			
		} else {		
			for (port=misdn_cfg_get_next_port(0); port > 0;
				 port=misdn_cfg_get_next_port(port)) {
				
				misdn_cfg_get( port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);

				chan_misdn_log(3,port, "Group [%s] Port [%d]\n", group, port);
				if (!strcasecmp(cfg_group, group)) {
					int port_up;
					int check;
					misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(int));
					port_up = misdn_lib_port_up(port, check);
					
					chan_misdn_log(4, port, "portup:%d\n", port_up);
					
					if ( port_up>0 ) {
						newbc = misdn_lib_get_free_bc(port, 0, 0, dec);
						if (newbc)
							break;
					}
				}
			}
		}
		
		/* Group dial failed ?*/
		if (!newbc) {
			ast_log(LOG_WARNING, 
					"Could not Dial out on group '%s'.\n"
					"\tEither the L2 and L1 on all of these ports where DOWN (see 'show application misdn_check_l2l1')\n"
					"\tOr there was no free channel on none of the ports\n\n"
					, group);
			return NULL;
		}
	} else { /* 'Normal' Port dial * Port dial */
		if (channel)
			chan_misdn_log(1, port," --> preselected_channel: %d\n",channel);
		newbc = misdn_lib_get_free_bc(port, channel, 0, dec);

		if (!newbc) {
			ast_log(LOG_WARNING, "Could not create channel on port:%d with extensions:%s\n",port,ext);
			return NULL;
		}
	}
	

	/* create ast_channel and link all the objects together */
	cl->bc=newbc;
	
	tmp = misdn_new(cl, AST_STATE_RESERVED, ext, NULL, format, port, channel);
	if (!tmp) {
		ast_log(LOG_ERROR,"Could not create Asterisk object\n");
		return NULL;
	}

	cl->ast=tmp;
	
	/* register chan in local list */
	cl_queue_chan(&cl_te, cl) ;
	
	/* fill in the config into the objects */
	read_config(cl, ORG_AST);

	/* important */
	cl->need_hangup=0;
	
	return tmp;
}


int misdn_send_text (struct ast_channel *chan, const char *text)
{
	struct chan_list *tmp=chan->tech_pvt;
	
	if (tmp && tmp->bc) {
		ast_copy_string(tmp->bc->display,text,sizeof(tmp->bc->display));
		misdn_lib_send_event(tmp->bc, EVENT_INFORMATION);
	} else {
		ast_log(LOG_WARNING, "No chan_list but send_text request?\n");
		return -1;
	}
	
	return 0;
}

static struct ast_channel_tech misdn_tech = {
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
	.send_text=misdn_send_text,
	.properties=0
};

static struct ast_channel_tech misdn_tech_wo_bridge = {
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
	.send_text=misdn_send_text,
	.properties=0
};


static unsigned long glob_channel=0;

static struct ast_channel *misdn_new(struct chan_list *chlist, int state,  char *exten, char *callerid, int format, int port, int c)
{
	struct ast_channel *tmp;
	
	tmp = ast_channel_alloc(1);
	
	if (tmp) {
		chan_misdn_log(2, 0, " --> * NEW CHANNEL dad:%s oad:%s\n",exten,callerid);
		
		
		if (c<=0) {
			c=glob_channel++;
			snprintf(tmp->name, sizeof(tmp->name), "%s/%d-u%d",
				 misdn_type, port, c);
		} else {
			snprintf(tmp->name, sizeof(tmp->name), "%s/%d-%d",
				 misdn_type, port, c);
		}
		
		tmp->type = misdn_type;
		
		tmp->nativeformats = prefformat;

		tmp->readformat = format;
		tmp->rawreadformat = format;
		tmp->writeformat = format;
		tmp->rawwriteformat = format;
    
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
		
		if (exten) 
			ast_copy_string(tmp->exten, exten,  sizeof(tmp->exten));
		else
			chan_misdn_log(1,0,"misdn_new: no exten given.\n");
		
		if (callerid) {
			char *cid_name, *cid_num;
      
			ast_callerid_parse(callerid, &cid_name, &cid_num);

			if (!ast_strlen_zero(cid_num))
				tmp->cid.cid_num = strdup(cid_num);
			if (!ast_strlen_zero(cid_name))
				tmp->cid.cid_name = strdup(cid_name);
		}

		{
			if (pipe(chlist->pipe)<0)
				perror("Pipe failed\n");
			
			tmp->fds[0]=chlist->pipe[0];
			
		}
		
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		else
			tmp->rings = 0;
		
		
	} else {
		chan_misdn_log(-1,0,"Unable to allocate channel structure\n");
	}
	
	return tmp;
}


static struct ast_frame *process_ast_dsp(struct chan_list *tmp, struct ast_frame *frame)
{
	struct ast_frame *f,*f2;
	if (tmp->trans)
		f2=ast_translate(tmp->trans, frame,0);
	else {
		chan_misdn_log(0, tmp->bc->port, "No T-Path found\n");
		return NULL;
	}
	
	f = ast_dsp_process(tmp->ast, tmp->dsp, f2);
	if (f && (f->frametype == AST_FRAME_DTMF)) {
		ast_log(LOG_DEBUG, "Detected inband DTMF digit: %c\n", f->subclass);
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
			
		}  else if ( tmp->ast_dsp) {
			chan_misdn_log(2, tmp->bc->port, " --> * SEND: DTMF (AST_DSP) :%c\n",f->subclass);
			return f;
		}
	}

	frame->frametype = AST_FRAME_NULL;
	frame->subclass = 0;
	return frame;
}


static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help=list;
	for (;help; help=help->next) {
		if (help->bc == bc) return help;
	}
  
	chan_misdn_log(6, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n",bc->oad,bc->dad);
  
	return NULL;
}

static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid)
{
	struct chan_list *help=list;
	for (;help; help=help->next) {
		if ( help->bc && (help->bc->pid == pid) ) return help;
	}
  
	chan_misdn_log(6, 0, "$$$ find_chan: No channel found for pid:%d\n",pid);
  
	return NULL;
}

static struct chan_list *find_holded(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help=list;

	if (bc->pri) return NULL;
	
	chan_misdn_log(6, bc->port, "$$$ find_holded: channel:%d oad:%s dad:%s\n",bc->channel, bc->oad,bc->dad);
	for (;help; help=help->next) {
		chan_misdn_log(4, bc->port, "$$$ find_holded: --> holded:%d channel:%d\n",help->state==MISDN_HOLDED, help->hold_info.channel);
		if (help->hold_info.port == bc->port
		) return help;
	}
	
	chan_misdn_log(6, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n",bc->oad,bc->dad);
  
	return NULL;
}


static struct chan_list *find_holded_l3(struct chan_list *list, unsigned long l3_id, int w) 

{
	struct chan_list *help=list;

	for (;help; help=help->next) {
		if ( (help->state == MISDN_HOLDED) &&
			 (help->l3id == l3_id)   
			) 
			return help;
	}

	return NULL;
}

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan)
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

static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan) 
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


static int pbx_start_chan(struct chan_list *ch)
{
	int ret=ast_pbx_start(ch->ast);	

	if (ret>=0) 
		ch->need_hangup=0;
	else
		ch->need_hangup=1;

	return ret;
}

static void hangup_chan(struct chan_list *ch)
{
	int port=ch?ch->bc?ch->bc->port:0:0;
	if (!ch) {
		cb_log(1,0,"Cannot hangup chan, no ch\n");
		return;
	}

	cb_log(5,port,"hangup_chan called\n");

	if (ch->need_hangup) 
	{
		cb_log(2,port," --> hangup\n");
		send_cause2ast(ch->ast,ch->bc,ch);
		ch->need_hangup=0;
		ch->need_queue_hangup=0;
		if (ch->ast)
			ast_hangup(ch->ast);
		return;
	}

	if (!ch->need_queue_hangup) {
		cb_log(2,port," --> No need to queue hangup\n");
	}

	ch->need_queue_hangup=0;
	if (ch->ast) {
		send_cause2ast(ch->ast,ch->bc,ch);

		if (ch->ast)
			ast_queue_hangup(ch->ast);
		cb_log(2,port," --> queue_hangup\n");
	} else {
		cb_log(1,port,"Cannot hangup chan, no ast\n");
	}
}

/** Isdn asks us to release channel, pendant to misdn_hangup **/
static void release_chan(struct misdn_bchannel *bc) {
	struct ast_channel *ast=NULL;

	ast_mutex_lock(&release_lock);
	{
		struct chan_list *ch=find_chan_by_bc(cl_te, bc);
		if (!ch)  {
			chan_misdn_log(1, bc->port, "release_chan: Ch not found!\n");
			ast_mutex_unlock(&release_lock);
			return;
		}
		
		if (ch->ast) {
			ast=ch->ast;
		} 
		
		chan_misdn_log(5, bc->port, "release_chan: bc with l3id: %x\n",bc->l3_id);
		
		/*releaseing jitterbuffer*/
		if (ch->jb ) {
			misdn_jb_destroy(ch->jb);
			ch->jb=NULL;
		} else {
			if (!bc->nojitter)
				chan_misdn_log(5,bc->port,"Jitterbuffer already destroyed.\n");
		}
		
		if (ch->overlap_dial) {
			if (ch->overlap_dial_task != -1) {
				misdn_tasks_remove(ch->overlap_dial_task);
				ch->overlap_dial_task = -1;
			}
			ast_mutex_destroy(&ch->overlap_tv_lock);
		}
		if (ch) {
			
			close(ch->pipe[0]);
			close(ch->pipe[1]);

			
			if (ast && MISDN_ASTERISK_TECH_PVT(ast)) {
				chan_misdn_log(1, bc->port, "* RELEASING CHANNEL pid:%d ctx:%s dad:%s oad:%s state: %s\n",bc?bc->pid:-1, ast->context, ast->exten,AST_CID_P(ast),misdn_get_ch_state(ch));
				chan_misdn_log(3, bc->port, " --> * State Down\n");
				MISDN_ASTERISK_TECH_PVT(ast)=NULL;
				
      
				if (ast->_state != AST_STATE_RESERVED) {
					chan_misdn_log(3, bc->port, " --> Setting AST State to down\n");
					ast_setstate(ast, AST_STATE_DOWN);
				}
			}
				
			ch->state=MISDN_CLEANING;
			cl_dequeue_chan(&cl_te, ch);
			
			free(ch);
		} else {
			/* chan is already cleaned, so exiting  */
		}
	}

	ast_mutex_unlock(&release_lock);
}
/*** release end **/

static void misdn_transfer_bc(struct chan_list *tmp_ch, struct chan_list *holded_chan)
{
	chan_misdn_log(4,0,"TRANSFERING %s to %s\n",holded_chan->ast->name, tmp_ch->ast->name);
	
	tmp_ch->state=MISDN_HOLD_DISCONNECT;
  
	ast_moh_stop(AST_BRIDGED_P(holded_chan->ast));

	holded_chan->state=MISDN_CONNECTED;
	//misdn_lib_transfer(holded_chan->bc);
	ast_channel_masquerade(holded_chan->ast, AST_BRIDGED_P(tmp_ch->ast));
}


static void do_immediate_setup(struct misdn_bchannel *bc,struct chan_list *ch , struct ast_channel *ast)
{
	char predial[256]="";
	char *p = predial;
  
	struct ast_frame fr;
  
	strncpy(predial, ast->exten, sizeof(predial) -1 );
  
	ch->state=MISDN_DIALING;

	if (!ch->noautorespond_on_setup) {
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
	} else {
		ch->state = MISDN_INCOMING_SETUP;
	}

	chan_misdn_log(1, bc->port, "* Starting Ast ctx:%s dad:%s oad:%s with 's' extension\n", ast->context, ast->exten, AST_CID_P(ast));
  
	strncpy(ast->exten,"s", 2);
  
	if (pbx_start_chan(ch)<0) {
		ast=NULL;
		hangup_chan(ch);
		hanguptone_indicate(ch);

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
		fr.delivery= ast_tv(0,0) ;

		if (ch->ast && MISDN_ASTERISK_PVT(ch->ast) && MISDN_ASTERISK_TECH_PVT(ch->ast)) {
			ast_queue_frame(ch->ast, &fr);
		}
		p++;
	}
}



static void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel*bc, struct chan_list *ch) {
	if (!ast) {
		chan_misdn_log(1,0,"send_cause2ast: No Ast\n");
		return;
	}
	if (!bc) {
		chan_misdn_log(1,0,"send_cause2ast: No BC\n");
		return;
	}
	if (!ch) {
		chan_misdn_log(1,0,"send_cause2ast: No Ch\n");
		return;
	}
	
	ast->hangupcause=bc->cause;
	
	switch ( bc->cause) {
		
	case 1: /** Congestion Cases **/
	case 2:
	case 3:
 	case 4:
 	case 22:
 	case 27:
		/*
		 * Not Queueing the Congestion anymore, since we want to hear
		 * the inband message
		 *
		chan_misdn_log(1, bc?bc->port:0, " --> * SEND: Queue Congestion pid:%d\n", bc?bc->pid:-1);
		ch->state=MISDN_BUSY;
		
		ast_queue_control(ast, AST_CONTROL_CONGESTION);
		*/
		break;
		
	case 21:
	case 17: /* user busy */
	
		ch->state=MISDN_BUSY;
			
		if (!ch->need_busy) {
			chan_misdn_log(1,bc?bc->port:0, "Queued busy already\n");
			break;
		}
		
		chan_misdn_log(1,  bc?bc->port:0, " --> * SEND: Queue Busy pid:%d\n", bc?bc->pid:-1);
		
		ast_queue_control(ast, AST_CONTROL_BUSY);
		
		ch->need_busy=0;
		
		break;
	}
}

void import_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch)
{
	char *tmp;
	tmp=pbx_builtin_getvar_helper(chan,"MISDN_PID");
	if (tmp) {
		ch->other_pid=atoi(tmp);
		chan_misdn_log(3,bc->port," --> IMPORT_PID: importing pid:%s\n",tmp);

		if (ch->other_pid >0) {
			ch->other_ch=find_chan_by_pid(cl_te,ch->other_pid);
			if (ch->other_ch) ch->other_ch->other_ch=ch;
		}
	}

	tmp=pbx_builtin_getvar_helper(chan,"MISDN_ADDRESS_COMPLETE");
	if (tmp && (atoi(tmp) == 1)) {
		bc->sending_complete=1;
	}
	
	const char *kp=pbx_builtin_getvar_helper(chan,"MISDN_KEYPAD");
	if (kp) {
		strncpy(bc->keypad,kp,sizeof(bc->keypad));
		bc->keypad[sizeof(bc->keypad)-1]=0;
	}
	
	
}
 
void export_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch)
{
	char tmp[32];

	chan_misdn_log(3,bc->port," --> EXPORT_PID: pid:%d\n",bc->pid);
	sprintf(tmp,"%d",bc->pid);
	pbx_builtin_setvar_helper(chan,"_MISDN_PID",tmp);

	if (bc->sending_complete) {
		sprintf(tmp,"%d",bc->sending_complete);
		pbx_builtin_setvar_helper(chan,"MISDN_ADDRESS_COMPLETE",tmp);
	}

	if (bc->urate) {
		sprintf(tmp,"%d",bc->urate);
		pbx_builtin_setvar_helper(chan,"MISDN_URATE",tmp);
	}

	if (bc->keypad[0]) 
		pbx_builtin_setvar_helper(chan,"MISDN_KEYPAD",bc->keypad);
}


static void start_pbx(struct chan_list *ch, struct misdn_bchannel *bc, struct ast_channel *chan) {
	if (pbx_start_chan(ch)<0) {
		hangup_chan(ch);
		chan_misdn_log(-1, bc->port, "ast_pbx_start returned <0 in SETUP\n");
		if (bc->nt) {
			hanguptone_indicate(ch);
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
		} else
			misdn_lib_send_event(bc, EVENT_RELEASE);
	}
}

static void wait_for_digits(struct chan_list *ch, struct misdn_bchannel *bc, struct ast_channel *chan) {
	ch->state=MISDN_WAITING4DIGS;
	misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
	if (bc->nt && !bc->dad[0])
		dialtone_indicate(ch);
}



/************************************************************/
/*  Receive Events from isdn_lib  here                     */
/************************************************************/
static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data)
{
	struct chan_list *ch=find_chan_by_bc(cl_te, bc);
	
	if (event != EVENT_BCHAN_DATA && event != EVENT_TONE_GENERATE) { /*  Debug Only Non-Bchan */
		int debuglevel=1;
		if ( event==EVENT_CLEANUP && !user_data)
			debuglevel=5;

		chan_misdn_log(debuglevel, bc->port, "I IND :%s oad:%s dad:%s pid:%d state:%s\n", manager_isdn_get_info(event), bc->oad, bc->dad, bc->pid, ch?misdn_get_ch_state(ch):"none");
		if (debuglevel==1) {
			misdn_lib_log_ies(bc);
			chan_misdn_log(4,bc->port," --> bc_state:%s\n",bc_state2str(bc->bc_state));
		}
	}
	
	if (!ch) {
		switch(event) {
			case EVENT_SETUP:
			case EVENT_DISCONNECT:
			case EVENT_PORT_ALARM:
			case EVENT_RETRIEVE:
			case EVENT_NEW_BC:
				break;
			case EVENT_RELEASE_COMPLETE:
				chan_misdn_log(1, bc->port, " --> no Ch, so we've already released.\n");
				break;
			case EVENT_CLEANUP:
			case EVENT_TONE_GENERATE:
			case EVENT_BCHAN_DATA:
				return -1;

			default:
				chan_misdn_log(1,bc->port, "Chan not existing at the moment bc->l3id:%x bc:%p event:%s port:%d channel:%d\n",bc->l3_id, bc, manager_isdn_get_info( event), bc->port,bc->channel);
				return -1;
		}
	}
	
	if (ch ) {
		switch (event) {
		case EVENT_TONE_GENERATE:
		break;
		case EVENT_DISCONNECT:
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
		case EVENT_CLEANUP:
		case EVENT_TIMEOUT:
			if (!ch->ast)
				chan_misdn_log(3,bc->port,"ast_hangup already called, so we have no ast ptr anymore in event(%s)\n",manager_isdn_get_info(event));
			break;
		default:
			if ( !ch->ast  || !MISDN_ASTERISK_PVT(ch->ast) || !MISDN_ASTERISK_TECH_PVT(ch->ast)) {
				if (event!=EVENT_BCHAN_DATA)
					ast_log(LOG_NOTICE, "No Ast or No private Pointer in Event (%d:%s)\n", event, manager_isdn_get_info(event));
				return -1;
			}
		}
	}
	
	
	switch (event) {
	case EVENT_PORT_ALARM:
		{
			int boa=0;

			misdn_cfg_get( bc->port, MISDN_CFG_ALARM_BLOCK, &boa, sizeof(int));
			if (boa) {
				cb_log(1,bc->port," --> blocking\n");
				misdn_lib_port_block(bc->port);	
			}
		}
		break;

	case EVENT_BCHAN_ACTIVATED:
		break;
		
	case EVENT_NEW_L3ID:
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		break;

	case EVENT_NEW_BC:
		if (!ch) {
			ch=find_holded(cl_te,bc);
		}
		
		if (!ch) {
			ast_log(LOG_WARNING,"NEW_BC without chan_list?\n");
			break;
		}

		if (bc)
			ch->bc=(struct misdn_bchannel*)user_data;
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
		fr.delivery= ast_tv(0,0) ;
		
		if (!ch->ignore_dtmf) {
			chan_misdn_log(2, bc->port, " --> DTMF:%c\n", bc->dtmf);
			ast_queue_frame(ch->ast, &fr);
		} else {
			chan_misdn_log(2, bc->port, " --> Ingoring DTMF:%c due to bridge flags\n", bc->dtmf);
		}
	}
	break;
	case EVENT_STATUS:
		break;
    
	case EVENT_INFORMATION:
	{
		int l;

		if ( ch->state != MISDN_CONNECTED ) 
			stop_indicate(ch);
	
		if (!ch->ast) break;

		if (ch->state == MISDN_WAITING4DIGS ) {
			/*  Ok, incomplete Setup, waiting till extension exists */
			if (ast_strlen_zero(bc->info_dad) && ! ast_strlen_zero(bc->keypad)) {
				chan_misdn_log(1, bc->port, " --> using keypad as info\n");
				strcpy(bc->info_dad,bc->keypad);
			}

			l = sizeof(bc->dad);
			strncat(bc->dad,bc->info_dad, l);
			bc->dad[l-1] = 0;

			l = sizeof(ch->ast->exten);
			strncpy(ch->ast->exten, bc->dad, l);
			ch->ast->exten[l-1] = 0;

			/* Check for Pickup Request first */
			if (!strcmp(ch->ast->exten, ast_pickup_ext())) {
				if (ast_pickup_call(ch->ast)) {
					hangup_chan(ch);
				} else {
					struct ast_channel *chan=ch->ast;
					ch->state = MISDN_CALLING_ACKNOWLEDGE;
					ast_setstate(chan, AST_STATE_DOWN);
					hangup_chan(ch);
					ch->ast=NULL;
					break;
				}
			}
			
			if(!ast_canmatch_extension(ch->ast, ch->context, bc->dad, 1, bc->oad)) {
				if (ast_exists_extension(ch->ast, ch->context, "i", 1, bc->oad)) {
					ast_log(LOG_WARNING, "Extension can never match, So jumping to 'i' extension. port(%d)\n",bc->port);
					strcpy(ch->ast->exten, "i");

					ch->state = MISDN_DIALING;
					start_pbx(ch, bc, ch->ast);
					break;
				}

				ast_log(LOG_WARNING, "Extension can never match, so disconnecting on port(%d)."
						"maybe you want to add an 'i' extension to catch this case.\n",
						bc->port);

				if (bc->nt)
					hanguptone_indicate(ch);
				ch->state=MISDN_EXTCANTMATCH;
				bc->out_cause=1;

				misdn_lib_send_event(bc, EVENT_DISCONNECT );
				break;
			}

			if (ch->overlap_dial) {
				ast_mutex_lock(&ch->overlap_tv_lock);
				ch->overlap_tv = ast_tvnow();
				ast_mutex_unlock(&ch->overlap_tv_lock);
				if (ch->overlap_dial_task == -1) {
					ch->overlap_dial_task = 
						misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);
				}
				break;
			}

			if (ast_exists_extension(ch->ast, ch->context, bc->dad, 1, bc->oad))  {
				
				ch->state = MISDN_DIALING;
				start_pbx(ch, bc, ch->ast);
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
			fr.delivery= ast_tv(0,0) ;

			
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
		if (ch) {
			switch (ch->state) {
				case MISDN_NOTHING:
				ch=NULL;
				break;
				default:
				chan_misdn_log(1, bc->port, " --> Ignoring Call we have already one\n");
				return RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE; /*  Ignore MSNs which are not in our List */
			}
		}
	}

	{
		int msn_valid = misdn_cfg_is_msn_valid(bc->port, bc->dad);
		if (!bc->nt && ! msn_valid) {
			chan_misdn_log(1, bc->port, " --> Ignoring Call, its not in our MSN List\n");
			return RESPONSE_IGNORE_SETUP; /*  Ignore MSNs which are not in our List */
		}

		if (bc->cw) {
			chan_misdn_log(0, bc->port, " --> Call Waiting on PMP sending RELEASE_COMPLETE\n");
			int cause;
			misdn_cfg_get( bc->port, MISDN_CFG_REJECT_CAUSE, &cause, sizeof(cause));
			bc->out_cause=cause?cause:16;
			return RESPONSE_RELEASE_SETUP;
		}

		print_bearer(bc);

		struct chan_list *ch=init_chan_list(ORG_MISDN);
		struct ast_channel *chan;

		if (!ch) { chan_misdn_log(-1, bc->port, "cb_events: malloc for chan_list failed!\n"); return 0;}

		ch->bc = bc;
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		ch->orginator = ORG_MISDN;

		chan=misdn_new(ch, AST_STATE_RESERVED,bc->dad, bc->oad, AST_FORMAT_ALAW, bc->port, bc->channel);

		if (!chan) {
			misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			ast_log(LOG_ERROR, "cb_events: misdn_new failed !\n"); 
			return 0;
		}

		ch->ast = chan;

		read_config(ch, ORG_MISDN);

		export_ch(chan, bc, ch);

		ch->ast->rings=1;
		ast_setstate(ch->ast, AST_STATE_RINGING);

		int pres,screen;

		switch (bc->pres) {
			case 1:
				pres=AST_PRES_RESTRICTED; chan_misdn_log(2,bc->port," --> PRES: Restricted (1)\n");
				break;
			case 2:
				pres=AST_PRES_UNAVAILABLE; chan_misdn_log(2,bc->port," --> PRES: Restricted (2)\n");
				break;
			default:
				pres=AST_PRES_ALLOWED; chan_misdn_log(2,bc->port," --> PRES: Restricted (%d)\n", bc->pres);
		}

		switch (bc->screen) {
			case 0:
				screen=AST_PRES_USER_NUMBER_UNSCREENED;  chan_misdn_log(2,bc->port," --> SCREEN: Unscreened (0)\n");
				break;
			case 1:
				screen=AST_PRES_USER_NUMBER_PASSED_SCREEN; chan_misdn_log(2,bc->port," --> SCREEN: Passed screen (1)\n");
				break;
			case 2:
				screen=AST_PRES_USER_NUMBER_FAILED_SCREEN; chan_misdn_log(2,bc->port," --> SCREEN: failed screen (2)\n");
				break;
			case 3:
				screen=AST_PRES_NETWORK_NUMBER; chan_misdn_log(2,bc->port," --> SCREEN: Network Number (3)\n");
				break;
			default:
				screen=AST_PRES_USER_NUMBER_UNSCREENED; chan_misdn_log(2,bc->port," --> SCREEN: Unscreened (%d)\n",bc->screen);
		}

		chan->cid.cid_pres=pres+screen;

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

		if (!strstr(ch->allowed_bearers,"all")) {
			int i;
			for (i=0; i< sizeof(allowed_bearers_array)/sizeof(struct allowed_bearers); i++) {
				if (allowed_bearers_array[i].cap == bc->capability) {
					if (  !strstr( ch->allowed_bearers, allowed_bearers_array[i].name)) {
						chan_misdn_log(0,bc->port,"Bearer Not allowed\b");
						bc->out_cause=88;

						ch->state=MISDN_EXTCANTMATCH;
						misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
						return RESPONSE_OK;
					}
				}

			}
		}

		/* Check for Pickup Request first */
		if (!strcmp(chan->exten, ast_pickup_ext())) {
			if (!ch->noautorespond_on_setup) {
				int ret;/** Sending SETUP_ACK**/
				ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			} else {
				ch->state = MISDN_INCOMING_SETUP;
			}
			if (ast_pickup_call(chan)) {
				hangup_chan(ch);
			} else {
				ch->state = MISDN_CALLING_ACKNOWLEDGE;
				ast_setstate(chan, AST_STATE_DOWN);
				hangup_chan(ch);
				ch->ast=NULL;
				break;
			}
		}

		/*
		   added support for s extension hope it will help those poor cretains
		   which haven't overlap dial.
		   */
		int ai;
		misdn_cfg_get( bc->port, MISDN_CFG_ALWAYS_IMMEDIATE, &ai, sizeof(ai));
		if ( ai ) {
			do_immediate_setup(bc, ch , chan);
			break;
		}

		/* check if we should jump into s when we have no dad */
		int im;
		misdn_cfg_get( bc->port, MISDN_CFG_IMMEDIATE, &im, sizeof(im));
		if ( im && ast_strlen_zero(bc->dad) ) {
			do_immediate_setup(bc, ch , chan);
			break;
		}

		chan_misdn_log(5,bc->port,"CONTEXT:%s\n",ch->context);
		if(!ast_canmatch_extension(ch->ast, ch->context, bc->dad, 1, bc->oad)) {
			if (ast_exists_extension(ch->ast, ch->context, "i", 1, bc->oad)) {
				ast_log(LOG_WARNING, "Extension can never match, So jumping to 'i' extension. port(%d)\n",bc->port);
				strcpy(ch->ast->exten, "i");
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
				ch->state=MISDN_DIALING;
				start_pbx(ch, bc, chan);
				break;
			}

			ast_log(LOG_WARNING, "Extension can never match, so disconnecting on port(%d)."
					"maybe you want to add an 'i' extension to catch this case.\n",
					bc->port);
			if (bc->nt)
				hanguptone_indicate(ch);

			ch->state=MISDN_EXTCANTMATCH;
			bc->out_cause=1;

			if (bc->nt)
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
			else
				misdn_lib_send_event(bc, EVENT_RELEASE );

			break;
		}

		/* Whatever happens, when sending_complete is set or we are PTMP TE, we will definitely 
		 * jump into the dialplan, when the dialed extension does not exist, the 's' extension 
		 * will be used by Asterisk automatically. */
		if (bc->sending_complete || (!bc->nt && !misdn_lib_is_ptp(bc->port))) {
			if (!ch->noautorespond_on_setup) {
				ch->state=MISDN_DIALING;
				misdn_lib_send_event(bc, EVENT_PROCEEDING );
			} else {
				ch->state = MISDN_INCOMING_SETUP;
			}
			start_pbx(ch, bc, chan);
			break;
		}

		/* 
		 * If overlapdial we will definitely send a SETUP_ACKNOWLEDGE and wait for more 
		 * Infos with a Interdigit Timeout.
		 * */
		if (ch->overlap_dial) {
			ast_mutex_lock(&ch->overlap_tv_lock);
			ch->overlap_tv = ast_tvnow();
			ast_mutex_unlock(&ch->overlap_tv_lock);

			if (ch->overlap_dial_task == -1) 
				ch->overlap_dial_task = 
					misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);

			wait_for_digits(ch, bc, chan);
			break;
		}

		/* If the extension does not exist and we're not TE_PTMP we wait for more digis 
		 * without interdigit timeout.
		 * */
		if (!ast_exists_extension(ch->ast, ch->context, bc->dad, 1, bc->oad))  {
			wait_for_digits(ch, bc, chan);
			break;
		}

		/*
		 * If the extension exists let's just jump into it.
		 * */
		if (ast_exists_extension(ch->ast, ch->context, bc->dad, 1, bc->oad)) {
			if (bc->need_more_infos)
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			else
				misdn_lib_send_event(bc, EVENT_PROCEEDING);

			ch->state=MISDN_DIALING;
			start_pbx(ch, bc, chan);
			break;
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
		
			if (!ch->ast) break;
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

		ch->state = MISDN_PROCEEDING;
		
		if (!ch->ast) break;

		ast_queue_control(ch->ast, AST_CONTROL_PROCEEDING);
	}
	break;
	case EVENT_PROGRESS:


		if (!bc->nt ) {
			if ( misdn_cap_is_speech(bc->capability) &&
			     misdn_inband_avail(bc)
				) {
				start_bc_tones(ch);
			}
			
			ch->state=MISDN_PROGRESS;

			if (!ch->ast) break;
			ast_queue_control(ch->ast, AST_CONTROL_PROGRESS);
		}
		break;
		
		
	case EVENT_ALERTING:
	{
		ch->state = MISDN_ALERTING;
		
		if (!ch->ast) break;

		ast_queue_control(ch->ast, AST_CONTROL_RINGING);
		ast_setstate(ch->ast, AST_STATE_RINGING);
		
		cb_log(7,bc->port," --> Set State Ringing\n");
		
		if ( misdn_cap_is_speech(bc->capability) && misdn_inband_avail(bc)) {
			cb_log(1,bc->port,"Starting Tones, we have inband Data\n");
			start_bc_tones(ch);
		} else {
			cb_log(3,bc->port," --> We have no inband Data, the other end must create ringing\n");
			if (ch->far_alerting) {
				cb_log(1,bc->port," --> The other end can not do ringing eh ?.. we must do all ourself..");
				start_bc_tones(ch);
				/*tone_indicate(ch, TONE_FAR_ALERTING);*/
			}
		}
	}
	break;
	case EVENT_CONNECT:
	{
		/*we answer when we've got our very new L3 ID from the NT stack */
		misdn_lib_send_event(bc,EVENT_CONNECT_ACKNOWLEDGE);

		if (!ch->ast) break;

		struct ast_channel *bridged=AST_BRIDGED_P(ch->ast);
		stop_indicate(ch);

		if (bridged && !strcasecmp(bridged->tech->type,"mISDN")) {
			struct chan_list *bridged_ch=MISDN_ASTERISK_TECH_PVT(bridged);

			chan_misdn_log(1,bc->port," --> copying cpndialplan:%d and cad:%s to the A-Channel\n",bc->cpnnumplan,bc->cad);
			if (bridged_ch) {
				bridged_ch->bc->cpnnumplan=bc->cpnnumplan;
				ast_copy_string(bridged_ch->bc->cad,bc->cad,sizeof(bc->cad));
			}
		}
	}
	
	/* notice that we don't break here!*/
	case EVENT_CONNECT_ACKNOWLEDGE:
	{
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		
		start_bc_tones(ch);
		
		ch->state = MISDN_CONNECTED;
		
		if (!ch->ast) break;

		ast_queue_control(ch->ast, AST_CONTROL_ANSWER);
	}
	break;
	case EVENT_DISCONNECT:
	/*we might not have an ch->ast ptr here anymore*/
	if (ch) {
		struct chan_list *holded_ch=find_holded(cl_te, bc);
		
		chan_misdn_log(3,bc->port," --> org:%d nt:%d, inbandavail:%d state:%d\n", ch->orginator, bc->nt, misdn_inband_avail(bc), ch->state);
		if ( ch->orginator==ORG_AST && !bc->nt && misdn_inband_avail(bc) && ch->state != MISDN_CONNECTED) {
			/* If there's inband information available (e.g. a
			   recorded message saying what was wrong with the
			   dialled number, or perhaps even giving an
			   alternative number, then play it instead of
			   immediately releasing the call */
			chan_misdn_log(1,bc->port, " --> Inband Info Avail, not sending RELEASE\n");
		
			ch->state=MISDN_DISCONNECTED;
			start_bc_tones(ch);

			if (ch->ast) {
				ch->ast->hangupcause=bc->cause;
				if (bc->cause == 17)
					ast_queue_control(ch->ast, AST_CONTROL_BUSY);
			}
			ch->need_busy=0;
			break;
		}
		
		/*Check for holded channel, to implement transfer*/
		if (	holded_ch && 
			holded_ch != ch && 
			ch->ast && 
			ch->state == MISDN_CONNECTED	) {
			cb_log(1,bc->port," --> found holded ch\n");
			misdn_transfer_bc(ch, holded_ch) ;
		}

		bc->need_disconnect=0;
		
		stop_bc_tones(ch);
		hangup_chan(ch);
	} else {
		ch=find_holded_l3(cl_te, bc->l3_id,1);
		if (ch) {
			hangup_chan(ch);
		}
	}
	bc->out_cause=-1;
	if (bc->need_release) misdn_lib_send_event(bc,EVENT_RELEASE);
	break;
	
	case EVENT_RELEASE:
		{
			bc->need_disconnect=0;
			bc->need_release=0;

			hangup_chan(ch);
			release_chan(bc);
		
			if (bc->need_release_complete) 
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
		}
		break;
	case EVENT_RELEASE_COMPLETE:
	{
		bc->need_disconnect=0;
		bc->need_release=0;
		bc->need_release_complete=0;

		stop_bc_tones(ch);
		hangup_chan(ch);

		if(ch)	
			ch->state=MISDN_CLEANING;

		release_chan(bc);
	}
	break;
	case EVENT_BCHAN_ERROR:
	case EVENT_CLEANUP:
	{
		stop_bc_tones(ch);
		
		switch(ch->state) {
			case MISDN_CALLING:
				bc->cause=27; /* Destination out of order */
			break;
			default:
			break;
		}
		
		hangup_chan(ch);
		release_chan(bc);
	}
	break;

	case EVENT_TONE_GENERATE:
	{
		int tone_len=bc->tone_cnt;
		struct ast_channel *ast=ch->ast;
		void *tmp;
		int res;
		int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples);

		chan_misdn_log(9,bc->port,"TONE_GEN: len:%d\n");

		if (!ast) break;

		if (!ast->generator) break;
	
		
	
		tmp = ast->generatordata;
		ast->generatordata = NULL;
		generate = ast->generator->generate;

		if (tone_len <0 || tone_len > 512 ) {
			ast_log(LOG_NOTICE, "TONE_GEN: len was %d, set to 128\n",tone_len);
			tone_len=128;
		}

		res = generate(ast, tmp, tone_len, tone_len);
		ast->generatordata = tmp;
		
		if (res) {
			ast_log(LOG_WARNING, "Auto-deactivating generator\n");
			ast_deactivate_generator(ast);
		} else {
			bc->tone_cnt=0;
		}
	}
	break;
		
	case EVENT_BCHAN_DATA:
	{
		if ( !misdn_cap_is_speech(ch->bc->capability) ) {
			struct ast_frame frame;
			/*In Data Modes we queue frames*/
			frame.frametype  = AST_FRAME_VOICE; /*we have no data frames yet*/
			frame.subclass = AST_FORMAT_ALAW;
			frame.datalen = bc->bframe_len;
			frame.samples = bc->bframe_len ;
			frame.mallocd =0 ;
			frame.offset= 0 ;
			frame.delivery= ast_tv(0,0) ;
			frame.src = NULL;
			frame.data = bc->bframe ;
			
			if (ch->ast) 
				ast_queue_frame(ch->ast,&frame);
		} else {
			fd_set wrfs;
			struct timeval tv;
			tv.tv_sec=0;
			tv.tv_usec=0;
			
			
			FD_ZERO(&wrfs);
			FD_SET(ch->pipe[1],&wrfs);
			
			int t=select(FD_SETSIZE,NULL,&wrfs,NULL,&tv);

			if (!t) {
				chan_misdn_log(9, bc->port, "Select Timed out\n");
				break;
			}
			
			if (t<0) {
				chan_misdn_log(-1, bc->port, "Select Error (err=%s)\n",strerror(errno));
				break;
			}
			
			if (FD_ISSET(ch->pipe[1],&wrfs)) {
				chan_misdn_log(9, bc->port, "writing %d bytes 2 asterisk\n",bc->bframe_len);
				int ret=write(ch->pipe[1], bc->bframe, bc->bframe_len);
				
				if (ret<=0) {
					chan_misdn_log(-1, bc->port, "Write returned <=0 (err=%s) --> hanging up channel\n",strerror(errno));

					stop_bc_tones(ch);
					hangup_chan(ch);
					release_chan(bc);
				}
			} else {
				chan_misdn_log(1, bc->port, "Wripe Pipe full!\n");
			}
		}
	}
	break;
	case EVENT_TIMEOUT:
		{
		if (ch && bc)
			chan_misdn_log(1,bc->port,"--> state: %s\n",misdn_get_ch_state(ch));

		switch (ch->state) {
			case MISDN_DIALING:
			case MISDN_PROGRESS:
				if (bc->nt && !ch->nttimeout) break;
			
			case MISDN_CALLING:
			case MISDN_ALERTING:
			case MISDN_PROCEEDING:
			case MISDN_CALLING_ACKNOWLEDGE:
				if (bc->nt) {
					bc->progress_indicator=8;
					hanguptone_indicate(ch);
				}
				
				bc->out_cause=1;
				misdn_lib_send_event(bc,EVENT_DISCONNECT);
			break;

			case MISDN_WAITING4DIGS:
				if (bc->nt) {
					bc->progress_indicator=8;
					bc->out_cause=1;
					hanguptone_indicate(ch);
					misdn_lib_send_event(bc,EVENT_DISCONNECT);
				} else {
					bc->out_cause=16;
					misdn_lib_send_event(bc,EVENT_RELEASE);
				}
				
			break;


			case MISDN_CLEANING: 
				chan_misdn_log(1,bc->port," --> in state cleaning .. so ingoring, the stack should clean it for us\n");
			break;

			default:
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			}
		}
		break;

    
	/***************************/
	/** Suplementary Services **/
	/***************************/
	case EVENT_RETRIEVE:
	{
		if (!ch) {
			chan_misdn_log(4, bc->port, " --> no CH, searching in holded");
			ch=find_holded_l3(cl_te, bc->l3_id,1);
		}

		if (!ch) {
			ast_log(LOG_WARNING, "Found no Holded channel, cannot Retrieve\n");
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
			break;
		}

		/*remember the channel again*/
		ch->bc=bc;
		ch->state = MISDN_CONNECTED;

		ch->hold_info.port=0;
		ch->hold_info.channel=0;
		
		struct ast_channel *hold_ast=AST_BRIDGED_P(ch->ast);
		
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

			chan_misdn_log(-1, bc->port, "Hold not allowed this port.\n");
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			break;
		}
		
		struct ast_channel *bridged=AST_BRIDGED_P(ch->ast);

		if (bridged) {
			chan_misdn_log(2,bc->port,"Bridge Partner is of type: %s\n",bridged->tech->type);
			ch->state = MISDN_HOLDED;
			ch->l3id = bc->l3_id;
			
			misdn_lib_send_event(bc, EVENT_HOLD_ACKNOWLEDGE);

			ast_moh_start(bridged, NULL);

			/*forget the channel now*/
			ch->bc=NULL;
			ch->hold_info.port=bc->port;
			ch->hold_info.channel=bc->channel;

		} else {
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			chan_misdn_log(0, bc->port, "We aren't bridged to anybody\n");
		}
	} 
	break;
	
	case EVENT_FACILITY:
		print_facility(bc);
		
		switch (bc->fac_type) {
		case FACILITY_CALLDEFLECT:
		{
			struct ast_channel *bridged=AST_BRIDGED_P(ch->ast);
			struct chan_list *ch;
			
			if (bridged && MISDN_ASTERISK_TECH_PVT(bridged)) {
				ch=MISDN_ASTERISK_TECH_PVT(bridged);
				/*ch->state=MISDN_FACILITY_DEFLECTED;*/
				if (ch->bc) {
					/* todo */
				}
				
			}
			
		} 
		
		break;
		default:
			chan_misdn_log(1, bc->port," --> not yet handled\n");
		}
		
		break;

	case EVENT_RESTART:

		if (!bc->dummy) {
			stop_bc_tones(ch);
			release_chan(bc);
		}
		
		break;
				
	default:
		chan_misdn_log(1,0, "Got Unknown Event\n");
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



int load_module(void)
{
	int i;
	
	char ports[256]="";
	
	max_ports=misdn_lib_maxports_get();
	
	if (max_ports<=0) {
		ast_log(LOG_ERROR, "Unable to initialize mISDN\n");
		return 0;
	}
	
	if (misdn_cfg_init(max_ports)<0) {
		ast_log(LOG_ERROR, "Unable to initialize mISDN Config System\n");
		return 0;
	}

	g_config_initialized=1;
	
	misdn_debug = (int *)malloc(sizeof(int) * (max_ports+1));
	misdn_cfg_get( 0, MISDN_GEN_DEBUG, &misdn_debug[0], sizeof(int));
	for (i = 1; i <= max_ports; i++)
		misdn_debug[i] = misdn_debug[0];
	misdn_debug_only = (int *)calloc(max_ports + 1, sizeof(int));

	
	{
		char tempbuf[BUFFERSIZE+1];
		misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, tempbuf, BUFFERSIZE);
		if (strlen(tempbuf))
			tracing = 1;
	}

	ast_mutex_init(&cl_te_lock);
	ast_mutex_init(&release_lock);

	misdn_cfg_update_ptp();
	misdn_cfg_get_ports_string(ports);
		
	if (strlen(ports))
		chan_misdn_log(0, 0, "Got: %s from get_ports\n",ports);
	
	{
		struct misdn_lib_iface iface = {
			.cb_event = cb_events,
			.cb_log = chan_misdn_log,
			.cb_jb_empty = chan_misdn_jb_empty,
		};
		if (misdn_lib_init(ports, &iface, NULL))
			chan_misdn_log(0, 0, "No te ports initialized\n");
	
		int ntflags=0;
		char ntfile[BUFFERSIZE+1];

		misdn_cfg_get( 0, MISDN_GEN_NTDEBUGFLAGS, &ntflags, sizeof(int));
		misdn_cfg_get( 0, MISDN_GEN_NTDEBUGFILE, &ntfile, BUFFERSIZE);

		misdn_lib_nt_debug_init(ntflags,ntfile);

	}


	{
		if (ast_channel_register(&misdn_tech)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", misdn_type);
			unload_module();
			return -1;
		}
	}
  
	ast_cli_register(&cli_send_display);
	ast_cli_register(&cli_send_cd);
	ast_cli_register(&cli_send_restart);
	ast_cli_register(&cli_send_digit);
	ast_cli_register(&cli_toggle_echocancel);
	ast_cli_register(&cli_set_tics);

	ast_cli_register(&cli_show_cls);
	ast_cli_register(&cli_show_cl);
	ast_cli_register(&cli_show_config);
	ast_cli_register(&cli_show_port);
	ast_cli_register(&cli_show_stacks);

	ast_cli_register(&cli_port_block);
	ast_cli_register(&cli_port_unblock);
	ast_cli_register(&cli_restart_port);
	ast_cli_register(&cli_restart_pid);
	ast_cli_register(&cli_port_up);
	ast_cli_register(&cli_port_down);
	ast_cli_register(&cli_set_debug);
	ast_cli_register(&cli_set_crypt_debug);
	ast_cli_register(&cli_reload);

  
	ast_register_application("misdn_set_opt", misdn_set_opt_exec, "misdn_set_opt",
				 "misdn_set_opt(:<opt><optarg>:<opt><optarg>..):\n"
				 "Sets mISDN opts. and optargs\n"
				 "\n"
				 "The available options are:\n"
				 "    d - Send display text on called phone, text is the optparam\n"
				 "    n - don't detect dtmf tones on called channel\n"
				 "    h - make digital outgoing call\n" 
				 "    c - make crypted outgoing call, param is keyindex\n"
				 "    e - perform echo cancelation on this channel,\n"
				 "        takes taps as arguments (32,64,128,256)\n"
				 "    s - send Non Inband DTMF as inband\n"
				 "   vr - rxgain control\n"
				 "   vt - txgain control\n"
		);

	
	ast_register_application("misdn_facility", misdn_facility_exec, "misdn_facility",
				 "misdn_facility(<FACILITY_TYPE>|<ARG1>|..)\n"
				 "Sends the Facility Message FACILITY_TYPE with \n"
				 "the given Arguments to the current ISDN Channel\n"
				 "Supported Facilities are:\n"
				 "\n"
				 "type=calldeflect args=Nr where to deflect\n"
		);


	ast_register_application("misdn_check_l2l1", misdn_check_l2l1, "misdn_check_l2l1",
				 "misdn_check_l2l1(<port>||g:<groupname>,timeout)"
				 "Checks if the L2 and L1 are up on either the given <port> or\n"
				 "on the ports in the group with <groupname>\n"
				 "If the L1/L2 are down, check_l2l1 gets up the L1/L2 and waits\n"
				 "for <timeout> seconds that this happens. Otherwise, nothing happens\n"
				 "\n"
				 "This application, ensures the L1/L2 state of the Ports in a group\n"
				 "it is intended to make the pmp_l1_check option redundant and to\n"
				 "fix a buggy switch config from your provider\n"
				 "\n"
				 "a sample dialplan would look like:\n\n"
				 "exten => _X.,1,misdn_check_l2l1(g:out|2)\n"
				 "exten => _X.,n,dial(mISDN/g:out/${EXTEN})\n"
				 "\n"
		);


	misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);

	chan_misdn_log(0, 0, "-- mISDN Channel Driver Registred -- (BE AWARE THIS DRIVER IS EXPERIMENTAL!)\n");

	return 0;
}



int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_log(LOG_VERBOSE, "-- Unregistering mISDN Channel Driver --\n");
	
	misdn_tasks_destroy();
	
	if (!g_config_initialized) return 0;
	
	ast_cli_unregister(&cli_send_display);
	
	ast_cli_unregister(&cli_send_cd);
	ast_cli_unregister(&cli_send_restart);
	
	ast_cli_unregister(&cli_send_digit);
	ast_cli_unregister(&cli_toggle_echocancel);
	ast_cli_unregister(&cli_set_tics);
  
	ast_cli_unregister(&cli_show_cls);
	ast_cli_unregister(&cli_show_cl);
	ast_cli_unregister(&cli_show_config);
	ast_cli_unregister(&cli_show_port);
	ast_cli_unregister(&cli_show_stacks);
	ast_cli_unregister(&cli_port_block);
	ast_cli_unregister(&cli_port_unblock);
	ast_cli_unregister(&cli_restart_port);
	ast_cli_unregister(&cli_restart_pid);
	ast_cli_unregister(&cli_port_up);
	ast_cli_unregister(&cli_port_down);
	ast_cli_unregister(&cli_set_debug);
	ast_cli_unregister(&cli_set_crypt_debug);
	ast_cli_unregister(&cli_reload);
	/* ast_unregister_application("misdn_crypt"); */
	ast_unregister_application("misdn_set_opt");
	ast_unregister_application("misdn_facility");
	ast_unregister_application("misdn_check_l2l1");
  
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

int reload(void)
{
	reload_config();

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




/*** SOME APPS ;)***/

static int misdn_facility_exec(struct ast_channel *chan, void *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *tok, *tokb;

	chan_misdn_log(0,0,"TYPE: %s\n",chan->tech->type);
	
	if (strcasecmp(chan->tech->type,"mISDN")) {
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
		chan_misdn_log(1, ch->bc->port, "Unknown Facility: %s\n",tok);
	}
	
	return 0;
	
}

static int misdn_check_l2l1(struct ast_channel *chan, void *data)
{
	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(grouppar);
			AST_APP_ARG(timeout);
	);

	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "misdn_check_l2l1 Requires arguments\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc != 2) {
		ast_log(LOG_WARNING, "Wrong argument count\n");
		return 0;
	}

	/*ast_log(LOG_NOTICE, "Arguments: group/port '%s' timeout '%s'\n", args.grouppar, args.timeout);*/
	char group[BUFFERSIZE+1];
	char *port_str;

	int port=0;
	int timeout=atoi(args.timeout);
	int dowait=0;

	port_str=args.grouppar;

	int port_up;
	if (port_str[0]=='g' && port_str[1]==':' ) {
		/* We make a group call lets checkout which ports are in my group */
		port_str += 2;
		strncpy(group, port_str, BUFFERSIZE);
		group[BUFFERSIZE-1] = 0;
		chan_misdn_log(2, 0, "Checking Ports in group: %s\n",group);

		for (	port = misdn_cfg_get_next_port(port); 
			port > 0;
			port = misdn_cfg_get_next_port(port)) {
			
			chan_misdn_log(2,0,"trying port %d\n",port);

			char cfg_group[BUFFERSIZE+1];
			misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);

			if (!strcasecmp(cfg_group, group)) {
				port_up = misdn_lib_port_up(port, 1);

				if (!port_up) {
					chan_misdn_log(2, 0, " --> port '%d'\n", port);
					misdn_lib_get_port_up(port);
					dowait=1;
				}
			}
		}

	} else {
		port = atoi(port_str);
		chan_misdn_log(2, 0, "Checking Port: %d\n",port);
		port_up = misdn_lib_port_up(port, 1);
		if (!port_up) {
			misdn_lib_get_port_up(port);
			dowait=1;
		}

	}

	if (dowait) {
		chan_misdn_log(2, 0, "Waiting for '%d' seconds\n",timeout);
		sleep(timeout);
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
	int change_jitter=0;
	
	if (strcasecmp(chan->tech->type,"mISDN")) {
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
			ast_copy_string(ch->bc->display,++tok,84);
			chan_misdn_log(1, ch->bc->port, "SETOPT: Display:%s\n",ch->bc->display);
			break;
			
		case 'n':
			chan_misdn_log(1, ch->bc->port, "SETOPT: No DSP\n");
			ch->bc->nodsp=1;
			break;

		case 'j':
			chan_misdn_log(1, ch->bc->port, "SETOPT: jitter\n");
			tok++;
			change_jitter=1;
			
			switch ( tok[0] ) {
			case 'b' :
				ch->jb_len=atoi(++tok);
				chan_misdn_log(1, ch->bc->port, " --> buffer_len:%d\n",ch->jb_len);
				break;
			case 't' :
				ch->jb_upper_threshold=atoi(++tok);
				chan_misdn_log(1, ch->bc->port, " --> upper_threshold:%d\n",ch->jb_upper_threshold);
				break;

			case 'n':
				ch->bc->nojitter=1;
				chan_misdn_log(1, ch->bc->port, " --> nojitter\n");
				break;
				
			default:
				ch->jb_len=4000;
				ch->jb_upper_threshold=0;
				chan_misdn_log(1, ch->bc->port, " --> buffer_len:%d (default)\n",ch->jb_len);
				chan_misdn_log(1, ch->bc->port, " --> upper_threshold:%d (default)\n",ch->jb_upper_threshold);
			}
			
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
      
			char keys[4096];
			char *key=NULL, *tmp;
			int i;
			misdn_cfg_get( 0, MISDN_GEN_CRYPT_KEYS, keys, sizeof(keys));

			tmp=keys;

			for (i=0; i<keyidx; i++) {
				key=strsep(&tmp,",");
			}

			if (key) {
				ast_copy_string(ch->bc->crypt_key, key, sizeof(ch->bc->crypt_key));
			}
			
			chan_misdn_log(0, ch->bc->port, "SETOPT: crypt with key:%s\n",ch->bc->crypt_key);
			break;

		case 'e':
			chan_misdn_log(1, ch->bc->port, "SETOPT: EchoCancel\n");
			
			if (neglect) {
				chan_misdn_log(1, ch->bc->port, " --> disabled\n");
#ifdef MISDN_1_2
				*ch->bc->pipeline=0;
#else
				ch->bc->ec_enable=0;
#endif
			} else {
#ifdef MISDN_1_2
				update_pipeline_config(ch->bc);
#else
				ch->bc->ec_enable=1;
				ch->bc->orig=ch->orginator;
				tok++;
				if (*tok) {
					ch->bc->ec_deftaps=atoi(tok);
				}
#endif
			}
			
			break;
      
		case 'h':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Digital\n");
			
			if (strlen(tok) > 1 && tok[1]=='1') {
				chan_misdn_log(1, ch->bc->port, "SETOPT: HDLC \n");
				if (!ch->bc->hdlc) {
					ch->bc->hdlc=1;
				}
			}  
			ch->bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
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

	if (change_jitter)
		config_jitterbuffer(ch);
	
	
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


int chan_misdn_jb_empty ( struct misdn_bchannel *bc, char *buf, int len) 
{
	struct chan_list *ch=find_chan_by_bc(cl_te, bc);
	
	if (ch && ch->jb) {
		return misdn_jb_empty(ch->jb, buf, len);
	}
	
	return -1;
}



/*******************************************************/
/***************** JITTERBUFFER ************************/
/*******************************************************/


/* allocates the jb-structure and initialise the elements*/
struct misdn_jb *misdn_jb_init(int size, int upper_threshold)
{
    int i;
    struct misdn_jb *jb = (struct misdn_jb*) malloc(sizeof(struct misdn_jb));
    jb->size = size;
    jb->upper_threshold = upper_threshold;
    jb->wp = 0;
    jb->rp = 0;
    jb->state_full = 0;
    jb->state_empty = 0;
    jb->bytes_wrote = 0;
    jb->samples = (char *)malloc(size*sizeof(char));

    if (!jb->samples) {
	    chan_misdn_log(-1,0,"No free Mem for jb->samples\n");
	    return NULL;
    }
    
    jb->ok = (char *)malloc(size*sizeof(char));

    if (!jb->ok) {
	    chan_misdn_log(-1,0,"No free Mem for jb->ok\n");
	    return NULL;
    }

    for(i=0; i<size; i++)
 	jb->ok[i]=0;

    ast_mutex_init(&jb->mutexjb);

    return jb;
}

/* frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb)
{
	ast_mutex_destroy(&jb->mutexjb);
	
	free(jb->samples);
	free(jb);
}

/* fills the jitterbuffer with len data returns < 0 if there was an
   error (bufferoverflow). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len)
{
    int i, j, rp, wp;

    if (!jb || ! data) return 0;

    ast_mutex_lock (&jb->mutexjb);
    
    wp=jb->wp;
    rp=jb->rp;
	
    for(i=0; i<len; i++)
    {
	jb->samples[wp]=data[i];
	jb->ok[wp]=1;
	wp = (wp!=jb->size-1 ? wp+1 : 0);

	if(wp==jb->rp)
	    jb->state_full=1;
    }
    
    if(wp>=rp)
      jb->state_buffer=wp-rp;
    else
      jb->state_buffer= jb->size-rp+wp;
    chan_misdn_log(9,0,"misdn_jb_fill: written:%d | Bufferstatus:%d p:%x\n",len,jb->state_buffer,jb);
    
    if(jb->state_full)
    {
	jb->wp=wp;

	rp=wp;
	for(j=0; j<jb->upper_threshold; j++)
	    rp = (rp!=0 ? rp-1 : jb->size-1);
	jb->rp=rp;
	jb->state_full=0;
	jb->state_empty=1;

	ast_mutex_unlock (&jb->mutexjb);
	
	return -1;
    }

    if(!jb->state_empty)
    {
	jb->bytes_wrote+=len;
	if(jb->bytes_wrote>=jb->upper_threshold)
	{
	    jb->state_empty=1;
	    jb->bytes_wrote=0;
	}
    }
    jb->wp=wp;

    ast_mutex_unlock (&jb->mutexjb);
    
    return 0;
}

/* gets len bytes out of the jitterbuffer if available, else only the
available data is returned and the return value indicates the number
of data. */
int misdn_jb_empty(struct misdn_jb *jb, char *data, int len)
{
    int i, wp, rp, read=0;

    ast_mutex_lock (&jb->mutexjb);

    rp=jb->rp;
    wp=jb->wp;

    if(jb->state_empty)
    {	
	for(i=0; i<len; i++)
	{
	    if(wp==rp)
	    {
		jb->rp=rp;
		jb->state_empty=0;

		ast_mutex_unlock (&jb->mutexjb);
		
		return read;
	    }
	    else
	    {
		if(jb->ok[rp]==1)
		{
		    data[i]=jb->samples[rp];
		    jb->ok[rp]=0;
		    rp=(rp!=jb->size-1 ? rp+1 : 0);
		    read+=1;
		}
	    }
	}

	if(wp >= rp)
		jb->state_buffer=wp-rp;
	else
		jb->state_buffer= jb->size-rp+wp;
	chan_misdn_log(9,0,"misdn_jb_empty: read:%d | Bufferstatus:%d p:%x\n",len,jb->state_buffer,jb);
	
	jb->rp=rp;
    }
    else
	    chan_misdn_log(9,0,"misdn_jb_empty: Wait...requested:%d p:%x\n",len,jb);
    
    ast_mutex_unlock (&jb->mutexjb);

    return read;
}




/*******************************************************/
/*************** JITTERBUFFER  END *********************/
/*******************************************************/




void chan_misdn_log(int level, int port, char *tmpl, ...)
{
	if (! ((0 <= port) && (port <= max_ports))) {
		ast_log(LOG_WARNING, "cb_log called with out-of-range port number! (%d)\n", port);
		port=0;
		level=-1;
	}
		
	va_list ap;
	char buf[1024];
	char port_buf[8];
	sprintf(port_buf,"P[%2d] ",port);
	
	va_start(ap, tmpl);
	vsnprintf( buf, 1023, tmpl, ap );
	va_end(ap);

	if (level == -1)
		ast_log(LOG_WARNING, buf);

	else if (misdn_debug_only[port] ? 
			(level==1 && misdn_debug[port]) || (level==misdn_debug[port]) 
		 : level <= misdn_debug[port]) {
		
		ast_console_puts(port_buf);
		ast_console_puts(buf);
	}
	
	if ((level <= misdn_debug[0]) && !ast_strlen_zero(global_tracefile) ) {
		time_t tm = time(NULL);
		char *tmp=ctime(&tm),*p;
		
		FILE *fp= fopen(global_tracefile, "a+");
		
		p=strchr(tmp,'\n');
		if (p) *p=':';
		
		if (!fp) {
			ast_console_puts("Error opening Tracefile: [ ");
			ast_console_puts(global_tracefile);
			ast_console_puts(" ] ");
			
			ast_console_puts(strerror(errno));
			ast_console_puts("\n");
			return ;
		}
		
		fputs(tmp,fp);
		fputs(" ", fp);
		fputs(port_buf,fp);
		fputs(" ", fp);
		fputs(buf, fp);

		fclose(fp);
	}
}


