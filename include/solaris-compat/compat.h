#ifndef _SOLARIS_COMPAT_H
#define _SOLARIS_COMPAT_H

#define __BEGIN_DECLS
#define __END_DECLS

#ifndef __P
#define __P(p) p
#endif

#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321

#ifdef __sparc__
#define BYTE_ORDER BIG_ENDIAN
#define __BYTE_ORDER BIG_ENDIAN
#else
#define BYTE_ORDER LITTLE_ENDIAN
#define ____BYTE_ORDER BIG_ENDIAN
#endif

#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__
typedef unsigned char	u_int8_t;
typedef unsigned short	u_int16_t;
typedef unsigned int	u_int32_t;
#endif

int setenv(const char *name, const char *value, int overwrite);

#endif
