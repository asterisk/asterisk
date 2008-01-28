/*
 * Asterisk -- An open source telephony toolkit.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 */

/*
Copyright (c) 2002 Jorge Acereda  <jacereda@users.sourceforge.net> &
                   Peter O'Gorman <ogorman@users.sourceforge.net>
                   
Portions may be copyright others, see the AUTHORS file included with this
distribution.

Maintained by Peter O'Gorman <ogorman@users.sourceforge.net>

Bug Reports and other queries should go to <ogorman@users.sourceforge.net>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _DLFCN_H_
#define _DLFCN_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined (__GNUC__) && __GNUC__ > 3
#define dl_restrict __restrict
#else
#define dl_restrict
#endif
/*
 * Structure filled in by dladdr().
 */

typedef struct dl_info {
        const char      *dli_fname;     /* Pathname of shared object */
        void            *dli_fbase;     /* Base address of shared object */
        const char      *dli_sname;     /* Name of nearest symbol */
        void            *dli_saddr;     /* Address of nearest symbol */
} Dl_info;

extern void * dlopen(const char *path, int mode);
extern void * dlsym(void * dl_restrict handle, const char * dl_restrict symbol);
extern const char * dlerror(void);
extern int dlclose(void * handle);
extern int dladdr(const void * dl_restrict, Dl_info * dl_restrict);

#define RTLD_LAZY	0x1
#define RTLD_NOW	0x2
#define RTLD_LOCAL	0x4
#define RTLD_GLOBAL	0x8
#define RTLD_NOLOAD	0x10
#define RTLD_NODELETE	0x80

/*
 * Special handle arguments for dlsym().
 */
#define	RTLD_NEXT		((void *) -1)	/* Search subsequent objects. */
#define	RTLD_DEFAULT	((void *) -2)	/* Use default search algorithm. */

#ifdef __cplusplus
}
#endif

#endif /* _DLFCN_H_ */
