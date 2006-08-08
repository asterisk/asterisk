#ifndef __FAC_H__
#define __FAC_H__

#include "isdn_lib_intern.h"

void fac_enc (__u8 **ntmsg, msg_t *msg, enum facility_type type, union facility fac, struct misdn_bchannel *bc);
void fac_dec (__u8 *p, Q931_info_t *qi, enum facility_type *type, union facility *fac, struct misdn_bchannel *bc);

#endif

