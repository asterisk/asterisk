#ifndef FAC_H
#define FAC_H

void fac_enc( unsigned char **ntmsg, msg_t * msg, enum facility_type type,  union facility fac, struct misdn_bchannel *bc);

void fac_dec( unsigned char *p, Q931_info_t *qi, enum facility_type *type,  union facility *fac, struct misdn_bchannel *bc);

#endif
