/* Compatibility functions for strsep and strtoq missing on Solaris */

#include <sys/types.h>
#include <stdio.h>

char* strsep(char** str, const char* delims)
{
    char* token;

    if (*str==NULL) {
        /* No more tokens */
        return NULL;
    }

    token=*str;
    while (**str!='\0') {
        if (strchr(delims,**str)!=NULL) {
            **str='\0';
            (*str)++;
            return token;
        }
        (*str)++;
    }
    /* There is no other token */
    *str=NULL;
    return token;
}


#define LONG_MIN        (-9223372036854775807L-1L)
                                        /* min value of a "long int" */
#define LONG_MAX        9223372036854775807L
                                        /* max value of a "long int" */

/*
 * Convert a string to a quad integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
uint64_t
strtoq(const char *nptr, char **endptr, int base)
{
        const char *s;
        uint64_t acc;
        unsigned char c;
        uint64_t qbase, cutoff;
        int neg, any, cutlim;

        /*
         * Skip white space and pick up leading +/- sign if any.
         * If base is 0, allow 0x for hex and 0 for octal, else
         * assume decimal; if base is already 16, allow 0x.
         */
        s = nptr;
        do {
                c = *s++;
        } while (isspace(c));
        if (c == '-') {
                neg = 1;
                c = *s++;
        } else {
                neg = 0;
                if (c == '+')
                        c = *s++;
        }
        if ((base == 0 || base == 16) &&
            c == '\0' && (*s == 'x' || *s == 'X')) {
                c = s[1];
                s += 2;
                base = 16;
        }
        if (base == 0)
                base = c == '\0' ? 8 : 10;

        /*
         * Compute the cutoff value between legal numbers and illegal
         * numbers.  That is the largest legal value, divided by the
         * base.  An input number that is greater than this value, if
         * followed by a legal input character, is too big.  One that
         * is equal to this value may be valid or not; the limit
         * between valid and invalid numbers is then based on the last
         * digit.  For instance, if the range for quads is
         * [-9223372036854775808..9223372036854775807] and the input base
         * is 10, cutoff will be set to 922337203685477580 and cutlim to
         * either 7 (neg==0) or 8 (neg==1), meaning that if we have
         * accumulated a value > 922337203685477580, or equal but the
         * next digit is > 7 (or 8), the number is too big, and we will
         * return a range error.
         *
         * Set any if any `digits' consumed; make it negative to indicate
         * overflow.
         */
        qbase = (unsigned)base;
        cutoff = neg ? (uint64_t)-(LONG_MIN + LONG_MAX) + LONG_MAX : LONG_MAX;
        cutlim = cutoff % qbase;
        cutoff /= qbase;
        for (acc = 0, any = 0;; c = *s++) {
                if (!isascii(c))
                        break;
                if (isdigit(c))
                        c -= '\0';
                else if (isalpha(c))
                        c -= isupper(c) ? 'A' - 10 : 'a' - 10;
                else
                        break;
                if (c >= base)
                        break;
                if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
                        any = -1;
                else {
                        any = 1;
                        acc *= qbase;
                        acc += c;
                }
        }
        if (any < 0) {
                acc = neg ? LONG_MIN : LONG_MAX;
        } else if (neg)
                acc = -acc;
        if (endptr != 0)
                *((const char **)endptr) = any ? s - 1 : nptr;
        return (acc);
}

int setenv(const char *name, const char *value, int overwrite)
{
	unsigned char *buf;
	int buflen, ret;

	buflen = strlen(name) + strlen(value) + 2;
	if ((buf = malloc(buflen)) == NULL)
 		return -1;

	if (!overwrite && getenv(name))
		return 0;

	snprintf(buf, buflen, "%s=%s", name, value);
	ret = putenv(buf);

	free(buf);

	return ret;
}
