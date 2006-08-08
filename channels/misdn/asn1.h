#ifndef __ASN1_H__
#define __ASN1_H__

#include <asm/types.h>

/*
** ASN.1 Tags
*/

#define ASN1_TAG_BOOLEAN           (0x01)
#define ASN1_TAG_INTEGER           (0x02)
#define ASN1_TAG_BIT_STRING        (0x03)
#define ASN1_TAG_OCTET_STRING      (0x04)
#define ASN1_TAG_NULL              (0x05)
#define ASN1_TAG_OBJECT_IDENTIFIER (0x06)
#define ASN1_TAG_ENUM              (0x0a)
#define ASN1_TAG_SEQUENCE          (0x30)
#define ASN1_TAG_SET               (0x31)
#define ASN1_TAG_NUMERIC_STRING    (0x12)
#define ASN1_TAG_PRINTABLE_STRING  (0x13)
#define ASN1_TAG_IA5_STRING        (0x16)
#define ASN1_TAG_UTC_TIME          (0x17)
#define ASN1_TAG_CONSTRUCTED       (0x20)
#define ASN1_TAG_CONTEXT_SPECIFIC  (0x80)
#define ASN1_TAG_EXPLICIT          (0x100)
#define ASN1_TAG_OPT               (0x200)
#define ASN1_NOT_TAGGED            (0x400)

/*
** ASN.1 Encoding
*/

#define enc_null(dest) _enc_null(dest,ASN1_TAG_NULL)
#define enc_bool(dest,i) _enc_bool(dest,i,ASN1_TAG_BOOLEAN)
#define enc_int(dest,i) _enc_int(dest,i,ASN1_TAG_INTEGER)
#define enc_enum(dest,i) _enc_enum(dest,i,ASN1_TAG_ENUM)
#define enc_num_string(dest,num,len) _enc_num_string(dest,num,len,ASN1_TAG_NUMERIC_STRING)
#define enc_sequence_start(dest,id) _enc_sequence_start(dest,id,ASN1_TAG_SEQUENCE)
#define enc_sequence_end(dest,id) _enc_sequence_end(dest,id,ASN1_TAG_SEQUENCE)

int _enc_null (__u8 *dest, int tag);
int _enc_bool (__u8 *dest, __u32 i, int tag);
int _enc_int (__u8 *dest, __u32 i, int tag);
int _enc_enum (__u8 *dest, __u32 i, int tag);
int _enc_num_string (__u8 *dest, __u8 *nd, __u8 len, int tag);
int _enc_sequence_start (__u8 *dest, __u8 **id, int tag);
int _enc_sequence_end (__u8 *dest, __u8 *id, int tag_dummy);

/*
** ASN.1 Decoding
*/

#define dec_null(p, end) _dec_null (p, end, NULL);
#define dec_bool(p, end,i) _dec_bool (p, end, i, NULL)
#define dec_int(p, end,i) _dec_int (p, end, i, NULL)
#define dec_enum(p, end,i) _dec_enum (p, end, i, NULL)
#define dec_num_string(p, end,str) _dec_num_string (p, end, str, NULL)
#define dec_octet_string(p, end,str) _dec_octet_string (p, end, str, NULL)
#define dec_sequence(p, end) _dec_sequence (p, end, NULL)

int _dec_null (__u8 *p, __u8 *end, int *tag);
int _dec_bool (__u8 *p, __u8 *end, int *i, int *tag);
int _dec_int (__u8 *p, __u8 *end, int *i, int *tag);
int _dec_enum (__u8 *p, __u8 *end, int *i, int *tag);
int _dec_num_string (__u8 *p, __u8 *end, char *str, int *tag);
int _dec_octet_string (__u8 *p, __u8 *end, char *str, int *tag);
int _dec_sequence (__u8 *p, __u8 *end, int *tag);

int dec_len (__u8 *p, int *len);

#endif

