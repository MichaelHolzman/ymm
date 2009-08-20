#ifndef _YAMM_H_
#define _YAMM_H_

#define MAX_THREAD_ID   1000

#define PRINT_ENTRIES   0x01
#define PRINT_ASSERTS   0x02

#define MAX_TRACED_SIZE 500000

#ifdef HPUX
#include <malloc.h>

#define MTDEBUGPATTERN  0x1000
#define MTDOUBLEFREE    0x2000
#define MTINITBUFFER    0x4000
#define MTCHUNKSIZE     0x8000

#define MAP_ANON        (MAP_ANONYMOUS)
#define MAP_ALIGN       0

#define EBADE           0xFFEB
#define EBADFD          0xFFEC
#define EBADR           0xFFED
#define EBADRQC         0xFFEE
#define EBADSLT         0xFFEF
#define EBFONT          0xFFF0
#define EDEADLOCK       0xFFF1
#define ELIBACC         0xFFF2
#define ELIBBAD         0xFFF3
#define ELIBEXEC        0xFFF4
#define ELIBMAX         0xFFF5
#define ELIBSCN         0xFFF6
#define ELOCKUNMAPPED   0xFFF7
#define ENOANO          0xFFF8
#define ENOTACTIVE      0xFFF9
#define ENOTUNIQ        0xFFFA
#define EOWNERDEAD      0xFFFB
#define EREMCHG         0xFFFC
#define ERESTART        0xFFFD
#define ESTRPIPE        0xFFFE
#define EXFULL          0xFFFF

#endif

#ifdef AIX

#include <malloc.h>

#define MTDEBUGPATTERN  0x1000
#define MTDOUBLEFREE    0x2000
#define MTINITBUFFER    0x4000
#define MTCHUNKSIZE     0x8000

#define MAP_ALIGN       0

#define EADV            0xFFE7
#define EBADE           0xFFE8
#define EBADFD          0xFFE9
#define EBADR           0xFFEA
#define EBADRQC         0xFFEB
#define EBADSLT         0xFFEC
#define EBFONT          0xFFED
#define ECOMM           0xFFEE
#define EDEADLOCK       0xFFEF
#define ELIBACC         0xFFF0
#define ELIBBAD         0xFFF1
#define ELIBEXEC        0xFFF2
#define ELIBMAX         0xFFF3
#define ELIBSCN         0xFFF4
#define ELOCKUNMAPPED   0xFFF5
#define ENOANO          0xFFF6
#define ENONET          0xFFF7
#define ENOPKG          0xFFF8
#define ENOTACTIVE      0xFFF9
#define ENOTUNIQ        0xFFFA
#define EOWNERDEAD      0xFFFB
#define EREMCHG         0xFFFC
#define ESRMNT          0xFFFD
#define ESTRPIPE        0xFFFE
#define EXFULL          0xFFFF

#endif /* AIX */

#ifdef LINUX

#include <linux/unistd.h>

#define MTDEBUGPATTERN  0x1000
#define MTDOUBLEFREE    0x2000
#define MTINITBUFFER    0x4000
#define MTCHUNKSIZE     0x8000

#define MAP_ALIGN       0

#if !defined(EOWNERDEAD)
#define EOWNERDEAD      0xFFFD
#endif
#define ELOCKUNMAPPED   0xFFFE
#define ENOTACTIVE      0xFFFF

#define pthread_self    getpid

#endif /* LINUX */

#endif /* _YAMM_H_ */
