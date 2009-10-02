#ifndef ISDN_LIB_INTERN
#define ISDN_LIB_INTERN


#include <mISDNuser/mISDNlib.h>
#include <mISDNuser/isdn_net.h>
#include <mISDNuser/l3dss1.h>
#include <mISDNuser/net_l3.h>

#include <pthread.h>

#include "isdn_lib.h"

#ifndef MISDNUSER_VERSION_CODE
#error "You need a newer version of mISDNuser ..."
#elif MISDNUSER_VERSION_CODE < MISDNUSER_VERSION(1, 0, 3)
#error "You need a newer version of mISDNuser ..."
#endif


#define QI_ELEMENT(a) a.off


#ifndef mISDNUSER_HEAD_SIZE

#define mISDNUSER_HEAD_SIZE (sizeof(mISDNuser_head_t))
/*#define mISDNUSER_HEAD_SIZE (sizeof(mISDN_head_t))*/
#endif


#if 0
ibuffer_t *astbuf;		/* Not used */
ibuffer_t *misdnbuf;	/* Not used */
#endif

struct send_lock {
	pthread_mutex_t lock;
};


struct isdn_msg {
	unsigned long misdn_msg;

	enum layer_e layer;
	enum event_e event;

	void (*msg_parser)(struct isdn_msg *msgs, msg_t *msg, struct misdn_bchannel *bc, int nt);
	msg_t *(*msg_builder)(struct isdn_msg *msgs, struct misdn_bchannel *bc, int nt);
	char *info;
} ;

/* for isdn_msg_parser.c */
msg_t *create_l3msg(int prim, int mt, int dinfo , int size, int nt);



struct misdn_stack {
	/** is first element because &nst equals &mISDNlist **/
	net_stack_t nst;
	manager_t mgr;
	pthread_mutex_t nstlock;

	/*! \brief Stack struct critical section lock. */
	pthread_mutex_t st_lock;

	/*! \brief D Channel mISDN driver stack ID (Parent stack ID) */
	int d_stid;

	/*! /brief Number of B channels supported by this port */
	int b_num;

	/*! \brief B Channel mISDN driver stack IDs (Child stack IDs) */
	int b_stids[MAX_BCHANS + 1];

	/*! \brief TRUE if Point-To-Point(PTP) (Point-To-Multipoint(PTMP) otherwise) */
	int ptp;

	/*! \brief Number of consecutive times PTP Layer 2 declared down */
	int l2upcnt;

	int l2_id;	/* Not used */

	/*! \brief Lower layer mISDN ID (addr) (Layer 1/3) */
	int lower_id;

	/*! \brief Upper layer mISDN ID (addr) (Layer 2/4) */
	int upper_id;

	/*! \brief TRUE if port is blocked */
  	int blocked;

	/*! \brief TRUE if Layer 2 is UP */
	int l2link;

	time_t l2establish;	/* Not used */

	/*! \brief TRUE if Layer 1 is UP */
	int l1link;

	/*! \brief TRUE if restart has been sent to the other side after stack startup */
	int restart_sent;

	/*! \brief mISDN device handle returned by mISDN_open() */
	int midev;

	/*! \brief TRUE if NT side of protocol (TE otherwise) */
	int nt;

	/*! \brief TRUE if ISDN-PRI (ISDN-BRI otherwise) */
	int pri;

	/*! \brief CR Process ID allocation table.  TRUE if ID allocated */
	int procids[0x100+1];

	/*! \brief Queue of Event messages to send to mISDN */
	msg_queue_t downqueue;
	msg_queue_t upqueue;	/* No code puts anything on this queue */
	int busy;	/* Not used */

	/*! \brief Logical Layer 1 port associated with this stack */
	int port;

	/*! \brief B Channel record pool array */
	struct misdn_bchannel bc[MAX_BCHANS + 1];

	struct misdn_bchannel* bc_list;	/* Not used */

	/*! \brief Array of B channels in use (a[0] = B1).  TRUE if B channel in use */
	int channels[MAX_BCHANS + 1];

	/*! \brief List of held channels */
	struct misdn_bchannel *holding;

	/*! \brief Next stack in the list of stacks */
	struct misdn_stack *next;
};


struct misdn_stack* get_stack_by_bc(struct misdn_bchannel *bc);

int isdn_msg_get_index(struct isdn_msg msgs[], msg_t *frm, int nt);
enum event_e isdn_msg_get_event(struct isdn_msg msgs[], msg_t *frm, int nt);
int isdn_msg_parse_event(struct isdn_msg msgs[], msg_t *frm, struct misdn_bchannel *bc, int nt);
char * isdn_get_info(struct isdn_msg msgs[], enum event_e event, int nt);
msg_t * isdn_msg_build_event(struct isdn_msg msgs[], struct misdn_bchannel *bc, enum event_e event, int nt);
int isdn_msg_get_index_by_event(struct isdn_msg msgs[], enum event_e event, int nt);
char * isdn_msg_get_info(struct isdn_msg msgs[], msg_t *msg, int nt);


#endif
