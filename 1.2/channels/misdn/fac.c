
#include "isdn_lib_intern.h"
#include "isdn_lib.h"

#include "string.h"




#define CENTREX_ID      0xa1
#define CALLDEFLECT_ID      0xa1

/**
   This file covers the encoding and decoding of facility messages and
   facility information elements.

   There will be 2 Functions as Interface:
   
   fac_enc( char **ntmsg, msg_t * msg, enum facility_type type,  union facility fac, struct misdn_bchannel *bc)
   fac_dec( unsigned char *p, Q931_info_t *qi, enum facility_type *type,  union facility *fac, struct misdn_bchannel *bc);

   Those will either read the union facility or fill it.

   internally, we will have deconding and encoding functions for each facility
   IE.
   
**/


/* support stuff */
static void strnncpy(char *dest, unsigned char *src, int len, int dst_len)
{
	if (len > dst_len-1)
		len = dst_len-1;
	strncpy((char *)dest, (char *)src, len);
	dest[len] = '\0';
}




/**********************/
/*** FACILITY STUFF ***/
/**********************/


/* IE_FACILITY */
void enc_ie_facility(unsigned char **ntmode, msg_t *msg, unsigned char *facility, int facility_len, int nt, struct misdn_bchannel *bc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;


	if (!facility || facility_len<=0)
	{
		return;
	}

	
	l = facility_len;
	p = msg_put(msg, l+2);
	if (nt)
		*ntmode = p+1;
	else
		qi->QI_ELEMENT(facility) = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_FACILITY;
	p[1] = l;
	memcpy(p+2, facility, facility_len);
}


/* facility for siemens CENTEX (known parts implemented only) */
void enc_ie_facility_centrex(unsigned char **ntmode, msg_t *msg, char *cnip, int setup, int nt, struct misdn_bchannel *bc)
{
	unsigned char centrex[256];
	int i = 0;

	if (!cnip)
		return;

	/* centrex facility */
	centrex[i++] = FACILITY_CENTREX;
	centrex[i++] = CENTREX_ID;

	/* cnip */
	if (strlen((char *)cnip) > 15)
	{
/* 		if (options.deb & DEBUG_PORT) */
		cb_log(1,0,"%s: CNIP/CONP text too long (max 13 chars), cutting.\n", __FUNCTION__);
		cnip[15] = '\0';
	}
	/*  dunno what the 8 bytes mean */
	if (setup)
	{
		centrex[i++] = 0x17;
		centrex[i++] = 0x02;
		centrex[i++] = 0x02;
		centrex[i++] = 0x44;
		centrex[i++] = 0x18;
		centrex[i++] = 0x02;
		centrex[i++] = 0x01;
		centrex[i++] = 0x09;
	} else
	{
		centrex[i++] = 0x18;
		centrex[i++] = 0x02;
		centrex[i++] = 0x02;
		centrex[i++] = 0x81;
		centrex[i++] = 0x09;
		centrex[i++] = 0x02;
		centrex[i++] = 0x01;
		centrex[i++] = 0x0a;
	}

	centrex[i++] = 0x80;
	centrex[i++] = strlen((char *)cnip);
	strcpy((char *)(&centrex[i]), (char *)cnip);
	i += strlen((char *)cnip);
	cb_log(4,0,"    cnip='%s'\n", cnip);

	/* encode facility */
	enc_ie_facility(ntmode, msg, centrex, i, nt , bc);
}

void dec_ie_facility_centrex(unsigned char *p, Q931_info_t *qi, unsigned char *centrex, int facility_len, char *cnip, int cnip_len, int nt, struct misdn_bchannel *bc)
{

	int i = 0;
	*cnip = '\0';
	
	if (facility_len >= 2)
	{
		if (centrex[i++] != FACILITY_CENTREX)
			return;
		if (centrex[i++] != CENTREX_ID)
			return;
	}

	/* loop sub IEs of facility */
	while(facility_len > i+1)
	{
		if (centrex[i+1]+i+1 > facility_len)
		{
			printf("%s: ERROR: short read of centrex facility.\n", __FUNCTION__);
			return;
		}
		switch(centrex[i])
		{
		case 0x80:
			strnncpy(cnip, &centrex[i+2], centrex[i+1], cnip_len);
			cb_log(4,0,"    CENTREX cnip='%s'\n", cnip);
			break;
		}
		i += 1+centrex[i+1];
	}
}




/* facility for CALL Deflect (known parts implemented only) */
void enc_ie_facility_calldeflect(unsigned char **ntmode, msg_t *msg, char *nr, int nt, struct misdn_bchannel *bc)
{
	unsigned char fac[256];
	
	if (!nr)
		return;

	int len = strlen(nr);
	/* calldeflect facility */
	
	/* cnip */
	if (strlen((char *)nr) > 15)
	{
/* 		if (options.deb & DEBUG_PORT) */
		cb_log(1,0,"%s: NR text too long (max 13 chars), cutting.\n", __FUNCTION__);
		nr[15] = '\0';
	}
	
	fac[0]=FACILITY_CALLDEFLECT;	// ..
	fac[1]=CALLDEFLECT_ID;
	fac[2]=0x0f + len;	// strlen destination + 15 = 26
	fac[3]=0x02;
	fac[4]=0x01;
	//fac[5]=0x70;
	fac[5]=0x09;
	fac[6]=0x02;
	fac[7]=0x01;
	fac[8]=0x0d;
	fac[9]=0x30;
	fac[10]=0x07 + len;	// strlen destination + 7 = 18
	fac[11]=0x30;	// ...hm 0x30
	fac[12]=0x02+ len;	// strlen destination + 2	
	fac[13]=0x80;	// CLIP
	fac[14]= len;	//  strlen destination 
	
	memcpy((unsigned char *)fac+15,nr,len);
	fac[15+len]=0x01; //sending complete
	fac[16+len]=0x01;
	fac[17+len]=0x80;
	
	enc_ie_facility(ntmode, msg, fac, 17+len +1 , nt , bc);
}


void dec_ie_facility_calldeflect(unsigned char *p, Q931_info_t *qi, unsigned char *fac, int fac_len, char *cd_nr,  int nt, struct misdn_bchannel *bc)
{
	*cd_nr = '\0';
	
	if (fac_len >= 15)
	{
		if (fac[0] != FACILITY_CALLDEFLECT)
			return;
		if (fac[1] != CALLDEFLECT_ID)
			return;
	} else {
		cb_log(1,bc->port, "IE too short: FAC_CALLDEFLECT\n");
		return ;
	}
	
	
	
	{
		int dest_len=fac[2]-0x0f;
		
		if (dest_len <0 || dest_len > 15) {
			cb_log(1,bc->port, "IE is garbage: FAC_CALLDEFLECT\n");
			return ;
		}
		
		if (fac_len < 15+dest_len) {
			cb_log(1,bc->port, "IE too short: FAC_CALLDEFLECT\n");
			return ;
		}
		
		memcpy(cd_nr, &fac[15],dest_len);
		cd_nr[dest_len]=0;
		
		cb_log(5,bc->port, "--> IE CALLDEFLECT NR: %s\n",cd_nr);
	}
}



void fac_enc( unsigned char **ntmsg, msg_t * msg, enum facility_type type,  union facility fac, struct misdn_bchannel *bc)
{
	switch (type) {
	case FACILITY_CENTREX:
	{
		int setup=0;
		enc_ie_facility_centrex(ntmsg, msg, fac.cnip, setup, bc->nt, bc);
	}
		break;
	case FACILITY_CALLDEFLECT:
		enc_ie_facility_calldeflect(ntmsg, msg, fac.calldeflect_nr, bc->nt, bc);
		break;
	default:
		cb_log(1,0,"Don't know how handle this facility: %d\n", type);
	}
}

void fac_dec( unsigned char *p, Q931_info_t *qi, enum facility_type *type,  union facility *fac, struct misdn_bchannel *bc)
{
	int i, fac_len=0;
	unsigned char facility[256];

	if (!bc->nt)
	{
		p = NULL;
		if (qi->QI_ELEMENT(facility))
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->QI_ELEMENT(facility) + 1;
	}
	if (!p)
		return;
	
	fac_len = p[0] & 0xff;

	memcpy(facility, p+1, fac_len);
	
	switch(facility[0]) {
	case FACILITY_CENTREX:
	{
		int cnip_len=15;
		
		dec_ie_facility_centrex(p, qi,facility, fac_len, fac->cnip, cnip_len, bc->nt, bc);
		
		*type=FACILITY_CENTREX;
	}
	break;
	case FACILITY_CALLDEFLECT:
		dec_ie_facility_calldeflect(p, qi,facility, fac_len, fac->calldeflect_nr,  bc->nt, bc);
		
		*type=FACILITY_CALLDEFLECT;
		break;
	default:
		cb_log(3, bc->port, "Unknown Facility received: ");
		i = 0;
		while(i < fac_len)
		{
			cb_log(3, bc->port, " %02x", facility[i]);
			i++;
		}
		cb_log(3, bc->port, "    facility\n");
		
		*type=FACILITY_NONE;
	}
	
	
}

/*** FACILITY END **/

