/* This file is auto generated from ustr-import */


#ifndef USTR_CONF_H
#define USTR_CONF_H 1


/* Same default, newer position. */
#ifndef USTR_CONF_INCLUDE_CODEONLY_HEADERS
#define USTR_CONF_INCLUDE_CODEONLY_HEADERS 1
#endif

/* We can't: if defined(__GLIBC__) && (!defined(_GNU_SOURCE) || !_GNU_SOURCE)
 *  because by the time we've included a libc header it's too late. */ 
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif


/* Same defaults, but can be altered at will. */
/* Note that you really shouldn't alter the _HAVE_* ones. */

# ifndef USTR_CONF_HAVE_64bit_SIZE_MAX
# define USTR_CONF_HAVE_64bit_SIZE_MAX 0
# endif
# ifndef USTR_CONF_HAVE_RETARDED_VSNPRINTF
# define USTR_CONF_HAVE_RETARDED_VSNPRINTF 0
# endif
# ifndef USTR_CONF_HAVE_STDINT_H
# define USTR_CONF_HAVE_STDINT_H 1
# endif
# ifndef USTR_CONF_HAVE_DYNAMIC_CONF
# define USTR_CONF_HAVE_DYNAMIC_CONF 0
# endif

/* no USE_DYNAMIC_CONF ... leave as default */

# ifndef USTR_CONF_REF_BYTES
# define USTR_CONF_REF_BYTES 1
# endif
# ifndef USTR_CONF_EXACT_BYTES
# define USTR_CONF_EXACT_BYTES 0
# endif
# ifndef USTR_CONF_USE_SIZE
# define USTR_CONF_USE_SIZE    0
# endif

# ifndef USTR_CONF_USE_ASSERT
# define USTR_CONF_USE_ASSERT 0
# endif
# ifndef USTR_CONF_USE_EOS_MARK
# define USTR_CONF_USE_EOS_MARK 0
# endif


#endif
