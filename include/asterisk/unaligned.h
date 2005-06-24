/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Handle unaligned data access
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser General Public License.  Other components of
 * Asterisk are distributed under The GNU General Public License
 * only.
 */

#ifndef _ASTERISK_UNALIGNED_H
#define _ASTERISK_UNALIGNED_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef __GNUC__
/* If we just tell GCC what's going on, we can trust it to behave optimally */
static inline unsigned int get_unaligned_uint32(void *p)
{
	struct { unsigned int d; } __attribute__((packed)) *pp = (void *)p;

	return pp->d;
}
static inline unsigned short get_unaligned_uint16(void *p)
{
	struct { unsigned short d; } __attribute__((packed)) *pp = (void *)p;

	return pp->d;
}

static inline void put_unaligned_uint32(void *p, unsigned int datum)
{
	struct { unsigned int d; } __attribute__((packed)) *pp = (void *)p;

	pp->d = datum;
}

static inline void put_unaligned_uint16(void *p, unsigned short datum)
{
	struct { unsigned short d; } __attribute__((packed)) *pp = (void *)p;

	pp->d = datum;
}
#elif defined(SOLARIS) && defined(__sparc__)
static inline unsigned int get_unaligned_uint32(void *p)
{
	unsigned char *cp = p;

	return (cp[0] << 24) | (cp[1] << 16) | (cp[2] << 8) | cp[3];
}

static inline unsigned short get_unaligned_uint16(void *p)
{
	unsigned char *cp = p;

	return (cp[0] << 8) | cp[1] ;
}

static inline void put_unaligned_uint32(void *p, unsigned int datum)
{
	unsigned char *cp = p;

	cp[0] = datum >> 24;
	cp[1] = datum >> 16;
	cp[2] = datum >> 8;
	cp[3] = datum;
}

static inline void put_unaligned_uint16(void *p, unsigned int datum)
{
	unsigned char *cp = p;

	cp[0] = datum >> 8;
	cp[1] = datum;
}
#else /* Not GCC, not Solaris/SPARC. Assume we can handle direct load/store. */
#define get_unaligned_uint32(p) (*((unsigned int *)(p)))
#define get_unaligned_uint16(p) (*((unsigned short *)(p)))
#define put_unaligned_uint32(p,d) do { unsigned int *__P = (p); *__P = d; } while(0)
#define put_unaligned_uint16(p,d) do { unsigned short *__P = (p); *__P = d; } while(0)
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif /* _ASTERISK_UNALIGNED_H */
