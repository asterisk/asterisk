
#include "asn1.h"
#include <string.h>

/*
** ASN.1 Encoding
*/

int _enc_null (__u8 *dest, int tag)
{
	dest[0] = tag;
	dest[1] = 0;
	return 2;
}

int _enc_bool (__u8 *dest, __u32 i, int tag)
{
	dest[0] = tag;
	dest[1] = 1;
	dest[2] = i ? 1:0;
	return 3;
}

int _enc_int (__u8 *dest, __u32 i, int tag)
{
	__u8 *p;
	dest[0] = tag;
	p = &dest[2];
	do {
		*p++ = i;
		i >>= 8;
	} while (i);
	dest[1] = p - &dest[2];
	return p - dest;
}

int _enc_enum (__u8 *dest, __u32 i, int tag)
{
	__u8 *p;

	dest[0] = tag;
	p = &dest[2];
	do {
		*p++ = i;
		i >>= 8;
	} while (i);
	dest[1] = p - &dest[2];
	return p - dest;
}

int _enc_num_string (__u8 *dest, __u8 *nd, __u8 len, int tag)
{
	__u8 *p;
	int i;

	dest[0] = tag;
	p = &dest[2];
	for (i = 0; i < len; i++)
		*p++ = *nd++;
	dest[1] = p - &dest[2];
	return p - dest;
}

int _enc_sequence_start (__u8 *dest, __u8 **id, int tag)
{
	dest[0] = tag;
	*id = &dest[1];
	return 2;
}

int _enc_sequence_end (__u8 *dest, __u8 *id, int tag_dummy)
{
	*id = dest - id - 1;
	return 0;
}

/*
** ASN.1 Decoding
*/

#define CHECK_P 						\
	do { \
		if (p >= end) \
			return -1; \
	} while (0) 

#define CallASN1(ret, p, end, todo)		\
	do { \
		ret = todo; \
		if (ret < 0) { \
			return -1; \
		} \
		p += ret; \
	} while (0)

#define INIT 							\
	int len, ret; \
	__u8 *begin = p; \
	if (tag) \
		*tag = *p; \
	p++; \
	CallASN1(ret, p, end, dec_len(p, &len)); \
	if (len >= 0) { \
		if (p + len > end) \
			return -1; \
		end = p + len; \
	}

int _dec_null (__u8 *p, __u8 *end, int *tag)
{
	INIT;
	return p - begin;
}

int _dec_bool (__u8 *p, __u8 *end, int *i, int *tag)
{
	INIT;
	*i = 0;
	while (len--) {
		CHECK_P;
		*i = (*i >> 8) + *p;
		p++;
	}
	return p - begin;
}

int _dec_int (__u8 *p, __u8 *end, int *i, int *tag)
{
	INIT;

	*i = 0;
	while (len--) {
		CHECK_P;
		*i = (*i << 8) + *p;
		p++;
	}
	return p - begin;
}

int _dec_enum (__u8 *p, __u8 *end, int *i, int *tag)
{
	INIT;

	*i = 0;
	while (len--) {
		CHECK_P;
		*i = (*i << 8) + *p;
		p++;
	}
	return p - begin;
}

int _dec_num_string (__u8 *p, __u8 *end, char *str, int *tag)
{
	INIT;

	while (len--) {
		CHECK_P;
		*str++ = *p;
		p++;
	}
	*str = 0;
	return p - begin;
}

int _dec_octet_string (__u8 *p, __u8 *end, char *str, int *tag)
{
	return _dec_num_string(p, end, str, tag);
}

int _dec_sequence (__u8 *p, __u8 *end, int *tag)
{
	INIT;
	return p - begin;
}

int dec_len (__u8 *p, int *len)
{
	*len = *p;
	return 1;
}
