/*! \file
 * \brief Interface to mISDN - port info
 * \author Christian Richter <crich@beronet.com>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "isdn_lib.h"
#include "isdn_lib_intern.h"


/*
 * global function to show all available isdn ports
 */
void isdn_port_info(void)
{
	int err;
	int i, ii, p;
	int useable, nt, pri;
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	stack_info_t *stinf;
	int device;

	/* open mISDN */
	if ((device = mISDN_open()) < 0)
	{
		fprintf(stderr, "mISDN_open() failed: ret=%d errno=%d (%s) Check for mISDN modules and device.\n", device, errno, strerror(errno));
		exit(-1);
	}

	/* get number of stacks */
	i = 1;
	ii = mISDN_get_stack_count(device);
	printf("\n");
	if (ii <= 0)
	{
		printf("Found no card. Please be sure to load card drivers.\n");
	}

	/* loop the number of cards and get their info */
	while(i <= ii)
	{
		err = mISDN_get_stack_info(device, i, buff, sizeof(buff));
		if (err <= 0)
		{
			fprintf(stderr, "mISDN_get_stack_info() failed: port=%d err=%d\n", i, err);
			break;
		}
		stinf = (stack_info_t *)&frm->data.p;

		nt = pri = 0;
		useable = 1;

		/* output the port info */
		printf("Port %2d: ", i);
		switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK)
		{
			case ISDN_PID_L0_TE_S0:
			printf("TE-mode BRI S/T interface line (for phone lines)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_TE_S0_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC multiport card");
#endif
			break;
			case ISDN_PID_L0_NT_S0:
			nt = 1;
			printf("NT-mode BRI S/T interface port (for phones)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_NT_S0_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC multiport card");
#endif
			break;
			case ISDN_PID_L0_TE_U:
			printf("TE-mode BRI U   interface line");
			break;
			case ISDN_PID_L0_NT_U:
			nt = 1;
			printf("NT-mode BRI U   interface port");
			break;
			case ISDN_PID_L0_TE_UP2:
			printf("TE-mode BRI Up2 interface line");
			break;
			case ISDN_PID_L0_NT_UP2:
			nt = 1;
			printf("NT-mode BRI Up2 interface port");
			break;
			case ISDN_PID_L0_TE_E1:
			pri = 1;
			printf("TE-mode PRI E1  interface line (for phone lines)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_TE_E1_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC-E1 card");
#endif
			break;
			case ISDN_PID_L0_NT_E1:
			nt = 1;
			pri = 1;
			printf("NT-mode PRI E1  interface port (for phones)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_NT_E1_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC-E1 card");
#endif
			break;
			default:
			useable = 0;
			printf("unknown type 0x%08x",stinf->pid.protocol[0]);
		}
		printf("\n");

		if (nt)
		{
			if (stinf->pid.protocol[1] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 1 NT-mode protocol.\n");
			}
			p = 2;
			while(p <= MAX_LAYER_NR) {
				if (stinf->pid.protocol[p])
				{
					useable = 0;
					printf(" -> Layer %d protocol 0x%08x is detected, but not allowed for NT lib.\n", p, stinf->pid.protocol[p]);
				}
				p++;
			}
			if (useable)
			{
				if (pri)
					printf(" -> Interface is Point-To-Point (PRI).\n");
				else
					printf(" -> Interface can be Poin-To-Point/Multipoint.\n");
			}
		} else
		{
			if (stinf->pid.protocol[1] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 1 protocol.\n");
			}
			if (stinf->pid.protocol[2] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 2 protocol.\n");
			}
			if (stinf->pid.protocol[2] & ISDN_PID_L2_DF_PTP)
			{
				printf(" -> Interface is Poin-To-Point.\n");
			}
			if (stinf->pid.protocol[3] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 3 protocol.\n");
			} else
			{
				printf(" -> Protocol: ");
				switch(stinf->pid.protocol[3] & ~ISDN_PID_FEATURE_MASK)
				{
					case ISDN_PID_L3_DSS1USER:
					printf("DSS1 (Euro ISDN)");
					break;

					default:
					useable = 0;
					printf("unknown protocol 0x%08x",stinf->pid.protocol[3]);
				}
				printf("\n");
			}
			p = 4;
			while(p <= MAX_LAYER_NR) {
				if (stinf->pid.protocol[p])
				{
					useable = 0;
					printf(" -> Layer %d protocol 0x%08x is detected, but not allowed for TE lib.\n", p, stinf->pid.protocol[p]);
				}
				p++;
			}
			printf(" -> childcnt: %d\n",stinf->childcnt);
		}

		if (!useable)
			printf(" * Port NOT useable for PBX\n");

		printf("--------\n");

		i++;
	}
	printf("\n");

	/* close mISDN */
	if ((err = mISDN_close(device)))
	{
		fprintf(stderr, "mISDN_close() failed: err=%d '%s'\n", err, strerror(err));
		exit(-1);
	}
}


int main()
{
  isdn_port_info();
  return 0;
}
